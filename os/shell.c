#define _POSIX_C_SOURCE 200809L

#include "ui.h"
#include "steam_client.h"

#include <SDL.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
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

static void forward_ui_input(const svrt_ui_input *input, void *opaque) {
    stearlight_steam_client *client = opaque;
    if (!input || !client || !input->on_surface) return;
    switch (input->type) {
        case SVRT_UI_INPUT_MOUSE_MOTION:
            stearlight_steam_client_send_mouse_motion(
                client, input->surface_x, input->surface_y,
                input->surface_width, input->surface_height);
            break;
        case SVRT_UI_INPUT_MOUSE_BUTTON_DOWN:
        case SVRT_UI_INPUT_MOUSE_BUTTON_UP:
            /* SDL can deliver the first button event before a motion event
               after the private X display is created. Put XTest's pointer on
               the same surface coordinate before sending the click so Valve
               receives it at the location the user selected. */
            stearlight_steam_client_send_mouse_motion(
                client, input->surface_x, input->surface_y,
                input->surface_width, input->surface_height);
            stearlight_steam_client_send_mouse_button(
                client, input->button,
                input->type == SVRT_UI_INPUT_MOUSE_BUTTON_DOWN);
            break;
        case SVRT_UI_INPUT_MOUSE_WHEEL:
            stearlight_steam_client_send_mouse_motion(
                client, input->surface_x, input->surface_y,
                input->surface_width, input->surface_height);
            stearlight_steam_client_send_mouse_wheel(client, input->wheel_y);
            break;
        case SVRT_UI_INPUT_KEY_DOWN:
        case SVRT_UI_INPUT_KEY_UP:
            stearlight_steam_client_send_key(
                client, input->keycode,
                input->type == SVRT_UI_INPUT_KEY_DOWN, input->modifiers);
            break;
    }
}

static void open_steam_page(stearlight_steam_client *client,
                            svrt_ui_action action) {
    const char *uri = NULL;
    switch (action) {
        /* These are Valve client URL handlers found in the installed Steam
           client. Keep navigation in Steam instead of recreating its pages
           in the Stearlight shell. */
        case SVRT_UI_ACTION_HOME: uri = "steam://open/bigpicture"; break;
        case SVRT_UI_ACTION_LIBRARY: uri = "steam://open/games"; break;
        case SVRT_UI_ACTION_SHOP: uri = "steam://store"; break;
        case SVRT_UI_ACTION_FRIENDS:
            uri = "steam://url/SteamIDFriendsPage";
            break;
        case SVRT_UI_ACTION_MEDIA: uri = "steam://open/screenshots"; break;
        case SVRT_UI_ACTION_DOWNLOADS: uri = "steam://open/downloads"; break;
        case SVRT_UI_ACTION_SETTINGS: uri = "steam://open/settings"; break;
        case SVRT_UI_ACTION_PROFILE: uri = "steam://url/CommunityHome"; break;
        case SVRT_UI_ACTION_CONNECTION:
            stearlight_steam_client_open_steam_link(client);
            return;
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

    /* The DRM/KMS boot hand-off runs the same shell for the short userspace
       splash, then gives the device back to gamescope.  Do not start Steam
       during this phase: starting it would keep the first compositor alive
       and either skip the movie or race the gamescope DRM takeover. */
    const char *boot_only = getenv("SVRT_BOOT_ONLY");
    if (boot_only && boot_only[0] && strcmp(boot_only, "0") != 0) {
        uint64_t deadline_ns = 0;
        while (!quitting && !svrt_ui_boot_finished(&ui)) {
            const uint32_t now = SDL_GetTicks();
            svrt_ui_draw(&ui, SVRT_UI_SEARCHING, NULL, NULL, NULL, now);
            sleep_frame(&deadline_ns);
        }
        svrt_ui_close(&ui);
        return 0;
    }

    stearlight_steam_client steam;
    if (stearlight_steam_client_start(&steam, svrt_ui_renderer(&ui))) {
        fprintf(stderr, "STEARLIGHT SHELL: Steam startup failed\n");
        svrt_ui_close(&ui);
        return 1;
    }
    svrt_ui_set_input_callback(&ui, forward_ui_input, &steam);
    svrt_ui_set_streaming_mode(&ui, 0);

    uint64_t deadline_ns = 0;
    while (!quitting) {
        const uint32_t now = SDL_GetTicks();
        stearlight_steam_client_update(&steam, svrt_ui_renderer(&ui), now);
        SDL_Texture *client_frame = stearlight_steam_client_frame(&steam);
        svrt_ui_set_client_frame(&ui, client_frame);
#if SVRT_UI_MINIMAL_STEAMOS
        /* The Steam window can become capturable while boot.mkv is still
           playing.  Keep SEARCHING until loop.mkv has actually been presented
           for one complete transition interval, then hand off to the first
           real Steam Gamepad UI frame. */
        const svrt_ui_state ui_state =
            client_frame && svrt_ui_loop_transition_ready(&ui, now) ?
                SVRT_UI_HOME : SVRT_UI_SEARCHING;
#else
        const svrt_ui_state ui_state = SVRT_UI_HOME;
#endif
        svrt_ui_draw(&ui, ui_state, NULL, NULL,
                     stearlight_steam_client_detail(&steam), now);
        /* Navigation chrome is only a pointer-friendly shell.  Every page
           action is handed back to Valve through its own URI handler; the
           shell never implements pairing or streaming. */
        const svrt_ui_action action = svrt_ui_take_action(&ui);
        open_steam_page(&steam, action);
        if (svrt_ui_take_connection_request(&ui) &&
            action != SVRT_UI_ACTION_CONNECTION)
            stearlight_steam_client_open_steam_link(&steam);
        sleep_frame(&deadline_ns);
    }

    stearlight_steam_client_stop(&steam);
    svrt_ui_close(&ui);
    return 0;
}
