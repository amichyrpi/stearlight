#pragma once

#include <SDL.h>
#include <stdint.h>
#include <sys/types.h>

/* This is Valve's Steam-client URL handler.  The OS never implements a
 * second Steam Link/VRLink discovery, pairing, or transport protocol. */
#define STEARLIGHT_STEAM_LINK_URI "steamlink://lookup/"
#define STEARLIGHT_STEAM_URI_MAX 512

/* Standalone Steam process/display bridge used only by the OS shell.  The
 * receiver has its own implementation in pi-receiver/ and is not part of
 * the appliance build. */
typedef enum stearlight_steam_client_state {
    STEARLIGHT_STEAM_CLIENT_MISSING,
    STEARLIGHT_STEAM_CLIENT_STARTING,
    STEARLIGHT_STEAM_CLIENT_RUNNING,
    STEARLIGHT_STEAM_CLIENT_FAILED
} stearlight_steam_client_state;

typedef struct stearlight_steam_client {
    pid_t display_pid;
    pid_t steam_pid;
    void *display;
    unsigned long root;
    unsigned long content_window;
    SDL_Texture *frame;
    int frame_width;
    int frame_height;
    int frame_announced;
    uint32_t next_connect_ms;
    uint32_t next_capture_ms;
    unsigned int launch_failures;
    unsigned int x_modifiers;
    stearlight_steam_client_state state;
    /* A navigation/connection request can arrive while Steam is starting or
     * being restarted. Keep the most recent Valve URI and dispatch it after
     * the first visible client frame, so the request is never lost and no
     * second transport implementation is needed. */
    char pending_uri[STEARLIGHT_STEAM_URI_MAX];
    char detail[160];
} stearlight_steam_client;

int stearlight_steam_client_start(stearlight_steam_client *client,
                                   SDL_Renderer *renderer);
void stearlight_steam_client_update(stearlight_steam_client *client,
                                     SDL_Renderer *renderer,
                                     uint32_t now_ms);
SDL_Texture *stearlight_steam_client_frame(
    const stearlight_steam_client *client);
const char *stearlight_steam_client_detail(
    const stearlight_steam_client *client);
void stearlight_steam_client_stop(stearlight_steam_client *client);
void stearlight_steam_client_open_uri(
    stearlight_steam_client *client, const char *uri);
void stearlight_steam_client_open_steam_link(
    stearlight_steam_client *client);
void stearlight_steam_client_send_mouse_motion(
    const stearlight_steam_client *client, int surface_x, int surface_y,
    int surface_width, int surface_height);
void stearlight_steam_client_send_mouse_button(
    const stearlight_steam_client *client, int button, int pressed);
void stearlight_steam_client_send_mouse_wheel(
    const stearlight_steam_client *client, int delta_y);
void stearlight_steam_client_send_key(
    stearlight_steam_client *client, int keycode, int pressed,
    int modifiers);
