#include "editor_viewport.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#define EDITOR_MIN_ZOOM 4.0f
#define EDITOR_MAX_ZOOM 200.0f

static void world_to_screen(
    const EditorViewport *editor,
    float world_x,
    float world_z,
    int width,
    int height,
    float *screen_x,
    float *screen_y
) {
    *screen_x =
        width * 0.5f +
        (world_x - editor->camera_x) * editor->zoom;

    *screen_y =
        height * 0.5f +
        (world_z - editor->camera_z) * editor->zoom;
}

void editor_viewport_init(EditorViewport *editor) {
    *editor = (EditorViewport){0};

    editor->camera_x = 0.0f;
    editor->camera_z = 0.0f;
    editor->zoom = 25.0f;

    editor->selected_vertex = -1;
    editor->vertex_dragging = false;
    editor->vertex_drag_offset_x = 0.0f;
    editor->vertex_drag_offset_z = 0.0f;

    editor->snap_enabled = true;
    editor->grid_size = 1.0f;

    editor->save_notification_visible = false;
    editor->save_notification_success = false;
    editor->save_notification_time = 0;

    editor->active_tool = EDITOR_TOOL_SELECT;
    editor->wall_start_vertex = -1;
    editor->selected_wall = -1;
    editor->selected_sector = 0;
}

static void screen_to_world(
    const EditorViewport *editor, int width, int height,
    float sx, float sy, float *x, float *z
) {
    *x = editor->camera_x + (sx - width * 0.5f) / editor->zoom;
    *z = editor->camera_z + (sy - height * 0.5f) / editor->zoom;
}

static int editor_find_vertex(
    const EditorViewport *editor,
    const Level *level,
    int width,
    int height,
    float mouse_x,
    float mouse_y
) {
    const float selection_radius = 10.0f;
    const float radius_squared =
        selection_radius * selection_radius;

    int closest_vertex = -1;
    float closest_distance = radius_squared;

    for (int i = 0; i < level->vertex_count; ++i) {
        float sx, sy;

        world_to_screen(
            editor,
            level->vertices[i].x,
            level->vertices[i].z,
            width,
            height,
            &sx,
            &sy
        );

        float dx = mouse_x - sx;
        float dy = mouse_y - sy;
        float distance = dx * dx + dy * dy;

        if (distance <= closest_distance) {
            closest_distance = distance;
            closest_vertex = i;
        }
    }

    return closest_vertex;
}

static float editor_snap_value(
    float value,
    float grid_size
) {
    if (grid_size <= 0.0f) {
        return value;
    }

    return roundf(value / grid_size) * grid_size;
}

static bool editor_create_vertex(Level *level, float x, float z, int *created_index)
{
    if (!level || level->vertex_count < 0 || level->vertex_count >= INT_MAX - 1)
        return false;

    for (int i = 0; i < level->vertex_count; ++i) {
        const float dx = level->vertices[i].x - x;
        const float dz = level->vertices[i].z - z;
        if (dx * dx + dz * dz < 0.000001f) {
            SDL_Log("Vertex already exists at (%.3f, %.3f)", x, z);
            return false;
        }
    }

    size_t count = (size_t)level->vertex_count + 1;
    if (count > SIZE_MAX / sizeof(Vertex)) return false;
    Vertex *resized = realloc(level->vertices, count * sizeof(Vertex));
    if (!resized) {
        SDL_Log("Unable to allocate vertex");
        return false;
    }
    level->vertices = resized;
    *created_index = level->vertex_count;
    level->vertices[level->vertex_count++] = (Vertex){ .x = x, .z = z };
    SDL_Log("Created vertex #%d at (%.3f, %.3f)", *created_index, x, z);
    return true;
}

// Insert a vertex directly into an existing wall, preserving its sector/texture data.
// Allocations happen before any geometry is modified.
static bool editor_split_wall(Level *level, int wall_index, float t, int *created_index)
{
    if (!level || wall_index < 0 || wall_index >= level->wall_count ||
        t <= 0.0001f || t >= 0.9999f ||
        level->vertex_count < 0 || level->wall_count < 0 ||
        level->vertex_count >= INT_MAX - 1 || level->wall_count >= INT_MAX - 1)
        return false;
    Wall old = level->walls[wall_index];
    if (old.vertex_start < 0 || old.vertex_end < 0 ||
        old.vertex_start >= level->vertex_count || old.vertex_end >= level->vertex_count)
        return false;
    Vertex a = level->vertices[old.vertex_start];
    Vertex b = level->vertices[old.vertex_end];
    Vertex point = { a.x + t * (b.x - a.x), a.z + t * (b.z - a.z) };
    // Reuse an existing vertex at the split point, if present.
    int index = -1;
    for (int i = 0; i < level->vertex_count; ++i) {
        float dx = level->vertices[i].x - point.x;
        float dz = level->vertices[i].z - point.z;
        if (dx * dx + dz * dz < 0.000001f) { index = i; break; }
    }
    if (index == old.vertex_start || index == old.vertex_end) return false;
    if (index < 0) {
        size_t count = (size_t)level->vertex_count + 1;
        if (count > SIZE_MAX / sizeof(Vertex)) return false;
        Vertex *v = realloc(level->vertices, count * sizeof(Vertex));
        if (!v) return false;
        level->vertices = v;
        index = level->vertex_count;
    }
    size_t wall_count = (size_t)level->wall_count + 1;
    if (wall_count > SIZE_MAX / sizeof(Wall)) return false;
    Wall *w = realloc(level->walls, wall_count * sizeof(Wall));
    if (!w) return false;
    level->walls = w;
    if (index == level->vertex_count) {
        level->vertices[index] = point;
        level->vertex_count++;
    }
    Wall second = old;
    second.vertex_start = index;
    level->walls[wall_index].vertex_end = index;
    level->walls[level->wall_count++] = second;
    *created_index = index;
    SDL_Log("Split wall %d at vertex %d", wall_index, index);
    return true;
}

// Find the closest wall within 10 screen pixels, returning its projected position.
static int editor_find_wall_split(const EditorViewport *editor, const Level *level,
                                  int width, int height, float mx, float my, float *out_t)
{
    int best = -1;
    float best_dist = 100.0f;
    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *w = &level->walls[i];
        if (w->vertex_start < 0 || w->vertex_end < 0 ||
            w->vertex_start >= level->vertex_count || w->vertex_end >= level->vertex_count)
            continue;
        float ax, ay, bx, by;
        world_to_screen(editor, level->vertices[w->vertex_start].x,
            level->vertices[w->vertex_start].z, width, height, &ax, &ay);
        world_to_screen(editor, level->vertices[w->vertex_end].x,
            level->vertices[w->vertex_end].z, width, height, &bx, &by);
        float dx = bx - ax, dy = by - ay;
        float length2 = dx * dx + dy * dy;
        if (length2 < 0.0001f) continue;
        float t = ((mx - ax) * dx + (my - ay) * dy) / length2;
        if (t <= 0.0001f || t >= 0.9999f) continue;
        float px = ax + t * dx, py = ay + t * dy;
        float dist = (mx - px) * (mx - px) + (my - py) * (my - py);
        if (dist < best_dist) { best_dist = dist; best = i; *out_t = t; }
    }
    return best;
}

