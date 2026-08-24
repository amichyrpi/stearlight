#include "pairing.h"

#include "ui.h"

#include <SDL.h>

void svrt_pairing_gui_show(svrt_steam_link_pairing *pairing,
                           volatile sig_atomic_t *quitting, svrt_ui *ui) {
    if (!pairing || !ui) return;
    uint32_t waiting_since = 0;
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
        SDL_Delay(16);
    }
}
