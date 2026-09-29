#include "player.h"

#include <math.h>
#include <stdbool.h>

#define PLAYER_RADIUS 0.5f

#define PLAYER_HEIGHT 1.8f
#define PLAYER_EYE_HEIGHT 1.6f

#define PLAYER_MOVE_SPEED 6.0f
#define PLAYER_ACCELERATION 30.0f

#define PLAYER_GRAVITY 25.0f
#define PLAYER_JUMP_SPEED 10.0f

#define PLAYER_MOUSE_SENSITIVITY 0.0025f

#define PLAYER_MAX_PITCH 1.55f

#define PLAYER_MAX_STEP 1.0f

#define PLAYER_MOVE_SUBSTEP 0.1f

#define EPSILON 0.0001f


/*
    Movement result

    This is the information produced by player_check_position().

    It describes the environment at the proposed position.
*/

typedef struct PlayerMoveResult {
    bool blocked;

    float floor_height;
    float ceiling_height;

    int sector;

} PlayerMoveResult;


/*
    Clamp a value between two limits.
*/

static float player_clamp(
    float value,
    float minimum,
    float maximum)
{
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return value;
}


/*
    Return the squared distance between a point and
    the closest point on a wall segment.
*/

static float player_distance_to_wall_squared(
    float px,
    float pz,
    const Vertex *start,
    const Vertex *end)
{
    float wall_x = end->x - start->x;
    float wall_z = end->z - start->z;

    float length_squared =
        wall_x * wall_x +
        wall_z * wall_z;

    if (length_squared <= EPSILON) {
        float dx = px - start->x;
        float dz = pz - start->z;

        return dx * dx + dz * dz;
    }

    float point_x = px - start->x;
    float point_z = pz - start->z;

    float projection =
        (point_x * wall_x +
         point_z * wall_z) /
        length_squared;

    projection = player_clamp(
        projection,
        0.0f,
        1.0f
    );

    float closest_x =
        start->x +
        wall_x * projection;

    float closest_z =
        start->z +
        wall_z * projection;

    float dx = px - closest_x;
    float dz = pz - closest_z;

    return dx * dx + dz * dz;
}


/*
    Determine which side of a wall a point occupies.

    The result is the 2D cross product between the
    wall direction and the point direction.
*/

static float player_wall_side(
    float x,
    float z,
    const Vertex *start,
    const Vertex *end)
{
    float wall_x = end->x - start->x;
    float wall_z = end->z - start->z;

    float point_x = x - start->x;
    float point_z = z - start->z;

    return wall_x * point_z -
           wall_z * point_x;
}


/*
    Determine whether movement crossed a wall.

    This is only geometric information.

    It does NOT decide whether the wall can be passed.

    The actual movement decision happens in
    player_check_position().
*/

static bool player_crossed_wall(
    float old_x,
    float old_z,
    float new_x,
    float new_z,
    const Vertex *start,
    const Vertex *end)
{
    float old_side =
        player_wall_side(
            old_x,
            old_z,
            start,
            end
        );

    float new_side =
        player_wall_side(
            new_x,
            new_z,
            start,
            end
        );

    if (old_side > EPSILON &&
        new_side < -EPSILON) {
        return true;
    }

    if (old_side < -EPSILON &&
        new_side > EPSILON) {
        return true;
    }

    return false;
}


/*
    Determine the sector on the opposite side of a
    two sided wall.
*/

static int player_get_destination_sector(
    const Player *player,
    const Wall *wall)
{
    if (wall->front_sector == player->sector) {
        return wall->back_sector;
    }

    if (wall->back_sector == player->sector) {
        return wall->front_sector;
    }

    return -1;
}


/*
    Check a proposed player position.

    This is the heart of the movement system.

    It determines:

    1. Whether the player is blocked
    2. Which sector the player would occupy
    3. The floor height
    4. The ceiling height
*/