/* A directed half-edge uses the wall's stored direction when reversed=0.
   The left side of a directed edge is its front side. */
static int edge_origin(const Level *l, int e) {
    Wall w = l->walls[e / 2];
    return (e & 1) ? w.vertex_end : w.vertex_start;
}
static int edge_target(const Level *l, int e) {
    Wall w = l->walls[e / 2];
    return (e & 1) ? w.vertex_start : w.vertex_end;
}
static int edge_sector(const Level *l, int e) {
    Wall w = l->walls[e / 2];
    return (e & 1) ? w.back_sector : w.front_sector;
}
static void edge_assign(Level *l, int e, int sector) {
    if (e & 1) l->walls[e / 2].back_sector = sector;
    else l->walls[e / 2].front_sector = sector;
}
/* Walk the face on the left of a directed edge. Choose the outgoing
   edge immediately clockwise from the reversed incoming edge. */
static int next_face_edge(const Level *l, int edge) {
    int at = edge_target(l, edge);
    Vertex v = l->vertices[at];
    Vertex from = l->vertices[edge_origin(l, edge)];
    float reverse = atan2f(from.z-v.z, from.x-v.x);
    float best = 7.0f;
    int chosen = -1;
    for (int w=0; w<l->wall_count; ++w) {
        for (int r=0; r<2; ++r) {
            int e=2*w+r;
            if (edge_origin(l,e)!=at) continue;
            Vertex to=l->vertices[edge_target(l,e)];
            float a=atan2f(to.z-v.z,to.x-v.x);
            float clockwise=reverse-a;
            while (clockwise<0) clockwise+=6.28318530718f;
            while (clockwise>=6.28318530718f) clockwise-=6.28318530718f;
            if (clockwise<best) { best=clockwise; chosen=e; }
        }
    }
    return chosen;
}
/* Positive signed area is an enclosed face in the editor's X/Z axes.
   Returns its half-edges and area, or zero for incomplete/outer faces. */
static int trace_face(const Level *l, int start, int *edges, double *area) {
    if (l->wall_count < 3) return 0;
    int edge=start, count=0;
    double twice=0.0;
    do {
        if (count>=l->wall_count) return 0;
        edges[count++]=edge;
        Vertex a=l->vertices[edge_origin(l,edge)];
        Vertex b=l->vertices[edge_target(l,edge)];
        twice+=(double)a.x*b.z-(double)b.x*a.z;
        edge=next_face_edge(l,edge);
        if (edge<0) return 0;
    } while (edge!=start);
    *area=twice*0.5;
    return *area>0.0001 ? count : 0;
}
static bool point_in_face(const Level *l, const int *edges, int count, float x, float z) {
    bool inside=false;
    for (int i=0,j=count-1; i<count; j=i++) {
        Vertex a=l->vertices[edge_origin(l,edges[i])];
        Vertex b=l->vertices[edge_origin(l,edges[j])];
        if ((a.z>z)!=(b.z>z) &&
            x < (b.x-a.x)*(z-a.z)/(b.z-a.z)+a.x) inside=!inside;
    }
    return inside;
}
/* Reconcile an enclosed face. If already owned, retain its sector;
   otherwise create one and automatically make shared edges portals. */
static int assign_face(Level *l, const int *edges, int count) {
    int owner=-1;
    for (int i=0;i<count;i++) {
        int s=edge_sector(l,edges[i]);
        if (s<0) continue;
        if (owner>=0 && owner!=s) {
            SDL_Log("Face has conflicting sector assignments; edit walls manually");
            return -1;
        }
        owner=s;
    }
    /* A newly drawn divider can split one existing sector into two faces.
       Do not leave the shared wall with the same sector on both sides. */
    bool split_existing=false;
    if (owner>=0) {
        for (int i=0;i<count;i++) {
            if (edge_sector(l,edges[i]^1)==owner) {
                split_existing=true;
                break;
            }
        }
    }
    int inherit=owner;
    if (split_existing) owner=-1;
    if (owner<0) {
        if (l->sector_count>=INT_MAX-1) return -1;
        size_t n=(size_t)l->sector_count+1;
        if (n>SIZE_MAX/sizeof(Sector)) return -1;
        Sector *p=realloc(l->sectors,n*sizeof(Sector));
        if (!p) return -1;
        l->sectors=p;
        owner=l->sector_count++;
        l->sectors[owner]=inherit>=0 ? l->sectors[inherit] :
            (Sector){.floor_height=0,.ceiling_height=5,
                     .floor_texture_id=3,.ceiling_texture_id=4};
        SDL_Log("Created sector %d from closed geometry",owner);
    }
    for (int i=0;i<count;i++) edge_assign(l,edges[i],owner);
    return owner;
}
/* Auto-detect new rooms after closing a wall loop; existing sector
   boundaries stay intact and a shared wall gains its second side. */
