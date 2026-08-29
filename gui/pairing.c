#include "pairing.h"

#include "ui.h"

#include <SDL.h>

static void pace_pairing_frame(uint64_t *next_deadline) {
    if (!next_deadline) return;
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    const uint64_t period = frequency / SVRT_DISPLAY_REFRESH_HZ;
    if (!frequency || !period) return;
    const uint64_t now = SDL_GetPerformanceCounter();
    if (!*next_deadline) *next_deadline = now;
    else if (now > *next_deadline + period * 4) {
        *next_deadline = now;
        return;
    }
    *next_deadline += period;
    const uint64_t current = SDL_GetPerformanceCounter();
    if (*next_deadline <= current) return;
    const uint64_t remaining = *next_deadline - current;
    SDL_Delay((Uint32)(remaining * 1000 / frequency));
}

void svrt_pairing_gui_show(svrt_steam_link_pairing *pairing,
                           volatile sig_atomic_t *quitting, svrt_ui *ui) {
    if (!pairing || !ui) return;
    uint32_t waiting_since = 0;
    uint64_t next_frame = 0;
    while (!*quitting) {
        char code[5] = {0};
        char hostname[64] = {0}, error[128] = {0};
        svrt_steam_link_state pairing_state;
        svrt_steam_link_pairing_snapshot(pairing, &pairing_state, code,
                                         hostname, error, NULL);
        svrt_ui_state state = SVRT_UI_SEARCHING;
        if (pairing_state == SVRT_STEAM_LINK_AUTHORIZING)
            state = SVRT_UI_AUTHORIZING;
        else if (pairing_state == SVRT_STEAM_LINK_PAIRED)
            state = SVRT_UI_STARTING;
        else if (pairing_state == SVRT_STEAM_LINK_FAILED)
            state = SVRT_UI_FAILED;
        const uint32_t now = SDL_GetTicks();
        svrt_ui_draw(ui, state, code, hostname,
                     error[0] ? error : NULL, now);
        if (state == SVRT_UI_STARTING) {
            if (!waiting_since) waiting_since = now;
            if (now - waiting_since >= 1000) break;
        } else waiting_since = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) *quitting = 1;
        pace_pairing_frame(&next_frame);
    }
}
