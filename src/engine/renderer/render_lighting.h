#ifndef RENDER_LIGHTING_H
#define RENDER_LIGHTING_H
#include "render_raster.h"
#include <SDL3/SDL_timer.h>
#include <math.h>
#include <stdlib.h>
/* Frame factors are computed once for all surfaces per frame. */
static inline float *render_light_factors(const Level *level) {
    if (!level || !level->light_count) return NULL;
    float *f=malloc((size_t)level->light_count*sizeof(float));
    if(!f) return NULL;
    float t=(float)SDL_GetTicks()*0.001f;
    for(int i=0;i<level->light_count;i++) {
        const PointLight *l=&level->lights[i];
        if(l->effect==1) f[i]=0.72f+0.28f*sinf(t*3.0f+(float)i);
        else if(l->effect==2) f[i]=0.65f+0.35f*(0.5f+0.5f*sinf(t*23.7f+i*11.0f)*sinf(t*13.1f+i*7.0f));
        else f[i]=1.0f;
    }
    return f;
}
static inline void render_light_target(RasterTarget *target, const Level *level,
                                       const float *factors) {
    if(level && level->light_count>0 && factors) {
        target->lights=level->lights;
        target->light_count=level->light_count;
        target->light_factors=factors;
        target->ambient[0]=level->ambient_r;
        target->ambient[1]=level->ambient_g;
        target->ambient[2]=level->ambient_b;
    }
}
/* Cull point lights to the axis-aligned bounds of a surface. All relevant
 * lights are retained; if the fixed scratch capacity overflows, fall back
 * to the full list to preserve visual correctness. */
static inline void render_light_cull(RasterTarget *target, const Level *level,
                                      const float *factors,
                                      const float minv[3], const float maxv[3],
                                      PointLight scratch[64], float scratch_factors[64]) {
    if(!target->lights || !level || !factors) return;
    int count=0;
    for(int i=0;i<level->light_count;i++) {
        const PointLight *l=&level->lights[i];
        const float pos[3]={l->x,l->y,l->z};
        float d2=0.0f;
        for(int k=0;k<3;k++) {
            float d=pos[k]<minv[k]?minv[k]-pos[k]:pos[k]>maxv[k]?pos[k]-maxv[k]:0.0f;
            d2+=d*d;
        }
        if(d2>=l->radius*l->radius) continue;
        if(count==64) return;
        scratch[count]=*l;
        scratch_factors[count]=factors[i];
        count++;
    }
    target->lights=count?scratch:level->lights;
    target->light_factors=scratch_factors;
    target->light_count=count;
}
#endif
