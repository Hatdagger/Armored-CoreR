/* PC shell (SDL3): window, input, the 64 MB memory block and the fixed-step
 * loop. For now it drives a placeholder marker so the clock, input and
 * interpolation can be checked on real hardware. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdlib.h>

#include "../../core/acr_clock.h"
#include "../../core/acr_fixed.h"
#include "../../core/acr_gen1.h"
#include "../../core/acr_pools.h"

#define VIEW_W 640
#define VIEW_H 480

typedef struct marker { acr_fx x, y, prev_x, prev_y; } marker;

static SDL_Gamepad *open_first_gamepad(void) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_Gamepad *pad = (ids && count > 0) ? SDL_OpenGamepad(ids[0]) : NULL;
    SDL_free(ids);
    return pad;
}

/* One logic tick: integer-only, so it gives the same result everywhere. */
static void tick(marker *m, int move_x, int move_y) {
    const acr_fx speed = acr_fx_from_int(8);
    m->prev_x = m->x;
    m->prev_y = m->y;
    m->x += move_x * speed / 128;
    m->y += move_y * speed / 128;
    const acr_fx max_x = acr_fx_from_int(VIEW_W - 16), max_y = acr_fx_from_int(VIEW_H - 16);
    if (m->x < 0) m->x = 0;
    if (m->y < 0) m->y = 0;
    if (m->x > max_x) m->x = max_x;
    if (m->y > max_y) m->y = max_y;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    void *block = malloc(ACR_POOLS_TOTAL_BYTES);
    acr_pools pools;
    if (!block || !acr_pools_init(&pools, block, ACR_POOLS_TOTAL_BYTES)) return 1;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return 1;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (!SDL_CreateWindowAndRenderer("Armored-CoreR", 1280, 960, SDL_WINDOW_RESIZABLE,
                                     &window, &renderer)) {
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetRenderLogicalPresentation(renderer, VIEW_W, VIEW_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Gamepad *pad = open_first_gamepad();

    acr_clock clock;
    acr_clock_init_ns(&clock, ACR_GEN1_TICK_NS, 8);   /* Gen 1: 22 steps a second */
    marker m = { acr_fx_from_int(VIEW_W / 2), acr_fx_from_int(VIEW_H / 2), 0, 0 };
    m.prev_x = m.x;
    m.prev_y = m.y;

    Uint64 last = SDL_GetTicksNS();
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = 0;
            if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad) pad = SDL_OpenGamepad(e.gdevice.which);
            if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad &&
                SDL_GetGamepadID(pad) == e.gdevice.which) {
                SDL_CloseGamepad(pad);
                pad = NULL;
            }
        }

        /* Input sampled once a frame as -128..127 per axis. */
        const bool *keys = SDL_GetKeyboardState(NULL);
        int move_x = (keys[SDL_SCANCODE_D] ? 127 : 0) - (keys[SDL_SCANCODE_A] ? 128 : 0);
        int move_y = (keys[SDL_SCANCODE_S] ? 127 : 0) - (keys[SDL_SCANCODE_W] ? 128 : 0);
        if (pad) {
            const int ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 256;
            const int ay = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 256;
            if (ax > 24 || ax < -24) move_x = ax;
            if (ay > 24 || ay < -24) move_y = ay;
            if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK)) running = 0;
        }
        if (keys[SDL_SCANCODE_ESCAPE]) running = 0;

        const Uint64 now = SDL_GetTicksNS();
        const uint32_t ticks = acr_clock_advance(&clock, now - last);
        last = now;
        for (uint32_t i = 0; i < ticks; ++i) tick(&m, move_x, move_y);

        /* Draw between the last two ticks, so motion is smooth at any refresh rate. */
        const acr_fx a = acr_clock_alpha(&clock);
        const acr_fx draw_x = m.prev_x + acr_fx_mul(m.x - m.prev_x, a);
        const acr_fx draw_y = m.prev_y + acr_fx_mul(m.y - m.prev_y, a);
        SDL_SetRenderDrawColor(renderer, 16, 20, 24, 255);
        SDL_RenderClear(renderer);
        SDL_SetRenderDrawColor(renderer, 230, 180, 40, 255);
        const SDL_FRect r = { (float)draw_x / ACR_FX_ONE, (float)draw_y / ACR_FX_ONE, 16.0f, 16.0f };
        SDL_RenderFillRect(renderer, &r);
        SDL_RenderPresent(renderer);
    }

    if (pad) SDL_CloseGamepad(pad);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(block);
    return 0;
}