static void player_check_position(
    Player *player,
    Level *level,
    float new_x,
    float new_z,
    PlayerMoveResult *result)
{
    result->blocked = false;

    result->floor_height =
        level->sectors[player->sector].floor_height;

    result->ceiling_height =
        level->sectors[player->sector].ceiling_height;

    result->sector = player->sector;


    /*
        Examine every wall.

        Our level format does not have Doom's subsectors,
        so we use the player's current sector to determine
        which walls are relevant.
    */

    for (int i = 0; i < level->wall_count; i++) {

        Wall *wall = &level->walls[i];


        /*
            Ignore walls that do not belong to the
            player's current sector.
        */

        if (wall->front_sector != player->sector &&
            wall->back_sector != player->sector) {
            continue;
        }


        Vertex *start =
            &level->vertices[wall->vertex_start];

        Vertex *end =
            &level->vertices[wall->vertex_end];


        /*
            Determine whether the proposed player circle
            touches this wall.
        */

        float distance_squared =
            player_distance_to_wall_squared(
                new_x,
                new_z,
                start,
                end
            );

        if (distance_squared >
            PLAYER_RADIUS * PLAYER_RADIUS) {
            continue;
        }


        /*
            One sided wall.

            There is no sector on the opposite side,
            therefore the wall is solid.
        */

        if (wall->front_sector == -1 ||
            wall->back_sector == -1) {

            result->blocked = true;
            return;
        }


        /*
            This is a two sided wall.

            Determine whether the movement actually
            crosses the wall.
        */

        bool crossed =
            player_crossed_wall(
                player->x,
                player->z,
                new_x,
                new_z,
                start,
                end
            );


        /*
            If we have not crossed the portal, the wall
            is simply a boundary we are touching.

            Do not allow the player to move through it.
        */

        if (!crossed) {
            result->blocked = true;
            return;
        }


        /*
            Determine the sector on the other side.
        */

        int destination_sector =
            player_get_destination_sector(
                player,
                wall
            );

        if (destination_sector < 0 ||
            destination_sector >= level->sector_count) {

            result->blocked = true;
            return;
        }


        Sector *current_sector =
            &level->sectors[player->sector];

        Sector *destination =
            &level->sectors[destination_sector];


        /*
            Doom style portal opening.

            The usable opening is bounded by the highest
            floor and the lowest ceiling.
        */

        float opening_bottom =
            fmaxf(
                current_sector->floor_height,
                destination->floor_height
            );

        float opening_top =
            fminf(
                current_sector->ceiling_height,
                destination->ceiling_height
            );

        float opening_height =
            opening_top - opening_bottom;


        /*
            The player must physically fit inside
            the opening.
        */

        if (opening_height < PLAYER_HEIGHT) {
            result->blocked = true;
            return;
        }


        /*
            Determine the new floor.

            The player cannot walk down through the
            floor boundary. The new floor is the
            higher of the two floors.
        */

        float new_floor =
            fmaxf(
                current_sector->floor_height,
                destination->floor_height
            );


        /*
            Walking up a step that is too high is blocked.
        */

        if (player->grounded) {

            float step_height =
                new_floor -
                current_sector->floor_height;

            if (step_height > PLAYER_MAX_STEP) {
                result->blocked = true;
                return;
            }
        }


        /*
            The portal is usable.

            Update the movement result.
        */

        result->sector =
            destination_sector;

        result->floor_height =
            destination->floor_height;

        result->ceiling_height =
            destination->ceiling_height;
    }
}


/*
    Attempt to move the player.

    Nothing is changed until the proposed position
    has passed the complete movement query.
*/

static bool player_try_move(
    Player *player,
    Level *level,
    float new_x,
    float new_z)
{
    PlayerMoveResult result;

    player_check_position(
        player,
        level,
        new_x,
        new_z,
        &result
    );


    /*
        The movement query rejected the position.
    */

    if (result.blocked) {
        return false;
    }


    /*
        Commit the horizontal position.
    */

    player->x = new_x;
    player->z = new_z;


    /*
        Commit the new sector.
    */

    player->sector =
        result.sector;


    /*
        If the player is grounded, place the
        player's feet directly on the floor.

        We are deliberately not smoothing stairs yet.
    */

    if (player->grounded) {
        player->y =
            result.floor_height;
    }


    return true;
}


/*
    Move the player using small substeps.

    This prevents the player from tunneling through
    geometry when delta_time becomes large.
*/

static bool player_move_substeps(
    Player *player,
    Level *level,
    float movement_x,
    float movement_z)
{
    float distance =
        sqrtf(
            movement_x * movement_x +
            movement_z * movement_z
        );

    int steps =
        (int)ceilf(
            distance /
            PLAYER_MOVE_SUBSTEP
        );

    if (steps < 1) {
        steps = 1;
    }


    float step_x =
        movement_x / (float)steps;

    float step_z =
        movement_z / (float)steps;


    bool moved = false;


    for (int i = 0; i < steps; i++) {

        float target_x =
            player->x + step_x;

        float target_z =
            player->z + step_z;


        /*
            First try the complete movement.
        */

        if (player_try_move(
                player,
                level,
                target_x,
                target_z)) {

            moved = true;
            continue;
        }


        /*
            The complete movement was blocked.

            Try X independently.
        */

        bool moved_x =
            player_try_move(
                player,
                level,
                player->x + step_x,
                player->z
            );


        /*
            Try Z independently.

            This provides basic wall sliding.
        */

        bool moved_z =
            player_try_move(
                player,
                level,
                player->x,
                player->z + step_z
            );


        if (moved_x || moved_z) {
            moved = true;
        }
    }


    return moved;
}


/*
    Initialize the player.
*/

