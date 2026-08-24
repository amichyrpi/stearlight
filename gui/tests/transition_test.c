#include "ui.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    setenv("SDL_AUDIODRIVER", "dummy", 1);

    svrt_ui ui;
    if (svrt_ui_open(&ui)) {
        fprintf(stderr, "SVRT UI shutdown-removal test: ui open failed\n");
        return 1;
    }

    /* The normal post-shutdown state is SEARCHING.  Let boot.mkv finish and
       verify that the idle loop starts directly without opening the removed
       SteamVR-shutdown screen or its loading movie. */
    const uint32_t deadline = SDL_GetTicks() + 15000;
    while ((!ui.loop_first_frame_ms || ui.boot) && SDL_GetTicks() < deadline) {
        svrt_ui_draw(&ui, SVRT_UI_SEARCHING, NULL, NULL, NULL, SDL_GetTicks());
        SDL_Delay(16);
    }

    const uint32_t loop_start = ui.loop_first_frame_ms;
    const uint32_t loop_delay = loop_start ? loop_start - ui.boot_started_ms : 0;
    fprintf(stderr, "SVRT_UI_TEST shutdown_screen=removed loop_started=%u\n",
            loop_delay);

    int failed = 0;
    failed |= !loop_start;
    failed |= ui.state != SVRT_UI_SEARCHING;
    failed |= ui.steam_loading != NULL;
    failed |= SDL_GetTicks() >= deadline;
    svrt_ui_close(&ui);
    if (failed) {
        fprintf(stderr, "SVRT UI shutdown-removal test: FAILED\n");
        return 1;
    }
    puts("SVRT UI shutdown-removal test: passed");
    return 0;
}
