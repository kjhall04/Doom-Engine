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

static void editor_history_reset(void);
void editor_viewport_init(EditorViewport *editor) {
    editor_history_reset();
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
    editor->selected_sector = -1;
    editor->selected_light = -1;
    editor->show_sector_fill = true;
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
            if (edge_origin(l,e)!=at || e==(edge^1)) continue;
            Vertex to=l->vertices[edge_target(l,e)];
            float a=atan2f(to.z-v.z,to.x-v.x);
            float clockwise=reverse-a;
            while (clockwise<0) clockwise+=6.28318530718f;
            while (clockwise>=6.28318530718f) clockwise-=6.28318530718f;
            if (clockwise<best) { best=clockwise; chosen=e; }
        }
    }
    return chosen>=0 ? chosen : (edge^1);
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
                     .floor_texture_id=3,.ceiling_texture_id=4,.light_level=255,.light_r=255,.light_g=255,.light_b=255};
        SDL_Log("Created sector %d from closed geometry",owner);
    }
    for (int i=0;i<count;i++) edge_assign(l,edges[i],owner);
    return owner;
}
/* Rebuild face membership from all closed loops. Each bounded face gets exactly
   one sector. Shared walls are portals; exterior half-edges remain -1.
   Keep sector IDs when possible to preserve existing floor/ceiling properties. */
static void auto_assign_closed_faces(Level *l) {
    if (!l || l->wall_count > 100000) return;
    if (l->wall_count < 3) {
        for (int i=0;i<l->wall_count;i++) {
            l->walls[i].front_sector=-1;
            l->walls[i].back_sector=-1;
        }
        return;
    }
    int count=l->wall_count*2;
    int *path=malloc((size_t)l->wall_count*sizeof(int));
    unsigned char *seen=calloc((size_t)count,1);
    int *old=malloc((size_t)count*sizeof(int));
    unsigned char *claimed=calloc((size_t)l->sector_count+1,1);
    if (!path || !seen || !old || !claimed) {
        free(path);free(seen);free(old);free(claimed);return;
    }
    for (int e=0;e<count;e++) old[e]=edge_sector(l,e);
    /* Every half-edge is reclassified. No stale portals survive a deletion. */
    for (int e=0;e<count;e++) edge_assign(l,e,-1);
    for (int e=0;e<count;e++) {
        if (seen[e]) continue;
        double area=0;
        int n=trace_face(l,e,path,&area);
        if (!n) {seen[e]=1;continue;}
        for (int i=0;i<n;i++) seen[path[i]]=1;
        int owner=-1,score=0;
        for (int i=0;i<n;i++) {
            int candidate=old[path[i]];
            if (candidate<0 || candidate>=l->sector_count || claimed[candidate]) continue;
            int votes=0;
            for (int j=0;j<n;j++) if (old[path[j]]==candidate) votes++;
            if (votes>score) {owner=candidate;score=votes;}
        }
        if (owner<0) {
            /* A face with no old owner must receive a fresh sector ID.
               Reusing arbitrary old IDs silently swaps room properties. */
            if (l->sector_count>=INT_MAX-1) break;
            int inherit=-1;
            for (int i=0;i<n;i++) {
                int candidate=old[path[i]];
                if (candidate>=0 && candidate<l->sector_count) {inherit=candidate;break;}
            }
            int new_count=l->sector_count+1;
            Sector *new_sectors=realloc(l->sectors,(size_t)new_count*sizeof(Sector));
            unsigned char *new_claimed=realloc(claimed,(size_t)new_count+1);
            if (!new_sectors || !new_claimed) {
                if (new_sectors) l->sectors=new_sectors;
                if (new_claimed) claimed=new_claimed;
                break;
            }
            l->sectors=new_sectors;
            claimed=new_claimed;
            owner=l->sector_count++;
            claimed[owner]=0;
            l->sectors[owner]=inherit>=0?l->sectors[inherit]:
                (Sector){.floor_height=0,.ceiling_height=5,
                         .floor_texture_id=3,.ceiling_texture_id=4,.light_level=255,.light_r=255,.light_g=255,.light_b=255};
        }
        claimed[owner]=1;
        for (int i=0;i<n;i++) edge_assign(l,path[i],owner);
    }
    free(path);free(seen);free(old);free(claimed);
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
                                 .floor_texture_id=3,.ceiling_texture_id=4,.light_level=255,.light_r=255,.light_g=255,.light_b=255};
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
    editor->room_dragging = false;
    editor->light_dragging = false;
    SDL_Log("Editor tool: %s", tool == EDITOR_TOOL_VERTEX ? "VERTEX" :
            tool == EDITOR_TOOL_WALL ? "WALL" :
            tool == EDITOR_TOOL_SECTOR ? "SECTOR" : tool == EDITOR_TOOL_ROOM ? "DRAW ROOM" : "SELECT");
}

// Shared legend content is used for both rendering and mouse hit testing.
typedef struct EditorLegend {
    const char *actions[14];
    int action_count;
    float width;
    float height;
} EditorLegend;

