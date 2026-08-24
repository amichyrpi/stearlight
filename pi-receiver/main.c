#include <svrt.h>

#include "status.h"
#include "audio.h"
#include "pairing.h"
#include "steam_link_pairing.h"
#include "steam_client.h"

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static svrt_context *running;
static volatile sig_atomic_t quitting;

static void stop(int sig) {
    (void)sig;
    quitting = 1;
}

typedef struct monitor_args {
    svrt_context *context;
    svrt_status_server *server;
    atomic_int stopping;
} monitor_args;

typedef struct run_args {
    svrt_context *context;
    atomic_int done;
    int result;
} run_args;

static void *run_receiver(void *opaque) {
    run_args *args = opaque;
    args->result = svrt_run(args->context);
    atomic_store(&args->done, 1);
    return NULL;
}

static void *monitor_receiver(void *opaque) {
    monitor_args *args = opaque;
    uint64_t previous_decoded = 0;
    while (!atomic_load(&args->stopping)) {
        svrt_stats stats = {0};
        svrt_get_stats(args->context, &stats);
        int state = stats.decoded_frames > previous_decoded
                        ? SVRT_RECEIVER_STREAMING
                        : SVRT_RECEIVER_READY;
        previous_decoded = stats.decoded_frames;
        svrt_status_server_update(args->server, state, &stats);
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 250000000};
        nanosleep(&delay, NULL);
    }
    return NULL;
}

static int finish_steam_link_pairing(svrt_steam_link_pairing *pairing,
                                     int headless,
                                     svrt_status_server *status, svrt_ui *ui) {
    if (headless) {
        svrt_steam_link_state state = SVRT_STEAM_LINK_SEARCHING;
        char error[128] = {0};
        while (!quitting && state != SVRT_STEAM_LINK_PAIRED &&
               state != SVRT_STEAM_LINK_FAILED) {
            svrt_steam_link_pairing_snapshot(pairing, &state, NULL, NULL,
                                             error, NULL);
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 50000000};
            nanosleep(&delay, NULL);
        }
        if (state == SVRT_STEAM_LINK_FAILED)
            fprintf(stderr, "SVRT Steam Link: %s\n", error);
    } else {
        svrt_pairing_gui_show(pairing, &quitting, ui);
    }
    const int paired = svrt_steam_link_pairing_is_paired(pairing);
    uint64_t device_id = 0;
    svrt_steam_link_pairing_snapshot(pairing, NULL, NULL, NULL, NULL,
                                     &device_id);
    svrt_steam_link_pairing_stop(pairing);
    svrt_status_server_set_steam_device_id(status, device_id);
    if (paired) {
        char address[64] = {0};
        if (svrt_steam_link_pairing_host_address(address, sizeof(address)))
            svrt_status_server_set_paired_host(status, address);
    }
    return paired;
}

static int authorize_steam_link(int headless, svrt_status_server *status,
                                svrt_ui *ui) {
    svrt_steam_link_pairing pairing;
    if (svrt_steam_link_pairing_start(&pairing)) {
        fprintf(stderr, "SVRT: cannot start Steam Link discovery\n");
        return -1;
    }
    return finish_steam_link_pairing(&pairing, headless, status, ui);
}

static void open_steam_page(svrt_steam_client *client,
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
    if (uri) svrt_steam_client_open_uri(client, uri);
}

