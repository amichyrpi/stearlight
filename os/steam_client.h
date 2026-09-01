#pragma once

#include <SDL.h>
#include <stdint.h>
#include <sys/types.h>

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
    uint32_t next_connect_ms;
    uint32_t next_capture_ms;
    stearlight_steam_client_state state;
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
    const stearlight_steam_client *client, const char *uri);