/* The legend is displayed at full height; no scrolling state is needed. */

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
        case EDITOR_TOOL_LIGHT:
            legend.actions[legend.action_count++] = "Click empty grid: add point light";
            legend.actions[legend.action_count++] = "Click light: select / drag";
            legend.actions[legend.action_count++] = "Delete: remove selected light";
            legend.actions[legend.action_count++] = "Arrows: change radius/intensity";
            legend.actions[legend.action_count++] = "PgUp/Dn: change height";
            legend.actions[legend.action_count++] = "C: cycle light color";
            legend.actions[legend.action_count++] = "F: steady/pulse/flicker";
            legend.actions[legend.action_count++] = "[ / ]: ambient light +/-";
            break;
        case EDITOR_TOOL_ROOM:
            legend.actions[legend.action_count++] = "Drag empty grid to draw a room";
            legend.actions[legend.action_count++] = "Shared edges become portals";
            legend.actions[legend.action_count++] = "Snap corners to existing vertices";
            legend.actions[legend.action_count++] = "Right click cancels room";
            break;
        case EDITOR_TOOL_SECTOR:
            legend.actions[legend.action_count++] = "Click Room   Select sector";
            legend.actions[legend.action_count++] = "Shift+Click  Separate room into sector";
            legend.actions[legend.action_count++] = "[ / ]       Previous / next sector";
            legend.actions[legend.action_count++] = "PgUp/PgDn   Raise / lower floor 0.25";
            legend.actions[legend.action_count++] = "Shift+PgUp/Dn  Adjust ceiling 0.25";
            legend.actions[legend.action_count++] = "Room tool creates sectors automatically";
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
        "3  Draw Walls / Auto Portals", "4  Select / Edit Sectors",
        "5  Draw Room (drag rectangle)", "6  Place / Edit Lights"
    };
    const char *general[] = {
        "Middle Drag  Pan view", "Wheel        Zoom",
        "G            Toggle grid snap", "Ctrl+S       Save",
        "Esc          Exit editor", "Ctrl+Z / Y   Undo / Redo",
        "H            Toggle sector colors", "6            Light editor"
    };
    size_t longest = SDL_strlen("ACTIVE TOOL: SECTOR");
    for (int i = 0; i < 6; ++i) {
        size_t len = SDL_strlen(tools[i]);
        if (len > longest) longest = len;
    }
    for (int i = 0; i < legend.action_count; ++i) {
        size_t len = SDL_strlen(legend.actions[i]);
        if (len > longest) longest = len;
    }
    for (int i = 0; i < 8; ++i) {
        size_t len = SDL_strlen(general[i]);
        if (len > longest) longest = len;
    }
    legend.width = (float)longest * 8.0f + 24.0f;
    // Header + tools + actions + general, with section separators.
    /* Match the actual cursor increments in editor_draw_controls.
       Last line starts at y + 283 + action_count*15; reserve 12px below it. */
    legend.height = 385.0f + legend.action_count * 15.0f;
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
    if (mx >= width - 300.0f && mx <= width - 12.0f &&
        my >= 12.0f && my <= 12.0f + 64.0f + 5.0f * 24.0f + 12.0f)
        return true;
    return false;
}


/* Right-hand inspector: fixed-size controls with explicit hit targets. */
#define INSPECTOR_WIDTH 288.0f
#define INSPECTOR_TOP 12.0f
#define INSPECTOR_ROW 24.0f
#define INSPECTOR_FIRST 64.0f
#define INSPECTOR_ROWS 9
#define INSPECTOR_BOTTOM_PAD 12.0f

