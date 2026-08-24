#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>

typedef enum svrt_ui_state {
    SVRT_UI_SEARCHING,
    SVRT_UI_AUTHORIZING,
    SVRT_UI_STARTING,
    SVRT_UI_FAILED
} svrt_ui_state;

typedef struct svrt_ui_video svrt_ui_video;

typedef struct svrt_ui {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    TTF_Font *code_font;
    svrt_ui_video *boot;
    svrt_ui_video *loop;
    svrt_ui_video *steam_loading;
    svrt_ui_video *background;
    SDL_Texture *panel;
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
