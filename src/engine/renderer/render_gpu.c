#include "render_gpu.h"
#include <SDL3/SDL_opengl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_GPU_LIGHTS 16
#define MAX_POLY_VERTICES 4096
#define PI 3.14159265358979323846f

static SDL_GLContext context;
static GLuint program, vertex_shader, fragment_shader;
static GLuint textures_gpu[TEXTURE_MANAGER_CAPACITY];
static GLint loc_count, loc_pos, loc_color, loc_ambient, loc_sampler;
static PFNGLCREATESHADERPROC pCreateShader;
static PFNGLSHADERSOURCEPROC pShaderSource;
static PFNGLCOMPILESHADERPROC pCompileShader;
static PFNGLGETSHADERIVPROC pGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC pGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC pCreateProgram;
static PFNGLATTACHSHADERPROC pAttachShader;
static PFNGLLINKPROGRAMPROC pLinkProgram;
static PFNGLGETPROGRAMIVPROC pGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC pGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC pUseProgram;
static PFNGLGETUNIFORMLOCATIONPROC pGetUniformLocation;
static PFNGLUNIFORM1IPROC pUniform1i;
static PFNGLUNIFORM3FPROC pUniform3f;
static PFNGLUNIFORM4FVPROC pUniform4fv;
static PFNGLDELETESHADERPROC pDeleteShader;
static PFNGLDELETEPROGRAMPROC pDeleteProgram;

static const char *vertex_source =
    "#version 120\n"
    "varying vec3 worldPos;\n"
    "varying vec2 uv;\n"
    "void main(){ worldPos=gl_Vertex.xyz; uv=gl_MultiTexCoord0.xy;"
    " gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex; }\n";
static const char *fragment_source =
    "#version 120\n"
    "uniform sampler2D diffuse;\n"
    "uniform int lightCount;\n"
    "uniform vec4 lightPosRadius[16];\n"
    "uniform vec4 lightColorIntensity[16];\n"
    "uniform vec3 ambient;\n"
    "varying vec3 worldPos; varying vec2 uv;\n"
    "void main(){ vec3 illumination=ambient;"
    " for(int i=0;i<16;i++){ if(i>=lightCount) break;"
    " vec3 d=worldPos-lightPosRadius[i].xyz;"
    " float radius=max(lightPosRadius[i].w,0.001);"
    " float attenuation=max(0.0,1.0-length(d)/radius);"
    " attenuation=attenuation*attenuation;"
    " illumination+=lightColorIntensity[i].rgb*lightColorIntensity[i].a*attenuation; }"
    " gl_FragColor=vec4(texture2D(diffuse,uv).rgb*clamp(illumination,0.0,1.0),1.0); }\n";

#define LOAD_GL(name, type) do { p##name=(type)SDL_GL_GetProcAddress("gl" #name); if(!p##name) { SDL_Log("Missing GL function: gl" #name); goto fail; } } while(0)

static bool compile_shader(GLuint shader, const char *source) {
    GLint ok=0;
    pShaderSource(shader,1,(const GLchar **)&source,NULL);
    pCompileShader(shader);
    pGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if(!ok){ char log[2048]={0}; pGetShaderInfoLog(shader,sizeof(log),NULL,log); SDL_Log("GPU shader error: %s",log); }
    return ok!=0;
}

