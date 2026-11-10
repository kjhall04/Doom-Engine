#include "player.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define PLAYER_RADIUS 0.5f

#define PLAYER_HEIGHT 1.8f
#define PLAYER_EYE_HEIGHT 1.6f

#define PLAYER_MOVE_SPEED 9.0f
#define PLAYER_ACCELERATION 30.0f

#define PLAYER_GRAVITY 21.0f
#define PLAYER_JUMP_SPEED 9.0f

#define PLAYER_MOUSE_SENSITIVITY 0.0025f

#define PLAYER_MAX_PITCH 1.55f

#define PLAYER_MAX_STEP 1.0f
#define PLAYER_STEP_SPEED 6.0f

#define PLAYER_MOVE_SUBSTEP 0.1f

#define EPSILON 0.0001f


/* Collision is evaluated against finite wall segments. A two-sided wall
 * becomes solid whenever its opening cannot accommodate the player. */
static float player_clamp(float v, float lo, float hi) {
    return fminf(hi, fmaxf(lo, v));
}

static bool player_valid_sector(const Level *level, int sector) {
    return sector >= 0 && sector < level->sector_count;
}

static bool player_valid_wall(const Level *level, const Wall *wall) {
    return wall->vertex_start >= 0 && wall->vertex_start < level->vertex_count &&
           wall->vertex_end >= 0 && wall->vertex_end < level->vertex_count;
}

static float player_point_segment_distance2(float x, float z,
                                             const Vertex *a, const Vertex *b) {
    float dx = b->x - a->x, dz = b->z - a->z;
    float length2 = dx * dx + dz * dz;
    float t = length2 > EPSILON * EPSILON ?
        player_clamp(((x - a->x) * dx + (z - a->z) * dz) / length2, 0, 1) : 0;
    float ex = x - (a->x + t * dx), ez = z - (a->z + t * dz);
    return ex * ex + ez * ez;
}

/* A sector boundary consists of all walls referencing that sector. Ray
 * casting does not depend on wall winding or sequential sector IDs. */
static bool player_point_in_sector(const Level *level, int sector, float x, float z) {
    if (!player_valid_sector(level, sector)) return false;
    bool inside = false;
    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *w = &level->walls[i];
        if (!player_valid_wall(level, w) ||
            (w->front_sector != sector && w->back_sector != sector)) continue;
        const Vertex *a = &level->vertices[w->vertex_start];
        const Vertex *b = &level->vertices[w->vertex_end];
        if ((a->z > z) != (b->z > z)) {
            float crossing_x = a->x + (z - a->z) * (b->x - a->x) / (b->z - a->z);
            if (crossing_x > x) inside = !inside;
        }
    }
    return inside;
}

static int player_find_sector(const Level *level, float x, float z, int preferred) {
    if (player_point_in_sector(level, preferred, x, z)) return preferred;
    for (int i = 0; i < level->sector_count; ++i)
        if (player_point_in_sector(level, i, x, z)) return i;
    return -1;
}

static int player_other_sector(const Wall *wall, int sector) {
    if (wall->front_sector == sector) return wall->back_sector;
    if (wall->back_sector == sector) return wall->front_sector;
    return -1;
}

static bool player_portal_passable(const Player *player, const Level *level,
                                   int from, int to) {
    if (!player_valid_sector(level, from) || !player_valid_sector(level, to)) return false;
    const Sector *a = &level->sectors[from], *b = &level->sectors[to];
    float bottom = fmaxf(a->floor_height, b->floor_height);
    float top = fminf(a->ceiling_height, b->ceiling_height);
    if (top - bottom + EPSILON < player->height) return false;
    if (player->grounded) {
        if (b->floor_height - a->floor_height > PLAYER_MAX_STEP + EPSILON) return false;
        /* When walking downhill, the player's body must still fit. */
        if (a->floor_height + player->height > top + EPSILON) return false;
    } else {
        if (player->y < bottom - EPSILON ||
            player->y + player->height > top + EPSILON) return false;
    }
    return true;
}

/* Intersection of movement and wall segments, with finite endpoint tests.
 * Returns movement fraction t and wall fraction u. */
static bool player_segment_cross(float x0, float z0, float x1, float z1,
                                 const Vertex *a, const Vertex *b,
                                 float *out_t, float *out_u) {
    float rx = x1 - x0, rz = z1 - z0;
    float sx = b->x - a->x, sz = b->z - a->z;
    float denom = rx * sz - rz * sx;
    if (fabsf(denom) < 1e-8f) return false;
    float qx = a->x - x0, qz = a->z - z0;
    float t = (qx * sz - qz * sx) / denom;
    float u = (qx * rz - qz * rx) / denom;
    if (t < -EPSILON || t > 1 + EPSILON || u < -EPSILON || u > 1 + EPSILON)
        return false;
    *out_t = player_clamp(t, 0, 1);
    *out_u = player_clamp(u, 0, 1);
    return true;
}