static int inspector_row_count(int mode) {
    return mode == 1 ? 2 : mode == 2 ? 5 : mode == 3 ? 9 : mode == 4 ? 5 : mode == 5 ? 9 : 2;
}
static float inspector_panel_height(int mode) {
    return INSPECTOR_FIRST + inspector_row_count(mode) * INSPECTOR_ROW + INSPECTOR_BOTTOM_PAD;
}
static float inspector_x(int width) { return (float)width - INSPECTOR_WIDTH - 12.0f; }
static int inspector_mode(const EditorViewport *ed, const Level *l) {
    if(ed->active_tool==EDITOR_TOOL_LIGHT && ed->selected_light>=0 && ed->selected_light<l->light_count) return 5;
    if (ed->active_tool == EDITOR_TOOL_WALL && ed->selected_wall >= 0 && ed->selected_wall < l->wall_count) return 2;
    if (ed->active_tool == EDITOR_TOOL_SECTOR && ed->selected_sector >= 0 && ed->selected_sector < l->sector_count) return 3;
    if (ed->spawn_selected && ed->active_tool == EDITOR_TOOL_SELECT) return 4;
    if (ed->selected_vertex >= 0 && ed->selected_vertex < l->vertex_count) return 1;
    return 0;
}
static bool inspector_click(EditorViewport *ed, Level *l, int width, int height, float mx, float my) {
    float x=inspector_x(width);
    int mode=inspector_mode(ed,l);
    float visible_height=fminf(inspector_panel_height(mode),fmaxf(0.0f,(float)height-24.0f));
    if (x<12.0f || visible_height<INSPECTOR_FIRST || mx<x || mx>x+INSPECTOR_WIDTH ||
        my<INSPECTOR_TOP || my>INSPECTOR_TOP+visible_height) return false;
    int row=(int)floorf((my-(INSPECTOR_TOP+INSPECTOR_FIRST))/INSPECTOR_ROW);
    if (row<0 || row>=inspector_row_count(mode) ||
        INSPECTOR_FIRST+(row+1)*INSPECTOR_ROW>visible_height) return true;
    /* +/- buttons on the right; text remains read-only for other clicks. */
    int delta=0;
    if (mx>=x+218 && mx<x+246) delta=-1;
    if (mx>=x+251 && mx<=x+280) delta=1;
    if (!delta) return true;
    bool fine=(SDL_GetModState() & SDL_KMOD_SHIFT)!=0;
    float step=fine?0.1f:1.0f;
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
        int *light_fields[] = {&sc->light_level, &sc->light_r, &sc->light_g, &sc->light_b};
        if (row>=5 && row<=8) {
            int *field=light_fields[row-5];
            int next=*field+delta*(fine?1:16);
            *field=next<0?0:(next>255?255:next);
        }
    } else if(mode==5) {
        PointLight *light=&l->lights[ed->selected_light];
        if(row==0) light->x+=delta*step;
        if(row==1) light->y+=delta*step;
        if(row==2) light->z+=delta*step;
        if(row==3) light->radius=fmaxf(0.5f,fminf(1000.0f,light->radius+delta*(fine?0.1f:1.0f)));
        if(row==4) light->intensity=fmaxf(0.0f,fminf(100.0f,light->intensity+delta*(fine?0.05f:0.2f)));
        int *rgb[]={&light->r,&light->g,&light->b};
        if(row>=5 && row<=7) {
            int *v=rgb[row-5]; *v+=delta*(fine?1:16);
            if(*v<0)*v=0;
            if(*v>255)*v=255;
        }
        if(row==8) light->effect=(light->effect+delta+3)%3;
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

/* Rectangle room builder: reuse vertices and existing walls, then rebuild
   sector topology once after the complete shape is present. */
static int room_vertex(Level *l,float x,float z) {
    for (int i=0;i<l->vertex_count;i++)
        if (fabsf(l->vertices[i].x-x)<0.0001f && fabsf(l->vertices[i].z-z)<0.0001f) return i;
    int result=-1;
    return editor_create_vertex(l,x,z,&result) ? result : -1;
}
static bool room_add_wall(Level *l,int a,int b) {
    if (a<0 || b<0 || a==b) return false;
    for (int i=0;i<l->wall_count;i++) {
        Wall w=l->walls[i];
        if ((w.vertex_start==a && w.vertex_end==b) || (w.vertex_start==b && w.vertex_end==a)) return true;
    }
    if (wall_would_cross(l,a,b) || l->wall_count>=INT_MAX-1) return false;
    Wall *p=realloc(l->walls,(size_t)(l->wall_count+1)*sizeof(Wall));
    if (!p) return false;
    l->walls=p;
    l->walls[l->wall_count++]=(Wall){.vertex_start=a,.vertex_end=b,
        .front_sector=-1,.back_sector=-1,.texture_id=0};
    return true;
}
/* Room brush supports complete and partial shared edges. Existing walls are
   split at new corners before edges are inserted, so a shared segment is a
   single wall with two owners, never overlapping duplicate walls. */
static bool room_make_rectangle(Level *l,float x1,float z1,float x2,float z2) {
    float left=fminf(x1,x2),right=fmaxf(x1,x2);
    float top=fminf(z1,z2),bottom=fmaxf(z1,z2);
    if (right-left<0.01f || bottom-top<0.01f) return false;
    /* Reject interior overlap with an existing face, including containment. */
    const float eps=0.0001f;
    float cx=(left+right)*0.5f,cz=(top+bottom)*0.5f;
    if (sector_at_point(l,cx,cz,false,false)>=0) return false;
    for (int i=0;i<l->wall_count;i++) {
        Wall w=l->walls[i];
        Vertex a=l->vertices[w.vertex_start],b=l->vertices[w.vertex_end];
        float mx=(a.x+b.x)*0.5f,mz=(a.z+b.z)*0.5f;
        if (mx>left+eps && mx<right-eps && mz>top+eps && mz<bottom-eps) return false;
    }
    Vertex corners[4]={{left,top},{right,top},{right,bottom},{left,bottom}};
    /* Split any wall crossed by a rectangle corner, including partial shares. */
    for (int k=0;k<4;k++) {
        for (int i=0;i<l->wall_count;i++) {
            Wall w=l->walls[i];
            Vertex a=l->vertices[w.vertex_start],b=l->vertices[w.vertex_end];
            float dx=b.x-a.x,dz=b.z-a.z,den=dx*dx+dz*dz;
            if (den<eps*eps) continue;
            float t=((corners[k].x-a.x)*dx+(corners[k].z-a.z)*dz)/den;
            float px=a.x+t*dx,pz=a.z+t*dz;
            if (t>eps && t<1.0f-eps && fabsf(px-corners[k].x)<eps && fabsf(pz-corners[k].z)<eps) {
                int created=-1;
                if (!editor_split_wall(l,i,t,&created)) return false;
                break;
            }
        }
    }
    int v[4];
    for (int i=0;i<4;i++) {
        v[i]=room_vertex(l,corners[i].x,corners[i].z);
        if (v[i]<0) return false;
    }
    /* Connect through any existing vertices on the proposed boundary. */
    for (int k=0;k<4;k++) {
        Vertex a=l->vertices[v[k]],b=l->vertices[v[(k+1)%4]];
        float dx=b.x-a.x,dz=b.z-a.z,den=dx*dx+dz*dz;
        int current=v[k];
        for (;;) {
            int next=-1;
            float best=2.0f;
            Vertex from=l->vertices[current];
            float from_t=((from.x-a.x)*dx+(from.z-a.z)*dz)/den;
            for (int j=0;j<l->vertex_count;j++) {
                Vertex p=l->vertices[j];
                float t=((p.x-a.x)*dx+(p.z-a.z)*dz)/den;
                float px=a.x+t*dx,pz=a.z+t*dz;
                if (t<=from_t+eps || t>1.0f+eps || t>=best ||
                    fabsf(p.x-px)>eps || fabsf(p.z-pz)>eps) continue;
                next=j;best=t;
            }
            if (next<0 || !room_add_wall(l,current,next)) return false;
            current=next;
            if (current==v[(k+1)%4]) break;
        }
    }
    auto_assign_closed_faces(l);
    return sector_at_point(l,cx,cz,false,false)>=0;
}

/* Deep-copy edit history. Snapshots are owned by the history stack and never
   alias the live Level's dynamically allocated arrays. */
#define EDITOR_HISTORY_LIMIT 64
typedef struct EditorSnapshot {
    Level level;
    int selected_vertex, selected_wall, selected_sector;
    bool spawn_selected;
} EditorSnapshot;
static EditorSnapshot undo_stack[EDITOR_HISTORY_LIMIT];
static EditorSnapshot redo_stack[EDITOR_HISTORY_LIMIT];
static int undo_count, redo_count;
static EditorSnapshot drag_start;
static bool drag_pending;

static void snapshot_dispose(EditorSnapshot *s) {
    free(s->level.vertices);
    free(s->level.walls);
    free(s->level.sectors);
    free(s->level.lights);
    memset(s,0,sizeof(*s));
}
static bool snapshot_take(EditorSnapshot *s, const Level *l, const EditorViewport *ed) {
    memset(s,0,sizeof(*s));
    s->level=*l;
    s->level.vertices=NULL;
    s->level.walls=NULL;
    s->level.sectors=NULL;
    s->level.lights=NULL;
    if (l->vertex_count>0) {
        s->level.vertices=malloc((size_t)l->vertex_count*sizeof(Vertex));
        if (!s->level.vertices) goto fail;
        memcpy(s->level.vertices,l->vertices,(size_t)l->vertex_count*sizeof(Vertex));
    }
    if (l->wall_count>0) {
        s->level.walls=malloc((size_t)l->wall_count*sizeof(Wall));
        if (!s->level.walls) goto fail;
        memcpy(s->level.walls,l->walls,(size_t)l->wall_count*sizeof(Wall));
    }
    if (l->sector_count>0) {
        s->level.sectors=malloc((size_t)l->sector_count*sizeof(Sector));
        if (!s->level.sectors) goto fail;
        memcpy(s->level.sectors,l->sectors,(size_t)l->sector_count*sizeof(Sector));
    }
    if(l->light_count>0) {
        s->level.lights=malloc((size_t)l->light_count*sizeof(PointLight));
        if(!s->level.lights) goto fail;
        memcpy(s->level.lights,l->lights,(size_t)l->light_count*sizeof(PointLight));
    }
    s->selected_vertex=ed->selected_vertex;
    s->selected_wall=ed->selected_wall;
    s->selected_sector=ed->selected_sector;
    s->spawn_selected=ed->spawn_selected;
    return true;
fail:
    snapshot_dispose(s);
    return false;
}
static bool snapshot_differs(const EditorSnapshot *s, const Level *l) {
    if (s->level.light_count!=l->light_count ||
        s->level.ambient_r!=l->ambient_r || s->level.ambient_g!=l->ambient_g || s->level.ambient_b!=l->ambient_b ||
        s->level.vertex_count!=l->vertex_count ||
        s->level.wall_count!=l->wall_count ||
        s->level.sector_count!=l->sector_count) return true;
    if (memcmp(&s->level.player_spawn,&l->player_spawn,sizeof(PlayerSpawn))) return true;
    if (l->vertex_count && memcmp(s->level.vertices,l->vertices,(size_t)l->vertex_count*sizeof(Vertex))) return true;
    if (l->wall_count && memcmp(s->level.walls,l->walls,(size_t)l->wall_count*sizeof(Wall))) return true;
    if (l->sector_count && memcmp(s->level.sectors,l->sectors,(size_t)l->sector_count*sizeof(Sector))) return true;
    if (l->light_count && memcmp(s->level.lights,l->lights,(size_t)l->light_count*sizeof(PointLight))) return true;
    return false;
}
static void history_clear(EditorSnapshot *stack,int *count) {
    for (int i=0;i<*count;i++) snapshot_dispose(&stack[i]);
    *count=0;
}
static void history_push(EditorSnapshot *stack,int *count,EditorSnapshot *s) {
    if (*count==EDITOR_HISTORY_LIMIT) {
        snapshot_dispose(&stack[0]);
        memmove(stack,stack+1,(EDITOR_HISTORY_LIMIT-1)*sizeof(*stack));
        --*count;
    }
    stack[(*count)++]=*s;
    memset(s,0,sizeof(*s));
}
static void history_commit(EditorSnapshot *before,const Level *l) {
    if (snapshot_differs(before,l)) {
        history_push(undo_stack,&undo_count,before);
        history_clear(redo_stack,&redo_count);
    } else snapshot_dispose(before);
}
static void history_apply(EditorSnapshot *s,Level *l,EditorViewport *ed) {
    free(l->vertices); free(l->walls); free(l->sectors); free(l->lights);
    *l=s->level;
    ed->selected_vertex=s->selected_vertex;
    ed->selected_light=-1; ed->light_dragging=false;
    ed->selected_wall=s->selected_wall;
    ed->selected_sector=s->selected_sector;
    ed->spawn_selected=s->spawn_selected;
    ed->vertex_dragging=false;
    ed->spawn_dragging=false;
    ed->spawn_rotating=false;
    ed->wall_start_vertex=-1;
    memset(s,0,sizeof(*s));
}
static void editor_history_reset(void) {
    history_clear(undo_stack,&undo_count);
    history_clear(redo_stack,&redo_count);
    snapshot_dispose(&drag_start);
    drag_pending=false;
}
static void history_step(Level *l,EditorViewport *ed,bool redo) {
    EditorSnapshot *from=redo?redo_stack:undo_stack;
    int *from_count=redo?&redo_count:&undo_count;
    EditorSnapshot *to=redo?undo_stack:redo_stack;
    int *to_count=redo?&undo_count:&redo_count;
    if (!*from_count) return;
    EditorSnapshot now;
    if (!snapshot_take(&now,l,ed)) return;
    EditorSnapshot previous=from[--*from_count];
    memset(&from[*from_count],0,sizeof(EditorSnapshot));
    history_apply(&previous,l,ed);
    history_push(to,to_count,&now);
    SDL_Log("Editor %s: %d undo, %d redo",redo?"redo":"undo",undo_count,redo_count);
}
/* Reject moved vertices that introduce zero-length walls, crossings, overlaps
   or T-junctions. Existing wall/sector references remain stable on valid moves. */
static bool geometry_valid_after_move(const Level *l) {
    for (int i=0;i<l->wall_count;i++) {
        Wall a=l->walls[i];
        if (a.vertex_start<0 || a.vertex_end<0 ||
            a.vertex_start>=l->vertex_count || a.vertex_end>=l->vertex_count ||
            a.vertex_start==a.vertex_end) return false;
        Vertex p=l->vertices[a.vertex_start],q=l->vertices[a.vertex_end];
        float dx=p.x-q.x,dz=p.z-q.z;
        if (dx*dx+dz*dz<0.000001f) return false;
    }
    for (int i=0;i<l->vertex_count;i++) for(int j=i+1;j<l->vertex_count;j++) {
        float dx=l->vertices[i].x-l->vertices[j].x;
        float dz=l->vertices[i].z-l->vertices[j].z;
        if (dx*dx+dz*dz<0.000001f) return false;
    }
    for (int i=0;i<l->wall_count;i++) {
        Wall a=l->walls[i]; Vertex p=l->vertices[a.vertex_start],q=l->vertices[a.vertex_end];
        for(int j=i+1;j<l->wall_count;j++) {
            Wall b=l->walls[j]; Vertex r=l->vertices[b.vertex_start],s=l->vertices[b.vertex_end];
            if (orient2(p,q,r)*orient2(p,q,s)<-0.000001f &&
                orient2(r,s,p)*orient2(r,s,q)<-0.000001f) return false;
            Vertex pts[4]={p,q,r,s};
            for(int k=0;k<4;k++) {
                Vertex v=pts[k],u=k<2?r:p,w=k<2?s:q;
                if (fabsf(orient2(u,w,v))>0.0001f) continue;
                float dot=(v.x-u.x)*(v.x-w.x)+(v.z-u.z)*(v.z-w.z);
                if (dot<-0.0001f) return false;
            }
        }
    }
    return true;
}
static void editor_delete_selection(EditorViewport *ed,Level *l) {
    if (ed->active_tool==EDITOR_TOOL_WALL && ed->selected_wall>=0 && ed->selected_wall<l->wall_count) {
        int at=ed->selected_wall;
        memmove(l->walls+at,l->walls+at+1,(size_t)(l->wall_count-at-1)*sizeof(Wall));
        --l->wall_count;
        ed->selected_wall=-1;
        auto_assign_closed_faces(l);
        SDL_Log("Deleted wall. Reconnect open boundaries before saving.");
    } else if (ed->selected_vertex>=0 && ed->selected_vertex<l->vertex_count &&
               (ed->active_tool==EDITOR_TOOL_SELECT || ed->active_tool==EDITOR_TOOL_VERTEX)) {
        int at=ed->selected_vertex;
        for(int i=l->wall_count-1;i>=0;i--) {
            if(l->walls[i].vertex_start==at || l->walls[i].vertex_end==at) {
                memmove(l->walls+i,l->walls+i+1,(size_t)(l->wall_count-i-1)*sizeof(Wall));
                --l->wall_count;
            }
        }
        memmove(l->vertices+at,l->vertices+at+1,(size_t)(l->vertex_count-at-1)*sizeof(Vertex));
        --l->vertex_count;
        for(int i=0;i<l->wall_count;i++) {
            if(l->walls[i].vertex_start>at) --l->walls[i].vertex_start;
            if(l->walls[i].vertex_end>at) --l->walls[i].vertex_end;
        }
        ed->selected_vertex=-1;
        auto_assign_closed_faces(l);
        SDL_Log("Deleted vertex and attached walls. Reconnect open boundaries before saving.");
    }
}

static void editor_process_event_impl(
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
            return; /* Do not zoom while the pointer is over the legend. */
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
            editor->active_tool == EDITOR_TOOL_ROOM) {editor->room_dragging=false;return;}

        if (event->button.button == SDL_BUTTON_RIGHT &&
            editor->active_tool == EDITOR_TOOL_WALL) {
            editor->wall_start_vertex = -1;
            return;
        }

        if (event->button.button == SDL_BUTTON_LEFT) {
            /* The five tool labels double as clickable tool buttons. */
            EditorLegend legend=editor_build_legend(editor);
            float pw=fminf(legend.width,fmaxf(0.0f,(float)width-24.0f));
            float ph=fminf(legend.height,fmaxf(0.0f,(float)height-24.0f));
            float ty=event->button.y-12.0f;
            if (event->button.x>=24.0f && event->button.x<=12.0f+pw &&
                event->button.y>=12.0f && event->button.y<=12.0f+ph &&
                ty>=95.0f && ty<185.0f) {
                int which=(int)((ty-95.0f)/15.0f);
                if (which>=0 && which<6) editor_set_tool(editor,(EditorTool)which);
                return;
            }
            if (inspector_click(editor, level, width, height, event->button.x, event->button.y)) return;
            if (editor_point_in_panel(editor, event->button.x, event->button.y, width, height)) return;

            if (editor->active_tool == EDITOR_TOOL_LIGHT) {
                for(int i=level->light_count-1;i>=0;i--) {
                    float sx,sy;
                    world_to_screen(editor,level->lights[i].x,level->lights[i].z,width,height,&sx,&sy);
                    float dx=sx-event->button.x,dy=sy-event->button.y;
                    if(dx*dx+dy*dy<144.0f) {
                        editor->selected_light=i; editor->light_dragging=true; return;
                    }
                }
                if(level->light_count>=1024) return;
                PointLight *new_lights=realloc(level->lights,(size_t)(level->light_count+1)*sizeof(PointLight));
                if(!new_lights) return;
                level->lights=new_lights;
                int i=level->light_count++;
                PointLight *light=&level->lights[i];
                screen_to_world(editor,width,height,event->button.x,event->button.y,&light->x,&light->z);
                light->y=2.5f; light->radius=8.0f; light->intensity=1.2f;
                light->r=255;light->g=210;light->b=150;light->effect=0;
                editor->selected_light=i;
                return;
            }
            if (editor->active_tool == EDITOR_TOOL_ROOM) {
                screen_to_world(editor,width,height,event->button.x,event->button.y,
                    &editor->room_start_x,&editor->room_start_z);
                if (editor->snap_enabled) {
                    editor->room_start_x=editor_snap_value(editor->room_start_x,editor->grid_size);
                    editor->room_start_z=editor_snap_value(editor->room_start_z,editor->grid_size);
                }
                editor->room_end_x=editor->room_start_x;
                editor->room_end_z=editor->room_start_z;
                editor->room_dragging=true;
                return;
            }
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
                int sector=sector_at_point(level,wx,wz,false,
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

            editor->light_dragging = false;
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
            if (editor->active_tool==EDITOR_TOOL_ROOM && editor->room_dragging) {
                screen_to_world(editor,width,height,event->button.x,event->button.y,
                    &editor->room_end_x,&editor->room_end_z);
                if (editor->snap_enabled) {
                    editor->room_end_x=editor_snap_value(editor->room_end_x,editor->grid_size);
                    editor->room_end_z=editor_snap_value(editor->room_end_z,editor->grid_size);
                }
                editor->room_dragging=false;
                editor->room_failed=!room_make_rectangle(level,editor->room_start_x,editor->room_start_z,
                    editor->room_end_x,editor->room_end_z);
                if (editor->room_failed)
                    SDL_Log("Room not created: check overlap, alignment, or wall junctions");
                else {
                    float cx=(editor->room_start_x+editor->room_end_x)*0.5f;
                    float cz=(editor->room_start_z+editor->room_end_z)*0.5f;
                    int sector=sector_at_point(level,cx,cz,false,false);
                    if (sector>=0) editor->selected_sector=sector;
                }
            }
            editor->light_dragging = false;
            editor->spawn_dragging = false;
            editor->vertex_dragging = false;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {
        if (editor->room_dragging) {
            screen_to_world(editor,width,height,event->motion.x,event->motion.y,
                &editor->room_end_x,&editor->room_end_z);
            if (editor->snap_enabled) {
                editor->room_end_x=editor_snap_value(editor->room_end_x,editor->grid_size);
                editor->room_end_z=editor_snap_value(editor->room_end_z,editor->grid_size);
            }
        }
        if(editor->light_dragging && editor->selected_light>=0 && editor->selected_light<level->light_count) {
            PointLight *light=&level->lights[editor->selected_light];
            screen_to_world(editor,width,height,event->motion.x,event->motion.y,&light->x,&light->z);
        }
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

            float old_x=vertex->x,old_z=vertex->z;
            vertex->x = new_x;
            vertex->z = new_z;
            if (!geometry_valid_after_move(level)) {
                vertex->x = old_x; vertex->z = old_z;
            }
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

    if(event->type==SDL_EVENT_KEY_DOWN && !event->key.repeat &&
       editor->active_tool==EDITOR_TOOL_LIGHT &&
       editor->selected_light>=0 && editor->selected_light<level->light_count) {
        PointLight *light=&level->lights[editor->selected_light];
        switch(event->key.scancode) {
            case SDL_SCANCODE_LEFTBRACKET:
            case SDL_SCANCODE_RIGHTBRACKET: {
                float delta=event->key.scancode==SDL_SCANCODE_RIGHTBRACKET?0.05f:-0.05f;
                level->ambient_r=fmaxf(0.0f,fminf(1.0f,level->ambient_r+delta));
                level->ambient_g=fmaxf(0.0f,fminf(1.0f,level->ambient_g+delta));
                level->ambient_b=fmaxf(0.0f,fminf(1.0f,level->ambient_b+delta));return;
            }
            case SDL_SCANCODE_UP: light->radius=fminf(100.0f,light->radius+1.0f);return;
            case SDL_SCANCODE_DOWN: light->radius=fmaxf(0.5f,light->radius-1.0f);return;
            case SDL_SCANCODE_RIGHT: light->intensity=fminf(10.0f,light->intensity+0.2f);return;
            case SDL_SCANCODE_LEFT: light->intensity=fmaxf(0.0f,light->intensity-0.2f);return;
            case SDL_SCANCODE_PAGEUP: light->y+=0.25f;return;
            case SDL_SCANCODE_PAGEDOWN: light->y-=0.25f;return;
            case SDL_SCANCODE_F: light->effect=(light->effect+1)%3;return;
            case SDL_SCANCODE_C: {
                static const int colors[][3]={{255,210,150},{255,65,45},{80,160,255},{100,255,140},{255,255,255}};
                int next=0;
                for(int i=0;i<5;i++) if(light->r==colors[i][0] && light->g==colors[i][1] && light->b==colors[i][2]) next=(i+1)%5;
                light->r=colors[next][0];light->g=colors[next][1];light->b=colors[next][2];return;
            }
            default:break;
        }
    }
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) {
        switch (event->key.scancode) {
            case SDL_SCANCODE_1: editor_set_tool(editor, EDITOR_TOOL_SELECT); return;
            case SDL_SCANCODE_2: editor_set_tool(editor, EDITOR_TOOL_VERTEX); return;
            case SDL_SCANCODE_3: editor_set_tool(editor, EDITOR_TOOL_WALL); return;
            case SDL_SCANCODE_4: editor_set_tool(editor, EDITOR_TOOL_SECTOR); return;
            case SDL_SCANCODE_5: editor_set_tool(editor, EDITOR_TOOL_ROOM); return;
            case SDL_SCANCODE_6: editor_set_tool(editor, EDITOR_TOOL_LIGHT); return;
            case SDL_SCANCODE_H: editor->show_sector_fill=!editor->show_sector_fill;return;
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


void editor_viewport_process_event(EditorViewport *ed,SDL_Renderer *renderer,
                                   const SDL_Event *event,Level *l) {
    if (event->type==SDL_EVENT_KEY_DOWN && !event->key.repeat &&
        (event->key.mod & SDL_KMOD_CTRL)) {
        if (event->key.scancode==SDL_SCANCODE_Z || event->key.scancode==SDL_SCANCODE_Y) {
            if (drag_pending) {
                history_commit(&drag_start,l);
                drag_pending=false;
            }
            bool redo=event->key.scancode==SDL_SCANCODE_Y ||
                      (event->key.scancode==SDL_SCANCODE_Z && (event->key.mod & SDL_KMOD_SHIFT));
            history_step(l,ed,redo);
            return;
        }
    }
    bool down=event->type==SDL_EVENT_MOUSE_BUTTON_DOWN &&
              event->button.button==SDL_BUTTON_LEFT;
    bool up=event->type==SDL_EVENT_MOUSE_BUTTON_UP &&
            event->button.button==SDL_BUTTON_LEFT;
    bool key=event->type==SDL_EVENT_KEY_DOWN && !event->key.repeat;
    EditorSnapshot before;
    bool captured=(down || (up && ed->active_tool==EDITOR_TOOL_ROOM && ed->room_dragging) ||
                   (key && event->key.scancode!=SDL_SCANCODE_R)) &&
                  snapshot_take(&before,l,ed);
    if(key && ed->active_tool==EDITOR_TOOL_LIGHT && ed->selected_light>=0 &&
       ed->selected_light<l->light_count &&
       (event->key.scancode==SDL_SCANCODE_DELETE || event->key.scancode==SDL_SCANCODE_BACKSPACE)) {
        int i=ed->selected_light;
        memmove(l->lights+i,l->lights+i+1,(size_t)(l->light_count-i-1)*sizeof(PointLight));
        l->light_count--; ed->selected_light=-1;
    } else if (key && (event->key.scancode==SDL_SCANCODE_DELETE ||
                event->key.scancode==SDL_SCANCODE_BACKSPACE)) {
        editor_delete_selection(ed,l);
    } else {
        editor_process_event_impl(ed,renderer,event,l);
    }
    if (captured) {
        if (up && ed->active_tool==EDITOR_TOOL_ROOM && ed->room_failed) {
            history_apply(&before,l,ed);
            ed->room_failed=false;
        } else if (geometry_valid_after_move(&before.level) &&
            !geometry_valid_after_move(l) && snapshot_differs(&before,l)) {
            /* Do not let inspector coordinates or other edits corrupt the map. */
            history_apply(&before,l,ed);
            SDL_Log("Edit rejected: overlapping or intersecting geometry");
        } else if (down && (ed->vertex_dragging || ed->spawn_dragging || ed->light_dragging)) {
            if (drag_pending) snapshot_dispose(&drag_start);
            drag_start=before;
            drag_pending=true;
        } else history_commit(&before,l);
    }
    if (up && drag_pending) {
        if (ed->selected_vertex>=0 && geometry_valid_after_move(l)) auto_assign_closed_faces(l);
        history_commit(&drag_start,l);
        drag_pending=false;
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

/* Filled sectors are drawn as clipped horizontal scanlines, so concave
   rooms display correctly without depending on triangulation. */
static void editor_draw_sector_fills(SDL_Renderer *r,const EditorViewport *ed,
                                     const Level *l,int width,int height) {
    if (!ed->show_sector_fill || l->wall_count<3 || l->wall_count>100000) return;
    int *edges=malloc((size_t)l->wall_count*sizeof(int));
    unsigned char *seen=calloc((size_t)l->wall_count*2,1);
    float *crossings=malloc((size_t)l->wall_count*sizeof(float));
    if (!edges || !seen || !crossings) {free(edges);free(seen);free(crossings);return;}
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    for (int e=0;e<l->wall_count*2;e++) {
        if (seen[e]) continue;
        double area=0;
        int n=trace_face(l,e,edges,&area);
        if (n<=0) {seen[e]=1;continue;}
        for (int i=0;i<n;i++) seen[edges[i]]=1;
        int sector=edge_sector(l,edges[0]);
        if (sector<0 || sector>=l->sector_count) continue;
        bool selected=sector==ed->selected_sector;
        static const Uint8 palette[][3]={{47,150,204},{105,165,90},{176,117,190},
            {195,137,67},{75,170,161},{185,100,112}};
        const Uint8 *color=palette[sector%6];
        SDL_SetRenderDrawColor(r,color[0],color[1],color[2],selected?112:42);
        float min_y=(float)height,max_y=0;
        for (int i=0;i<n;i++) {
            Vertex v=l->vertices[edge_origin(l,edges[i])];float sx,sy;
            world_to_screen(ed,v.x,v.z,width,height,&sx,&sy);
            if (sy<min_y) min_y=sy;
            if (sy>max_y) max_y=sy;
        }
        int start=(int)fmaxf(0,ceilf(min_y)),end=(int)fminf((float)height-1,floorf(max_y));
        for (int y=start;y<=end;y+=2) {
            int count=0;
            for (int i=0;i<n;i++) {
                Vertex a=l->vertices[edge_origin(l,edges[i])];
                Vertex b=l->vertices[edge_target(l,edges[i])];
                float ax,ay,bx,by;
                world_to_screen(ed,a.x,a.z,width,height,&ax,&ay);
                world_to_screen(ed,b.x,b.z,width,height,&bx,&by);
                if ((ay<=y && by>y)||(by<=y && ay>y))
                    crossings[count++]=ax+((float)y-ay)*(bx-ax)/(by-ay);
            }
            for (int i=1;i<count;i++) {
                float x=crossings[i];int j=i-1;
                while (j>=0 && crossings[j]>x) {crossings[j+1]=crossings[j];j--;}
                crossings[j+1]=x;
            }
            for (int i=0;i+1<count;i+=2)
                SDL_RenderLine(r,fmaxf(0,crossings[i]),(float)y,
                    fminf((float)width,crossings[i+1]),(float)y);
        }
        if (selected) {
            SDL_SetRenderDrawColor(r,255,220,90,255);
            for (int i=0;i<n;i++) {
                Vertex a=l->vertices[edge_origin(l,edges[i])];
                Vertex b=l->vertices[edge_target(l,edges[i])];
                float ax,ay,bx,by;
                world_to_screen(ed,a.x,a.z,width,height,&ax,&ay);
                world_to_screen(ed,b.x,b.z,width,height,&bx,&by);
                SDL_RenderLine(r,ax,ay,bx,by);
            }
        }
    }
    free(edges);free(seen);free(crossings);
}
static void editor_draw_room_preview(SDL_Renderer *r,const EditorViewport *ed,int width,int height) {
    if (!ed->room_dragging) return;
    float ax,ay,bx,by;
    world_to_screen(ed,ed->room_start_x,ed->room_start_z,width,height,&ax,&ay);
    world_to_screen(ed,ed->room_end_x,ed->room_end_z,width,height,&bx,&by);
    SDL_FRect rect={fminf(ax,bx),fminf(ay,by),fabsf(bx-ax),fabsf(by-ay)};
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r,65,195,235,60);SDL_RenderFillRect(r,&rect);
    SDL_SetRenderDrawColor(r,75,210,245,255);SDL_RenderRect(r,&rect);
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
    const float y = 12.0f;
    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(renderer, x + pad, y + 12.0f, "LEVEL EDITOR");
    const char *tool_name = editor->active_tool == EDITOR_TOOL_VERTEX ? "VERTEX" :
        editor->active_tool == EDITOR_TOOL_WALL ? "WALL" :
        editor->active_tool == EDITOR_TOOL_SECTOR ? "SECTOR" : editor->active_tool == EDITOR_TOOL_ROOM ? "DRAW ROOM" : "SELECT";
    char line[96];
    snprintf(line, sizeof(line), "ACTIVE TOOL: %s", tool_name);
    SDL_RenderDebugText(renderer, x + pad, y + 29.0f, line);
    SDL_SetRenderDrawColor(renderer, 170, 205, 225, 255);
    const char *hint = editor->active_tool == EDITOR_TOOL_VERTEX ?
        "Place vertices or split walls" :
        editor->active_tool == EDITOR_TOOL_WALL ?
            (editor->wall_start_vertex >= 0 ? "Choose the ending vertex" : "Choose the starting vertex") :
        editor->active_tool == EDITOR_TOOL_SECTOR ? "Click room to select; edit heights" :
        editor->active_tool == EDITOR_TOOL_ROOM ? "Drag to create a closed room" :
        "Select and move geometry";
    SDL_RenderDebugText(renderer, x + pad, y + 46.0f, hint);

    float cursor = y + 68.0f;
    SDL_SetRenderDrawColor(renderer, 95, 110, 135, 255);
    SDL_RenderLine(renderer, x + pad, cursor, x + visible_width - pad, cursor);
    cursor += 10.0f;
    SDL_SetRenderDrawColor(renderer, 255, 215, 95, 255);
    SDL_RenderDebugText(renderer, x + pad, cursor, "TOOLS  (1-5)");
    cursor += 17.0f;
    const char *tools[] = {
        "1  Select / Move", "2  Vertex / Split Wall",
        "3  Draw Walls / Auto Portals", "4  Select / Edit Sectors",
        "5  Draw Room (drag rectangle)", "6  Place / Edit Lights"
    };
    for (int i = 0; i < 6; ++i) {
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
        "Esc          Exit editor", "Ctrl+Z / Y   Undo / Redo",
        "H            Toggle sector colors", "6            Light editor"
    };
    for (int i = 0; i < 8; ++i) {
        SDL_RenderDebugText(renderer, x + pad, cursor, general[i]);
        cursor += 15.0f;
    }
    SDL_SetRenderClipRect(renderer, NULL);
}

static void editor_draw_properties(SDL_Renderer *renderer,
    const EditorViewport *editor, const Level *level) {
    int width,height;
    if (!SDL_GetRenderOutputSize(renderer,&width,&height)) return;
    float x=inspector_x(width),y=INSPECTOR_TOP;
    int mode=inspector_mode(editor,level);
    float panel_height=fminf(inspector_panel_height(mode),fmaxf(0.0f,(float)height-24.0f));
    if (x<12.0f || panel_height<INSPECTOR_FIRST) return;
    SDL_FRect bg={x,y,INSPECTOR_WIDTH,panel_height};
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer,12,16,24,238);
    SDL_RenderFillRect(renderer,&bg);
    SDL_SetRenderDrawColor(renderer,95,110,135,255);
    SDL_RenderRect(renderer,&bg);
    SDL_Rect inspector_clip={(int)x+1,(int)y+1,(int)INSPECTOR_WIDTH-2,(int)panel_height-2};
    SDL_SetRenderClipRect(renderer,&inspector_clip);
    const char *heading=mode==1?"VERTEX":mode==2?"WALL":mode==3?"SECTOR":mode==4?"PLAYER SPAWN":mode==5?"POINT LIGHT":"PROPERTIES";
    SDL_SetRenderDrawColor(renderer,255,215,95,255);
    SDL_RenderDebugText(renderer,x+12,y+12,heading);
    SDL_SetRenderDrawColor(renderer,175,190,205,255);
    SDL_RenderDebugText(renderer,x+12,y+30,"Click - / + to edit");
    SDL_RenderDebugText(renderer,x+12,y+44,"Shift: fine adjustments");
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
        snprintf(rows[count++],80,"Brightness: %d",sc->light_level);
        snprintf(rows[count++],80,"Light red: %d",sc->light_r);
        snprintf(rows[count++],80,"Light green: %d",sc->light_g);
        snprintf(rows[count++],80,"Light blue: %d",sc->light_b);
    } else if(mode==5) {
        const PointLight *light=&level->lights[editor->selected_light];
        snprintf(rows[count++],80,"X: %.2f",light->x);
        snprintf(rows[count++],80,"Y: %.2f",light->y);
        snprintf(rows[count++],80,"Z: %.2f",light->z);
        snprintf(rows[count++],80,"Radius: %.2f",light->radius);
        snprintf(rows[count++],80,"Intensity: %.2f",light->intensity);
        snprintf(rows[count++],80,"Red: %d",light->r);
        snprintf(rows[count++],80,"Green: %d",light->g);
        snprintf(rows[count++],80,"Blue: %d",light->b);
        snprintf(rows[count++],80,"Effect: %d",light->effect);
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
        if (yy+INSPECTOR_ROW>y+panel_height) break;
        SDL_SetRenderDrawColor(renderer,220,225,235,255);
        /* SDL debug font is 8 px per glyph. Keep text clear of the buttons. */
        char visible_label[25];
        snprintf(visible_label,sizeof(visible_label),"%.24s",rows[i]);
        SDL_RenderDebugText(renderer,x+12,yy+5,visible_label);
        bool editable=(mode==1 && i<2)||(mode==2 && i<3)||
                      (mode==3 && i<5)||(mode==4 && i<5)||(mode==5 && i<9);
        if(!editable)continue;
        SDL_FRect minus={x+218,yy,28,19},plus={x+251,yy,29,19};
        SDL_SetRenderDrawColor(renderer,53,65,82,255);
        SDL_RenderFillRect(renderer,&minus); SDL_RenderFillRect(renderer,&plus);
        SDL_SetRenderDrawColor(renderer,255,215,95,255);
        SDL_RenderDebugText(renderer,x+228,yy+5,"-");
        SDL_RenderDebugText(renderer,x+262,yy+5,"+");
    }
    SDL_SetRenderClipRect(renderer,NULL);
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
    const float y = fminf((float)height - panel_height - 12.0f, 320.0f);

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
    editor_draw_sector_fills(renderer,editor,level,width,height);
    editor_draw_room_preview(renderer,editor,width,height);
    editor_draw_walls(renderer, editor, level, width, height);
    editor_draw_vertices(renderer, editor, level, width, height);
    editor_draw_spawn(renderer, editor, level, width, height);

    for(int i=0;i<level->light_count;i++) {
        const PointLight *light=&level->lights[i];
        float sx,sy;
        world_to_screen(editor,light->x,light->z,width,height,&sx,&sy);
        SDL_SetRenderDrawColor(renderer,(Uint8)light->r,(Uint8)light->g,(Uint8)light->b,255);
        SDL_FRect marker={sx-5,sy-5,10,10};
        SDL_RenderFillRect(renderer,&marker);
        if(editor->selected_light==i && editor->active_tool==EDITOR_TOOL_LIGHT) {
            float radius=light->radius*editor->zoom;
            SDL_FRect outline={sx-radius,sy-radius,2*radius,2*radius};
            SDL_RenderRect(renderer,&outline);
            char info[160];
            snprintf(info,sizeof(info),"Light %d  Y %.2f  Radius %.1f  Intensity %.1f  RGB %d,%d,%d  Effect %d",
                     i,light->y,light->radius,light->intensity,light->r,light->g,light->b,light->effect);
            SDL_RenderDebugText(renderer,20,(float)height-28,info);
        }
    }
    editor_draw_controls(renderer, editor);
    editor_draw_properties(renderer, editor, level);

    editor_draw_save_notification(renderer, editor);

    SDL_RenderPresent(renderer);
}