static void auto_assign_closed_faces(Level *l) {
    if (!l || l->wall_count<3 || l->wall_count>100000) return;
    int *edges=malloc((size_t)l->wall_count*sizeof(int));
    unsigned char *seen=calloc((size_t)l->wall_count*2,1);
    if (!edges || !seen) { free(edges); free(seen); return; }
    for (int e=0;e<l->wall_count*2;e++) {
        if (seen[e]) continue;
        double area=0;
        int n=trace_face(l,e,edges,&area);
        if (n>0) {
            for (int i=0;i<n;i++) seen[edges[i]]=1;
            assign_face(l,edges,n);
        } else seen[e]=1;
    }
    free(edges); free(seen);
}
static int sector_at_point(Level *l,float x,float z,bool create,bool separate) {
    if (l->wall_count<3 || l->wall_count>100000) return -1;
    int *edges=malloc((size_t)l->wall_count*sizeof(int));
    if (!edges) return -1;
    double best=1e100;
    int result=-1;
    int *chosen=malloc((size_t)l->wall_count*sizeof(int));
    if (!chosen) {free(edges);return -1;}
    int chosen_count=0;
    for (int e=0;e<l->wall_count*2;e++) {
        double area=0;
        int n=trace_face(l,e,edges,&area);
        if (n && area<best && point_in_face(l,edges,n,x,z)) {
            best=area;
            chosen_count=n;
            memcpy(chosen,edges,(size_t)n*sizeof(int));
        }
    }
    if (chosen_count) {
        if (create && separate) {
            if (l->sector_count < INT_MAX-1 &&
                (size_t)(l->sector_count+1) <= SIZE_MAX/sizeof(Sector)) {
                Sector *new_sectors=realloc(l->sectors,(size_t)(l->sector_count+1)*sizeof(Sector));
                if (new_sectors) {
                    l->sectors=new_sectors;
                    int previous=-1;
                    for (int i=0;i<chosen_count;i++) {
                        int old=edge_sector(l,chosen[i]);
                        if (old>=0) {previous=old;break;}
                    }
                    int fresh=l->sector_count++;
                    l->sectors[fresh]=previous>=0 ? l->sectors[previous] :
                        (Sector){.floor_height=0,.ceiling_height=5,
                                 .floor_texture_id=3,.ceiling_texture_id=4};
                    for (int i=0;i<chosen_count;i++) edge_assign(l,chosen[i],fresh);
                    result=fresh;
                    SDL_Log("Separated room into new sector %d",fresh);
                }
            }
        } else if (create) result=assign_face(l,chosen,chosen_count);
        else {
            for (int i=0;i<chosen_count;i++) {
                int s=edge_sector(l,chosen[i]);
                if (s>=0) {result=s;break;}
            }
        }
    }
    free(chosen);free(edges);
    return result;
}
static bool wall_would_cross(const Level *l, int start, int end);
static bool editor_create_wall(Level *level, int start, int end, int sector)
{
    (void)sector; /* Sector membership comes from closed faces, not shortcuts. */
    if (!level || start<0 || end<0 || start==end ||
        start>=level->vertex_count || end>=level->vertex_count ||
        level->wall_count<0 || level->wall_count>=INT_MAX-1) return false;
    if (wall_would_cross(level,start,end)) { SDL_Log("Wall intersects existing geometry: split at junction first"); return false; }
    for (int i=0;i<level->wall_count;i++) {
        Wall w=level->walls[i];
        if ((w.vertex_start==start && w.vertex_end==end) ||
            (w.vertex_start==end && w.vertex_end==start)) return false;
    }
    size_t n=(size_t)level->wall_count+1;
    if (n>SIZE_MAX/sizeof(Wall)) return false;
    Wall *w=realloc(level->walls,n*sizeof(Wall));
    if (!w) return false;
    level->walls=w;
    level->walls[level->wall_count++]=(Wall){.vertex_start=start,.vertex_end=end,
        .front_sector=-1,.back_sector=-1,.texture_id=0};
    auto_assign_closed_faces(level);
    return true;
}

static void editor_set_tool(EditorViewport *editor, EditorTool tool)
{
    editor->active_tool = tool;
    editor->wall_start_vertex = -1;
    editor->vertex_dragging = false;
    editor->spawn_dragging = false;
    editor->spawn_rotating = false;
    SDL_Log("Editor tool: %s", tool == EDITOR_TOOL_VERTEX ? "VERTEX" :
            tool == EDITOR_TOOL_WALL ? "WALL" :
            tool == EDITOR_TOOL_SECTOR ? "SECTOR" : "SELECT");
}

// Shared legend content is used for both rendering and mouse hit testing.
typedef struct EditorLegend {
    const char *actions[12];
    int action_count;
    float width;
    float height;
} EditorLegend;

static float editor_legend_scroll = 0.0f;

static EditorLegend editor_build_legend(const EditorViewport *editor)
{
    EditorLegend legend = {0};
    switch (editor->active_tool) {
        case EDITOR_TOOL_VERTEX:
            legend.actions[legend.action_count++] = "Left Click   Place vertex on grid";
            legend.actions[legend.action_count++] = "Click Wall   Insert vertex / split wall";
            legend.actions[legend.action_count++] = "Click Vertex Select existing vertex";
            break;
        case EDITOR_TOOL_WALL:
            legend.actions[legend.action_count++] = "Click Vertex Choose wall start";
            legend.actions[legend.action_count++] = "Next Vertex  Complete wall";
            legend.actions[legend.action_count++] = "Right Click  Cancel pending wall";
            legend.actions[legend.action_count++] = "Closed loops auto-create sectors";
            legend.actions[legend.action_count++] = "Click Wall  Select wall";
            legend.actions[legend.action_count++] = "F  Set front to active sector";
            legend.actions[legend.action_count++] = "B  Set back (portal) to active";
            legend.actions[legend.action_count++] = "X  Clear back (solid wall)";
            break;
        case EDITOR_TOOL_SECTOR:
            legend.actions[legend.action_count++] = "Click Room   Select / create sector";
            legend.actions[legend.action_count++] = "Shift+Click  Separate room into sector";
            legend.actions[legend.action_count++] = "[ / ]       Previous / next sector";
            legend.actions[legend.action_count++] = "PgUp/PgDn   Raise / lower floor 0.25";
            legend.actions[legend.action_count++] = "Shift+PgUp/Dn  Adjust ceiling 0.25";
            legend.actions[legend.action_count++] = "Closed rooms become sectors automatically";
            break;
        case EDITOR_TOOL_SELECT:
        default:
            legend.actions[legend.action_count++] = "Left Click   Select vertex / spawn";
            legend.actions[legend.action_count++] = "Left Drag    Move selected object";
            legend.actions[legend.action_count++] = "Hold R      Rotate selected spawn";
            legend.actions[legend.action_count++] = "[ / ]       Adjust spawn pitch";
            legend.actions[legend.action_count++] = "PgUp/PgDn   Adjust spawn height";
            break;
    }
    const char *tools[] = {
        "1  Select / Move", "2  Vertex / Split Wall",
        "3  Draw Walls / Auto Portals", "4  Rooms / Sector Heights"
    };
    const char *general[] = {
        "Middle Drag  Pan view", "Wheel        Zoom",
        "G            Toggle grid snap", "Ctrl+S       Save",
        "Esc          Cancel / Select"
    };
    size_t longest = SDL_strlen("ACTIVE TOOL: SECTOR");
    for (int i = 0; i < 4; ++i) {
        size_t len = SDL_strlen(tools[i]);
        if (len > longest) longest = len;
    }
    for (int i = 0; i < legend.action_count; ++i) {
        size_t len = SDL_strlen(legend.actions[i]);
        if (len > longest) longest = len;
    }
    for (int i = 0; i < 5; ++i) {
        size_t len = SDL_strlen(general[i]);
        if (len > longest) longest = len;
    }
    legend.width = (float)longest * 8.0f + 24.0f;
    // Header + tools + actions + general, with section separators.
    /* Match the actual cursor increments in editor_draw_controls.
       Last line starts at y + 283 + action_count*15; reserve 12px below it. */
    legend.height = 310.0f + legend.action_count * 15.0f;
    return legend;
}