/* Resolve portal crossings in travel order, not wall-array order. */
static bool player_trace_sectors(const Player *player, const Level *level,
                                 float x, float z, int *out_sector) {
    int sector = player->sector;
    float t_cursor = 0;
    float target_x = x, target_z = z;
    /* A move is substepped; normally at most one portal is crossed. */
    for (int hop = 0; hop <= level->wall_count; ++hop) {
        int nearest = -1, destination = -1;
        float nearest_t = 2;
        for (int i = 0; i < level->wall_count; ++i) {
            const Wall *w = &level->walls[i];
            if (!player_valid_wall(level, w) ||
                (w->front_sector != sector && w->back_sector != sector)) continue;
            float t, u;
            if (!player_segment_cross(player->x, player->z, target_x, target_z,
                   &level->vertices[w->vertex_start], &level->vertices[w->vertex_end],
                   &t, &u) || t <= t_cursor + 1e-5f || t >= nearest_t) continue;
            nearest = i;
            nearest_t = t;
            destination = player_other_sector(w, sector);
        }
        if (nearest < 0) {
            *out_sector = sector;
            return true;
        }
        if (!player_portal_passable(player, level, sector, destination)) return false;
        sector = destination;
        t_cursor = nearest_t;
    }
    return false;
}

/* A closed or impassable portal is a solid capsule obstacle. Portal endpoints
 * remain blocked by any adjacent solid wall; no wall-order assumptions. */
static bool player_position_clear(const Player *player, const Level *level,
                                  float x, float z, int sector, int entering_from) {
    float radius2 = (PLAYER_RADIUS - 0.001f) * (PLAYER_RADIUS - 0.001f);
    for (int i = 0; i < level->wall_count; ++i) {
        const Wall *w = &level->walls[i];
        if (!player_valid_wall(level, w)) continue;
        if (w->front_sector != sector && w->back_sector != sector) continue;
        int other = player_other_sector(w, sector);
        if (player_portal_passable(player, level, sector, other) ||
            (other == entering_from &&
             player_portal_passable(player, level, entering_from, sector))) continue;
        float new_distance2 = player_point_segment_distance2(x, z,
                &level->vertices[w->vertex_start],
                &level->vertices[w->vertex_end]);
        if (new_distance2 < radius2) {
            /* After a ledge drop the portal's lower wall becomes solid.
             * Never trap a player who is already overlapping its radius:
             * permit movement that increases clearance from the wall. */
            float old_distance2 = player_point_segment_distance2(player->x, player->z,
                    &level->vertices[w->vertex_start],
                    &level->vertices[w->vertex_end]);
            if (other >= 0 && old_distance2 < radius2) {
                /* A player already overlapping the ledge wall must be able
                 * to slide PARALLEL to it, not just move away.  Preserve
                 * the existing clearance (within floating-point tolerance),
                 * but never allow crossing the wall to its other side. */
                const Vertex *a = &level->vertices[w->vertex_start];
                const Vertex *b = &level->vertices[w->vertex_end];
                float wx = b->x - a->x, wz = b->z - a->z;
                float old_side = wx * (player->z - a->z) - wz * (player->x - a->x);
                float new_side = wx * (z - a->z) - wz * (x - a->x);
                if (new_distance2 >= old_distance2 - 1e-5f &&
                    old_side * new_side >= -1e-6f) continue;
            }
            return false;
        }
    }
    return true;
}

static bool player_try_move(Player *player, Level *level, float x, float z) {
    int destination;
    if (!player_trace_sectors(player, level, x, z, &destination)) return false;
    if (!player_position_clear(player, level, x, z, destination, player->sector)) return false;
    /* Also check the originating sector's solid walls. This prevents
     * clipping past a wall near a shared three-way corner. */
    if (destination != player->sector &&
        !player_position_clear(player, level, x, z, player->sector, -1)) {
        /* Only walls which are actually solid in the origin matter; however
         * the destination may be near a different origin wall. */
        return false;
    }
    int old_sector = player->sector;
    player->x = x;
    player->z = z;
    player->sector = destination;
    if (destination != old_sector && player->grounded) {
        float old_floor = level->sectors[old_sector].floor_height;
        float new_floor = level->sectors[destination].floor_height;
        float floor_change = new_floor - old_floor;
        if (floor_change < -PLAYER_MAX_STEP - EPSILON) {
            /* A large drop is not a step. Keep the player's current height
             * and let gravity handle the fall after crossing the portal. */
            player->stepping = false;
            player->step_progress = 1.0f;
            player->step_start_y = player->y;
            player->step_target_y = player->y;
            player->grounded = false;
            if (player->velocity_y > 0.0f) player->velocity_y = 0.0f;
        } else if (fabsf(floor_change) > EPSILON) {
            /* Small height differences retain the smooth step animation. */
            player->stepping = true;
            player->step_start_y = player->y;
            player->step_target_y = new_floor;
            player->step_progress = 0.0f;
        } else if (!player->stepping) {
            player->y = new_floor;
        }
    }
    return true;
}