void player_init(Player *player)
{
    player->x = -5.0f;
    player->y = 0.0f;
    player->z = 0.0f;

    player->eye_height =
        PLAYER_EYE_HEIGHT;

    player->height =
        PLAYER_HEIGHT;


    player->velocity_x = 0.0f;
    player->velocity_y = 0.0f;
    player->velocity_z = 0.0f;


    player->yaw = 0.0f;
    player->pitch = 0.0f;


    player->move_speed =
        PLAYER_MOVE_SPEED;

    player->acceleration =
        PLAYER_ACCELERATION;


    player->sector = 0;

    player->grounded = true;
}


/*
    Update player movement.
*/

void player_update(
    Player *player,
    Input *input,
    Level *level,
    float delta_time)
{
    /*
        Prevent a bad frame time from producing
        an enormous movement step.
    */

    if (delta_time > 0.1f) {
        delta_time = 0.1f;
    }


    /*
        Mouse look.
    */

    player->yaw -=
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


    /*
        Reset mouse deltas after consuming them.
    */

    input->mouse_delta_x = 0.0f;
    input->mouse_delta_y = 0.0f;


    /*
        Determine movement direction.
    */

    float forward = 0.0f;
    float strafe = 0.0f;


    if (input->move_forward) {
        forward += 1.0f;
    }

    if (input->move_backward) {
        forward -= 1.0f;
    }

    if (input->move_right) {
        strafe += 1.0f;
    }

    if (input->move_left) {
        strafe -= 1.0f;
    }


    /*
        Normalize diagonal movement.

        Without this, pressing W + D would move
        faster than pressing either key alone.
    */

    float movement_length =
        sqrtf(
            forward * forward +
            strafe * strafe
        );

    if (movement_length > 1.0f) {

        forward /=
            movement_length;

        strafe /=
            movement_length;
    }


    /*
        Convert local movement into world movement
        using the player's yaw.
    */

    float forward_x =
        sinf(player->yaw);

    float forward_z =
        cosf(player->yaw);

    float right_x =
        cosf(player->yaw);

    float right_z =
        -sinf(player->yaw);


    float desired_velocity_x =
        forward_x * forward +
        right_x * strafe;

    float desired_velocity_z =
        forward_z * forward +
        right_z * strafe;


    desired_velocity_x *=
        player->move_speed;

    desired_velocity_z *=
        player->move_speed;


    /*
        Accelerate toward the desired movement.
    */

    float acceleration =
        player->acceleration *
        delta_time;


    float velocity_difference_x =
        desired_velocity_x -
        player->velocity_x;

    float velocity_difference_z =
        desired_velocity_z -
        player->velocity_z;


    if (velocity_difference_x > acceleration) {
        velocity_difference_x = acceleration;
    }

    if (velocity_difference_x < -acceleration) {
        velocity_difference_x = -acceleration;
    }

    if (velocity_difference_z > acceleration) {
        velocity_difference_z = acceleration;
    }

    if (velocity_difference_z < -acceleration) {
        velocity_difference_z = -acceleration;
    }


    player->velocity_x +=
        velocity_difference_x;

    player->velocity_z +=
        velocity_difference_z;


    /*
        Stop tiny residual movement when no movement
        input is being supplied.
    */

    if (forward == 0.0f &&
        strafe == 0.0f) {

        float friction =
            player->acceleration *
            delta_time;

        if (fabsf(player->velocity_x) < friction) {
            player->velocity_x = 0.0f;
        }

        if (fabsf(player->velocity_z) < friction) {
            player->velocity_z = 0.0f;
        }
    }


    /*
        Jump.
    */

    if (input->jump &&
        player->grounded) {

        player->velocity_y =
            PLAYER_JUMP_SPEED;

        player->grounded = false;
    }


    /*
        Gravity.
    */

    player->velocity_y -=
        PLAYER_GRAVITY *
        delta_time;


    /*
        Horizontal movement.

        All horizontal collision decisions go through
        player_try_move().
    */

    float movement_x =
        player->velocity_x *
        delta_time;

    float movement_z =
        player->velocity_z *
        delta_time;


    player_move_substeps(
        player,
        level,
        movement_x,
        movement_z
    );


    /*
        Determine the current sector's floor and ceiling.
    */

    Sector *sector =
        &level->sectors[player->sector];

    float floor_height =
        sector->floor_height;

    float ceiling_height =
        sector->ceiling_height;


    /*
        Vertical movement.
    */

    player->y +=
        player->velocity_y *
        delta_time;


    /*
        Floor collision.
    */

    if (player->y <= floor_height) {

        player->y =
            floor_height;

        player->velocity_y =
            0.0f;

        player->grounded =
            true;
    }
    else {
        player->grounded =
            false;
    }


    /*
        Ceiling collision.

        The player's top cannot pass through
        the sector ceiling.
    */

    float player_top =
        player->y +
        player->height;


    if (player_top > ceiling_height) {

        player->y =
            ceiling_height -
            player->height;

        player->velocity_y =
            0.0f;
    }
}