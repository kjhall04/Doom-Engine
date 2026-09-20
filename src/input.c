#include "input.h"
#include <stdio.h>

void input_init(Input *input) {
    input->move_forward = false;
    input->move_backward = false;
    input->move_left = false;
    input->move_right = false;

    input->jump = false;

    input->mouse_delta_x = 0.0f;
    input->mouse_delta_y = 0.0f;
}

void input_process_event(Input *input, SDL_Event *event) {

    if (event->type == SDL_EVENT_KEY_DOWN) {

        switch (event->key.scancode) {

        case SDL_SCANCODE_W:
            input->move_forward = true;
            break;

        case SDL_SCANCODE_S:
            input->move_backward = true;
            break;

        case SDL_SCANCODE_A:
            input->move_left = true;
            break;

        case SDL_SCANCODE_D:
            input->move_right = true;
            break;

        case SDL_SCANCODE_SPACE:
            input->jump = true;
            break;

        default:
            break;
        }
    }

    if (event->type == SDL_EVENT_KEY_UP) {

        switch (event->key.scancode) {

        case SDL_SCANCODE_W:
            input->move_forward = false;
            break;

        case SDL_SCANCODE_S:
            input->move_backward = false;
            break;

        case SDL_SCANCODE_A:
            input->move_left = false;
            break;

        case SDL_SCANCODE_D:
            input->move_right = false;
            break;

        case SDL_SCANCODE_SPACE:
            input->jump = false;
            break;

        default:
            break;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION) {

        input->mouse_delta_x += event->motion.xrel;
        input->mouse_delta_y += event->motion.yrel;
    }
}