static void player_move_substeps(Player *player, Level *level, float dx, float dz) {
    float distance = sqrtf(dx * dx + dz * dz);
    int steps = (int)ceilf(distance / PLAYER_MOVE_SUBSTEP);
    if (steps < 1) steps = 1;
    float sx = dx / steps, sz = dz / steps;
    for (int i = 0; i < steps; ++i) {
        if (player_try_move(player, level, player->x + sx, player->z + sz)) continue;
        /* Axis-separated fallback supplies basic wall sliding. */
        player_try_move(player, level, player->x + sx, player->z);
        player_try_move(player, level, player->x, player->z + sz);
    }
}

void player_init(Player *player)
{
    player->x =
        -5.0f;

    player->y =
        0.0f;

    player->z =
        0.0f;


    player->eye_height =
        PLAYER_EYE_HEIGHT;

    player->height =
        PLAYER_HEIGHT;


    player->velocity_x =
        0.0f;

    player->velocity_y =
        0.0f;

    player->velocity_z =
        0.0f;


    player->yaw =
        0.0f;

    player->pitch =
        0.0f;


    player->move_speed =
        PLAYER_MOVE_SPEED;

    player->acceleration =
        PLAYER_ACCELERATION;


    player->sector =
        0;


    player->grounded =
        true;

    player->stepping = false;
    player->step_start_y = player->y;
    player->step_target_y = player->y;
    player->step_progress = 1.0f;
}


static void player_update_step(Player *player, float delta_time) {

    if (!player->stepping) {
        return;
    }

    player->step_progress += PLAYER_STEP_SPEED * delta_time;

    if (player->step_progress >= 1.0f) {

        player->step_progress = 1.0f;
        player->stepping = false;

        player->y =
            player->step_target_y;

        return;
    }

    float progress = player->step_progress;

    // Smoothstep interpolation
    progress = progress * progress * (3.0f - 2.0f * progress);

    player->y =
        player->step_start_y +
        (player->step_target_y - player->step_start_y) * progress;
}


/*
 * Update the player.
 */

