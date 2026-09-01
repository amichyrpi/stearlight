#define _POSIX_C_SOURCE 200809L

#include "ui.h"
#include "steam_client.h"

#include <SDL.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t quitting;

static void stop_shell(int sig) {
    (void)sig;
    quitting = 1;
}

static void sleep_frame(uint64_t *deadline_ns) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return;
    uint64_t current = (uint64_t)now.tv_sec * 1000000000ULL +
                       (uint64_t)now.tv_nsec;
    if (!*deadline_ns || *deadline_ns < current)
        *deadline_ns = current;
    *deadline_ns += (uint64_t)SVRT_UI_FRAME_INTERVAL_NS;
    if (*deadline_ns <= current) return;
    struct timespec target = {
        .tv_sec = (time_t)(*deadline_ns / 1000000000ULL),
        .tv_nsec = (long)(*deadline_ns % 1000000000ULL)};
    while (!quitting && clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                                        &target, NULL) == EINTR) {}
}

static void open_steam_page(stearlight_steam_client *client,
                            svrt_ui_action action) {
    const char *uri = NULL;
    switch (action) {
        case SVRT_UI_ACTION_HOME: uri = "steam://open/main"; break;
        case SVRT_UI_ACTION_LIBRARY: uri = "steam://open/games"; break;
        case SVRT_UI_ACTION_SHOP: uri = "steam://store"; break;
        case SVRT_UI_ACTION_FRIENDS: uri = "steam://open/friends"; break;
        case SVRT_UI_ACTION_DOWNLOADS: uri = "steam://open/downloads"; break;
        case SVRT_UI_ACTION_SETTINGS: uri = "steam://open/settings"; break;
        case SVRT_UI_ACTION_PROFILE: uri = "steam://open/account"; break;
        default: break;
    }
    if (uri) stearlight_steam_client_open_uri(client, uri);
}

int main(void) {
    signal(SIGINT, stop_shell);
    signal(SIGTERM, stop_shell);
    fprintf(stderr, "STEARLIGHT STEAM SHELL STARTING\n");

    svrt_ui ui;
    if (svrt_ui_open(&ui)) {
        fprintf(stderr, "STEARLIGHT SHELL: UI initialization failed\n");
        return 1;
    }
    fprintf(stderr, "SVRT UI INITIALIZED\n");

    stearlight_steam_client steam;
    if (stearlight_steam_client_start(&steam, svrt_ui_renderer(&ui))) {
        fprintf(stderr, "STEARLIGHT SHELL: Steam startup failed\n");
        svrt_ui_close(&ui);
        return 1;
    }
    svrt_ui_set_streaming_mode(&ui, 0);

    uint64_t deadline_ns = 0;
    while (!quitting) {
        const uint32_t now = SDL_GetTicks();
        stearlight_steam_client_update(&steam, svrt_ui_renderer(&ui), now);
        svrt_ui_set_client_frame(&ui,
                                 stearlight_steam_client_frame(&steam));
        svrt_ui_draw(&ui, SVRT_UI_HOME, NULL, NULL,
                     stearlight_steam_client_detail(&steam), now);
        open_steam_page(&steam, svrt_ui_take_action(&ui));
        sleep_frame(&deadline_ns);
    }

    stearlight_steam_client_stop(&steam);
    svrt_ui_close(&ui);
    return 0;
}