bool render_gpu_init(SDL_Window *window, const TextureManager *textures) {
    if(!window || !textures) return false;
    context=SDL_GL_CreateContext(window);
    if(!context){ SDL_Log("OpenGL context unavailable: %s",SDL_GetError()); return false; }
    if(!SDL_GL_MakeCurrent(window,context)) goto fail;
    LOAD_GL(CreateShader,PFNGLCREATESHADERPROC);
    LOAD_GL(ShaderSource,PFNGLSHADERSOURCEPROC);
    LOAD_GL(CompileShader,PFNGLCOMPILESHADERPROC);
    LOAD_GL(GetShaderiv,PFNGLGETSHADERIVPROC);
    LOAD_GL(GetShaderInfoLog,PFNGLGETSHADERINFOLOGPROC);
    LOAD_GL(CreateProgram,PFNGLCREATEPROGRAMPROC);
    LOAD_GL(AttachShader,PFNGLATTACHSHADERPROC);
    LOAD_GL(LinkProgram,PFNGLLINKPROGRAMPROC);
    LOAD_GL(GetProgramiv,PFNGLGETPROGRAMIVPROC);
    LOAD_GL(GetProgramInfoLog,PFNGLGETPROGRAMINFOLOGPROC);
    LOAD_GL(UseProgram,PFNGLUSEPROGRAMPROC);
    LOAD_GL(GetUniformLocation,PFNGLGETUNIFORMLOCATIONPROC);
    LOAD_GL(Uniform1i,PFNGLUNIFORM1IPROC);
    LOAD_GL(Uniform3f,PFNGLUNIFORM3FPROC);
    LOAD_GL(Uniform4fv,PFNGLUNIFORM4FVPROC);
    LOAD_GL(DeleteShader,PFNGLDELETESHADERPROC);
    LOAD_GL(DeleteProgram,PFNGLDELETEPROGRAMPROC);
    vertex_shader=pCreateShader(GL_VERTEX_SHADER);
    fragment_shader=pCreateShader(GL_FRAGMENT_SHADER);
    if(!vertex_shader || !fragment_shader || !compile_shader(vertex_shader,vertex_source) || !compile_shader(fragment_shader,fragment_source)) goto fail;
    program=pCreateProgram(); if(!program) goto fail;
    pAttachShader(program,vertex_shader); pAttachShader(program,fragment_shader);
    pLinkProgram(program);
    GLint ok=0; pGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){char log[2048]={0};pGetProgramInfoLog(program,sizeof(log),NULL,log);SDL_Log("GPU link error: %s",log);goto fail;}
    loc_count=pGetUniformLocation(program,"lightCount");
    loc_pos=pGetUniformLocation(program,"lightPosRadius");
    loc_color=pGetUniformLocation(program,"lightColorIntensity");
    loc_ambient=pGetUniformLocation(program,"ambient");
    loc_sampler=pGetUniformLocation(program,"diffuse");
    if (loc_count < 0 || loc_pos < 0 || loc_color < 0 ||
        loc_ambient < 0 || loc_sampler < 0) {
        SDL_Log("GPU lighting shader uniforms unavailable: count=%d pos=%d color=%d ambient=%d sampler=%d",
                loc_count, loc_pos, loc_color, loc_ambient, loc_sampler);
        goto fail; /* Fall back to CPU instead of silently losing lighting. */
    }
    glGenTextures(TEXTURE_MANAGER_CAPACITY,textures_gpu);
    for(int i=0;i<TEXTURE_MANAGER_CAPACITY;i++) {
        const Texture *t=texture_manager_get(textures,i);
        if(!t || !t->pixels || t->width<=0 || t->height<=0) continue;
        size_t count=(size_t)t->width*t->height;
        unsigned char *rgba=malloc(count*4);
        if(!rgba) goto fail;
        for(size_t p=0;p<count;p++){
            uint32_t c=t->pixels[p];
            rgba[p*4+0]=(unsigned char)(c>>16);
            rgba[p*4+1]=(unsigned char)(c>>8);
            rgba[p*4+2]=(unsigned char)c;
            rgba[p*4+3]=(unsigned char)(c>>24);
        }
        glBindTexture(GL_TEXTURE_2D,textures_gpu[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t->width,t->height,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
        free(rgba);
    }
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glEnable(GL_TEXTURE_2D); glDisable(GL_CULL_FACE);
    SDL_GL_SetSwapInterval(0);
    SDL_Log("GPU backend active: %s",(const char *)glGetString(GL_RENDERER));
    return true;
fail:
    render_gpu_shutdown();
    return false;
}

void render_gpu_shutdown(void) {
    if(context){
        glDeleteTextures(TEXTURE_MANAGER_CAPACITY,textures_gpu);
        if(program && pDeleteProgram) pDeleteProgram(program);
        if(vertex_shader && pDeleteShader) pDeleteShader(vertex_shader);
        if(fragment_shader && pDeleteShader) pDeleteShader(fragment_shader);
        SDL_GL_DestroyContext(context);
    }
    context=NULL; program=vertex_shader=fragment_shader=0;
    memset(textures_gpu,0,sizeof(textures_gpu));
}

static void bind_texture(int id) {
    if(id<0 || id>=TEXTURE_MANAGER_CAPACITY || !textures_gpu[id]) id=0;
    glBindTexture(GL_TEXTURE_2D,textures_gpu[id]);
}
static void vertex(float x,float y,float z,float u,float v){
    glTexCoord2f(u,v); glVertex3f(x,y,z);
}
static void quad(float ax,float az,float bx,float bz,float bottom,float top,float u0,float u1) {
    if(top<=bottom+0.0001f) return;
    glBegin(GL_TRIANGLES);
    vertex(ax,bottom,az,u0,bottom); vertex(bx,bottom,bz,u1,bottom); vertex(bx,top,bz,u1,top);
    vertex(ax,bottom,az,u0,bottom); vertex(bx,top,bz,u1,top); vertex(ax,top,az,u0,top);
    glEnd();
}

/* Build a closed sector boundary from unordered walls. Supports ordinary
 * simple sectors; holes and self-intersections are not yet supported. */
static int sector_polygon(const Level *level,int sector,int *indices) {
    if(level->wall_count<=0 || level->wall_count>MAX_POLY_VERTICES) return 0;
    int *starts=malloc((size_t)level->wall_count*sizeof(int));
    int *ends=malloc((size_t)level->wall_count*sizeof(int));
    unsigned char *used=calloc((size_t)level->wall_count,1);
    if(!starts || !ends || !used){free(starts);free(ends);free(used);return 0;}
    int n=0;
    for(int i=0;i<level->wall_count;i++){
        const Wall *w=&level->walls[i];
        if(w->front_sector!=sector && w->back_sector!=sector) continue;
        starts[n]=w->vertex_start;ends[n]=w->vertex_end;n++;
    }
    int count=0;
    if(n>=3){
        indices[count++]=starts[0];used[0]=1;
        int current=ends[0];
        while(count<n && current!=indices[0]){
            indices[count++]=current;
            int next=-1;
            for(int i=0;i<n;i++) if(!used[i] && (starts[i]==current || ends[i]==current)){next=i;break;}
            if(next<0) break;
            used[next]=1;
            current=(starts[next]==current)?ends[next]:starts[next];
        }
        if(current!=indices[0]) count=0;
    }
    free(starts);free(ends);free(used);
    return count;
}
static float cross2(const Vertex *a,const Vertex *b,const Vertex *c){
    return (b->x-a->x)*(c->z-a->z)-(b->z-a->z)*(c->x-a->x);
}
static bool inside_triangle(const Vertex *p,const Vertex *a,const Vertex *b,const Vertex *c,float sign){
    return cross2(a,b,p)*sign>=-0.00001f && cross2(b,c,p)*sign>=-0.00001f && cross2(c,a,p)*sign>=-0.00001f;
}
/* Ear clipping avoids fan artifacts on concave room polygons. */
static void draw_sector_plane(const Level *level,int sector,bool ceiling) {
    int ids[MAX_POLY_VERTICES];
    int n=sector_polygon(level,sector,ids);
    if(n<3) return;
    const Sector *s=&level->sectors[sector];
    float y=ceiling?s->ceiling_height:s->floor_height;
    bind_texture(ceiling?s->ceiling_texture_id:s->floor_texture_id);
    float area=0;
    for(int i=0;i<n;i++){
        const Vertex *a=&level->vertices[ids[i]],*b=&level->vertices[ids[(i+1)%n]];
        area+=a->x*b->z-b->x*a->z;
    }
    float sign=area>=0?1.0f:-1.0f;
    int remaining=n,limit=n*n;
    while(remaining>=3 && limit-->0){
        bool found=false;
        for(int i=0;i<remaining;i++){
            int ia=ids[(i+remaining-1)%remaining],ib=ids[i],ic=ids[(i+1)%remaining];
            const Vertex *a=&level->vertices[ia],*b=&level->vertices[ib],*c=&level->vertices[ic];
            if(cross2(a,b,c)*sign<=0.00001f) continue;
            bool contains=false;
            for(int j=0;j<remaining;j++){
                int p=ids[j];if(p==ia||p==ib||p==ic) continue;
                if(inside_triangle(&level->vertices[p],a,b,c,sign)){contains=true;break;}
            }
            if(contains) continue;
            glBegin(GL_TRIANGLES);
            vertex(a->x,y,a->z,a->x,a->z);
            vertex(b->x,y,b->z,b->x,b->z);
            vertex(c->x,y,c->z,c->x,c->z);
            glEnd();
            memmove(&ids[i],&ids[i+1],(size_t)(remaining-i-1)*sizeof(int));
            remaining--;found=true;break;
        }
        if(!found) break;
    }
}

static void upload_lights(const Level *level) {
    GLfloat pos[MAX_GPU_LIGHTS*4]={0},col[MAX_GPU_LIGHTS*4]={0};
    int count=0;
    float time=(float)SDL_GetTicks()/1000.0f;
    for(int i=0;i<level->light_count && count<MAX_GPU_LIGHTS;i++){
        const PointLight *l=&level->lights[i];
        if(l->radius<=0 || l->intensity<=0) continue;
        float factor=1.0f;
        if(l->effect==1) factor=0.75f+0.25f*sinf(time*4.0f+i);
        if(l->effect==2) factor=0.7f+0.3f*sinf(time*31.0f+i*7.1f)*sinf(time*13.0f+i);
        pos[count*4]=l->x;pos[count*4+1]=l->y;pos[count*4+2]=l->z;pos[count*4+3]=l->radius;
        col[count*4]=(float)l->r/255.0f;
        col[count*4+1]=(float)l->g/255.0f;
        col[count*4+2]=(float)l->b/255.0f;
        col[count*4+3]=l->intensity*factor;
        count++;
    }
    pUniform1i(loc_count,count);
    if(count){pUniform4fv(loc_pos,count,pos);pUniform4fv(loc_color,count,col);}
    pUniform3f(loc_ambient,level->ambient_r,level->ambient_g,level->ambient_b);
    pUniform1i(loc_sampler,0);
}

void render_gpu_draw(SDL_Window *window,const Player *player,const Level *level) {
    static Uint64 accumulated_ticks=0;
    static int frames=0;
    Uint64 begin=SDL_GetPerformanceCounter();
    if(!context || !player || !level) return;
    int width=0,height=0;
    if(!SDL_GetWindowSizeInPixels(window,&width,&height) || width<=0 || height<=0) return;
    glViewport(0,0,width,height);
    glClearColor(0.07f,0.07f,0.10f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();
    float near_z=0.1f,far_z=2000.0f;
    float aspect=(float)width/(float)height;
    /* Match software renderer's 90-degree horizontal FOV. */
    glFrustum(-near_z,near_z,-near_z/aspect,near_z/aspect,near_z,far_z);
    glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glRotatef(-player->pitch*180.0f/PI,1,0,0);
    glRotatef(player->yaw*180.0f/PI,0,1,0);
    glTranslatef(-player->x,-player->y-player->eye_height,-player->z);
    pUseProgram(program);
    upload_lights(level);
    for(int s=0;s<level->sector_count;s++){
        draw_sector_plane(level,s,false);
        draw_sector_plane(level,s,true);
    }
    for(int i=0;i<level->wall_count;i++){
        const Wall *w=&level->walls[i];
        if(w->vertex_start<0 || w->vertex_start>=level->vertex_count || w->vertex_end<0 || w->vertex_end>=level->vertex_count) continue;
        int f=w->front_sector,b=w->back_sector;
        if(f<0 || f>=level->sector_count) continue;
        const Sector *front=&level->sectors[f];
        const Vertex *a=&level->vertices[w->vertex_start],*c=&level->vertices[w->vertex_end];
        float length=hypotf(c->x-a->x,c->z-a->z);
        bind_texture(w->texture_id);
        if(b<0 || b>=level->sector_count){
            quad(a->x,a->z,c->x,c->z,front->floor_height,front->ceiling_height,0,length);
        } else {
            const Sector *back=&level->sectors[b];
            if(fabsf(front->floor_height-back->floor_height)>0.0001f)
                quad(a->x,a->z,c->x,c->z,
                     fminf(front->floor_height,back->floor_height),
                     fmaxf(front->floor_height,back->floor_height),0,length);
            if(fabsf(front->ceiling_height-back->ceiling_height)>0.0001f)
                quad(a->x,a->z,c->x,c->z,
                     fminf(front->ceiling_height,back->ceiling_height),
                     fmaxf(front->ceiling_height,back->ceiling_height),0,length);
        }
    }
    pUseProgram(0);
    SDL_GL_SwapWindow(window);
    accumulated_ticks+=SDL_GetPerformanceCounter()-begin;
    if(++frames==60){
        double ms=(double)accumulated_ticks*1000.0/
                  ((double)SDL_GetPerformanceFrequency()*frames);
        SDL_Log("GPU renderer average over 60 frames | CPU submit + swap: %.2f ms (not GPU execution time)",ms);
        frames=0;accumulated_ticks=0;
    }
}