static bool editor_point_in_panel(const EditorViewport *editor,
                                  float mx, float my, int width, int height)
{
    EditorLegend legend = editor_build_legend(editor);
    float panel_width = fminf(legend.width, fmaxf(0.0f, (float)width - 24.0f));
    float panel_height = fminf(legend.height, fmaxf(0.0f, (float)height - 24.0f));
    if (mx >= 12.0f && mx <= 12.0f + panel_width &&
        my >= 12.0f && my <= 12.0f + panel_height)
        return true;
    if (mx >= width - 272.0f && mx <= width - 12.0f &&
        my >= 12.0f && my <= 270.0f)
        return true;
    return false;
}


/* Right-hand inspector: fixed-size controls with explicit hit targets. */
#define INSPECTOR_WIDTH 260.0f
#define INSPECTOR_TOP 12.0f
#define INSPECTOR_ROW 24.0f
#define INSPECTOR_FIRST 64.0f
#define INSPECTOR_ROWS 7
static float inspector_x(int width) { return (float)width - INSPECTOR_WIDTH - 12.0f; }
static int inspector_mode(const EditorViewport *ed, const Level *l) {
    if (ed->active_tool == EDITOR_TOOL_WALL && ed->selected_wall >= 0 && ed->selected_wall < l->wall_count) return 2;
    if (ed->active_tool == EDITOR_TOOL_SECTOR && l->sector_count > 0) return 3;
    if (ed->spawn_selected && ed->active_tool == EDITOR_TOOL_SELECT) return 4;
    if (ed->selected_vertex >= 0 && ed->selected_vertex < l->vertex_count) return 1;
    return 0;
}
static bool inspector_click(EditorViewport *ed, Level *l, int width, float mx, float my) {
    float x=inspector_x(width);
    if (mx<x || mx>x+INSPECTOR_WIDTH || my<INSPECTOR_TOP || my>INSPECTOR_TOP+258.0f) return false;
    int row=(int)floorf((my-(INSPECTOR_TOP+INSPECTOR_FIRST))/INSPECTOR_ROW);
    if (row<0 || row>=INSPECTOR_ROWS) return true;
    /* +/- buttons on the right; text remains read-only for other clicks. */
    int delta=0;
    if (mx>=x+190 && mx<x+218) delta=-1;
    if (mx>=x+223 && mx<=x+252) delta=1;
    if (!delta) return true;
    bool fine=(SDL_GetModState() & SDL_KMOD_SHIFT)!=0;
    float step=fine?0.1f:1.0f;
    int mode=inspector_mode(ed,l);
    if (mode==1) {
        Vertex *v=&l->vertices[ed->selected_vertex];
        if (row==0) v->x+=delta*step;
        if (row==1) v->z+=delta*step;
    } else if (mode==2) {
        Wall *w=&l->walls[ed->selected_wall];
        if (row==0 && l->sector_count>0) {
            int next=w->front_sector+delta;
            if (next>=0 && next<l->sector_count && next!=w->back_sector) w->front_sector=next;
        } else if (row==1) {
            int next=w->back_sector+delta;
            if (next>=-1 && next<l->sector_count && next!=w->front_sector) w->back_sector=next;
        } else if (row==2 && w->texture_id+delta>=0) w->texture_id+=delta;
    } else if (mode==3) {
        int n=ed->selected_sector;
        if (n<0 || n>=l->sector_count) n=ed->selected_sector=0;
        Sector *sc=&l->sectors[n];
        if (row==0) ed->selected_sector=(n+delta+l->sector_count)%l->sector_count;
        if (row==1 && sc->floor_height+delta*step<sc->ceiling_height) sc->floor_height+=delta*step;
        if (row==2 && sc->ceiling_height+delta*step>sc->floor_height) sc->ceiling_height+=delta*step;
        if (row==3 && sc->floor_texture_id+delta>=0) sc->floor_texture_id+=delta;
        if (row==4 && sc->ceiling_texture_id+delta>=0) sc->ceiling_texture_id+=delta;
    } else if (mode==4) {
        PlayerSpawn *sp=&l->player_spawn;
        if (row==0) sp->x+=delta*step;
        if (row==1) sp->y+=delta*(fine?0.05f:0.25f);
        if (row==2) sp->z+=delta*step;
        if (row==3) sp->yaw+=delta*(fine?1.0f:5.0f);
        if (row==4) sp->pitch=fmaxf(-89.0f,fminf(89.0f,sp->pitch+delta));
    }
    return true;
}
/* Avoid ambiguous crossings and T-junctions: split walls explicitly first. */
static float orient2(Vertex a, Vertex b, Vertex c) {
    return (b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
}
static bool wall_would_cross(const Level *l, int start, int end) {
    Vertex a=l->vertices[start],b=l->vertices[end];
    for(int i=0;i<l->wall_count;i++) {
        Wall w=l->walls[i];
        Vertex c=l->vertices[w.vertex_start],d=l->vertices[w.vertex_end];
        float ab_c=orient2(a,b,c),ab_d=orient2(a,b,d);
        float cd_a=orient2(c,d,a),cd_b=orient2(c,d,b);
        if (ab_c*ab_d < -0.000001f && cd_a*cd_b < -0.000001f) return true;
        /* Endpoint lying in the middle of another segment is a T-junction. */
        Vertex pts[]={a,b,c,d};
        for(int j=0;j<4;j++) {
            Vertex p=pts[j],u=j<2?c:a,v=j<2?d:b;
            if(fabsf(orient2(u,v,p))>0.0001f) continue;
            float dot=(p.x-u.x)*(p.x-v.x)+(p.z-u.z)*(p.z-v.z);
            if(dot < -0.0001f) return true;
        }
    }
    return false;
}
void editor_viewport_process_event(
    EditorViewport *editor,
    SDL_Renderer *renderer,
    const SDL_Event *event,
    Level *level
) {
    int width = 0, height = 0;
    if (!SDL_GetRenderOutputSize(renderer, &width, &height)) return;

    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        float mouse_x = 0.0f, mouse_y = 0.0f;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        EditorLegend legend = editor_build_legend(editor);
        float panel_width = fminf(legend.width, fmaxf(0.0f, (float)width - 24.0f));
        float panel_height = fminf(legend.height, fmaxf(0.0f, (float)height - 24.0f));
        if (mouse_x >= 12.0f && mouse_x <= 12.0f + panel_width &&
            mouse_y >= 12.0f && mouse_y <= 12.0f + panel_height) {
            float max_scroll = fmaxf(0.0f, legend.height - panel_height);
            editor_legend_scroll = fmaxf(0.0f, fminf(max_scroll,
                editor_legend_scroll - event->wheel.y * 36.0f));
            return; /* Wheel over help panel scrolls help, not the map. */
        }
        if (event->wheel.y != 0) {
            editor->zoom *= event->wheel.y > 0 ? 1.1f : 0.9f;
            if (editor->zoom < EDITOR_MIN_ZOOM) editor->zoom = EDITOR_MIN_ZOOM;
            if (editor->zoom > EDITOR_MAX_ZOOM) editor->zoom = EDITOR_MAX_ZOOM;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {

        if (event->button.button == SDL_BUTTON_MIDDLE) {
            editor->panning = true;
        }

        if (event->button.button == SDL_BUTTON_RIGHT &&
            editor->active_tool == EDITOR_TOOL_WALL) {
            editor->wall_start_vertex = -1;
            return;
        }

        if (event->button.button == SDL_BUTTON_LEFT) {
            if (inspector_click(editor, level, width, event->button.x, event->button.y)) return;
            if (editor_point_in_panel(editor, event->button.x, event->button.y, width, height)) return;

            if (editor->active_tool == EDITOR_TOOL_VERTEX) {
                // Clicking an existing vertex does not create a duplicate.
                int existing = editor_find_vertex(editor, level, width, height,
                    event->button.x, event->button.y);
                if (existing >= 0) {
                    editor->selected_vertex = existing;
                    editor->spawn_selected = false;
                    return;
                }
                float split_t = 0.0f;
                int wall_index = editor_find_wall_split(editor, level, width, height,
                    event->button.x, event->button.y, &split_t);
                if (wall_index >= 0) {
                    int created = -1;
                    if (editor_split_wall(level, wall_index, split_t, &created)) {
                        editor->selected_vertex = created;
                        editor->spawn_selected = false;
                    }
                    return;
                }
                float x, z;
                screen_to_world(editor, width, height, event->button.x,
                                event->button.y, &x, &z);
                if (editor->snap_enabled) {
                    x = editor_snap_value(x, editor->grid_size);
                    z = editor_snap_value(z, editor->grid_size);
                }
                int created = -1;
                if (editor_create_vertex(level, x, z, &created)) {
                    editor->selected_vertex = created;
                    editor->spawn_selected = false;
                }
                return;
            }

            if (editor->active_tool == EDITOR_TOOL_WALL) {
                int vertex = editor_find_vertex(editor, level, width, height,
                    event->button.x, event->button.y);
                if (vertex >= 0) {
                    if (editor->wall_start_vertex < 0) {
                        editor->wall_start_vertex = vertex;
                        SDL_Log("Wall start vertex: %d", vertex);
                    } else {
                        if (editor_create_wall(level, editor->wall_start_vertex, vertex, editor->selected_sector))
                            editor->selected_wall = level->wall_count - 1;
                        editor->wall_start_vertex = -1;
                    }
                }
                // Clicking the middle of a wall selects it for sector assignment.
                float wall_t = 0.0f;
                int wall = editor_find_wall_split(editor, level, width, height,
                    event->button.x, event->button.y, &wall_t);
                if (wall >= 0) {
                    editor->selected_wall = wall;
                    editor->wall_start_vertex = -1;
                }
                return;
            }

            if (editor->active_tool == EDITOR_TOOL_SECTOR) {
                float wx,wz;
                screen_to_world(editor,width,height,event->button.x,event->button.y,&wx,&wz);
                int sector=sector_at_point(level,wx,wz,true,
                    (SDL_GetModState() & SDL_KMOD_SHIFT) != 0);
                if (sector>=0) {
                    editor->selected_sector=sector;
                    SDL_Log("Selected sector %d",sector);
                } else SDL_Log("No closed sector under cursor");
                return;
            }

            float mouse_x = event->button.x;
            float mouse_y = event->button.y;

            float spawn_x, spawn_y;

            world_to_screen(
                editor,
                level->player_spawn.x,
                level->player_spawn.z,
                width,
                height,
                &spawn_x,
                &spawn_y
            );

            float dx = mouse_x - spawn_x;
            float dy = mouse_y - spawn_y;

            bool clicked_spawn =
                dx * dx + dy * dy <= 144.0f;

            editor->spawn_dragging = false;
            editor->vertex_dragging = false;
            editor->spawn_rotating = false;

            if (clicked_spawn) {
                editor->spawn_selected = true;
                editor->selected_vertex = -1;
                editor->spawn_dragging = true;
            } else {
                editor->spawn_selected = false;

                int vertex = editor_find_vertex(
                    editor,
                    level,
                    width,
                    height,
                    mouse_x,
                    mouse_y
                );

                editor->selected_vertex = vertex;

                if (vertex >= 0) {
                    editor->vertex_dragging = true;

                    float world_x, world_z;

                    screen_to_world(
                        editor,
                        width,
                        height,
                        mouse_x,
                        mouse_y,
                        &world_x,
                        &world_z
                    );

                    editor->vertex_drag_offset_x =
                        level->vertices[vertex].x - world_x;

                    editor->vertex_drag_offset_z =
                        level->vertices[vertex].z - world_z;
                }
            }
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) {

        if (event->button.button == SDL_BUTTON_MIDDLE) {
            editor->panning = false;
        }

        if (event->button.button == SDL_BUTTON_LEFT) {
            editor->spawn_dragging = false;
            editor->vertex_dragging = false;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (editor->panning) {
            editor->camera_x -= event->motion.xrel / editor->zoom;
            editor->camera_z -= event->motion.yrel / editor->zoom;
        }
        if (editor->spawn_dragging) {
            screen_to_world(editor, width, height, event->motion.x,
                            event->motion.y, &level->player_spawn.x,
                            &level->player_spawn.z);

            if (editor->snap_enabled) {
                level->player_spawn.x = editor_snap_value(
                    level->player_spawn.x,
                    editor->grid_size
                );

                level->player_spawn.z = editor_snap_value(
                    level->player_spawn.z,
                    editor->grid_size
                );
            }
        }
        if (editor->vertex_dragging &&
            editor->selected_vertex >= 0 &&
            editor->selected_vertex < level->vertex_count) {

            float world_x, world_z;

            screen_to_world(
                editor,
                width,
                height,
                event->motion.x,
                event->motion.y,
                &world_x,
                &world_z
            );

            Vertex *vertex =
                &level->vertices[editor->selected_vertex];

            float new_x = world_x + editor->vertex_drag_offset_x;
            float new_z = world_z + editor->vertex_drag_offset_z;

            if (editor->snap_enabled) {
                new_x = editor_snap_value(new_x, editor->grid_size);
                new_z = editor_snap_value(new_z, editor->grid_size);
            }

            vertex->x = new_x;
            vertex->z = new_z;
        }
        if (editor->spawn_rotating) {
            float sx, sy;
            world_to_screen(editor, level->player_spawn.x,
                            level->player_spawn.z, width, height, &sx, &sy);
            level->player_spawn.yaw = atan2f(event->motion.x - sx,
                                             -(event->motion.y - sy)) *
                                             (180.0f / 3.14159265358979323846f);
        }
    }

    // Toggle grid snapping with G
    if (event->type == SDL_EVENT_KEY_DOWN &&
        event->key.scancode == SDL_SCANCODE_G &&
        !event->key.repeat) {

        editor->snap_enabled = !editor->snap_enabled;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        switch (event->key.scancode) {
            case SDL_SCANCODE_1: editor_set_tool(editor, EDITOR_TOOL_SELECT); return;
            case SDL_SCANCODE_2: editor_set_tool(editor, EDITOR_TOOL_VERTEX); return;
            case SDL_SCANCODE_3: editor_set_tool(editor, EDITOR_TOOL_WALL); return;
            case SDL_SCANCODE_4: editor_set_tool(editor, EDITOR_TOOL_SECTOR); return;
            case SDL_SCANCODE_ESCAPE:
                if (editor->active_tool == EDITOR_TOOL_WALL &&
                    editor->wall_start_vertex >= 0) {
                    editor->wall_start_vertex = -1;
                } else {
                    editor_set_tool(editor, EDITOR_TOOL_SELECT);
                }
                return;
            default: break;
        }
    }

    // The active sector is also the owner of newly created walls.
    // Explicit assignments avoid serializing invalid -1 front-sector references.
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
        editor->active_tool == EDITOR_TOOL_WALL && level->sector_count > 0) {
        int *index = &editor->selected_sector;
        if (*index < 0 || *index >= level->sector_count) *index = 0;
        if (event->key.scancode == SDL_SCANCODE_LEFTBRACKET) {
            *index = (*index + level->sector_count - 1) % level->sector_count;
            return;
        }
        if (event->key.scancode == SDL_SCANCODE_RIGHTBRACKET) {
            *index = (*index + 1) % level->sector_count;
            return;
        }
        if (editor->selected_wall >= 0 && editor->selected_wall < level->wall_count) {
            Wall *w = &level->walls[editor->selected_wall];
            switch (event->key.scancode) {
                case SDL_SCANCODE_F:
                    if (w->back_sector != *index) w->front_sector = *index;
                    return;
                case SDL_SCANCODE_B:
                    if (w->front_sector != *index) w->back_sector = *index;
                    return;
                case SDL_SCANCODE_X:
                    w->back_sector = -1;
                    return;
                default: break;
            }
        }
    }

    // Sector heights are editable by index until spatial sector picking is implemented.
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat &&
        editor->active_tool == EDITOR_TOOL_SECTOR && level->sector_count > 0) {
        if (editor->selected_sector < 0 || editor->selected_sector >= level->sector_count)
            editor->selected_sector = 0;
        switch (event->key.scancode) {
            case SDL_SCANCODE_LEFTBRACKET:
                editor->selected_sector = (editor->selected_sector + level->sector_count - 1) % level->sector_count;
                return;
            case SDL_SCANCODE_RIGHTBRACKET:
                editor->selected_sector = (editor->selected_sector + 1) % level->sector_count;
                return;
            case SDL_SCANCODE_PAGEUP:
            case SDL_SCANCODE_PAGEDOWN: {
                Sector *sector = &level->sectors[editor->selected_sector];
                float delta = event->key.scancode == SDL_SCANCODE_PAGEUP ? 0.25f : -0.25f;
                if (event->key.mod & SDL_KMOD_SHIFT) {
                    float next = sector->ceiling_height + delta;
                    if (next > sector->floor_height) sector->ceiling_height = next;
                } else {
                    float next = sector->floor_height + delta;
                    if (next < sector->ceiling_height) sector->floor_height = next;
                }
                return;
            }
            default: break;
        }
    }

    if (event->type == SDL_EVENT_KEY_DOWN && editor->spawn_selected &&
        editor->active_tool == EDITOR_TOOL_SELECT) {
        switch (event->key.scancode) {
            case SDL_SCANCODE_R: editor->spawn_rotating = true; break;
            case SDL_SCANCODE_LEFTBRACKET: level->player_spawn.pitch -= 1.0f; break;
            case SDL_SCANCODE_RIGHTBRACKET: level->player_spawn.pitch += 1.0f; break;
            case SDL_SCANCODE_PAGEUP: level->player_spawn.y += 0.1f; break;
            case SDL_SCANCODE_PAGEDOWN: level->player_spawn.y -= 0.1f; break;
            default: break;
        }
        if (level->player_spawn.pitch > 89.0f) level->player_spawn.pitch = 89.0f;
        if (level->player_spawn.pitch < -89.0f) level->player_spawn.pitch = -89.0f;
    }
    if (event->type == SDL_EVENT_KEY_UP &&
        event->key.scancode == SDL_SCANCODE_R) {
        editor->spawn_rotating = false;
    }
}

static void editor_draw_grid(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    int width,
    int height
) {
    const float spacing = editor->zoom;

    SDL_SetRenderDrawColor(renderer, 48, 55, 68, 255);

    float origin_x =
        width * 0.5f - editor->camera_x * editor->zoom;

    float origin_y =
        height * 0.5f - editor->camera_z * editor->zoom;

    float start_x = fmodf(origin_x, spacing);
    float start_y = fmodf(origin_y, spacing);

    if (start_x < 0) start_x += spacing;
    if (start_y < 0) start_y += spacing;

    for (float x = start_x; x < width; x += spacing) {
        SDL_RenderLine(renderer, x, 0, x, (float)height);
    }

    for (float y = start_y; y < height; y += spacing) {
        SDL_RenderLine(renderer, 0, y, (float)width, y);
    }
}

static void editor_draw_walls(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level,
    int width,
    int height
) {
    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *wall = &level->walls[i];

        const Vertex *a = &level->vertices[wall->vertex_start];
        const Vertex *b = &level->vertices[wall->vertex_end];

        float x1, y1, x2, y2;

        world_to_screen(
            editor, a->x, a->z,
            width, height, &x1, &y1
        );

        world_to_screen(
            editor, b->x, b->z,
            width, height, &x2, &y2
        );

        if (editor->active_tool == EDITOR_TOOL_WALL && i == editor->selected_wall) {
            SDL_SetRenderDrawColor(renderer, 255, 220, 90, 255);
        } else if (wall->back_sector >= 0) {
            SDL_SetRenderDrawColor(renderer, 235, 175, 75, 255);
        } else {
            SDL_SetRenderDrawColor(renderer, 220, 225, 235, 255);
        }

        SDL_RenderLine(renderer, x1, y1, x2, y2);
    }
}

static void editor_draw_vertices(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level,
    int width,
    int height
) {
    SDL_SetRenderDrawColor(renderer, 90, 205, 245, 255);

    for (int i = 0; i < level->vertex_count; ++i) {
        float x, y;

        world_to_screen(
            editor,
            level->vertices[i].x,
            level->vertices[i].z,
            width, height,
            &x, &y
        );

        bool selected = i == editor->selected_vertex;
        bool wall_start = editor->active_tool == EDITOR_TOOL_WALL &&
                          i == editor->wall_start_vertex;

        if (wall_start) {
            SDL_SetRenderDrawColor(renderer, 255, 140, 50, 255);
        } else if (selected) {
            SDL_SetRenderDrawColor(
                renderer, 255, 220, 90, 255
            );
        } else {
            SDL_SetRenderDrawColor(
                renderer, 90, 205, 245, 255
            );
        }

        float size = (selected || wall_start) ? 10.0f : 6.0f;

        SDL_FRect point = {
            x - size * 0.5f,
            y - size * 0.5f,
            size,
            size
        };

        SDL_RenderFillRect(renderer, &point);

        if (selected) {
            SDL_SetRenderDrawColor(
                renderer, 255, 255, 255, 255
            );

            SDL_FRect outline = {
                x - 7.0f,
                y - 7.0f,
                14.0f,
                14.0f
            };

            SDL_RenderRect(renderer, &outline);
        }
    }
}

static void editor_draw_spawn(
    SDL_Renderer *renderer, const EditorViewport *editor,
    const Level *level, int width, int height
) {
    float x, y;
    world_to_screen(editor, level->player_spawn.x,
                    level->player_spawn.z, width, height, &x, &y);
    float yaw = level->player_spawn.yaw *
                (3.14159265358979323846f / 180.0f);
    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_FRect marker = {x - 6.0f, y - 6.0f, 12.0f, 12.0f};
    SDL_RenderFillRect(renderer, &marker);
    SDL_RenderLine(renderer, x, y,
                   x + sinf(yaw) * 30.0f,
                   y - cosf(yaw) * 30.0f);
    if (editor->spawn_selected) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_FRect selection = {x - 10.0f, y - 10.0f, 20.0f, 20.0f};
        SDL_RenderRect(renderer, &selection);
    }
}

