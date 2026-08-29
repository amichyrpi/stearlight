#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>

/*
 * DEBUGGING ONLY: draws the loading and loop scenes for the left eye only.
 *
 * The left-eye image is uniformly enlarged and centered with black side
 * margins, preserving its projection, panel curvature, and grid geometry.
 * The flat boot movie and decoded SteamVR stream are deliberately unaffected.
 * Set to 0 to restore the normal stereo loading and loop scenes.
 */
#ifndef SVRT_ENABLE_DEBUG_LEFT_EYE_UI
#define SVRT_ENABLE_DEBUG_LEFT_EYE_UI 1
#endif
#ifndef SVRT_DEBUG_LEFT_EYE_UI_SCALE
#define SVRT_DEBUG_LEFT_EYE_UI_SCALE 1.25f
#endif

/* The appliance image renders only the world-locked Steam surface.  The
 * normal receiver keeps its SteamVR-style environment and controls. */
#ifndef SVRT_UI_MINIMAL_STEAMOS
#define SVRT_UI_MINIMAL_STEAMOS 0
#endif

typedef enum svrt_ui_state {
    SVRT_UI_SEARCHING,
    SVRT_UI_AUTHORIZING,
    SVRT_UI_STARTING,
    SVRT_UI_FAILED,
    SVRT_UI_HOME
} svrt_ui_state;

typedef enum svrt_ui_action {
    SVRT_UI_ACTION_NONE,
    SVRT_UI_ACTION_HOME,
    SVRT_UI_ACTION_LIBRARY,
    SVRT_UI_ACTION_SHOP,
    SVRT_UI_ACTION_FRIENDS,
    SVRT_UI_ACTION_DOWNLOADS,
    SVRT_UI_ACTION_SETTINGS,
    SVRT_UI_ACTION_PROFILE,
    SVRT_UI_ACTION_CONNECTION
} svrt_ui_action;

typedef struct svrt_ui_video svrt_ui_video;

typedef struct svrt_ui {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    TTF_Font *code_font;
    TTF_Font *small_font;
    svrt_ui_video *boot;
    svrt_ui_video *loop;
    svrt_ui_video *steam_loading;
    svrt_ui_video *background;
    svrt_ui_video *avatar;
    SDL_Texture *panel;
    SDL_Texture *left_eye_scene;
    SDL_Texture *client_frame;
    int connection_requested;
    int streaming_mode;
    int selected_page;
    svrt_ui_action pending_action;
    uint32_t next_avatar_scan_ms;
    char avatar_path[512];
    svrt_ui_state state;
    uint32_t state_started_ms;
    uint32_t boot_started_ms;
    /* Monotonic UI trace points.  They are intentionally public so the Pi
       transition test and the receiver log can verify ordering/timing. */
    uint32_t loop_first_frame_ms;
    int owns_sdl;
} svrt_ui;

int svrt_ui_open(svrt_ui *ui);
void svrt_ui_close(svrt_ui *ui);
void svrt_ui_draw(svrt_ui *ui, svrt_ui_state state, const char code[5],
                  const char *hostname, const char *detail, uint32_t now_ms);
SDL_Window *svrt_ui_window(svrt_ui *ui);
SDL_Renderer *svrt_ui_renderer(svrt_ui *ui);
void svrt_ui_set_client_frame(svrt_ui *ui, SDL_Texture *frame);
void svrt_ui_set_streaming_mode(svrt_ui *ui, int enabled);
int svrt_ui_take_connection_request(svrt_ui *ui);
svrt_ui_action svrt_ui_take_action(svrt_ui *ui);