int main(int argc, char **argv) {
    int headless = 0;
    uint16_t port = 9944;
    uint16_t status_port = 0;
    uint16_t audio_port = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            puts("usage: svrt-receiver [--headless] [--status-port PORT] [--audio-port PORT] [video-port]");
            return 0;
        }
        if (!strcmp(argv[i], "--headless")) {
            headless = 1;
        } else if (!strcmp(argv[i], "--status-port") && i + 1 < argc) {
            status_port = (uint16_t)atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--audio-port") && i + 1 < argc) {
            audio_port = (uint16_t)atoi(argv[++i]);
        } else {
            port = (uint16_t)atoi(argv[i]);
        }
    }
    if (!status_port) status_port = (uint16_t)(port + 1);
    if (!audio_port) audio_port = (uint16_t)(port + 2);

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    int exit_code = 0;
    svrt_status_server status;
    if (svrt_status_server_start(&status, status_port)) {
        fprintf(stderr, "SVRT: cannot listen for driver health checks on port %u\n",
                status_port);
        return 1;
    }
    svrt_ui ui;
    svrt_steam_client steam_client;
    int steam_client_started = 0;
    int have_ui = 0;
    if (!headless) {
        have_ui = svrt_ui_open(&ui) == 0;
        if (!have_ui) {
            fprintf(stderr, "SVRT: GUI unavailable; falling back to headless mode\n");
            headless = 1;
        }
    }
    if (have_ui) {
        svrt_steam_client_start(&steam_client, svrt_ui_renderer(&ui));
        steam_client_started = 1;
    }
    const char *start_streaming = getenv("SVRT_START_IN_STREAMING_MODE");
    int streaming_mode = headless ||
                         (start_streaming && start_streaming[0] &&
                          strcmp(start_streaming, "0"));
    if (have_ui) svrt_ui_set_streaming_mode(&ui, streaming_mode);
    while (!quitting) {
        svrt_status_server_update(&status, SVRT_RECEIVER_UNAUTHORIZED, NULL);
        /* Standalone Steam is the receiver's home mode.  Steam Link pairing
           and the video socket are entered only from the connection tile. */
        while (have_ui && !quitting && !streaming_mode) {
            const uint32_t now = SDL_GetTicks();
            svrt_steam_client_update(&steam_client,
                                     svrt_ui_renderer(&ui), now);
            svrt_ui_set_client_frame(
                &ui, svrt_steam_client_frame(&steam_client));
            svrt_ui_draw(&ui, SVRT_UI_HOME, NULL, NULL,
                         svrt_steam_client_detail(&steam_client), now);
            const svrt_ui_action action = svrt_ui_take_action(&ui);
            const int connection_requested =
                svrt_ui_take_connection_request(&ui);
            if (action == SVRT_UI_ACTION_CONNECTION ||
                connection_requested) {
                streaming_mode = 1;
                svrt_ui_set_streaming_mode(&ui, 1);
                fprintf(stderr, "SVRT: connection tile selected\n");
            } else
                open_steam_page(&steam_client, action);
            struct timespec delay = {.tv_sec = 0, .tv_nsec = 16000000};
            nanosleep(&delay, NULL);
        }
        if (quitting) break;
        const int authorized = authorize_steam_link(headless, &status,
                                                     have_ui ? &ui : NULL);
        if (quitting) break;
        if (authorized <= 0) {
            exit_code = 1;
            struct timespec retry = {.tv_sec = 2};
            nanosleep(&retry, NULL);
            continue;
        }
        exit_code = 0;
        svrt_status_server_reset_authorization(&status);
        svrt_status_server_update(&status, SVRT_RECEIVER_STARTING, NULL);
        svrt_audio_receiver audio;
        int audio_started = svrt_audio_receiver_start(&audio, audio_port) == 0;
        if (!audio_started)
            fprintf(stderr, "SVRT audio: failed to start receiver\n");

        svrt_ui_state wait_state = SVRT_UI_HOME;
        if (have_ui) svrt_ui_set_streaming_mode(&ui, streaming_mode);
        while (!quitting &&
               !svrt_status_server_authorization_revoked(&status)) {
            svrt_config cfg = {.port = port,
                               .require_hardware = 1,
                               .require_zero_copy = 1,
                               .fullscreen = 1,
                               .headless = headless,
                               .packet_event = svrt_status_server_packet_event,
                               .packet_event_opaque = &status,
                               .display_window = have_ui ? svrt_ui_window(&ui) : NULL,
                               .display_renderer = have_ui ? svrt_ui_renderer(&ui) : NULL};
            svrt_status_server_reset_trace(&status);
            if (svrt_open(&running, &cfg)) {
                svrt_status_server_update(&status, SVRT_RECEIVER_ERROR, NULL);
                exit_code = 1;
                struct timespec retry = {.tv_sec = 2};
                nanosleep(&retry, NULL);
                continue;
            }
            exit_code = 0;
            svrt_status_server_update(&status, SVRT_RECEIVER_READY, NULL);
            monitor_args monitor = {.context = running, .server = &status};
            pthread_t monitor_thread;
            int monitoring = pthread_create(&monitor_thread, NULL,
                                             monitor_receiver, &monitor) == 0;
            run_args runner = {.context = running, .result = 0};
            pthread_t run_thread;
            int running_in_thread = pthread_create(&run_thread, NULL,
                                                   run_receiver, &runner) == 0;
            svrt_steam_link_pairing authorization_probe;
            int probe_active = 0, probe_requires_login = 0;
            uint32_t probe_authorizing_ms = 0;
            uint32_t next_probe_ms = SDL_GetTicks() + 3000;
            int rc;
            if (running_in_thread) {
                while (!quitting && !atomic_load(&runner.done) &&
                       !svrt_status_server_authorization_revoked(&status)) {
                    svrt_stats ui_stats = {0};
                    svrt_get_stats(running, &ui_stats);
                    const uint32_t now = SDL_GetTicks();
                    if (have_ui) {
                        const svrt_ui_action action = svrt_ui_take_action(&ui);
                        const int connection_requested =
                            svrt_ui_take_connection_request(&ui);
                        if (action == SVRT_UI_ACTION_CONNECTION ||
                            connection_requested) {
                            streaming_mode = !streaming_mode;
                            svrt_ui_set_streaming_mode(&ui, streaming_mode);
                            fprintf(stderr, "SVRT: switched to %s mode\n",
                                    streaming_mode ? "streaming" : "Steam");
                        } else
                            open_steam_page(&steam_client, action);
                        if (!streaming_mode || ui_stats.decoded_frames == 0) {
                            svrt_steam_client_update(&steam_client,
                                                     svrt_ui_renderer(&ui), now);
                            svrt_ui_set_client_frame(
                                &ui, svrt_steam_client_frame(&steam_client));
                            svrt_ui_draw(
                                &ui,
                                streaming_mode ? wait_state : SVRT_UI_HOME,
                                NULL, NULL,
                                svrt_steam_client_detail(&steam_client), now);
                        }
                    }

                    /* SteamVR is not required for authorization monitoring.
                       While idle, periodically ask Steam Remote Play to
                       validate this device. Trusted devices complete the
                       request silently; a revoked device remains in the PIN
                       state and that same request is promoted to the UI. */
                    if (ui_stats.decoded_frames == 0) {
                        if (!probe_active && now >= next_probe_ms) {
                            if (!svrt_steam_link_pairing_start(
                                    &authorization_probe)) {
                                probe_active = 1;
                                probe_authorizing_ms = 0;
                            } else {
                                next_probe_ms = now + 5000;
                            }
                        }
                        if (probe_active) {
                            svrt_steam_link_state probe_state;
                            svrt_steam_link_pairing_snapshot(
                                &authorization_probe, &probe_state, NULL,
                                NULL, NULL, NULL);
                            if (probe_state == SVRT_STEAM_LINK_PAIRED) {
                                svrt_steam_link_pairing_stop(
                                    &authorization_probe);
                                probe_active = 0;
                                next_probe_ms = now + 5000;
                            } else if (probe_state ==
                                       SVRT_STEAM_LINK_AUTHORIZING) {
                                if (!probe_authorizing_ms)
                                    probe_authorizing_ms = now;
                                else if (now - probe_authorizing_ms >= 1000) {
                                    probe_requires_login = 1;
                                    svrt_stop(running);
                                }
                            } else if (probe_state ==
                                       SVRT_STEAM_LINK_FAILED) {
                                svrt_steam_link_pairing_stop(
                                    &authorization_probe);
                                probe_active = 0;
                                next_probe_ms = now + 5000;
                            }
                        }
                    } else if (probe_active) {
                        svrt_steam_link_pairing_stop(&authorization_probe);
                        probe_active = 0;
                        next_probe_ms = now + 5000;
                    }
                    struct timespec delay = {.tv_sec = 0,
                                             .tv_nsec = 16000000};
                    nanosleep(&delay, NULL);
                }
                if (quitting ||
                    svrt_status_server_authorization_revoked(&status))
                    svrt_stop(running);
                pthread_join(run_thread, NULL);
                rc = runner.result;
            } else {
                fprintf(stderr, "SVRT: failed to start video worker thread\n");
                rc = -1;
                exit_code = 1;
            }
            if (probe_active && !probe_requires_login)
                svrt_steam_link_pairing_stop(&authorization_probe);
            if (monitoring) {
                atomic_store(&monitor.stopping, 1);
                pthread_join(monitor_thread, NULL);
            }

            svrt_stats stats = {0};
            svrt_get_stats(running, &stats);
            const svrt_end_reason end_reason = svrt_get_end_reason(running);
            if (probe_requires_login) {
                fprintf(stderr,
                        "SVRT Steam Link: authorization probe requires a PIN\n");
                svrt_status_server_update(&status,
                                          SVRT_RECEIVER_UNAUTHORIZED, &stats);
                svrt_close(&running);
                running = NULL;
                const int paired = finish_steam_link_pairing(
                    &authorization_probe, headless, &status,
                    have_ui ? &ui : NULL);
                if (paired) {
                    svrt_status_server_reset_authorization(&status);
                    svrt_status_server_update(&status,
                                              SVRT_RECEIVER_STARTING, NULL);
                    wait_state = SVRT_UI_STARTING;
                    exit_code = 0;
                } else {
                    svrt_status_server_revoke_authorization(&status);
                }
                continue;
            }
            if (end_reason == SVRT_END_DISCONNECTED) {
                /* Steam Link explicitly detached this headset.  Do not wait
                   for a future SteamVR driver poll to discover that the
                   grant is gone: return to the PIN flow immediately. */
                fprintf(stderr,
                        "SVRT Steam Link: headset disconnected; requesting authorization again\n");
                svrt_status_server_revoke_authorization(&status);
            }
            const int revoked =
                svrt_status_server_authorization_revoked(&status);
            if (revoked || quitting) {
                svrt_status_server_update(&status,
                                          SVRT_RECEIVER_UNAUTHORIZED, &stats);
            } else if (rc) {
                fprintf(stderr, "SVRT: %s\n", svrt_last_error(running));
                svrt_status_server_update(&status, SVRT_RECEIVER_ERROR, &stats);
                exit_code = 1;
            } else {
                svrt_status_server_update(&status, SVRT_RECEIVER_READY, &stats);
                exit_code = 0;
            }
            fprintf(stderr, "SVRT: %llu decoded, %llu shown, %llu dropped\n",
                    (unsigned long long)stats.decoded_frames,
                    (unsigned long long)stats.presented_frames,
                    (unsigned long long)stats.dropped_frames);
            svrt_close(&running);
            running = NULL;
            if (!quitting && !revoked) wait_state = SVRT_UI_SEARCHING;
            if (have_ui && !quitting && !revoked) {
                /* Neither shutdown nor disconnect has a status screen.  Prime
                   the idle animation immediately between video sessions. */
                const uint32_t loop_start = SDL_GetTicks();
                while (!quitting && SDL_GetTicks() - loop_start < 500) {
                    const uint32_t now = SDL_GetTicks();
                    svrt_ui_draw(&ui, SVRT_UI_SEARCHING, NULL, NULL, NULL, now);
                    struct timespec delay = {.tv_sec = 0, .tv_nsec = 16000000};
                    nanosleep(&delay, NULL);
                }
            }
            if (!quitting && !revoked) {
                struct timespec retry = {.tv_sec = rc ? 2 : 0,
                                         .tv_nsec = rc ? 0 : 250000000};
                nanosleep(&retry, NULL);
            }
        }
        if (audio_started) svrt_audio_receiver_stop(&audio);
        if (svrt_status_server_authorization_revoked(&status)) {
            fprintf(stderr, "SVRT Steam Link: host revoked this headset; returning to pairing\n");
            svrt_steam_link_pairing_forget_host();
            svrt_status_server_set_paired_host(&status, NULL);
            svrt_status_server_update(&status, SVRT_RECEIVER_UNAUTHORIZED, NULL);
        }
    }
    svrt_status_server_stop(&status);
    if (steam_client_started) svrt_steam_client_stop(&steam_client);
    if (have_ui) svrt_ui_close(&ui);
    return exit_code;
}