static void editor_draw_controls(SDL_Renderer *renderer, const EditorViewport *editor)
{
    int width, height;
    if (!SDL_GetRenderOutputSize(renderer, &width, &height)) return;
    EditorLegend legend = editor_build_legend(editor);
    const float x = 12.0f, pad = 12.0f;
    float visible_width = fminf(legend.width, fmaxf(0.0f, (float)width - 24.0f));
    float visible_height = fminf(legend.height, fmaxf(0.0f, (float)height - 24.0f));
    if (visible_width <= 0.0f || visible_height <= 0.0f) return;
    float max_scroll = fmaxf(0.0f, legend.height - visible_height);
    editor_legend_scroll = fmaxf(0.0f, fminf(editor_legend_scroll, max_scroll));
    SDL_FRect panel = { x, 12.0f, visible_width, visible_height };
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 16, 24, 230);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 95, 110, 135, 255);
    SDL_RenderRect(renderer, &panel);

    /* Clip every label and separator to the panel. No text can escape it. */
    SDL_Rect clip = { (int)x + 1, 13, (int)visible_width - 2,
                      (int)visible_height - 2 };
    if (clip.w <= 0 || clip.h <= 0) return;
    SDL_SetRenderClipRect(renderer, &clip);
    const float y = 12.0f - editor_legend_scroll;
    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(renderer, x + pad, y + 12.0f, "LEVEL EDITOR");
    const char *tool_name = editor->active_tool == EDITOR_TOOL_VERTEX ? "VERTEX" :
        editor->active_tool == EDITOR_TOOL_WALL ? "WALL" :
        editor->active_tool == EDITOR_TOOL_SECTOR ? "SECTOR" : "SELECT";
    char line[96];
    snprintf(line, sizeof(line), "ACTIVE TOOL: %s", tool_name);
    SDL_RenderDebugText(renderer, x + pad, y + 29.0f, line);
    SDL_SetRenderDrawColor(renderer, 170, 205, 225, 255);
    const char *hint = editor->active_tool == EDITOR_TOOL_VERTEX ?
        "Place vertices or split walls" :
        editor->active_tool == EDITOR_TOOL_WALL ?
            (editor->wall_start_vertex >= 0 ? "Choose the ending vertex" : "Choose the starting vertex") :
        editor->active_tool == EDITOR_TOOL_SECTOR ? "Click room to select; edit heights" :
        "Select and move geometry";
    SDL_RenderDebugText(renderer, x + pad, y + 46.0f, hint);

    float cursor = y + 68.0f;
    SDL_SetRenderDrawColor(renderer, 95, 110, 135, 255);
    SDL_RenderLine(renderer, x + pad, cursor, x + visible_width - pad, cursor);
    cursor += 10.0f;
    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(renderer, x + pad, cursor, "TOOLS  (1-4)");
    cursor += 17.0f;
    const char *tools[] = {
        "1  Select / Move", "2  Vertex / Split Wall",
        "3  Draw Walls / Auto Portals", "4  Rooms / Sector Heights"
    };
    for (int i = 0; i < 4; ++i) {
        SDL_SetRenderDrawColor(renderer, i == (int)editor->active_tool ? 255 : 220,
            i == (int)editor->active_tool ? 215 : 225,
            i == (int)editor->active_tool ? 95 : 235, 255);
        SDL_RenderDebugText(renderer, x + pad, cursor, tools[i]);
        cursor += 15.0f;
    }
    cursor += 7.0f;
    SDL_SetRenderDrawColor(renderer, 95, 110, 135, 255);
    SDL_RenderLine(renderer, x + pad, cursor, x + visible_width - pad, cursor);
    cursor += 10.0f;
    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(renderer, x + pad, cursor, "ACTIVE TOOL ACTIONS");
    cursor += 17.0f;
    SDL_SetRenderDrawColor(renderer, 220, 225, 235, 255);
    for (int i = 0; i < legend.action_count; ++i) {
        SDL_RenderDebugText(renderer, x + pad, cursor, legend.actions[i]);
        cursor += 15.0f;
    }
    cursor += 7.0f;
    SDL_SetRenderDrawColor(renderer, 95, 110, 135, 255);
    SDL_RenderLine(renderer, x + pad, cursor, x + visible_width - pad, cursor);
    cursor += 10.0f;
    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(renderer, x + pad, cursor, "GENERAL CONTROLS");
    cursor += 17.0f;
    SDL_SetRenderDrawColor(renderer, 220, 225, 235, 255);
    const char *general[] = {
        "Middle Drag  Pan view", "Wheel        Zoom",
        "G            Toggle grid snap", "Ctrl+S       Save",
        "Esc          Cancel / Select"
    };
    for (int i = 0; i < 5; ++i) {
        SDL_RenderDebugText(renderer, x + pad, cursor, general[i]);
        cursor += 15.0f;
    }
    SDL_SetRenderClipRect(renderer, NULL);
    if (max_scroll > 0.0f) {
        /* Scrollbar shows that more controls are available below. */
        float track = visible_height - 8.0f;
        float thumb_height = fmaxf(20.0f, track * visible_height / legend.height);
        float thumb_y = 16.0f + (track - thumb_height) * editor_legend_scroll / max_scroll;
        SDL_FRect thumb = { x + visible_width - 5.0f, thumb_y, 2.0f, thumb_height };
        SDL_SetRenderDrawColor(renderer, 150, 175, 200, 230);
        SDL_RenderFillRect(renderer, &thumb);
    }
}

