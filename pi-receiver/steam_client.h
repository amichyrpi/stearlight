#ifndef SVRT_STEAM_CLIENT_H
#define SVRT_STEAM_CLIENT_H

#include <SDL.h>
#include <X11/Xlib.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum svrt_steam_client_state {
    SVRT_STEAM_CLIENT_MISSING,
    SVRT_STEAM_CLIENT_STARTING,
    SVRT_STEAM_CLIENT_RUNNING,
    SVRT_STEAM_CLIENT_FAILED
} svrt_steam_client_state;

typedef struct svrt_steam_client {
    pid_t display_pid;
    pid_t window_manager_pid;
    pid_t steam_pid;
    Display *display;
    Window root;
    SDL_Texture *frame;
    svrt_steam_client_state state;
    uint32_t next_connect_ms;
    uint32_t next_capture_ms;
    char detail[160];
} svrt_steam_client;

int svrt_steam_client_start(svrt_steam_client *client, SDL_Renderer *renderer);
void svrt_steam_client_update(svrt_steam_client *client,
                              SDL_Renderer *renderer, uint32_t now_ms);
SDL_Texture *svrt_steam_client_frame(const svrt_steam_client *client);
const char *svrt_steam_client_detail(const svrt_steam_client *client);
void svrt_steam_client_stop(svrt_steam_client *client);
void svrt_steam_client_open_uri(const svrt_steam_client *client,
                                const char *uri);

#endif
