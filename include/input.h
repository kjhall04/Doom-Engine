#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <SDL3/SDL.h>

typedef struct Input {
    bool move_forward;
    bool move_backward;
    bool move_left;
    bool move_right;

    bool jump;

    float mouse_delta_x;
    float mouse_delta_y;

    bool right_click;
    bool left_click;
} Input;

void input_init(Input *input);
void input_process_event(Input *input, SDL_Event *__event);

#endif