static void editor_draw_properties(SDL_Renderer *renderer,
    const EditorViewport *editor, const Level *level) {
    int width,height;
    if (!SDL_GetRenderOutputSize(renderer,&width,&height)) return;
    float x=inspector_x(width),y=INSPECTOR_TOP;
    if (x<12.0f || height<290) return;
    SDL_FRect bg={x,y,INSPECTOR_WIDTH,258.0f};
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer,12,16,24,238);
    SDL_RenderFillRect(renderer,&bg);
    SDL_SetRenderDrawColor(renderer,95,110,135,255);
    SDL_RenderRect(renderer,&bg);
    int mode=inspector_mode(editor,level);
    const char *heading=mode==1?"VERTEX":mode==2?"WALL":mode==3?"SECTOR":mode==4?"PLAYER SPAWN":"PROPERTIES";
    SDL_SetRenderDrawColor(renderer,255,215,95,255);
    SDL_RenderDebugText(renderer,x+12,y+12,heading);
    SDL_SetRenderDrawColor(renderer,175,190,205,255);
    SDL_RenderDebugText(renderer,x+12,y+33,"Click - / + to edit  |  Shift: fine");
    char rows[INSPECTOR_ROWS][80]={{0}};
    int count=0;
    if(mode==1) {
        const Vertex *v=&level->vertices[editor->selected_vertex];
        snprintf(rows[count++],80,"X: %.2f",v->x);
        snprintf(rows[count++],80,"Z: %.2f",v->z);
    } else if(mode==2) {
        const Wall *w=&level->walls[editor->selected_wall];
        snprintf(rows[count++],80,"Front sector: %d",w->front_sector);
        snprintf(rows[count++],80,"Back sector: %d",w->back_sector);
        snprintf(rows[count++],80,"Texture ID: %d",w->texture_id);
        snprintf(rows[count++],80,"Wall #%d",editor->selected_wall);
        snprintf(rows[count++],80,"%s",w->back_sector>=0?"Portal":"Solid / unassigned");
    } else if(mode==3) {
        int i=editor->selected_sector;
        if(i<0||i>=level->sector_count)i=0;
        const Sector *sc=&level->sectors[i];
        snprintf(rows[count++],80,"Sector: %d / %d",i,level->sector_count-1);
        snprintf(rows[count++],80,"Floor: %.2f",sc->floor_height);
        snprintf(rows[count++],80,"Ceiling: %.2f",sc->ceiling_height);
        snprintf(rows[count++],80,"Floor texture: %d",sc->floor_texture_id);
        snprintf(rows[count++],80,"Ceiling texture: %d",sc->ceiling_texture_id);
    } else if(mode==4) {
        const PlayerSpawn *sp=&level->player_spawn;
        snprintf(rows[count++],80,"X: %.2f",sp->x);
        snprintf(rows[count++],80,"Y: %.2f",sp->y);
        snprintf(rows[count++],80,"Z: %.2f",sp->z);
        snprintf(rows[count++],80,"Yaw: %.1f",sp->yaw);
        snprintf(rows[count++],80,"Pitch: %.1f",sp->pitch);
    } else {
        snprintf(rows[count++],80,"Select a wall, room,");
        snprintf(rows[count++],80,"vertex or player spawn");
    }
    for(int i=0;i<count;i++) {
        float yy=y+INSPECTOR_FIRST+i*INSPECTOR_ROW;
        SDL_SetRenderDrawColor(renderer,220,225,235,255);
        SDL_RenderDebugText(renderer,x+12,yy+5,rows[i]);
        bool editable=(mode==1 && i<2)||(mode==2 && i<3)||
                      (mode==3 && i<5)||(mode==4 && i<5);
        if(!editable)continue;
        SDL_FRect minus={x+190,yy,28,19},plus={x+223,yy,29,19};
        SDL_SetRenderDrawColor(renderer,53,65,82,255);
        SDL_RenderFillRect(renderer,&minus); SDL_RenderFillRect(renderer,&plus);
        SDL_SetRenderDrawColor(renderer,255,215,95,255);
        SDL_RenderDebugText(renderer,x+200,yy+5,"-");
        SDL_RenderDebugText(renderer,x+234,yy+5,"+");
    }
}