void player_update(
    Player *player,
    Input *input,
    Level *level,
    float delta_time
) {

    /*
     * Limit the maximum frame time.
     */

    if (
        delta_time >
        0.1f
    ) {

        delta_time =
            0.1f;
    }


    /*
     * Mouse look.
     */

    player->yaw +=
        input->mouse_delta_x *
        PLAYER_MOUSE_SENSITIVITY;


    player->pitch -=
        input->mouse_delta_y *
        PLAYER_MOUSE_SENSITIVITY;


    player->pitch =
        player_clamp(
            player->pitch,
            -PLAYER_MAX_PITCH,
            PLAYER_MAX_PITCH
        );


    input->mouse_delta_x =
        0.0f;

    input->mouse_delta_y =
        0.0f;


    /*
     * Local movement direction.

     * Negative Z is forward.
     */

    float direction_x =
        0.0f;

    float direction_z =
        0.0f;


    if (input->move_forward) {

        direction_z -=
            1.0f;
    }


    if (input->move_backward) {

        direction_z +=
            1.0f;
    }


    if (input->move_left) {

        direction_x -=
            1.0f;
    }


    if (input->move_right) {

        direction_x +=
            1.0f;
    }


    /*
     * Normalize diagonal movement.
     */

    float movement_length =
        sqrtf(
            direction_x * direction_x +
            direction_z * direction_z
        );


    if (
        movement_length >
        0.0f
    ) {

        direction_x /=
            movement_length;

        direction_z /=
            movement_length;
    }


    /*
     * Rotate local movement by yaw.
     */

    float sin_yaw =
        sinf(player->yaw);

    float cos_yaw =
        cosf(player->yaw);


    float acceleration_x =
        direction_x * cos_yaw -
        direction_z * sin_yaw;


    float acceleration_z =
        direction_x * sin_yaw +
        direction_z * cos_yaw;


    /*
     * Apply acceleration.
     */

    float acceleration =
        player->acceleration *
        delta_time;


    float target_velocity_x =
        acceleration_x *
        player->move_speed;


    float target_velocity_z =
        acceleration_z *
        player->move_speed;


    float difference_x =
        target_velocity_x -
        player->velocity_x;


    float difference_z =
        target_velocity_z -
        player->velocity_z;


    difference_x =
        player_clamp(
            difference_x,
            -acceleration,
            acceleration
        );


    difference_z =
        player_clamp(
            difference_z,
            -acceleration,
            acceleration
        );


    player->velocity_x +=
        difference_x;


    player->velocity_z +=
        difference_z;


    /*
     * Stop completely when there is no
     * movement input.
     */

    if (
        direction_x == 0.0f &&
        direction_z == 0.0f
    ) {

        player->velocity_x =
            0.0f;

        player->velocity_z =
            0.0f;
    }


    /*
     * Jump.
     */

    if (
        input->jump &&
        player->grounded
    ) {

        player->velocity_y =
            PLAYER_JUMP_SPEED;

        player->grounded =
            false;

        /* A jump must cancel any in-progress step animation. Otherwise
         * floor collision stays disabled while stepping is true. */
        player->stepping = false;
        player->step_progress = 1.0f;

        input->jump =
            false;
    }


    /*
     * Gravity.
     */

    player->velocity_y -=
        PLAYER_GRAVITY *
        delta_time;


    /*
     * Horizontal movement.
     */

    float movement_x =
        player->velocity_x *
        delta_time;


    float movement_z =
        player->velocity_z *
        delta_time;


    /* Repair stale sector IDs before collision checks. Never index sectors
     * with an invalid ID, even if the spawn was not initialized correctly. */
    if (!level || level->sector_count <= 0) return;
    int found = player_find_sector(level, player->x, player->z, player->sector);
    if (found >= 0) {
        if (found != player->sector) {
            player->sector = found;
            /* Do not teleport a jumping/falling player to a floor. */
            if (player->grounded && !player->stepping &&
                player->y < level->sectors[found].floor_height)
                player->y = level->sectors[found].floor_height;
        }
    } else if (!player_valid_sector(level, player->sector)) {
        /* No trustworthy sector: avoid unsafe array access. */
        return;
    }

    player_move_substeps(
        player,
        level,
        movement_x,
        movement_z
    );

    /* The movement tracer may cross a corner or a split portal. Resolve
     * the sector from the final XY point, not the last wall in iteration. */
    found = player_find_sector(level, player->x, player->z, player->sector);
    if (found >= 0 && found != player->sector) {
        int previous = player->sector;
        player->sector = found;
        if (player->grounded && !player->stepping &&
            player_valid_sector(level, previous)) {
            float drop = level->sectors[previous].floor_height -
                         level->sectors[found].floor_height;
            if (drop > PLAYER_MAX_STEP + EPSILON) {
                player->grounded = false;
                player->stepping = false;
            }
        }
    }

    if (player->grounded) player_update_step(player, delta_time);

    /*
     * Get the current sector.
     */

    Sector *sector =
        &level->sectors[
            player->sector
        ];


    float floor_height =
        sector->floor_height;


    float ceiling_height =
        sector->ceiling_height;


    /* Vertical integration and landing are separate from step animation.
     * A falling player's feet cannot tunnel through the active floor: we
     * clamp on crossing and also recover any below-floor state. */
    float previous_y = player->y;
    if (!player->stepping) {
        player->y += player->velocity_y * delta_time;
        if (player->y <= floor_height + EPSILON) {
            if (previous_y >= floor_height - EPSILON ||
                player->y < floor_height - EPSILON) {
                player->y = floor_height;
                player->velocity_y = 0.0f;
                player->grounded = true;
            }
        } else {
            player->grounded = false;
        }
    }

    /* If a step ends, enforce the same floor invariant immediately. */
    if (!player->stepping && player->y < floor_height) {
        player->y = floor_height;
        player->velocity_y = 0.0f;
        player->grounded = true;
    }

    if (player->y + player->height > ceiling_height) {
        player->y = ceiling_height - player->height;
        if (player->velocity_y > 0.0f) player->velocity_y = 0.0f;
        /* Do not leave the player below the floor in a too-short sector. */
        if (player->y < floor_height) {
            player->y = floor_height;
            player->velocity_y = 0.0f;
            player->grounded = true;
        }
    }
}