void editor_viewport_show_save_notification(
    EditorViewport *editor,
    bool success
) {
    editor->save_notification_visible = true;
    editor->save_notification_success = success;
    editor->save_notification_time = SDL_GetTicks();
}

static void editor_draw_save_notification(
    SDL_Renderer *renderer,
    const EditorViewport *editor
) {
    if (!editor->save_notification_visible)
        return;

    Uint64 elapsed =
        SDL_GetTicks() - editor->save_notification_time;

    // Display for 3 seconds
    if (elapsed >= 3000)
        return;

    int width, height;

    if (!SDL_GetRenderOutputSize(renderer, &width, &height))
        return;

    const float panel_width = 260.0f;
    const float panel_height = 60.0f;

    const float x = (float)width - panel_width - 12.0f;
    const float y = 215.0f;

    SDL_FRect panel = {
        x, y, panel_width, panel_height
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (editor->save_notification_success) {
        SDL_SetRenderDrawColor(renderer, 20, 55, 35, 235);
    } else {
        SDL_SetRenderDrawColor(renderer, 75, 25, 25, 235);
    }

    SDL_RenderFillRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 180, 190, 200, 255);
    SDL_RenderRect(renderer, &panel);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    SDL_RenderDebugText(
        renderer,
        x + 12,
        y + 12,
        editor->save_notification_success
            ? "LEVEL SAVED"
            : "SAVE FAILED"
    );

    SDL_RenderDebugText(
        renderer,
        x + 12,
        y + 32,
        editor->save_notification_success
            ? "Changes saved successfully."
            : "Could not save level."
    );
}

void editor_viewport_render(
    SDL_Renderer *renderer,
    const EditorViewport *editor,
    const Level *level
) {
    int width, height;

    if (!SDL_GetRenderOutputSize(renderer, &width, &height))
        return;

    SDL_SetRenderDrawColor(renderer, 24, 29, 38, 255);
    SDL_RenderClear(renderer);

    editor_draw_grid(renderer, editor, width, height);
    editor_draw_walls(renderer, editor, level, width, height);
    editor_draw_vertices(renderer, editor, level, width, height);
    editor_draw_spawn(renderer, editor, level, width, height);

    editor_draw_controls(renderer, editor);
    editor_draw_properties(renderer, editor, level);

    editor_draw_save_notification(renderer, editor);

    SDL_RenderPresent(renderer);
}