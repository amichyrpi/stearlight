#include "ui.h"

#include "dashboard_layout.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>

#define SVRT_EYE_HEIGHT_METERS 1.70f
#define SVRT_EYE_SEPARATION_METERS 0.064f
#define SVRT_VERTICAL_FOV_DEGREES 100.0f
#define SVRT_UI_HORIZONTAL_ASPECT 1.075f
#define SVRT_UI_PANEL_CENTER_X_RATIO 0.519f
#define SVRT_UI_PANEL_CENTER_Y_RATIO 0.440f
#define SVRT_BOOT_SCALE 0.78f
#define SVRT_PI 3.14159265358979323846f

enum {
    SVRT_DASHBOARD_POPOVER_NONE = 0,
    SVRT_DASHBOARD_POPOVER_PERFORMANCE,
    SVRT_DASHBOARD_POPOVER_BATTERY,
    SVRT_DASHBOARD_POPOVER_POWER
};

static SDL_Rect dashboard_popover_rect(const svrt_dashboard_layout *layout,
                                       int popover) {
    SDL_Rect empty = {0, 0, 0, 0};
    if (!layout || popover == SVRT_DASHBOARD_POPOVER_NONE) return empty;
    if (popover == SVRT_DASHBOARD_POPOVER_POWER) {
        const int width = layout->status_bar.w / 2 > 280 ?
                          layout->status_bar.w / 2 : 280;
        const int height = layout->status_bar.h * 3;
        SDL_Rect panel = {
            layout->rail.x + layout->rail.w + layout->rail.w / 5,
            layout->power_button.y + layout->power_button.h - height,
            width, height};
        if (panel.y < 24) panel.y = 24;
        return panel;
    }
    const int width = layout->status_bar.w * 3 / 5 > 300 ?
                      layout->status_bar.w * 3 / 5 : 300;
    const int height = popover == SVRT_DASHBOARD_POPOVER_BATTERY ?
                       (int)lroundf((float)layout->status_bar.h * 6.4f) :
                       (int)lroundf((float)layout->status_bar.h * 5.8f);
    SDL_Rect panel = {
        layout->status_bar.x + layout->status_bar.w / 20,
        layout->status_bar.y - height - layout->status_bar.h / 8,
        width, height};
    if (panel.y < 24) panel.y = 24;
    return panel;
}

struct svrt_ui_video {
    AVFormatContext *format;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    SDL_Texture *texture;
    AVBufferRef *hw_device;
    AVRational time_base;
    int stream;
    int have_frame;
    int64_t frame_ms;
    uint32_t frame_duration_ms;
    uint32_t duration_ms;
    uint32_t playback_start_ms;
    int playback_started;
    uint32_t loops_completed;
    int ended;
    const char *path;
};

static void trace_render_present(svrt_ui *ui, const char *phase) {
    if (!ui || !ui->trace_ui_fps || !phase) return;
    const uint32_t now_ms = SDL_GetTicks();
    if (!ui->trace_window_start_ms)
        ui->trace_window_start_ms = now_ms;
    ++ui->trace_frame_count;
    const uint32_t elapsed_ms = now_ms - ui->trace_window_start_ms;
    if (elapsed_ms < 1000) return;
    fprintf(stderr, "SVRT UI FPS phase=%s frames=%u elapsed=%u fps=%.2f\n",
            phase, ui->trace_frame_count, elapsed_ms,
            ui->trace_frame_count * 1000.0 / elapsed_ms);
    ui->trace_window_start_ms = now_ms;
    ui->trace_frame_count = 0;
}

static svrt_ui_video *video_open(const char *path);

static float scene_focal_length(int eye_height);

static void panel_screen_bounds(int eye_x, int eye_width, int height, int eye,
                                float *left, float *right, float *top,
                                float *bottom) {
    const float focal = scene_focal_length(height);
    const float eye_offset = eye ? SVRT_EYE_SEPARATION_METERS * 0.5f :
                                   -SVRT_EYE_SEPARATION_METERS * 0.5f;
    const float radius = 3.5f;
    const float distance = 2.15f;
    const float arc = (SVRT_UI_MINIMAL_STEAMOS ? SVRT_UI_PANEL_ARC_DEGREES : 44.0f) *
                      SVRT_PI / 180.0f;
    const float panel_height = SVRT_UI_MINIMAL_STEAMOS ?
                                   SVRT_UI_PANEL_HEIGHT_METERS : 1.50f;
    const float center = eye_x + eye_width * SVRT_UI_PANEL_CENTER_X_RATIO;
    float min_x = center;
    float max_x = center;
    float min_y = height;
    float max_y = 0.0f;
    enum { slices = 96 };
    for (int column = 0; column <= slices; ++column) {
        const float angle = ((float)column / slices - 0.5f) * arc;
        const float world_x = radius * sinf(angle);
        const float z = distance - radius * (1.0f - cosf(angle));
        const float x = center + SVRT_UI_HORIZONTAL_ASPECT * focal *
                                  (world_x - eye_offset) / z;
        const float half_height = focal * panel_height * 0.5f / z;
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (height * SVRT_UI_PANEL_CENTER_Y_RATIO - half_height < min_y)
            min_y = height * SVRT_UI_PANEL_CENTER_Y_RATIO - half_height;
        if (height * SVRT_UI_PANEL_CENTER_Y_RATIO + half_height > max_y)
            max_y = height * SVRT_UI_PANEL_CENTER_Y_RATIO + half_height;
    }
    if (left) *left = min_x;
    if (right) *right = max_x;
    if (top) *top = min_y;
    if (bottom) *bottom = max_y;
}

static void map_screen_position(const svrt_ui *ui, int raw_x, int raw_y,
                                int width, int height, float *scene_x,
                                float *scene_y) {
    (void)ui;
    float x = (float)raw_x;
    float y = (float)raw_y;
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
    const float scene_width = width * 0.5f * SVRT_DEBUG_LEFT_EYE_UI_SCALE;
    const float scene_height = height * SVRT_DEBUG_LEFT_EYE_UI_SCALE;
    x = (x - (width - scene_width) * 0.5f) /
        SVRT_DEBUG_LEFT_EYE_UI_SCALE;
    y = (y - (height - scene_height) * 0.5f) /
        SVRT_DEBUG_LEFT_EYE_UI_SCALE;
#endif
    if (scene_x) *scene_x = x;
    if (scene_y) *scene_y = y;
}

static float panel_projected_x(int eye_x, int eye_width, int height, int eye,
                               float u) {
    const float focal = scene_focal_length(height);
    const float eye_offset = eye ? SVRT_EYE_SEPARATION_METERS * 0.5f :
                                   -SVRT_EYE_SEPARATION_METERS * 0.5f;
    const float radius = 3.5f;
    const float distance = 2.15f;
    const float arc = (SVRT_UI_MINIMAL_STEAMOS ? SVRT_UI_PANEL_ARC_DEGREES : 44.0f) *
                      SVRT_PI / 180.0f;
    const float angle = (u - 0.5f) * arc;
    const float world_x = radius * sinf(angle);
    const float z = distance - radius * (1.0f - cosf(angle));
    return eye_x + eye_width * SVRT_UI_PANEL_CENTER_X_RATIO +
           SVRT_UI_HORIZONTAL_ASPECT * focal *
           (world_x - eye_offset) / z;
}

static void panel_projected_vertical(int eye_x, int eye_width, int height,
                                     int eye, float u, float *top,
                                     float *bottom) {
    (void)eye_x;
    (void)eye_width;
    const float focal = scene_focal_length(height);
    const float radius = 3.5f;
    const float distance = 2.15f;
    const float arc = (SVRT_UI_MINIMAL_STEAMOS ? SVRT_UI_PANEL_ARC_DEGREES : 44.0f) *
                      SVRT_PI / 180.0f;
    const float panel_height = SVRT_UI_MINIMAL_STEAMOS ?
                                   SVRT_UI_PANEL_HEIGHT_METERS : 1.50f;
    const float angle = (u - 0.5f) * arc;
    const float z = distance - radius * (1.0f - cosf(angle));
    const float half_height = focal * panel_height * 0.5f / z;
    if (top) *top = height * SVRT_UI_PANEL_CENTER_Y_RATIO - half_height;
    if (bottom) *bottom = height * SVRT_UI_PANEL_CENTER_Y_RATIO + half_height;
    (void)eye;
}

static void map_surface_position(float scene_x, float scene_y, int width,
                                 int height, int *on_surface,
                                 int *surface_x, int *surface_y) {
    const int eye_width = width / 2;
    int eye = scene_x >= eye_width;
    int eye_x = eye ? eye_width : 0;
    if (scene_x < 0.0f || scene_x >= (float)width) eye = 0;
    eye_x = eye ? eye_width : 0;
    const int current_eye_width = eye ? width - eye_width : eye_width;
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    panel_screen_bounds(eye_x, current_eye_width, height, eye, &left, &right,
                        &top, &bottom);
    const int inside = scene_x >= left && scene_x <= right &&
                       scene_y >= top && scene_y <= bottom;
    if (on_surface) *on_surface = inside;
    if (!inside) return;

    /* The panel is projected from a cylinder, so its screen x coordinate is
       not linear in the source texture. Invert the same projection used by
       draw_curved_panel_eye() before forwarding the pointer to Steam. This
       keeps the laser tip and Valve's actual hit target aligned near the
       curved edges instead of drifting horizontally. */
    float low = 0.0f, high = 1.0f;
    const float left_x = panel_projected_x(eye_x, current_eye_width, height,
                                           eye, low);
    const float right_x = panel_projected_x(eye_x, current_eye_width, height,
                                            eye, high);
    if (right_x > left_x) {
        for (int iteration = 0; iteration < 24; ++iteration) {
            const float middle = (low + high) * 0.5f;
            if (panel_projected_x(eye_x, current_eye_width, height, eye,
                                  middle) < scene_x)
                low = middle;
            else
                high = middle;
        }
    } else {
        for (int iteration = 0; iteration < 24; ++iteration) {
            const float middle = (low + high) * 0.5f;
            if (panel_projected_x(eye_x, current_eye_width, height, eye,
                                  middle) > scene_x)
                low = middle;
            else
                high = middle;
        }
    }
    const float u = (low + high) * 0.5f;
    float projected_top = 0.0f, projected_bottom = 0.0f;
    panel_projected_vertical(eye_x, current_eye_width, height, eye, u,
                             &projected_top, &projected_bottom);
    if (surface_x)
        *surface_x = (int)lroundf(u * (SVRT_PANEL_WIDTH - 1));
    if (surface_y && projected_bottom > projected_top) {
        float v = (scene_y - projected_top) /
                  (projected_bottom - projected_top);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        *surface_y = (int)lroundf(v * (SVRT_PANEL_HEIGHT - 1));
    }
}

static void dashboard_make_layout(const svrt_ui *ui, int eye_x, int eye_width,
                                  int height, svrt_dashboard_layout *layout) {
    (void)ui;
    svrt_dashboard_layout_make(
        eye_x, eye_width, height, SVRT_UI_DASHBOARD_RAIL_WIDTH_RATIO,
        SVRT_UI_DASHBOARD_RAIL_HEIGHT_RATIO,
        SVRT_UI_DASHBOARD_DOCK_Y_RATIO,
        SVRT_UI_DASHBOARD_DOCK_WIDTH_RATIO, layout);
}

static void dashboard_refresh_hover(svrt_ui *ui, int eye_width, int height) {
    (void)eye_width;
    (void)height;
    /* The Steam Frame rail stays compact. Hover changes the selected tile;
       it does not turn the rail into a labelled desktop sidebar. */
    if (ui) ui->rail_expanded = 0;
}

static int dashboard_hit_at(const svrt_ui *ui, float scene_x, float scene_y,
                            int width, int height) {
    if (!ui || ui->state != SVRT_UI_HOME || width <= 0 || height <= 0)
        return SVRT_DASHBOARD_HIT_NONE;
    const int eye_width = width / 2;
    const int eye_x = scene_x >= eye_width ? eye_width : 0;
    const int local_x = (int)lroundf(scene_x - eye_x);
    const int local_y = (int)lroundf(scene_y);
    svrt_dashboard_layout layout;
    dashboard_make_layout(ui, 0, eye_width, height, &layout);
    if (ui->dashboard_popover != SVRT_DASHBOARD_POPOVER_NONE) {
        const SDL_Rect panel = dashboard_popover_rect(
            &layout, ui->dashboard_popover);
        if (local_x >= panel.x && local_x < panel.x + panel.w &&
            local_y >= panel.y && local_y < panel.y + panel.h)
            return SVRT_DASHBOARD_HIT_POPOVER;
    }
    return svrt_dashboard_layout_hit(&layout, local_x, local_y);
}

static svrt_ui_action navigation_action_at(const svrt_ui *ui,
                                           float scene_x, float scene_y,
                                           int width, int height) {
    if (!ui || ui->state != SVRT_UI_HOME || width <= 0 || height <= 0)
        return SVRT_UI_ACTION_NONE;
    const int hit = dashboard_hit_at(ui, scene_x, scene_y, width, height);
    if (hit >= 0 && hit < SVRT_DASHBOARD_NAV_ITEMS)
        return (svrt_ui_action)(SVRT_UI_ACTION_HOME + hit);
    if (hit == SVRT_DASHBOARD_HIT_STEAM)
        return SVRT_UI_ACTION_HOME;
    if (hit == SVRT_DASHBOARD_HIT_FRAME)
        return SVRT_UI_ACTION_CONNECTION;
    if (hit == SVRT_DASHBOARD_HIT_AVATAR)
        return SVRT_UI_ACTION_PROFILE;
    if (hit == SVRT_DASHBOARD_HIT_POWER)
        return SVRT_UI_ACTION_POWER;
    if (hit == SVRT_DASHBOARD_HIT_ADD)
        return SVRT_UI_ACTION_ADD;
    if (hit == SVRT_DASHBOARD_HIT_WIFI)
        return SVRT_UI_ACTION_NETWORK;
    return SVRT_UI_ACTION_NONE;
}

static int handle_local_dashboard_click(svrt_ui *ui, float scene_x,
                                         float scene_y, int width, int height) {
    if (!ui) return 0;
    if (ui->dashboard_popover != SVRT_DASHBOARD_POPOVER_NONE) {
        const int eye_width = width > 0 ? width / 2 : 0;
        svrt_dashboard_layout layout;
        dashboard_make_layout(ui, 0, eye_width, height, &layout);
        const SDL_Rect panel = dashboard_popover_rect(
            &layout, ui->dashboard_popover);
        const int eye_x = scene_x >= eye_width ? eye_width : 0;
        const int x = (int)lroundf(scene_x - eye_x);
        const int y = (int)lroundf(scene_y);
        const int inside = x >= panel.x && x < panel.x + panel.w &&
                           y >= panel.y && y < panel.y + panel.h;
        if (!inside) {
            ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_NONE;
            return 1;
        }
        const int footer_h = panel.h / 9;
        if (ui->dashboard_popover != SVRT_DASHBOARD_POPOVER_POWER &&
            y >= panel.y + panel.h - footer_h) {
            const int tab = (x - panel.x) * 5 / panel.w;
            if (tab == 2)
                ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_PERFORMANCE;
            else if (tab == 3)
                ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_BATTERY;
            return 1;
        }
        if (ui->dashboard_popover == SVRT_DASHBOARD_POPOVER_POWER) {
            const int row_top = panel.y + 54;
            const int row_height = (panel.h - 66) / 4;
            if (y >= row_top) {
                const int row = (y - row_top) / row_height;
                ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_NONE;
                if (row == 0) ui->pending_action = SVRT_UI_ACTION_SUSPEND;
                else if (row == 1) ui->pending_action = SVRT_UI_ACTION_RESTART;
                else if (row == 2) ui->pending_action = SVRT_UI_ACTION_SHUTDOWN;
                return 1;
            }
            return 1;
        }
        if (ui->dashboard_popover == SVRT_DASHBOARD_POPOVER_PERFORMANCE) {
            const SDL_Rect basic = {panel.x + 20, panel.y + 96,
                                    panel.w - 40, panel.h / 9};
            if (x >= basic.x && x < basic.x + basic.w &&
                y >= basic.y && y < basic.y + basic.h) {
                ui->performance_basic_view = !ui->performance_basic_view;
                return 1;
            }
            const SDL_Rect toggle = {panel.x + panel.w - 78,
                                     panel.y + 211, 54, 29};
            if (x >= toggle.x && x < toggle.x + toggle.w &&
                y >= toggle.y && y < toggle.y + toggle.h) {
                ui->manual_gpu_clock = !ui->manual_gpu_clock;
                return 1;
            }
            const int slider_left = panel.x + 24;
            const int slider_right = panel.x + panel.w - 24;
            if (y >= panel.y + 284 && y <= panel.y + 320 &&
                x >= slider_left && x <= slider_right) {
                const int span = slider_right - slider_left;
                ui->performance_gpu_mhz = 500 +
                    (span > 0 ? (x - slider_left) * 403 / span : 0);
                ui->manual_gpu_clock = 1;
                return 1;
            }
        }
        return 1;
    }
    const int hit = dashboard_hit_at(ui, scene_x, scene_y, width, height);
    if (hit == SVRT_DASHBOARD_HIT_NONE) {
        if (ui->dashboard_popover != SVRT_DASHBOARD_POPOVER_NONE) {
            ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_NONE;
            return 1;
        }
        return 0;
    }
    if (hit == SVRT_DASHBOARD_HIT_FRAME) {
        ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_PERFORMANCE;
        return 1;
    }
    if (hit == SVRT_DASHBOARD_HIT_BATTERY) {
        ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_BATTERY;
        return 1;
    }
    if (hit == SVRT_DASHBOARD_HIT_VIEW) {
        ui->view_enabled = !ui->view_enabled;
        return 1;
    }
    if (hit == SVRT_DASHBOARD_HIT_POWER) {
        ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_POWER;
        return 1;
    }

    const svrt_ui_action action = navigation_action_at(
        ui, scene_x, scene_y, width, height);
    if (action == SVRT_UI_ACTION_NONE) return 1;
    ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_NONE;
    ui->pending_action = action;
    if (action >= SVRT_UI_ACTION_HOME && action < SVRT_UI_ACTION_POWER)
        ui->selected_page = action - SVRT_UI_ACTION_HOME;
    if (action == SVRT_UI_ACTION_CONNECTION)
        ui->connection_requested = 1;
    return 1;
}

static void notify_input(svrt_ui *ui, svrt_ui_input_type type, int screen_x,
                          int screen_y, int delta_x, int delta_y, int button,
                          int wheel_y, int keycode, int scancode,
                          int modifiers) {
    if (!ui) return;
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
    if (width <= 0 || height <= 0) return;
    float scene_x = 0.0f, scene_y = 0.0f;
    map_screen_position(ui, screen_x, screen_y, width, height, &scene_x,
                        &scene_y);
    const int eye_width = width / 2;
    const int eye_x = scene_x >= eye_width ? eye_width : 0;
    ui->cursor_x = (int)lroundf(scene_x - eye_x);
    ui->cursor_y = (int)lroundf(scene_y);
    ui->cursor_visible = 1;
    dashboard_refresh_hover(ui, eye_width, height);
    if (type == SVRT_UI_INPUT_KEY_DOWN &&
        (keycode == SDLK_ESCAPE || scancode == SDL_SCANCODE_ESCAPE) &&
        ui->dashboard_popover != SVRT_DASHBOARD_POPOVER_NONE) {
        ui->dashboard_popover = SVRT_DASHBOARD_POPOVER_NONE;
        return;
    }
#if SVRT_UI_MINIMAL_STEAMOS && SVRT_UI_SHOW_NAV_CHROME
    /* Valve buttons arm on press and activate only when released over the
       same control. Capture the complete press/release pair locally. */
    if (type == SVRT_UI_INPUT_MOUSE_BUTTON_DOWN) {
        const int hit = dashboard_hit_at(ui, scene_x, scene_y, width, height);
        if (hit != SVRT_DASHBOARD_HIT_NONE ||
            ui->dashboard_popover != SVRT_DASHBOARD_POPOVER_NONE) {
            ui->dashboard_pressed_hit = hit != SVRT_DASHBOARD_HIT_NONE ?
                                            hit : SVRT_DASHBOARD_HIT_POPOVER;
            return;
        }
    } else if (type == SVRT_UI_INPUT_MOUSE_BUTTON_UP &&
               ui->dashboard_pressed_hit != SVRT_DASHBOARD_HIT_NONE) {
        const int armed = ui->dashboard_pressed_hit;
        const int hit = dashboard_hit_at(ui, scene_x, scene_y, width, height);
        ui->dashboard_pressed_hit = SVRT_DASHBOARD_HIT_NONE;
        if (ui->dashboard_popover != SVRT_DASHBOARD_POPOVER_NONE ||
            hit == armed)
            handle_local_dashboard_click(ui, scene_x, scene_y, width, height);
        return;
    }
#endif
#if SVRT_UI_SHOW_NAV_CHROME
    /* The standalone shell owns the fallback VRDashboardBar layer because
       the captured Valve -steamframe page window does not contain that
       separate compositor overlay.  Keep chrome events out of the page
       surface so laser hover and release target the same layer that is drawn. */
    const int chrome_hit = dashboard_hit_at(ui, scene_x, scene_y,
                                            width, height);
    if (chrome_hit != SVRT_DASHBOARD_HIT_NONE) {
        if (type == SVRT_UI_INPUT_MOUSE_MOTION ||
            type == SVRT_UI_INPUT_MOUSE_WHEEL ||
            type == SVRT_UI_INPUT_MOUSE_BUTTON_UP)
            return;
    }
#endif
    if (!ui->input_callback) return;
    svrt_ui_input input = {
        .type = type,
        .screen_x = screen_x,
        .screen_y = screen_y,
        .screen_width = width,
        .screen_height = height,
        .delta_x = delta_x,
        .delta_y = delta_y,
        .button = button,
        .wheel_y = wheel_y,
        .keycode = keycode,
        .scancode = scancode,
        .modifiers = modifiers,
        .surface_width = SVRT_PANEL_WIDTH,
        .surface_height = SVRT_PANEL_HEIGHT};
    map_surface_position(scene_x, scene_y, width, height,
                         &input.on_surface, &input.surface_x,
                         &input.surface_y);
    if (type == SVRT_UI_INPUT_KEY_DOWN || type == SVRT_UI_INPUT_KEY_UP)
        input.on_surface = 1;
    ui->input_callback(&input, ui->input_callback_opaque);
}

static void disable_local_input(void) {
    static const Uint32 events[] = {
        SDL_TEXTEDITING, SDL_TEXTINPUT,
        SDL_KEYMAPCHANGED};
    SDL_ShowCursor(SDL_DISABLE);
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
        SDL_EventState(events[i], SDL_IGNORE);
    SDL_EventState(SDL_KEYDOWN, SDL_ENABLE);
    SDL_EventState(SDL_KEYUP, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
    SDL_EventState(SDL_MOUSEWHEEL, SDL_ENABLE);
}

static void open_game_controller(svrt_ui *ui, int device_index) {
    if (!ui || ui->game_controller || device_index < 0 ||
        !SDL_IsGameController(device_index))
        return;
    ui->game_controller = SDL_GameControllerOpen(device_index);
    if (ui->game_controller) {
        SDL_Joystick *joystick = SDL_GameControllerGetJoystick(
            ui->game_controller);
        ui->gamepad_instance_id = joystick ? SDL_JoystickInstanceID(joystick) : -1;
        ui->gamepad_left_x = SDL_GameControllerGetAxis(
            ui->game_controller, SDL_CONTROLLER_AXIS_LEFTX);
        ui->gamepad_left_y = SDL_GameControllerGetAxis(
            ui->game_controller, SDL_CONTROLLER_AXIS_LEFTY);
        ui->gamepad_right_y = SDL_GameControllerGetAxis(
            ui->game_controller, SDL_CONTROLLER_AXIS_RIGHTY);
        fprintf(stderr, "SVRT UI: game controller=%s\n",
                SDL_GameControllerName(ui->game_controller) ?
                    SDL_GameControllerName(ui->game_controller) : "unknown");
        return;
    }
    fprintf(stderr, "SVRT UI: game controller open failed: %s\n",
            SDL_GetError());
}

static int gamepad_axis_step(int value) {
    const int deadzone = 8000;
    const int magnitude = value < 0 ? -value : value;
    if (magnitude <= deadzone) return 0;
    const int span = 32767 - deadzone;
    int step = 1 + (magnitude - deadzone) * 7 / span;
    if (step > 8) step = 8;
    return value < 0 ? -step : step;
}

static void move_gamepad_cursor(svrt_ui *ui, int delta_x, int delta_y) {
    if (!ui || !ui->renderer || (!delta_x && !delta_y)) return;
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
    const int eye_width = width / 2;
    if (eye_width <= 0 || height <= 0) return;
    if (!ui->cursor_visible) {
        ui->cursor_x = eye_width / 2;
        ui->cursor_y = height / 2;
        ui->cursor_visible = 1;
    }
    ui->cursor_x += delta_x;
    ui->cursor_y += delta_y;
    if (ui->cursor_x < 0) ui->cursor_x = 0;
    if (ui->cursor_x >= eye_width) ui->cursor_x = eye_width - 1;
    if (ui->cursor_y < 0) ui->cursor_y = 0;
    if (ui->cursor_y >= height) ui->cursor_y = height - 1;
    /* Use the left eye's local coordinates as the canonical pointer.  The
       renderer draws the same laser target in both eyes and the input mapper
       applies the inverse cylinder projection before forwarding to Steam. */
    notify_input(ui, SVRT_UI_INPUT_MOUSE_MOTION, ui->cursor_x, ui->cursor_y,
                 delta_x, delta_y, 0, 0, 0, 0, 0);
}

static void send_gamepad_button(svrt_ui *ui, SDL_GameControllerButton button,
                                int pressed) {
    if (!ui || !ui->renderer) return;
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
    const int eye_width = width / 2;
    if (eye_width <= 0 || height <= 0) return;
    if (!ui->cursor_visible) {
        ui->cursor_x = eye_width / 2;
        ui->cursor_y = height / 2;
        ui->cursor_visible = 1;
    }
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
            /* A is the Steam Frame select action.  The existing input bridge
               handles this exactly like a left-click, including local nav
               chrome hit testing before the event reaches Valve's client. */
            notify_input(ui,
                         pressed ? SVRT_UI_INPUT_MOUSE_BUTTON_DOWN :
                                   SVRT_UI_INPUT_MOUSE_BUTTON_UP,
                         ui->cursor_x, ui->cursor_y, 0, 0, 1, 0,
                         0, 0, 0);
            break;
        case SDL_CONTROLLER_BUTTON_B:
            /* B is the Steam Frame back action.  Escape is also the native
               Gamepad UI back key and keeps the whole page stack in Steam. */
            notify_input(ui,
                         pressed ? SVRT_UI_INPUT_KEY_DOWN :
                                   SVRT_UI_INPUT_KEY_UP,
                         ui->cursor_x, ui->cursor_y, 0, 0, 0, 0,
                         SDLK_ESCAPE, 0, 0);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            if (pressed) move_gamepad_cursor(ui, 0, -48);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            if (pressed) move_gamepad_cursor(ui, 0, 48);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (pressed) move_gamepad_cursor(ui, -48, 0);
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (pressed) move_gamepad_cursor(ui, 48, 0);
            break;
        default:
            break;
    }
}

static void update_gamepad_axes(svrt_ui *ui) {
    if (!ui || !ui->game_controller || !ui->renderer) return;
    const uint32_t now_ms = SDL_GetTicks();
    if (now_ms >= ui->next_gamepad_cursor_ms) {
        const int delta_x = gamepad_axis_step(ui->gamepad_left_x);
        const int delta_y = gamepad_axis_step(ui->gamepad_left_y);
        if (delta_x || delta_y) {
            move_gamepad_cursor(ui, delta_x, delta_y);
            ui->next_gamepad_cursor_ms = now_ms + 16;
        }
    }
    if (now_ms >= ui->next_gamepad_wheel_ms) {
        const int step = gamepad_axis_step(ui->gamepad_right_y);
        if (step) {
            if (!ui->cursor_visible) {
                int width = 0, height = 0;
                SDL_GetRendererOutputSize(ui->renderer, &width, &height);
                ui->cursor_x = width / 4;
                ui->cursor_y = height / 2;
                ui->cursor_visible = 1;
            }
            notify_input(ui, SVRT_UI_INPUT_MOUSE_WHEEL,
                         ui->cursor_x, ui->cursor_y, 0, 0, 0,
                         step < 0 ? 1 : -1, 0, 0, 0);
            ui->next_gamepad_wheel_ms = now_ms + 80;
        }
    }
}

static void poll_ui_actions(svrt_ui *ui) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_F9)
                ui->connection_requested = 1;
            else
                notify_input(ui, SVRT_UI_INPUT_KEY_DOWN, ui->cursor_x,
                             ui->cursor_y, 0, 0, 0, 0,
                             event.key.keysym.sym, event.key.keysym.scancode,
                             event.key.keysym.mod);
        } else if (event.type == SDL_KEYUP) {
            notify_input(ui, SVRT_UI_INPUT_KEY_UP, ui->cursor_x,
                         ui->cursor_y, 0, 0, 0, 0,
                         event.key.keysym.sym, event.key.keysym.scancode,
                         event.key.keysym.mod);
        }
        if (event.type == SDL_MOUSEMOTION) {
            notify_input(ui, SVRT_UI_INPUT_MOUSE_MOTION, event.motion.x,
                         event.motion.y, event.motion.xrel, event.motion.yrel,
                         0, 0, 0, 0, 0);
        } else if (event.type == SDL_MOUSEBUTTONDOWN ||
                   event.type == SDL_MOUSEBUTTONUP) {
            notify_input(ui,
                         event.type == SDL_MOUSEBUTTONDOWN ?
                             SVRT_UI_INPUT_MOUSE_BUTTON_DOWN :
                             SVRT_UI_INPUT_MOUSE_BUTTON_UP,
                          event.button.x, event.button.y, 0, 0,
                          event.button.button, 0, 0, 0, 0);
        } else if (event.type == SDL_MOUSEWHEEL) {
            int mouse_x = 0, mouse_y = 0;
            SDL_GetMouseState(&mouse_x, &mouse_y);
            notify_input(ui, SVRT_UI_INPUT_MOUSE_WHEEL, mouse_x, mouse_y,
                         event.wheel.x, event.wheel.y, 0, event.wheel.y,
                         0, 0, 0);
        } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
            open_game_controller(ui, event.cdevice.which);
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            if (ui->game_controller &&
                ui->gamepad_instance_id == event.cdevice.which) {
                SDL_GameControllerClose(ui->game_controller);
                ui->game_controller = NULL;
                ui->gamepad_instance_id = -1;
                ui->gamepad_left_x = 0;
                ui->gamepad_left_y = 0;
                ui->gamepad_right_y = 0;
                fprintf(stderr, "SVRT UI: game controller removed\n");
            }
        } else if (event.type == SDL_CONTROLLERAXISMOTION) {
            if (ui->game_controller &&
                event.caxis.which == ui->gamepad_instance_id &&
                event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                ui->gamepad_left_x = event.caxis.value;
            } else if (ui->game_controller &&
                       event.caxis.which == ui->gamepad_instance_id &&
                       event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                ui->gamepad_left_y = event.caxis.value;
            } else if (ui->game_controller &&
                       event.caxis.which == ui->gamepad_instance_id &&
                       event.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                ui->gamepad_right_y = event.caxis.value;
            }
        } else if (event.type == SDL_CONTROLLERBUTTONDOWN ||
                   event.type == SDL_CONTROLLERBUTTONUP) {
            if (ui->game_controller &&
                event.cbutton.which == ui->gamepad_instance_id)
                send_gamepad_button(ui, event.cbutton.button,
                                    event.type == SDL_CONTROLLERBUTTONDOWN);
        }
#if !SVRT_UI_MINIMAL_STEAMOS
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            int width = 0, height = 0;
            SDL_GetRendererOutputSize(ui->renderer, &width, &height);
            float scene_x = 0.0f, scene_y = 0.0f;
            map_screen_position(ui, event.button.x, event.button.y, width,
                                height, &scene_x, &scene_y);
            const svrt_ui_action action = navigation_action_at(
                ui, scene_x, scene_y, width, height);
            if (action != SVRT_UI_ACTION_NONE) {
                ui->pending_action = action;
                if (action >= SVRT_UI_ACTION_HOME &&
                    action < SVRT_UI_ACTION_POWER)
                    ui->selected_page = action - SVRT_UI_ACTION_HOME;
                if (action == SVRT_UI_ACTION_CONNECTION)
                    ui->connection_requested = 1;
            }
        }
#endif
    }
    update_gamepad_axes(ui);
}

static int read_integer_file(const char *path, int *value) {
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    const int valid = fscanf(file, "%d", value) == 1;
    fclose(file);
    return valid;
}

static int wifi_connected(void) {
    static uint32_t next_check_ms;
    static int cached;
    const uint32_t now_ms = SDL_GetTicks();
    if (now_ms < next_check_ms) return cached;
    next_check_ms = now_ms + 2000;
    FILE *file = fopen("/sys/class/net/wlan0/operstate", "r");
    if (!file) return cached = 0;
    char state[16] = {0};
    const int connected = fgets(state, sizeof(state), file) &&
                          !strncmp(state, "up", 2);
    fclose(file);
    return cached = connected;
}

static int system_battery_percent(void) {
    static uint32_t next_check_ms;
    static int cached = -1;
    const uint32_t now_ms = SDL_GetTicks();
    if (now_ms < next_check_ms) return cached;
    next_check_ms = now_ms + 5000;
    glob_t matches = {0};
    int percent = -1;
    if (!glob("/sys/class/power_supply/*/capacity", 0, NULL, &matches)) {
        for (size_t i = 0; i < matches.gl_pathc; ++i) {
            if (read_integer_file(matches.gl_pathv[i], &percent)) break;
        }
        globfree(&matches);
    }
    cached = percent >= 0 && percent <= 100 ? percent : -1;
    return cached;
}

static const char *steam_home_for_ui(void) {
    const char *configured = getenv("SVRT_STEAM_HOME");
    return configured && configured[0] ? configured : "/var/lib/svrt-receiver";
}

static void update_steam_avatar(svrt_ui *ui) {
    if (!ui || ui->avatar) return;
    const uint32_t now_ms = SDL_GetTicks();
    if (now_ms < ui->next_avatar_scan_ms) return;
    ui->next_avatar_scan_ms = now_ms + 5000;
    glob_t matches = {0};
    char pattern[640];
    snprintf(pattern, sizeof(pattern),
             "%s/.local/share/Steam/config/avatarcache/*",
             steam_home_for_ui());
    if (glob(pattern, 0, NULL, &matches))
        return;
    for (size_t i = 0; i < matches.gl_pathc && !ui->avatar; ++i) {
        snprintf(ui->avatar_path, sizeof(ui->avatar_path), "%s",
                 matches.gl_pathv[i]);
        ui->avatar = video_open(ui->avatar_path);
    }
    globfree(&matches);
}

static void video_close(svrt_ui_video **video) {
    if (!video || !*video) return;
    svrt_ui_video *v = *video;
    if (v->texture) SDL_DestroyTexture(v->texture);
    av_packet_free(&v->packet);
    av_frame_free(&v->frame);
    avcodec_free_context(&v->codec);
    av_buffer_unref(&v->hw_device);
    avformat_close_input(&v->format);
    free(v);
    *video = NULL;
}

static enum AVPixelFormat video_choose_format(AVCodecContext *unused,
                                               const enum AVPixelFormat *formats) {
    (void)unused;
    for (const enum AVPixelFormat *format = formats;
         *format != AV_PIX_FMT_NONE; ++format)
        if (*format == AV_PIX_FMT_DRM_PRIME) return *format;
    return formats[0];
}

static svrt_ui_video *video_open(const char *path) {
    svrt_ui_video *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->path = path;
    if (avformat_open_input(&v->format, path, NULL, NULL) < 0 ||
        avformat_find_stream_info(v->format, NULL) < 0) {
        video_close(&v); return NULL;
    }
    v->stream = av_find_best_stream(v->format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                    NULL, 0);
    if (v->stream < 0) { video_close(&v); return NULL; }
    const AVStream *stream = v->format->streams[v->stream];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) { video_close(&v); return NULL; }
    v->codec = avcodec_alloc_context3(decoder);
    if (!v->codec || avcodec_parameters_to_context(v->codec, stream->codecpar) < 0) {
        video_close(&v); return NULL;
    }
    /* The Pi's ARM cores cannot decode these high-resolution HEVC UI movies
       at real time.  Use the same DRM PRIME HEVC path as the live receiver;
       frames are transferred to ordinary YUV only for the SDL texture upload. */
    if (av_hwdevice_ctx_create(&v->hw_device, AV_HWDEVICE_TYPE_DRM,
                               NULL, NULL, 0) >= 0) {
        v->codec->hw_device_ctx = av_buffer_ref(v->hw_device);
        v->codec->get_format = video_choose_format;
        v->codec->extra_hw_frames = 4;
    }
    if (avcodec_open2(v->codec, decoder, NULL) < 0) {
        video_close(&v); return NULL;
    }
    v->packet = av_packet_alloc(); v->frame = av_frame_alloc();
    if (!v->packet || !v->frame) { video_close(&v); return NULL; }
    v->time_base = stream->time_base;
    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        int64_t frame_duration = av_rescale_q(
            1, av_inv_q(stream->avg_frame_rate), (AVRational){1, 1000});
        if (frame_duration > 0) v->frame_duration_ms = (uint32_t)frame_duration;
    }
    if (!v->frame_duration_ms) v->frame_duration_ms = 33;
    if (stream->duration > 0) {
        int64_t duration = av_rescale_q(stream->duration, stream->time_base,
                                        (AVRational){1, 1000});
        if (duration > 0) v->duration_ms = (uint32_t)duration;
    }
    if (!v->duration_ms && v->format->duration > 0)
        v->duration_ms = (uint32_t)av_rescale_q(v->format->duration,
                                                AV_TIME_BASE_Q,
                                                (AVRational){1, 1000});
    return v;
}

static void video_rewind(svrt_ui_video *v) {
    if (!v || !v->format) return;
    av_seek_frame(v->format, v->stream, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(v->codec);
    v->have_frame = 0; v->frame_ms = 0; v->ended = 0;
    v->playback_started = 0;
}

static int video_upload_frame(svrt_ui_video *v, const AVFrame *frame,
                              SDL_Renderer *renderer) {
    if (!v || !frame || !renderer) return -1;
    Uint32 format = SDL_PIXELFORMAT_UNKNOWN;
    int packed = 0;
    switch (frame->format) {
        case AV_PIX_FMT_NV12: format = SDL_PIXELFORMAT_NV12; break;
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P: format = SDL_PIXELFORMAT_IYUV; break;
        case AV_PIX_FMT_RGB24:
            format = SDL_PIXELFORMAT_RGB24; packed = 1; break;
        case AV_PIX_FMT_RGBA:
            format = SDL_PIXELFORMAT_RGBA32; packed = 1; break;
        case AV_PIX_FMT_BGRA:
            format = SDL_PIXELFORMAT_BGRA32; packed = 1; break;
        default: return -1;
    }
    int width = frame->width, height = frame->height;
    if (v->texture) {
        Uint32 old_format; int old_width, old_height;
        SDL_QueryTexture(v->texture, &old_format, NULL, &old_width, &old_height);
        if (old_format != format || old_width != width || old_height != height) {
            SDL_DestroyTexture(v->texture); v->texture = NULL;
        }
    }
    if (!v->texture)
        v->texture = SDL_CreateTexture(renderer, format,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       width, height);
    if (!v->texture) return -1;
    SDL_SetTextureBlendMode(v->texture, SDL_BLENDMODE_BLEND);
    if (packed)
        return SDL_UpdateTexture(v->texture, NULL, frame->data[0],
                                 frame->linesize[0]);
    if (format == SDL_PIXELFORMAT_NV12)
        return SDL_UpdateNVTexture(v->texture, NULL, frame->data[0],
                                   frame->linesize[0], frame->data[1],
                                   frame->linesize[1]);
    return SDL_UpdateYUVTexture(v->texture, NULL, frame->data[0],
                                frame->linesize[0], frame->data[1],
                                frame->linesize[1], frame->data[2],
                                frame->linesize[2]);
}

static int video_decode_next(svrt_ui_video *v, SDL_Renderer *renderer) {
    for (;;) {
        int rc = avcodec_receive_frame(v->codec, v->frame);
        if (rc == 0) {
            int64_t pts = v->frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = 0;
            v->frame_ms = av_rescale_q(pts, v->time_base, (AVRational){1, 1000});
            AVFrame *upload = v->frame;
            AVFrame *transferred = NULL;
            if (v->frame->format == AV_PIX_FMT_DRM_PRIME) {
                transferred = av_frame_alloc();
                if (!transferred || av_hwframe_transfer_data(transferred,
                                                              v->frame, 0) < 0) {
                    av_frame_free(&transferred);
                    v->have_frame = 0;
                    av_frame_unref(v->frame);
                    return -1;
                }
                upload = transferred;
            }
            v->have_frame = video_upload_frame(v, upload, renderer) == 0;
            av_frame_free(&transferred);
            av_frame_unref(v->frame);
            return v->have_frame ? 0 : -1;
        }
        if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) return -1;
        if (rc == AVERROR_EOF) return 1;
        rc = av_read_frame(v->format, v->packet);
        if (rc < 0) {
            avcodec_send_packet(v->codec, NULL);
            continue;
        }
        if (v->packet->stream_index == v->stream)
            avcodec_send_packet(v->codec, v->packet);
        av_packet_unref(v->packet);
    }
}

static void video_draw(svrt_ui_video *v, SDL_Renderer *renderer,
                       const SDL_Rect *destination, uint32_t elapsed_ms,
                       int loop) {
    if (!v || !renderer || !destination) return;
    (void)elapsed_ms;
    uint32_t playback_ms = v->playback_started ?
        SDL_GetTicks() - v->playback_start_ms : 0;
    if (loop && v->duration_ms && playback_ms >= v->duration_ms) {
        ++v->loops_completed;
        video_rewind(v);
        playback_ms = 0;
    }
    if (!v->have_frame) {
        int rc = video_decode_next(v, renderer);
        if (rc == 1 && loop) {
            ++v->loops_completed;
            video_rewind(v);
            if (video_decode_next(v, renderer) == 0) {
                v->playback_start_ms = SDL_GetTicks();
                v->playback_started = 1;
            }
        } else if (rc == 1) {
            v->ended = 1;
        } else if (rc < 0 && !loop) {
            fprintf(stderr, "SVRT UI: video decode failed for %s; skipping it\n",
                    v->path ? v->path : "unknown asset");
            v->ended = 1;
        } else if (rc == 0 && !v->playback_started) {
            /* Do not count decoder/device initialization time against the
               animation.  Otherwise the first draw can try to catch up
               thousands of milliseconds of stale frames synchronously. */
            v->playback_start_ms = SDL_GetTicks();
            v->playback_started = 1;
            playback_ms = 0;
        }
    }
    int catchup = 0;
    while (v->have_frame &&
           (uint32_t)v->frame_ms + 2 < playback_ms) {
        if (++catchup >= 8) {
            /* If decoding falls behind, show the newest frame available and
               resynchronize the playback clock instead of blocking the UI
               while it decodes an unbounded backlog. */
            v->playback_start_ms = SDL_GetTicks() - (uint32_t)v->frame_ms;
            break;
        }
        int rc = video_decode_next(v, renderer);
        if (rc == 1) {
            if (loop) {
                /* Some Matroska files do not expose a container duration.
                   Derive it when the decoder reaches EOF, then use the
                   elapsed-time modulo so the loop does not rewind to frame 0
                   on every draw after the first cycle. */
                if (!v->duration_ms)
                    v->duration_ms = (uint32_t)v->frame_ms +
                                     v->frame_duration_ms;
                ++v->loops_completed;
                video_rewind(v);
                if (video_decode_next(v, renderer) == 0) {
                    v->playback_start_ms = SDL_GetTicks();
                    v->playback_started = 1;
                }
            } else {
                v->ended = 1;
            }
            break;
        }
        if (rc < 0) break;
    }
    if (v->texture) SDL_RenderCopy(renderer, v->texture, NULL, destination);
}

static void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *value,
                      int center_x, int y, uint8_t alpha) {
    if (!renderer || !font || !value || !value[0]) return;
    SDL_Color color = {255, 255, 255, alpha};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_Rect target = {center_x - surface->w / 2, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, NULL, &target);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static void draw_text_left(SDL_Renderer *renderer, TTF_Font *font,
                           const char *value, int x, int y, uint8_t alpha) {
    if (!renderer || !font || !value || !value[0]) return;
    SDL_Color color = {255, 255, 255, alpha};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_Rect target = {x, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, NULL, &target);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static void draw_text_left_scaled(SDL_Renderer *renderer, TTF_Font *font,
                                  const char *value, int x, int y,
                                  float scale, uint8_t alpha) {
    if (!renderer || !font || !value || !value[0] || scale <= 0.0f) return;
    SDL_Color color = {255, 255, 255, alpha};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_Rect target = {x, y,
                           (int)lroundf(surface->w * scale),
                           (int)lroundf(surface->h * scale)};
        SDL_RenderCopy(renderer, texture, NULL, &target);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static void fill_rounded_rect(SDL_Renderer *renderer, const SDL_Rect *rect,
                              int radius, SDL_Color color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > rect->w / 2) radius = rect->w / 2;
    if (radius > rect->h / 2) radius = rect->h / 2;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect middle = {rect->x + radius, rect->y,
                       rect->w - radius * 2, rect->h};
    SDL_Rect center = {rect->x, rect->y + radius,
                       rect->w, rect->h - radius * 2};
    SDL_RenderFillRect(renderer, &middle);
    SDL_RenderFillRect(renderer, &center);
    for (int y = 0; y < radius; ++y) {
        const float dy = radius - y - 0.5f;
        const int inset = radius - (int)sqrtf(radius * radius - dy * dy);
        SDL_RenderDrawLine(renderer, rect->x + inset, rect->y + y,
                          rect->x + rect->w - inset - 1, rect->y + y);
        SDL_RenderDrawLine(renderer, rect->x + inset,
                          rect->y + rect->h - y - 1,
                          rect->x + rect->w - inset - 1,
                          rect->y + rect->h - y - 1);
    }
}

static void draw_filled_circle(SDL_Renderer *renderer, int center_x,
                               int center_y, int radius, SDL_Color color) {
    if (!renderer || radius <= 0) return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        const int span = (int)sqrtf((float)(radius * radius - y * y));
        SDL_RenderDrawLine(renderer, center_x - span, center_y + y,
                           center_x + span, center_y + y);
    }
}

static void draw_circle_outline(SDL_Renderer *renderer, int center_x,
                                int center_y, int radius, SDL_Color color) {
    if (!renderer || radius <= 0) return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        const int span = (int)sqrtf((float)(radius * radius - y * y));
        SDL_RenderDrawPoint(renderer, center_x - span, center_y + y);
        SDL_RenderDrawPoint(renderer, center_x + span, center_y + y);
    }
}

/* The real Steam surface is still Valve's captured client. These tiny
   vector marks only keep the shell's pointer/navigation chrome crisp on the
   Pi; shipping the full SVG artwork would add an image loader and another
   runtime dependency for these small, fixed dashboard marks. */
static void draw_nav_icon(SDL_Renderer *renderer, int item, int center_x,
                          int center_y, int size, SDL_Color color) {
    if (!renderer || size < 6) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    const int radius = size / 2 - 2;
    switch (item) {
        case 0: { /* Home */
            SDL_Point roof[] = {{center_x - radius, center_y},
                                {center_x, center_y - radius},
                                {center_x + radius, center_y},
                                {center_x - radius, center_y}};
            SDL_RenderDrawLines(renderer, roof, 4);
            SDL_Rect body = {center_x - radius + 3, center_y,
                             radius * 2 - 6, radius};
            SDL_RenderDrawRect(renderer, &body);
            break;
        }
        case 1: { /* Library */
            const int cell = (size - 7) / 2;
            const int gap = 3;
            for (int row = 0; row < 2; ++row)
                for (int column = 0; column < 2; ++column) {
                    SDL_Rect cell_rect = {
                        center_x - cell - gap / 2 + column * (cell + gap),
                        center_y - cell - gap / 2 + row * (cell + gap),
                        cell, cell};
                    SDL_RenderFillRect(renderer, &cell_rect);
                }
            break;
        }
        case 2: { /* Shop/tag */
            SDL_Point tag[] = {{center_x - radius, center_y - radius / 2},
                               {center_x + radius / 3, center_y - radius / 2},
                               {center_x + radius, center_y},
                               {center_x + radius / 3, center_y + radius / 2},
                               {center_x - radius, center_y + radius / 2},
                               {center_x - radius, center_y - radius / 2}};
            SDL_RenderDrawLines(renderer, tag, 6);
            draw_filled_circle(renderer, center_x - radius / 2,
                               center_y - radius / 5, 1, color);
            break;
        }
        case 3: { /* Friends */
            draw_filled_circle(renderer, center_x - radius / 3,
                               center_y - radius / 3, 2, color);
            draw_filled_circle(renderer, center_x + radius / 3,
                               center_y - radius / 3, 2, color);
            SDL_RenderDrawLine(renderer, center_x - radius,
                               center_y + radius / 2,
                               center_x, center_y + radius / 2);
            SDL_RenderDrawLine(renderer, center_x,
                               center_y + radius / 2,
                               center_x + radius, center_y + radius / 2);
            SDL_RenderDrawLine(renderer, center_x - radius + 2,
                               center_y + radius / 2,
                               center_x - radius / 2, center_y + radius);
            SDL_RenderDrawLine(renderer, center_x + radius - 2,
                               center_y + radius / 2,
                               center_x + radius / 2, center_y + radius);
            break;
        }
        case 4: { /* Media/screenshots */
            SDL_Rect image = {center_x - radius, center_y - radius * 2 / 3,
                              radius * 2, radius * 4 / 3};
            SDL_RenderDrawRect(renderer, &image);
            SDL_RenderDrawLine(renderer, image.x + 2, image.y + image.h - 3,
                               center_x - 1, center_y);
            SDL_RenderDrawLine(renderer, center_x - 1, center_y,
                               image.x + image.w - 2, image.y + 3);
            draw_filled_circle(renderer, image.x + image.w - 5,
                               image.y + 5, 1, color);
            break;
        }
        case 5: { /* Downloads */
            SDL_RenderDrawLine(renderer, center_x, center_y - radius,
                               center_x, center_y + radius / 2);
            SDL_RenderDrawLine(renderer, center_x - radius / 2,
                               center_y, center_x,
                               center_y + radius / 2);
            SDL_RenderDrawLine(renderer, center_x + radius / 2,
                               center_y, center_x,
                               center_y + radius / 2);
            SDL_Rect tray = {center_x - radius, center_y + radius / 2,
                             radius * 2, 3};
            SDL_RenderFillRect(renderer, &tray);
            break;
        }
        case 6: { /* Settings */
            draw_circle_outline(renderer, center_x, center_y, radius / 2,
                                color);
            draw_filled_circle(renderer, center_x, center_y, 2,
                               (SDL_Color){22, 27, 35, 255});
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
                                   color.a);
            for (int spoke = 0; spoke < 8; ++spoke) {
                const float angle = spoke * SVRT_PI / 4.0f;
                const int x0 = center_x + (int)lroundf(cosf(angle) * radius);
                const int y0 = center_y + (int)lroundf(sinf(angle) * radius);
                const int x1 = center_x + (int)lroundf(cosf(angle) *
                                                       (radius - 3));
                const int y1 = center_y + (int)lroundf(sinf(angle) *
                                                       (radius - 3));
                SDL_RenderDrawLine(renderer, x0, y0, x1, y1);
            }
            break;
        }
        case 7: { /* Steam Frame / gamepad */
            const SDL_Rect gamepad = {center_x - radius,
                                      center_y - radius / 2,
                                      radius * 2,
                                      radius};
            SDL_RenderDrawRect(renderer, &gamepad);
            draw_filled_circle(renderer, center_x - radius / 2,
                               center_y, 2, color);
            SDL_RenderDrawLine(renderer, center_x - radius / 2 - 4,
                               center_y,
                               center_x - radius / 2 + 4,
                               center_y);
            SDL_RenderDrawLine(renderer, center_x - radius / 2,
                               center_y - 4,
                               center_x - radius / 2,
                               center_y + 4);
            draw_filled_circle(renderer, center_x + radius / 2,
                               center_y - 3, 2, color);
            draw_filled_circle(renderer, center_x + radius / 2 + 5,
                               center_y + 3, 2, color);
            break;
        }
        case 8: { /* Steam mark */
            draw_circle_outline(renderer, center_x, center_y, radius, color);
            draw_filled_circle(renderer, center_x + radius / 3,
                               center_y - radius / 3, 2, color);
            SDL_RenderDrawLine(renderer, center_x - radius / 2,
                               center_y + radius / 3,
                               center_x + radius / 3,
                               center_y - radius / 3);
            SDL_RenderDrawLine(renderer, center_x - radius / 2,
                               center_y + radius / 3,
                               center_x - radius / 3,
                               center_y + radius / 2);
            break;
        }
        case 9: { /* Connection / Steam Link target */
            SDL_Point link[] = {{center_x - radius, center_y},
                                {center_x - radius / 2, center_y - radius / 2},
                                {center_x + radius / 2, center_y - radius / 2},
                                {center_x + radius, center_y},
                                {center_x + radius / 2, center_y + radius / 2},
                                {center_x - radius / 2, center_y + radius / 2},
                                {center_x - radius, center_y}};
            SDL_RenderDrawLines(renderer, link, 7);
            draw_filled_circle(renderer, center_x, center_y, 2, color);
            break;
        }
        case 10: { /* Eye / view */
            SDL_Point eye[] = {{center_x - radius, center_y},
                               {center_x - radius / 2, center_y - radius / 2},
                               {center_x + radius / 2, center_y - radius / 2},
                               {center_x + radius, center_y},
                               {center_x + radius / 2, center_y + radius / 2},
                               {center_x - radius / 2, center_y + radius / 2},
                               {center_x - radius, center_y}};
            SDL_RenderDrawLines(renderer, eye, 7);
            draw_filled_circle(renderer, center_x, center_y, radius / 3, color);
            draw_filled_circle(renderer, center_x, center_y, radius / 7,
                               (SDL_Color){35, 38, 46, 255});
            break;
        }
        case 11: { /* Power */
            draw_circle_outline(renderer, center_x, center_y, radius - 1, color);
            SDL_RenderDrawLine(renderer, center_x, center_y - radius - 1,
                               center_x, center_y - 1);
            break;
        }
        default: break;
    }
}

enum {
    SVRT_ICON_HOME = 0,
    SVRT_ICON_LIBRARY,
    SVRT_ICON_STORE,
    SVRT_ICON_PEOPLE,
    SVRT_ICON_ITEMS,
    SVRT_ICON_DESKTOP,
    SVRT_ICON_SETTINGS,
    SVRT_ICON_EYE,
    SVRT_ICON_POWER,
    SVRT_ICON_CONTROLLER,
    SVRT_ICON_STEAM,
    SVRT_ICON_ADD,
    SVRT_ICON_VR,
    SVRT_ICON_VOLUME
};

static int draw_dashboard_asset_icon(svrt_ui *ui, int icon, int center_x,
                                     int center_y, int size,
                                     SDL_Color color) {
    if (!ui || !ui->renderer || icon < 0 ||
        icon >= SVRT_DASHBOARD_ICON_COUNT || size < 4)
        return 0;
    svrt_ui_video *asset = ui->dashboard_icons[icon];
    if (!asset) return 0;
    if (!asset->texture && video_decode_next(asset, ui->renderer) < 0)
        return 0;
    if (!asset->texture) return 0;
    SDL_SetTextureColorMod(asset->texture, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(asset->texture, color.a);
    SDL_SetTextureBlendMode(asset->texture, SDL_BLENDMODE_BLEND);
    int width = size;
    int height = size;
    int texture_width = 0;
    int texture_height = 0;
    SDL_QueryTexture(asset->texture, NULL, NULL, &texture_width,
                     &texture_height);
    if (texture_width > 0 && texture_height > 0) {
        if (texture_width > texture_height)
            height = (int)lroundf((float)size * texture_height /
                                  texture_width);
        else if (texture_height > texture_width)
            width = (int)lroundf((float)size * texture_width /
                                 texture_height);
    }
    SDL_Rect destination = {center_x - width / 2, center_y - height / 2,
                            width, height};
    SDL_RenderCopy(ui->renderer, asset->texture, NULL, &destination);
    return 1;
}

static int draw_exact_nav_icon(svrt_ui *ui, int item, int center_x,
                               int center_y, int size, SDL_Color color) {
    if (!ui) return 0;
    static const int icon_for_nav[SVRT_DASHBOARD_NAV_ITEMS] = {
        SVRT_ICON_HOME, SVRT_ICON_LIBRARY, SVRT_ICON_STORE, SVRT_ICON_PEOPLE,
        SVRT_ICON_ITEMS, SVRT_ICON_DESKTOP, SVRT_ICON_SETTINGS, SVRT_ICON_VR};
    if (item >= 0 && item < SVRT_DASHBOARD_NAV_ITEMS &&
        draw_dashboard_asset_icon(ui, icon_for_nav[item], center_x, center_y,
                                  size, color))
        return 1;
    draw_nav_icon(ui->renderer, item, center_x, center_y, size, color);
    return 0;
}

static void draw_round_texture(SDL_Renderer *renderer, SDL_Texture *texture,
                               const SDL_Rect *rect) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (!renderer || !texture || !rect) return;
    enum { segments = 24 };
    SDL_Vertex vertices[segments + 1];
    int indices[segments * 3];
    const SDL_Color white = {255, 255, 255, 255};
    vertices[0] = (SDL_Vertex){
        {rect->x + rect->w * 0.5f, rect->y + rect->h * 0.5f},
        white, {0.5f, 0.5f}};
    for (int i = 0; i < segments; ++i) {
        const float angle = i * 2.0f * SVRT_PI / segments;
        const float x = cosf(angle), y = sinf(angle);
        vertices[i + 1] = (SDL_Vertex){
            {rect->x + rect->w * (0.5f + x * 0.5f),
             rect->y + rect->h * (0.5f + y * 0.5f)},
            white, {0.5f + x * 0.5f, 0.5f + y * 0.5f}};
        indices[i * 3] = 0;
        indices[i * 3 + 1] = i + 1;
        indices[i * 3 + 2] = (i + 1) % segments + 1;
    }
    SDL_RenderGeometry(renderer, texture, vertices, segments + 1,
                       indices, segments * 3);
#else
    SDL_RenderCopy(renderer, texture, NULL, rect);
#endif
}

static void draw_arc(SDL_Renderer *renderer, int center_x, int center_y,
                     int radius, float start_degrees, float end_degrees,
                     SDL_Color color) {
    if (!renderer || radius <= 0) return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_FPoint previous = {0.0f, 0.0f};
    int have_previous = 0;
    for (float degrees = start_degrees; degrees <= end_degrees;
         degrees += 5.0f) {
        const float angle = degrees * SVRT_PI / 180.0f;
        const SDL_FPoint current = {
            center_x + cosf(angle) * radius,
            center_y + sinf(angle) * radius};
        if (have_previous)
            SDL_RenderDrawLine(renderer, (int)lroundf(previous.x),
                               (int)lroundf(previous.y),
                               (int)lroundf(current.x),
                               (int)lroundf(current.y));
        previous = current;
        have_previous = 1;
    }
}

static void draw_battery_icon(SDL_Renderer *renderer, const SDL_Rect *rect,
                              int percent, SDL_Color color) {
    if (!renderer || !rect || rect->w <= 8 || rect->h <= 8) return;
    const int terminal_width = (rect->w / 10) + 1;
    SDL_Rect body = {rect->x, rect->y + rect->h / 5,
                     rect->w - terminal_width - 2,
                     rect->h * 3 / 5};
    SDL_Rect terminal = {body.x + body.w + 2,
                         body.y + body.h / 3,
                         terminal_width,
                         body.h / 3};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawRect(renderer, &body);
    SDL_RenderFillRect(renderer, &terminal);
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    if (percent > 0) {
        SDL_Rect fill = {body.x + 2, body.y + 2,
                         (body.w - 4) * percent / 100,
                         body.h - 4};
        SDL_RenderFillRect(renderer, &fill);
    }
}

static void draw_wifi_icon(SDL_Renderer *renderer, const SDL_Rect *rect,
                           int connected, SDL_Color color) {
    if (!renderer || !rect || rect->w <= 8 || rect->h <= 8) return;
    const int center_x = rect->x + rect->w / 2;
    const int center_y = rect->y + rect->h * 3 / 4;
    const int radius = rect->w / 2;
    if (connected) {
        draw_arc(renderer, center_x, center_y, radius, 225.0f, 315.0f,
                 color);
        draw_arc(renderer, center_x, center_y, radius * 2 / 3,
                 225.0f, 315.0f, color);
        draw_arc(renderer, center_x, center_y, radius / 3,
                 225.0f, 315.0f, color);
    }
    draw_filled_circle(renderer, center_x, center_y, 2,
                       connected ? color : (SDL_Color){103, 112, 123, 180});
}

static void draw_speaker_icon(SDL_Renderer *renderer, const SDL_Rect *rect,
                              SDL_Color color) {
    if (!renderer || !rect || rect->w < 8 || rect->h < 8) return;
    const int cx = rect->x + rect->w / 2;
    const int cy = rect->y + rect->h / 2;
    const int r = (rect->w < rect->h ? rect->w : rect->h) / 2;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect box = {cx - r, cy - r / 3, r / 2, r * 2 / 3};
    SDL_RenderFillRect(renderer, &box);
    SDL_RenderDrawLine(renderer, box.x + box.w, box.y,
                       cx + r / 4, cy - r * 2 / 3);
    SDL_RenderDrawLine(renderer, cx + r / 4, cy - r * 2 / 3,
                       cx + r / 4, cy + r * 2 / 3);
    SDL_RenderDrawLine(renderer, cx + r / 4, cy + r * 2 / 3,
                       box.x + box.w, box.y + box.h);
    draw_arc(renderer, cx + r / 5, cy, r * 2 / 3,
             -48.0f, 48.0f, color);
}

static void draw_bell_icon(SDL_Renderer *renderer, const SDL_Rect *rect,
                           SDL_Color color) {
    if (!renderer || !rect || rect->w < 8 || rect->h < 8) return;
    const int cx = rect->x + rect->w / 2;
    const int cy = rect->y + rect->h / 2;
    const int r = (rect->w < rect->h ? rect->w : rect->h) / 2;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    draw_arc(renderer, cx, cy, r * 2 / 3, 198.0f, 342.0f, color);
    SDL_RenderDrawLine(renderer, cx - r * 2 / 3, cy + r / 3,
                       cx + r * 2 / 3, cy + r / 3);
    SDL_RenderDrawPoint(renderer, cx, cy + r / 2);
    SDL_RenderDrawPoint(renderer, cx, cy + r / 2 + 1);
    /* Steam Frame marks unread notifications with the small blue status dot
       used by the source dashboard glyphs. */
    draw_filled_circle(renderer, cx + r * 2 / 3, cy - r * 2 / 3,
                       r > 8 ? 2 : 1, (SDL_Color){26, 159, 255, 255});
}

static void draw_frame_icon(SDL_Renderer *renderer, int cx, int cy,
                            int size, SDL_Color color) {
    if (!renderer || size < 8) return;
    const int r = size / 2;
    const int q = r / 2;
    const SDL_Point hexagon[] = {
        {cx, cy - r}, {cx + r * 7 / 8, cy - q},
        {cx + r * 7 / 8, cy + q}, {cx, cy + r},
        {cx - r * 7 / 8, cy + q}, {cx - r * 7 / 8, cy - q},
        {cx, cy - r}};
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderDrawLines(renderer, hexagon,
                        (int)(sizeof(hexagon) / sizeof(hexagon[0])));
    SDL_Rect center = {cx - size / 8, cy - size / 8,
                       size / 4, size / 4};
    SDL_RenderDrawRect(renderer, &center);
}

static void draw_round_button_state(SDL_Renderer *renderer, int cx, int cy,
                                    int radius, int hovered, int active,
                                    SDL_Color normal, SDL_Color hover) {
    if (!renderer || radius < 2) return;
    draw_filled_circle(renderer, cx, cy, radius,
                       hovered || active ? hover : normal);
    if (hovered) {
        draw_circle_outline(renderer, cx, cy, radius,
                            (SDL_Color){255, 255, 255, 255});
        draw_circle_outline(renderer, cx, cy, radius + 2,
                            (SDL_Color){136, 204, 241, 230});
        draw_circle_outline(renderer, cx, cy, radius + 4,
                            (SDL_Color){136, 204, 241, 80});
    } else if (active) {
        draw_circle_outline(renderer, cx, cy, radius + 1,
                            (SDL_Color){136, 204, 241, 255});
    }
}

static void draw_centered_text(SDL_Renderer *renderer, TTF_Font *font,
                               const char *value, const SDL_Rect *rect,
                               uint8_t alpha) {
    if (!renderer || !font || !value || !rect) return;
    const int font_height = TTF_FontHeight(font);
    draw_text(renderer, font, value, rect->x + rect->w / 2,
              rect->y + (rect->h - font_height) / 2, alpha);
}

static void draw_dashboard_toggle(SDL_Renderer *renderer, const SDL_Rect *rect,
                                  int enabled, SDL_Color blue,
                                  SDL_Color white) {
    if (!renderer || !rect) return;
    fill_rounded_rect(renderer, rect, rect->h / 2,
                      enabled ? blue : (SDL_Color){75, 83, 95, 255});
    const int radius = rect->h / 2 - 4;
    draw_filled_circle(renderer,
                       enabled ? rect->x + rect->w - rect->h / 2 :
                                 rect->x + rect->h / 2,
                       rect->y + rect->h / 2, radius, white);
}

static void draw_dashboard_footer(svrt_ui *ui, const SDL_Rect *panel,
                                  SDL_Color muted, SDL_Color text) {
    if (!ui || !panel) return;
    const int footer_h = panel->h / 9;
    SDL_Rect footer = {panel->x, panel->y + panel->h - footer_h,
                       panel->w, footer_h};
    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->renderer, 14, 20, 29, 255);
    SDL_RenderFillRect(ui->renderer, &footer);
    const int step = panel->w / 5;
    const int selected_item =
        ui->dashboard_popover == SVRT_DASHBOARD_POPOVER_PERFORMANCE ? 2 : 3;
    for (int item = 0; item < 5; ++item) {
        const int center_x = panel->x + step / 2 + item * step;
        const int center_y = footer.y + footer.h / 2;
        const SDL_Color icon_color = item == selected_item ? text : muted;
        if (item == selected_item) {
            SDL_Rect selected = {panel->x + item * step + 2, footer.y + 2,
                                 step - 4, footer.h - 4};
            SDL_SetRenderDrawColor(ui->renderer, 38, 47, 59, 255);
            SDL_RenderFillRect(ui->renderer, &selected);
        }
        if (item == 0) {
            SDL_Rect icon = {center_x - footer.h / 7,
                             center_y - footer.h / 7,
                             footer.h * 2 / 7, footer.h * 2 / 7};
            draw_bell_icon(ui->renderer, &icon, icon_color);
        } else if (item == 1) {
            if (!draw_dashboard_asset_icon(ui, SVRT_ICON_SETTINGS,
                                           center_x, center_y,
                                           footer.h / 3, icon_color))
                draw_nav_icon(ui->renderer, 6, center_x, center_y,
                              footer.h / 3, icon_color);
        } else if (item == 2) {
            const int r = footer.h / 6;
            const SDL_Point bolt[] = {
                {center_x + r / 5, center_y - r},
                {center_x - r, center_y + r / 5},
                {center_x - r / 5, center_y + r / 5},
                {center_x - r / 3, center_y + r},
                {center_x + r, center_y - r / 5},
                {center_x + r / 5, center_y - r / 5}};
            SDL_SetRenderDrawColor(ui->renderer, icon_color.r,
                                   icon_color.g, icon_color.b,
                                   icon_color.a);
            SDL_RenderDrawLines(ui->renderer, bolt, 6);
        } else if (item == 3) {
            SDL_Rect display = {center_x - footer.h / 6,
                                center_y - footer.h / 9,
                                footer.h / 3, footer.h * 2 / 9};
            SDL_SetRenderDrawColor(ui->renderer, icon_color.r,
                                   icon_color.g, icon_color.b,
                                   icon_color.a);
            SDL_RenderDrawRect(ui->renderer, &display);
            SDL_RenderDrawLine(ui->renderer, center_x, display.y + display.h,
                               center_x, display.y + display.h + 3);
        } else {
            const int r = footer.h / 7;
            draw_circle_outline(ui->renderer, center_x, center_y, r,
                                icon_color);
            SDL_RenderDrawLine(ui->renderer, center_x, center_y - r / 2,
                               center_x + r / 3, center_y - r / 4);
            SDL_RenderDrawLine(ui->renderer, center_x + r / 3,
                               center_y - r / 4, center_x,
                               center_y + r / 5);
            SDL_RenderDrawPoint(ui->renderer, center_x,
                                center_y + r * 2 / 3);
        }
    }
}

static void draw_dashboard_popover(svrt_ui *ui,
                                   const svrt_dashboard_layout *layout,
                                   TTF_Font *chrome_font, SDL_Color dark,
                                   SDL_Color text, SDL_Color muted,
                                   SDL_Color blue) {
    if (!ui || !layout || !ui->renderer ||
        ui->dashboard_popover == SVRT_DASHBOARD_POPOVER_NONE) return;
    SDL_Rect panel = dashboard_popover_rect(layout, ui->dashboard_popover);
    SDL_Rect shadow = panel;
    shadow.x += 5; shadow.y += 7;
    fill_rounded_rect(ui->renderer, &shadow, 12, (SDL_Color){0, 0, 0, 150});
    fill_rounded_rect(ui->renderer, &panel, 10, (SDL_Color){12, 18, 27, 252});
    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->renderer, 54, 65, 79, 255);
    SDL_RenderDrawRect(ui->renderer, &panel);

    if (ui->dashboard_popover == SVRT_DASHBOARD_POPOVER_PERFORMANCE) {
        draw_text_left(ui->renderer, chrome_font, "Performance",
                       panel.x + 24, panel.y + 20, text.a);
        draw_text_left(ui->renderer, chrome_font, "PERFORMANCE",
                       panel.x + 24, panel.y + 66, muted.a);
        draw_text_left(ui->renderer, chrome_font, "PLAYTIME",
                       panel.x + panel.w - 112, panel.y + 66, muted.a);
        SDL_Rect basic = {panel.x + 20, panel.y + 96,
                          panel.w - 40, panel.h / 9};
        fill_rounded_rect(ui->renderer, &basic, 2,
                          (SDL_Color){57, 67, 80, 255});
        draw_centered_text(ui->renderer, chrome_font,
                           ui->performance_basic_view ? "Advanced View" :
                                                         "Basic View",
                           &basic, text.a);
        draw_text_left(ui->renderer, chrome_font, "COMMON SETTINGS",
                       panel.x + 24, panel.y + 184, text.a);
        draw_nav_icon(ui->renderer, 10, panel.x + 38, panel.y + 228, 22,
                      muted);
        draw_text_left(ui->renderer, chrome_font, "Manual GPU Clock",
                       panel.x + 58, panel.y + 215, text.a);
        SDL_Rect toggle = {panel.x + panel.w - 78, panel.y + 211, 54, 29};
        draw_dashboard_toggle(ui->renderer, &toggle, ui->manual_gpu_clock,
                              blue, (SDL_Color){246, 249, 252, 255});
        char gpu_text[32];
        snprintf(gpu_text, sizeof(gpu_text), "%d MHz",
                 ui->performance_gpu_mhz > 0 ? ui->performance_gpu_mhz : 903);
        draw_text_left(ui->renderer, chrome_font, gpu_text,
                       panel.x + panel.w - 112, panel.y + 260, text.a);
        SDL_SetRenderDrawColor(ui->renderer, 26, 159, 255, 255);
        SDL_RenderDrawLine(ui->renderer, panel.x + 24, panel.y + 300,
                           panel.x + panel.w - 24, panel.y + 300);
        draw_filled_circle(ui->renderer, panel.x + panel.w - 28,
                           panel.y + 300, 10,
                           (SDL_Color){245, 249, 252, 255});
        draw_text_left(ui->renderer, chrome_font, "500",
                       panel.x + 24, panel.y + 315, muted.a);
        draw_text_left(ui->renderer, chrome_font, "NON-VR SETTINGS",
                       panel.x + 24, panel.y + 360, muted.a);
        draw_dashboard_footer(ui, &panel, muted, text);
    } else if (ui->dashboard_popover == SVRT_DASHBOARD_POPOVER_BATTERY) {
        draw_text_left(ui->renderer, chrome_font, "Battery Info",
                       panel.x + 24, panel.y + 20, text.a);
        int system_percent = system_battery_percent();
        if (system_percent < 0) system_percent = 91;
        SDL_Rect large_battery = {panel.x + 26, panel.y + 72, 42, 68};
        draw_battery_icon(ui->renderer, &large_battery,
                          system_percent, (SDL_Color){97, 235, 117, 255});
        char percent[16];
        snprintf(percent, sizeof(percent), "%d%%", system_percent);
        draw_text_left(ui->renderer, ui->font ? ui->font : chrome_font, percent,
                       panel.x + 82, panel.y + 82, text.a);
        draw_text_left(ui->renderer, chrome_font, "0h 59m",
                       panel.x + panel.w - 84, panel.y + 78, text.a);
        draw_text_left_scaled(ui->renderer, chrome_font,
                              "PROJECTED BATTERY LIFE",
                              panel.x + panel.w - 150, panel.y + 108,
                              0.55f, muted.a);
        const int row_y[] = {panel.y + 166, panel.y + 222, panel.y + 278};
        const char *const labels[] = {"Right Controller", "Left Controller",
                                      "Steam Ctrl (BT)"};
        const char *const values[] = {"47%", "58%", "83%"};
        for (int row = 0; row < 3; ++row) {
            SDL_SetRenderDrawColor(ui->renderer, 48, 59, 72, 255);
            SDL_RenderDrawLine(ui->renderer, panel.x + 22, row_y[row] + 40,
                               panel.x + panel.w - 22, row_y[row] + 40);
            draw_nav_icon(ui->renderer, row == 2 ? 7 : 10,
                          panel.x + 38, row_y[row] + 10, 21, muted);
            draw_text_left(ui->renderer, chrome_font, labels[row],
                           panel.x + 62, row_y[row], text.a);
            draw_text_left(ui->renderer, chrome_font, values[row],
                           panel.x + panel.w - 78, row_y[row], muted.a);
            SDL_Rect controller_battery = {panel.x + panel.w - 44,
                                           row_y[row] + 4, 25, 14};
            draw_battery_icon(ui->renderer, &controller_battery,
                              47 + row * 11,
                              row == 1 ? (SDL_Color){91, 229, 131, 255} :
                                         muted);
        }
        draw_dashboard_footer(ui, &panel, muted, text);
    } else {
        draw_text_left(ui->renderer, chrome_font, "Power",
                       panel.x + 22, panel.y + 16, text.a);
        static const char *const labels[] = {
            "Suspend", "Restart", "Shut Down", "Cancel"};
        const int row_top = panel.y + 54;
        const int row_height = (panel.h - 66) / 4;
        for (int row = 0; row < 4; ++row) {
            SDL_Rect item = {panel.x + 10, row_top + row * row_height,
                             panel.w - 20, row_height - 4};
            fill_rounded_rect(ui->renderer, &item, 4,
                              (SDL_Color){35, 38, 46, 255});
            draw_centered_text(ui->renderer, chrome_font, labels[row],
                               &item, row == 2 ? 235 : text.a);
        }
    }
}

static void draw_dashboard_chrome_eye(svrt_ui *ui, int eye_x,
                                       int eye_width, int height) {
    if (!ui || !ui->renderer || eye_width <= 0 || height <= 0) return;
    const SDL_Color rail_surface = {35, 38, 46, 255}; /* #23262e */
    const SDL_Color rail_shadow = {14, 20, 27, 210}; /* #0e141b */
    const SDL_Color selected = {26, 159, 255, 255}; /* #1a9fff */
    const SDL_Color text = {220, 222, 223, 255}; /* #dcdedf */
    const SDL_Color muted = {139, 146, 154, 255}; /* #8b929a */
    const SDL_Color dark = {14, 20, 27, 255}; /* --gamepadui-darkest-grey */
    const SDL_Color soft_black = {6, 10, 14, 255}; /* --gamepadui-soft-black */
    const SDL_Color button = {61, 68, 80, 255}; /* #3d4450 */
    const SDL_Color eye_surface = {184, 188, 191, 255};
    const SDL_Color eye_hover = {220, 222, 223, 255};
    TTF_Font *chrome_font = ui->dashboard_font ?
                                ui->dashboard_font : ui->small_font;
    svrt_dashboard_layout layout;
    dashboard_refresh_hover(ui, eye_width, height);
    dashboard_make_layout(ui, eye_x, eye_width, height, &layout);

    SDL_Rect shadow = layout.rail;
    shadow.x += 3;
    shadow.y += 5;
    fill_rounded_rect(ui->renderer, &shadow, layout.rail.w / 8, rail_shadow);
    fill_rounded_rect(ui->renderer, &layout.rail, layout.rail.w / 8,
                      rail_surface);

    const int hovered_hit = ui->cursor_visible ? svrt_dashboard_layout_hit(
        &layout, eye_x + ui->cursor_x, ui->cursor_y) :
        SVRT_DASHBOARD_HIT_NONE;
    int selected_page = ui->selected_page;
    if (selected_page < 0 || selected_page >= SVRT_DASHBOARD_NAV_ITEMS)
        selected_page = 0;
    for (int item = 0; item < SVRT_DASHBOARD_NAV_ITEMS; ++item) {
        const SDL_Rect *item_rect = &layout.rail_items[item];
        if (hovered_hit == item) {
            SDL_Rect hover_rect = *item_rect;
            hover_rect.x += 3;
            hover_rect.w -= 6;
            fill_rounded_rect(ui->renderer, &hover_rect, hover_rect.h / 5,
                              button);
        }
        if (item == selected_page) {
            SDL_Rect accent = {layout.rail.x,
                               item_rect->y + item_rect->h / 5,
                               layout.rail.w * 8 / 100 > 3 ?
                                   layout.rail.w * 8 / 100 : 3,
                               item_rect->h * 3 / 5};
            fill_rounded_rect(ui->renderer, &accent, 2, selected);
        }
        const int icon_size = (int)lroundf(layout.rail.w *
                                           (ui->rail_expanded ? 0.22f : 0.42f));
        const int icon_x = ui->rail_expanded ?
            layout.rail.x + layout.rail.w / 8 :
            item_rect->x + item_rect->w / 2;
        draw_exact_nav_icon(ui, item, icon_x,
                            item_rect->y + item_rect->h / 2,
                            icon_size > 18 ? icon_size : 18,
                            item == selected_page ? text : muted);
        if (ui->rail_expanded) {
            static const char *const labels[SVRT_DASHBOARD_NAV_ITEMS] = {
                "Home", "Library", "Store", "Friends & Chat", "Media",
                "Downloads", "Steam Settings", "VR Settings"};
            draw_text_left(ui->renderer, chrome_font, labels[item],
                           layout.rail.x + layout.rail.w / 4,
                           item_rect->y + (item_rect->h -
                                           TTF_FontHeight(chrome_font)) / 2,
                           item == selected_page ? text.a : muted.a);
        }
    }
    if (layout.power_button.w > 0) {
        SDL_Rect power_shadow = layout.power_button;
        power_shadow.x += 3;
        power_shadow.y += 5;
        fill_rounded_rect(ui->renderer, &power_shadow,
                          layout.power_button.w / 6, rail_shadow);
        fill_rounded_rect(ui->renderer, &layout.power_button,
                          layout.power_button.w / 6, rail_surface);
        if (hovered_hit == SVRT_DASHBOARD_HIT_POWER) {
            SDL_Rect hover_rect = layout.power_button;
            hover_rect.x += 3;
            hover_rect.w -= 6;
            fill_rounded_rect(ui->renderer, &hover_rect, hover_rect.h / 5,
                              button);
        }
        const int power_center_x = ui->rail_expanded ?
            layout.power_button.x + layout.power_button.w / 8 :
            layout.power_button.x + layout.power_button.w / 2;
        const int power_center_y = layout.power_button.y +
                                   layout.power_button.h / 2;
        if (!draw_dashboard_asset_icon(
                ui, SVRT_ICON_POWER, power_center_x, power_center_y,
                (int)lroundf(layout.rail.w * 0.38f), muted))
            draw_nav_icon(ui->renderer, 11, power_center_x, power_center_y,
                          (int)lroundf(layout.rail.w * 0.38f), muted);
        if (ui->rail_expanded)
            draw_text_left(ui->renderer, chrome_font, "Power",
                           layout.rail.x + layout.rail.w / 4,
                           layout.power_button.y +
                               (layout.power_button.h - TTF_FontHeight(chrome_font)) / 2,
                           muted.a);
    }

    SDL_Rect steam_shadow = layout.steam_tile;
    steam_shadow.x += 3;
    steam_shadow.y += 5;
    SDL_Rect bar_shadow = layout.status_bar;
    bar_shadow.x += 3;
    bar_shadow.y += 5;
    fill_rounded_rect(ui->renderer, &steam_shadow,
                      layout.steam_tile.h / 10, rail_shadow);
    fill_rounded_rect(ui->renderer, &bar_shadow,
                      layout.status_bar.h / 10, rail_shadow);
    fill_rounded_rect(ui->renderer, &layout.steam_tile,
                      layout.steam_tile.h / 10, soft_black);
    fill_rounded_rect(ui->renderer, &layout.status_bar,
                      layout.status_bar.h / 10, dark);
    /* The Frame dock is the unified bar, not the older SteamVR legacy bar.
       Its shield and view actions sit directly on the dark surface; only the
       eye action receives the light circular button treatment visible in the
       Steam Frame reference. */
    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ui->renderer, 149, 149, 149, 90);
    SDL_RenderDrawLine(ui->renderer, layout.status_bar.x + 10,
                       layout.status_bar.y + 1,
                       layout.status_bar.x + layout.status_bar.w - 10,
                       layout.status_bar.y + 1);

    SDL_Rect steam_active = {layout.steam_tile.x + layout.steam_tile.w / 5,
                             layout.steam_tile.y + layout.steam_tile.h - 5,
                             layout.steam_tile.w * 3 / 5,
                             3};
    fill_rounded_rect(ui->renderer, &steam_active, 2, selected);
    if (!draw_dashboard_asset_icon(
            ui, SVRT_ICON_STEAM,
            layout.steam_tile.x + layout.steam_tile.w / 2,
            layout.steam_tile.y + layout.steam_tile.h / 2,
            layout.steam_tile.w * 48 / 100, text))
        draw_nav_icon(ui->renderer, 8,
                      layout.steam_tile.x + layout.steam_tile.w / 2,
                      layout.steam_tile.y + layout.steam_tile.h / 2,
                      layout.steam_tile.w * 48 / 100, text);

    if (!draw_dashboard_asset_icon(
            ui, SVRT_ICON_ADD,
            layout.plus_button.x + layout.plus_button.w / 2,
            layout.plus_button.y + layout.plus_button.h / 2,
            layout.plus_button.w * 52 / 100, muted)) {
        const int plus_center_x = layout.plus_button.x +
                                  layout.plus_button.w / 2;
        const int plus_center_y = layout.plus_button.y +
                                  layout.plus_button.h / 2;
        const int plus_radius = layout.plus_button.w / 5;
        SDL_SetRenderDrawColor(ui->renderer, muted.r, muted.g, muted.b,
                               muted.a);
        SDL_RenderDrawLine(ui->renderer, plus_center_x - plus_radius,
                           plus_center_y, plus_center_x + plus_radius,
                           plus_center_y);
        SDL_RenderDrawLine(ui->renderer, plus_center_x,
                           plus_center_y - plus_radius, plus_center_x,
                           plus_center_y + plus_radius);
    }
    if (hovered_hit == SVRT_DASHBOARD_HIT_ADD)
        draw_circle_outline(ui->renderer,
                            layout.plus_button.x + layout.plus_button.w / 2,
                            layout.plus_button.y + layout.plus_button.h / 2,
                            layout.plus_button.w / 2 - 2,
                            (SDL_Color){136, 204, 241, 255});

    const int frame_hovered = hovered_hit == SVRT_DASHBOARD_HIT_FRAME;
    const int frame_center_x = layout.frame_button.x +
                               layout.frame_button.w / 2;
    const int frame_center_y = layout.frame_button.y +
                               layout.frame_button.h / 2;
    if (frame_hovered) {
        draw_circle_outline(ui->renderer, frame_center_x, frame_center_y,
                            layout.frame_button.w / 2,
                            (SDL_Color){136, 204, 241, 220});
    }
    draw_frame_icon(ui->renderer, frame_center_x, frame_center_y,
                    layout.frame_button.w * 52 / 100, text);
    const int view_center_x = layout.view_button.x +
                              layout.view_button.w / 2;
    const int view_center_y = layout.view_button.y +
                              layout.view_button.h / 2;
    const int view_hovered = hovered_hit == SVRT_DASHBOARD_HIT_VIEW;
    draw_round_button_state(ui->renderer, view_center_x, view_center_y,
                            layout.view_button.w / 2, view_hovered,
                            ui->view_enabled, eye_surface, eye_hover);
    if (!draw_dashboard_asset_icon(ui, SVRT_ICON_EYE, view_center_x,
                                   view_center_y,
                                   layout.view_button.w * 52 / 100,
                                   dark))
        draw_nav_icon(ui->renderer, 10, view_center_x, view_center_y,
                      layout.view_button.w * 52 / 100, dark);

    const int device_center_x = layout.device_region.x +
                                layout.device_region.w / 2;
    const int device_center_y = layout.device_region.y +
                                layout.device_region.h / 2;
    if (!draw_dashboard_asset_icon(ui, SVRT_ICON_CONTROLLER,
                                   device_center_x, device_center_y,
                                   layout.status_bar.h * 27 / 100, muted))
        draw_nav_icon(ui->renderer, 7, device_center_x, device_center_y,
                      layout.status_bar.h * 27 / 100, muted);

    char clock_text[16] = {0};
    time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local)) {
        strftime(clock_text, sizeof(clock_text), "%I:%M %p", &local);
        if (clock_text[0] == '0')
            memmove(clock_text, clock_text + 1, strlen(clock_text));
    }
    draw_centered_text(ui->renderer, chrome_font, clock_text,
                       &layout.clock_region, text.a);
    int battery_percent = system_battery_percent();
    if (battery_percent < 0) battery_percent = 95;
    const int status_icon_size = layout.status_bar.h / 5 > 16 ?
                                 layout.status_bar.h / 5 : 16;
    const int status_center_y = layout.battery_region.y +
                                layout.battery_region.h / 2;
    const int battery_center_x = layout.battery_region.x +
                                 layout.battery_region.w * 3 / 4;
    SDL_Rect battery_icon = {
        battery_center_x - status_icon_size / 2,
        status_center_y - status_icon_size / 4,
        status_icon_size,
        status_icon_size / 2};
    if (battery_percent >= 0) {
        char battery_text[8];
        snprintf(battery_text, sizeof(battery_text), "%d%%", battery_percent);
        int text_width = 0;
        TTF_SizeUTF8(chrome_font, battery_text, &text_width, NULL);
        draw_text(ui->renderer, chrome_font, battery_text,
                  layout.battery_region.x + text_width / 2,
                  status_center_y - TTF_FontHeight(chrome_font) / 2,
                  muted.a);
    }
    draw_battery_icon(ui->renderer, &battery_icon, battery_percent,
                      battery_percent > 20 ?
                          (SDL_Color){116, 196, 101, 255} : muted);
    SDL_Rect volume_icon = {
        layout.volume_region.x,
        layout.volume_region.y,
        layout.volume_region.w,
        layout.volume_region.h};
    if (!draw_dashboard_asset_icon(ui, SVRT_ICON_VOLUME,
                                   volume_icon.x + volume_icon.w / 2,
                                   volume_icon.y + volume_icon.h / 2,
                                   status_icon_size, muted))
        draw_speaker_icon(ui->renderer, &volume_icon, muted);
    const int wifi_size = status_icon_size;
    SDL_Rect wifi_icon = {
        layout.wifi_region.x + (layout.wifi_region.w - wifi_size) / 2,
        status_center_y - wifi_size / 2,
        wifi_size,
        wifi_size};
    draw_wifi_icon(ui->renderer, &wifi_icon, wifi_connected(), muted);
    SDL_Rect bell_icon = {
        layout.notification_region.x,
        layout.notification_region.y,
        layout.notification_region.w,
        layout.notification_region.h};
    draw_bell_icon(ui->renderer, &bell_icon, muted);

    fill_rounded_rect(ui->renderer, &layout.avatar, layout.avatar.w / 5,
                      (SDL_Color){50, 62, 77, 255});
    update_steam_avatar(ui);
    if (ui->avatar && !ui->avatar->texture)
        video_decode_next(ui->avatar, ui->renderer);
    if (ui->avatar && ui->avatar->texture)
        SDL_RenderCopy(ui->renderer, ui->avatar->texture, NULL,
                       &layout.avatar);
    else
        draw_centered_text(ui->renderer, chrome_font, "U",
                           &layout.avatar, text.a);
    draw_dashboard_popover(ui, &layout, chrome_font, dark, text, muted,
                           selected);
}

static void fit_rect(SDL_Rect *out, int x, int y, int width, int height,
                     int source_width, int source_height, int margin) {
    int available_width = width - margin * 2, available_height = height - margin * 2;
    if ((int64_t)available_width * source_height >
        (int64_t)available_height * source_width) {
        out->h = available_height; out->w = (int64_t)available_height * source_width / source_height;
    } else {
        out->w = available_width; out->h = (int64_t)available_width * source_height / source_width;
    }
    out->x = x + (width - out->w) / 2; out->y = y + (height - out->h) / 2;
}

static void draw_panel_video(svrt_ui *ui, svrt_ui_video *video,
                             uint32_t elapsed_ms, int source_width,
                             int source_height, int margin, int loop) {
    SDL_Rect destination;
    fit_rect(&destination, 0, 0, SVRT_PANEL_WIDTH, SVRT_PANEL_HEIGHT,
             source_width, source_height, margin);
    video_draw(video, ui->renderer, &destination, elapsed_ms, loop);
}

static void draw_loading_content(svrt_ui *ui, svrt_ui_state state,
                                 const char code[5], const char *detail,
                                 uint32_t elapsed_ms, uint8_t alpha) {
    if (!ui->steam_loading)
        ui->steam_loading = video_open(SVRT_GUI_STEAM_LOADING_PATH);

    const int animation_width = 520;
    const int animation_height = animation_width * 1080 / 1920;
    const int text_height = TTF_FontHeight(ui->font);
    const int code_height = TTF_FontHeight(ui->code_font);
    const int animation_text_gap = state == SVRT_UI_AUTHORIZING ? 18 : 24;
    const int text_code_gap = 8;
    const int group_height = animation_height + animation_text_gap +
                             text_height +
                             (state == SVRT_UI_AUTHORIZING ?
                                  text_code_gap + code_height : 0);
    /* The movie has a small, symmetric black border around the logo.  Moving
       the logical group up by half of that border keeps the visible logo and
       its text centered together, rather than centering the logo alone. */
    int y = (SVRT_PANEL_HEIGHT - group_height) / 2 - 12;

    if (ui->steam_loading) {
        SDL_Rect destination = {
            (SVRT_PANEL_WIDTH - animation_width) / 2,
            y,
            animation_width,
            animation_height,
        };
        video_draw(ui->steam_loading, ui->renderer, &destination,
                   elapsed_ms, 1);
    }
    y += animation_height + animation_text_gap;

    const char *text = state == SVRT_UI_AUTHORIZING ?
                           "Enter this pin in Steam" :
                       state == SVRT_UI_STARTING ?
                           (SVRT_UI_MINIMAL_STEAMOS ?
                                (detail ? detail : "Starting Steam") :
                                "Starting SteamVR stream") :
                           (detail ? detail : "Steam Link pairing failed");
    draw_text(ui->renderer, ui->font, text, SVRT_PANEL_WIDTH / 2, y, alpha);

    if (state == SVRT_UI_AUTHORIZING && code && code[0]) {
        y += text_height + text_code_gap;
        draw_text(ui->renderer, ui->code_font, code,
                  SVRT_PANEL_WIDTH / 2, y, alpha);
    }
}

static void draw_video_per_eye(svrt_ui *ui, svrt_ui_video *video,
                               uint32_t elapsed_ms, int source_width,
                               int source_height, int margin, int loop,
                               float scale, float horizontal_aspect) {
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
    for (int eye = 0; eye < 2; ++eye) {
        const int eye_x = eye * (width / 2);
        const int eye_width = eye ? width - width / 2 : width / 2;
        SDL_Rect destination;
        fit_rect(&destination, eye_x, 0, eye_width, height,
                 source_width, source_height, margin);
        destination.w = (int)lroundf(destination.w * scale * horizontal_aspect);
        destination.h = (int)lroundf(destination.h * scale);
        destination.x = eye_x + (eye_width - destination.w) / 2;
        destination.y = (height - destination.h) / 2;
        video_draw(video, ui->renderer, &destination, elapsed_ms, loop);
    }
}

static void draw_panel_content(svrt_ui *ui, svrt_ui_state state,
                               const char code[5], const char *detail,
                               uint32_t elapsed_ms, uint8_t alpha) {
    if (!ui->panel) return;
    if (SDL_SetRenderTarget(ui->renderer, ui->panel)) {
        static int reported_target_error;
        if (!reported_target_error) {
            fprintf(stderr, "SVRT UI: panel render target unavailable: %s\n",
                    SDL_GetError());
            reported_target_error = 1;
        }
        return;
    }
    const int loading_state = state == SVRT_UI_AUTHORIZING ||
                              state == SVRT_UI_STARTING ||
                              state == SVRT_UI_FAILED ||
                              (SVRT_UI_MINIMAL_STEAMOS &&
                               state == SVRT_UI_HOME && !ui->client_frame);
    /* steam_loading.mkv has an opaque black background.  Match it while the
       animation is visible so its scaled frame never appears as a rectangle. */
    if (loading_state)
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
    else
        SDL_SetRenderDrawColor(ui->renderer, 5, 7, 10, 255);
    SDL_RenderClear(ui->renderer);
    if (state == SVRT_UI_SEARCHING) {
        if (!ui->loop) ui->loop = video_open(SVRT_GUI_LOOP_PATH);
        if (ui->loop)
            draw_panel_video(ui, ui->loop, elapsed_ms, 1440, 1600, 0, 1);
        if (ui->loop && ui->loop->playback_started &&
            !ui->loop_first_frame_ms) {
            ui->loop_first_frame_ms = SDL_GetTicks();
            fprintf(stderr, "STEARLIGHT LOOP STARTING %s\n",
                    SVRT_GUI_LOOP_PATH);
        }
    } else if (state == SVRT_UI_AUTHORIZING || state == SVRT_UI_STARTING) {
        draw_loading_content(ui, state, code, detail, elapsed_ms, alpha);
    } else if (state == SVRT_UI_FAILED) {
        draw_loading_content(ui, state, code, detail, elapsed_ms, alpha);
    } else if (state == SVRT_UI_HOME) {
        if (ui->client_frame) {
            SDL_Rect destination = {0, 0, SVRT_PANEL_WIDTH, SVRT_PANEL_HEIGHT};
            SDL_RenderCopy(ui->renderer, ui->client_frame, NULL, &destination);
        } else if (SVRT_UI_MINIMAL_STEAMOS) {
#if SVRT_SHOW_STEAM_STARTING_FALLBACK
            draw_loading_content(ui, SVRT_UI_STARTING, NULL, detail,
                                 elapsed_ms, alpha);
#endif
        } else {
            draw_text(ui->renderer, ui->font, "Steam ARM64",
                      SVRT_PANEL_WIDTH / 2, SVRT_PANEL_HEIGHT / 2 - 42, 255);
            draw_text(ui->renderer, ui->small_font,
                      detail ? detail : "Starting Big Picture",
                      SVRT_PANEL_WIDTH / 2, SVRT_PANEL_HEIGHT / 2 + 12, 210);
        }
    }
    SDL_SetRenderTarget(ui->renderer, NULL);
}

static float scene_focal_length(int eye_height) {
    const float half_fov = SVRT_VERTICAL_FOV_DEGREES * SVRT_PI / 360.0f;
    return (float)eye_height * 0.5f / tanf(half_fov);
}

static SDL_FPoint project_ground(float world_x, float world_z, int eye_x,
                                 int eye_width, int height, int eye) {
    const float focal = scene_focal_length(height);
    const float eye_offset = eye ? SVRT_EYE_SEPARATION_METERS * 0.5f :
                                   -SVRT_EYE_SEPARATION_METERS * 0.5f;
    return (SDL_FPoint){
        eye_x + eye_width * 0.5f + focal * (world_x - eye_offset) / world_z,
        height * 0.5f + focal * SVRT_EYE_HEIGHT_METERS / world_z};
}

static SDL_FPoint distort_ring_point(SDL_FPoint point, int eye_x,
                                     int eye_width, int height) {
    const float center_x = eye_x + eye_width * 0.5f;
    const float center_y = height * 0.5f;
    const float nx = (point.x - center_x) / (eye_width * 0.5f);
    const float ny = (point.y - center_y) / (height * 0.5f);
    float radius_squared = nx * nx + ny * ny;
    if (radius_squared > 4.0f) radius_squared = 4.0f;
    const float scale = 1.0f + 0.065f * radius_squared;
    point.x = center_x + (point.x - center_x) * scale;
    point.y = center_y + (point.y - center_y) * scale;
    return point;
}

static SDL_FPoint polar_grid_point(float distance, float angle, int eye_x,
                                   int eye_width, int height, int eye) {
    const float center_x = eye_x + eye_width * 0.5f;
    const float origin_y = height * 1.025f;
    const float horizon_y = height * 0.5f;
    const float radial = 1.0f -
        expf(-distance / (SVRT_EYE_HEIGHT_METERS * 1.5f));
    const float curved_radial = powf(radial, 1.18f);
    const float stereo = (eye ? -1.0f : 1.0f) * radial * 2.0f;
    return (SDL_FPoint){
        center_x + eye_width * 0.68f * curved_radial * cosf(angle) + stereo,
        origin_y - (origin_y - horizon_y) * radial * sinf(angle)};
}

static void draw_floor_grid(svrt_ui *ui, int eye_x, int eye_width,
                            int height, int eye) {
    SDL_Rect clip = {eye_x, height / 2, eye_width, height - height / 2};
    SDL_RenderSetClipRect(ui->renderer, &clip);
    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_NONE);
    /* SteamVR's visible floor-grid line color.  Keep it opaque so the final
       scanout is exactly #24282F instead of depending on blend rounding. */
    SDL_SetRenderDrawColor(ui->renderer, 36, 40, 47, 255);

    /* Rings are circles in world space. Perspective makes them the familiar
       SteamVR ellipses while the 1.70 m camera height fixes their scale. */
    for (float radius = 0.75f; radius <= 12.0f; radius += 0.75f) {
        SDL_FPoint points[65];
        int count = 0;
        for (int sample = 0; sample <= 64; ++sample) {
            const float angle = (8.0f + sample * 164.0f / 64.0f) *
                                SVRT_PI / 180.0f;
            const float z = radius * sinf(angle);
            if (z > 0.20f)
                points[count++] = distort_ring_point(
                    project_ground(radius * cosf(angle), z, eye_x, eye_width,
                                   height, eye), eye_x, eye_width, height);
        }
        if (count > 1) SDL_RenderDrawLinesF(ui->renderer, points, count);
    }
    for (int spoke = -8; spoke <= 8; ++spoke) {
        const float angle = (90.0f + spoke * 10.0f) * SVRT_PI / 180.0f;
        SDL_FPoint points[41];
        for (int sample = 0; sample <= 40; ++sample) {
            const float distance = 12.0f * sample / 40.0f;
            points[sample] = polar_grid_point(distance, angle, eye_x,
                                               eye_width, height, eye);
            if (spoke == 0)
                points[sample].x = eye_x + eye_width * 0.5f;
        }
        SDL_RenderDrawLinesF(ui->renderer, points, 41);
        if (spoke == 0) {
            for (int sample = 0; sample <= 40; ++sample)
                points[sample].x += 1.0f;
            SDL_RenderDrawLinesF(ui->renderer, points, 41);
        }
    }
    SDL_RenderSetClipRect(ui->renderer, NULL);
}

static void draw_circle_curved_environment_eye(svrt_ui *ui, int eye_x,
                                                int eye_width, int height,
                                                int eye, int draw_floor) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    enum { segments = 64 };
    SDL_Vertex sky[(segments + 1) * 2];
    SDL_Vertex floor[(segments + 1) * 2];
    int indices[segments * 6];
    const SDL_Color white = {255, 255, 255, 255};
    const SDL_Color black = {0, 0, 0, 255};
    for (int column = 0; column <= segments; ++column) {
        /* Numerically invert an actual 12.75 m world-space circle at this
           screen column. This is the same camera/lens projection as a ring,
           without feeding extreme off-screen vertices to SDL's sampler. */
        const float radius = 12.75f;
        const float u = (float)column / segments;
        const float target_x = eye_x + u * eye_width;
        float low = 8.0f * SVRT_PI / 180.0f;
        float high = 172.0f * SVRT_PI / 180.0f;
        SDL_FPoint boundary = {target_x, (float)height};
        for (int iteration = 0; iteration < 24; ++iteration) {
            const float angle = (low + high) * 0.5f;
            const float z = radius * sinf(angle);
            boundary = distort_ring_point(
                project_ground(radius * cosf(angle), z, eye_x, eye_width,
                               height, eye), eye_x, eye_width, height);
            if (boundary.x > target_x) low = angle;
            else high = angle;
        }
        boundary.x = target_x;
        sky[column * 2] =
            (SDL_Vertex){{boundary.x, 0.0f}, white, {u, 0.0f}};
        sky[column * 2 + 1] =
            (SDL_Vertex){{boundary.x, boundary.y}, white, {u, 0.5f}};
        floor[column * 2] =
            (SDL_Vertex){{boundary.x, boundary.y}, black, {0.0f, 0.0f}};
        floor[column * 2 + 1] =
            (SDL_Vertex){{boundary.x, (float)height}, black, {0.0f, 0.0f}};
    }
    for (int column = 0; column < segments; ++column) {
        const int vertex = column * 2, index = column * 6;
        indices[index] = vertex; indices[index + 1] = vertex + 2;
        indices[index + 2] = vertex + 1; indices[index + 3] = vertex + 1;
        indices[index + 4] = vertex + 2; indices[index + 5] = vertex + 3;
    }
    SDL_RenderGeometry(ui->renderer, ui->background->texture, sky,
                       (segments + 1) * 2, indices, segments * 6);
    if (draw_floor)
        SDL_RenderGeometry(ui->renderer, NULL, floor,
                           (segments + 1) * 2, indices, segments * 6);
#else
    SDL_Rect destination = {eye_x, 0, eye_width, height};
    SDL_RenderCopy(ui->renderer, ui->background->texture, NULL, &destination);
    if (draw_floor) {
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
        SDL_Rect floor = {eye_x, height / 2, eye_width, height - height / 2};
        SDL_RenderFillRect(ui->renderer, &floor);
    }
#endif
}

static void draw_environment(svrt_ui *ui) {
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
    if (ui->background && !ui->background->texture)
        video_decode_next(ui->background, ui->renderer);
    const int eye_count = SVRT_ENABLE_DEBUG_LEFT_EYE_UI ? 1 : 2;
    for (int eye = 0; eye < eye_count; ++eye) {
        const int eye_x = eye * (width / 2);
        const int eye_width = eye ? width - width / 2 : width / 2;
        SDL_Rect eye_clip = {eye_x, 0, eye_width, height};
        SDL_RenderSetClipRect(ui->renderer, &eye_clip);
        if (ui->background && ui->background->texture)
            draw_circle_curved_environment_eye(ui, eye_x, eye_width, height,
                                                eye, 1);
        draw_floor_grid(ui, eye_x, eye_width, height, eye);
        /* Paint the sky once more to mask the portion of a spoke beyond the
           outer circle. The grid therefore terminates exactly at its curved
           sky boundary. */
        if (ui->background && ui->background->texture)
            draw_circle_curved_environment_eye(ui, eye_x, eye_width, height,
                                                eye, 0);
        SDL_RenderSetClipRect(ui->renderer, NULL);
    }
}

static void draw_curved_panel_eye(svrt_ui *ui, int eye_x, int eye_width,
                                  int height, int eye) {
    /* Keep the curved silhouette smooth even on renderers that have to use
       the RenderCopy fallback below.  The SteamOS window intentionally uses
       square corners; the curved projection belongs to the panel surface,
       not to its UI artwork. */
    enum { slices = 96 };
#if SDL_VERSION_ATLEAST(2, 0, 18)
    SDL_Vertex vertices[(slices + 1) * 2];
    int indices[slices * 6];
    const float focal = scene_focal_length(height);
    const float eye_offset = eye ? SVRT_EYE_SEPARATION_METERS * 0.5f :
                                   -SVRT_EYE_SEPARATION_METERS * 0.5f;
    const float radius = 3.5f, distance = 2.15f;
    const float arc = (SVRT_UI_MINIMAL_STEAMOS ? SVRT_UI_PANEL_ARC_DEGREES : 44.0f) *
                      SVRT_PI / 180.0f;
    const float panel_height = SVRT_UI_MINIMAL_STEAMOS ?
                                   SVRT_UI_PANEL_HEIGHT_METERS : 1.50f;
    for (int column = 0; column <= slices; ++column) {
        const float u = (float)column / slices;
        const float angle = (u - 0.5f) * arc;
        const float world_x = radius * sinf(angle);
        const float z = distance - radius * (1.0f - cosf(angle));
        const float screen_x = eye_x +
                               eye_width * SVRT_UI_PANEL_CENTER_X_RATIO +
                               SVRT_UI_HORIZONTAL_ASPECT * focal *
                                   (world_x - eye_offset) / z;
        const float half_screen_height = focal * panel_height * 0.5f / z;
        SDL_Color white = {255, 255, 255, 255};
        vertices[column * 2] = (SDL_Vertex){
            {screen_x, height * SVRT_UI_PANEL_CENTER_Y_RATIO -
                           half_screen_height},
            white, {u, 0.0f}};
        vertices[column * 2 + 1] = (SDL_Vertex){
            {screen_x, height * SVRT_UI_PANEL_CENTER_Y_RATIO +
                           half_screen_height},
            white, {u, 1.0f}};
    }
    for (int column = 0; column < slices; ++column) {
        const int vertex = column * 2, index = column * 6;
        indices[index] = vertex; indices[index + 1] = vertex + 1;
        indices[index + 2] = vertex + 2; indices[index + 3] = vertex + 2;
        indices[index + 4] = vertex + 1; indices[index + 5] = vertex + 3;
    }
#if SVRT_UI_FORCE_PANEL_STRIPS
    /* SDL's OpenGL renderer on the VM exposes RenderGeometry but can drop
       textured geometry silently. Draw narrow projected strips with the
       well-supported RenderCopy path instead; the varying top/bottom and x
       coordinates preserve the curved floating-panel silhouette. */
    for (int column = 0; column < slices; ++column) {
        const SDL_FPoint top = {
            (vertices[column * 2].position.x +
             vertices[(column + 1) * 2].position.x) * 0.5f,
            (vertices[column * 2].position.y +
             vertices[(column + 1) * 2].position.y) * 0.5f};
        const SDL_FPoint bottom = {
            (vertices[column * 2 + 1].position.x +
             vertices[(column + 1) * 2 + 1].position.x) * 0.5f,
            (vertices[column * 2 + 1].position.y +
             vertices[(column + 1) * 2 + 1].position.y) * 0.5f};
        const int source_x = column * SVRT_PANEL_WIDTH / slices;
        const int source_next = (column + 1) * SVRT_PANEL_WIDTH / slices;
        const int destination_x = (int)floorf(
            vertices[column * 2].position.x);
        const int destination_next = (int)ceilf(
            vertices[(column + 1) * 2].position.x);
        SDL_Rect source = {source_x, 0,
                           source_next - source_x, SVRT_PANEL_HEIGHT};
        SDL_Rect destination = {
            destination_x, (int)floorf(top.y),
            destination_next - destination_x,
            (int)ceilf(bottom.y) - (int)floorf(top.y)};
        if (source.w > 0 && destination.w > 0 && destination.h > 0)
            SDL_RenderCopy(ui->renderer, ui->panel, &source, &destination);
    }
#else
    const int geometry_rc = SDL_RenderGeometry(ui->renderer, ui->panel,
                                                vertices, (slices + 1) * 2,
                                                indices, slices * 6);
    if (geometry_rc < 0) {
        static int reported_geometry_error;
        if (!reported_geometry_error) {
            fprintf(stderr, "SVRT UI: curved panel geometry unavailable: %s\n",
                    SDL_GetError());
            reported_geometry_error = 1;
        }
        /* Some X11/llvmpipe combinations expose SDL_RenderGeometry but do
           not support textured geometry. Keep the shell visible while
           retaining the curved path on renderers that implement it. */
        const int fallback_width = (int)lroundf(
            eye_width * 0.82f * SVRT_UI_HORIZONTAL_ASPECT);
        const int fallback_height = (int)lroundf(height * 0.72f);
        SDL_Rect fallback = {
            eye_x + (eye_width - fallback_width) / 2 +
                (int)lroundf(eye_width *
                             (SVRT_UI_PANEL_CENTER_X_RATIO - 0.5f)),
            (int)lroundf(height * SVRT_UI_PANEL_CENTER_Y_RATIO -
                         fallback_height * 0.5f),
            fallback_width, fallback_height};
        SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ui->renderer, 14, 18, 24, 255);
        SDL_RenderFillRect(ui->renderer, &fallback);
        SDL_SetRenderDrawColor(ui->renderer, 48, 151, 238, 230);
        SDL_RenderDrawRect(ui->renderer, &fallback);
    }
#endif
#else
    (void)eye;
    const int corrected_width = (int)lroundf(
        eye_width * 8.0f / 10.0f * SVRT_UI_HORIZONTAL_ASPECT);
    SDL_Rect destination = {eye_x + (eye_width - corrected_width) / 2,
                            (int)lroundf(height *
                                (SVRT_UI_PANEL_CENTER_Y_RATIO - 0.25f)),
                            corrected_width, height / 2};
    SDL_RenderCopy(ui->renderer, ui->panel, NULL, &destination);
#endif
}

/* A visible transition for the generic receiver target.  The standalone OS
 * deliberately compiles this out: Valve's Steam surface owns the first-run
 * experience and a missing frame must remain a black environment. */
#if SVRT_SHOW_STEAM_STARTING_FALLBACK
static void draw_steam_starting_eye(svrt_ui *ui, int eye_x, int eye_width,
                                    int height, uint32_t elapsed_ms) {
    const int panel_width = (int)lroundf(eye_width * 0.72f);
    const int panel_height = (int)lroundf(height * 0.55f);
    SDL_Rect panel = {eye_x + (eye_width - panel_width) / 2,
                      (height - panel_height) / 2,
                      panel_width, panel_height};
    fill_rounded_rect(ui->renderer, &panel, panel_width / 24,
                      (SDL_Color){14, 18, 24, 255});

    const int animation_width = (int)lroundf(eye_width * 0.30f);
    const int animation_height = animation_width * 1080 / 1920;
    const int gap = (int)lroundf(height * 0.018f);
    const int text_height = TTF_FontHeight(ui->font);
    const int group_height = animation_height + gap + text_height;
    const int group_y = panel.y + (panel.h - group_height) / 2;
    if (ui->steam_loading) {
        SDL_Rect animation = {eye_x + (eye_width - animation_width) / 2,
                              group_y, animation_width, animation_height};
        video_draw(ui->steam_loading, ui->renderer, &animation,
                   elapsed_ms, 1);
    }
    draw_text(ui->renderer, ui->font, "Starting Steam",
              eye_x + eye_width / 2, group_y + animation_height + gap, 255);
}
#endif

static void draw_cursor_circle(SDL_Renderer *renderer, int center_x,
                               int center_y, int radius, SDL_Color color) {
    if (!renderer || radius <= 0) return;
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int y = -radius; y <= radius; ++y) {
        const int span = (int)sqrtf((float)(radius * radius - y * y));
        SDL_RenderDrawLine(renderer, center_x - span, center_y + y,
                           center_x + span, center_y + y);
    }
}

#if !SVRT_UI_NATIVE_DASHBOARD
static void draw_laser_cursor_eye(svrt_ui *ui, int eye_x, int eye_width,
                                  int height) {
    if (!ui || ui->state != SVRT_UI_HOME || !ui->cursor_visible ||
        eye_width <= 0 || height <= 0)
        return;
    int cursor_x = ui->cursor_x;
    int cursor_y = ui->cursor_y;
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_x >= eye_width) cursor_x = eye_width - 1;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_y >= height) cursor_y = height - 1;
    const int target_x = eye_x + cursor_x;
    const int target_y = cursor_y;
    const int origin_x = eye_x +
                         (int)lroundf(eye_width * SVRT_UI_LASER_ORIGIN_X_RATIO);
    const int origin_y = (int)lroundf(height * SVRT_UI_LASER_ORIGIN_Y_RATIO);
    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_BLEND);
    /* SteamVR's laser model is a single narrow cyan ray.  The old fallback
       added three decorative dots along it; those dots are not in the
       shipped laser_pointer asset and made the cursor visibly different. */
    SDL_SetRenderDrawColor(ui->renderer, 4, 18, 42, 210);
    SDL_RenderDrawLine(ui->renderer, origin_x - 1, origin_y,
                       target_x - 1, target_y);
    SDL_RenderDrawLine(ui->renderer, origin_x + 1, origin_y,
                       target_x + 1, target_y);
    SDL_SetRenderDrawColor(ui->renderer, 26, 159, 255, 225);
    SDL_RenderDrawLine(ui->renderer, origin_x, origin_y, target_x, target_y);

    const int hit = SVRT_UI_SHOW_NAV_CHROME ?
                        dashboard_hit_at(ui, eye_x + cursor_x, cursor_y,
                                         eye_width * 2, height) :
                        SVRT_DASHBOARD_HIT_NONE;
    int drew_pointer_texture = 0;
    /* overlay_pointer is the shipped default surface cursor.  The circle
       variant is the cursor Steam uses while the laser is over dashboard
       chrome, where the hit target is a control rather than a web surface. */
    svrt_ui_video *pointer = NULL;
    if (hit != SVRT_DASHBOARD_HIT_NONE && ui->pointer_circle)
        pointer = ui->pointer_circle;
    else if (ui->pointer_style == SVRT_UI_POINTER_IBEAM && ui->pointer_ibeam)
        pointer = ui->pointer_ibeam;
    else
        pointer = ui->pointer ? ui->pointer : ui->pointer_circle;
    if (pointer) {
        /* Valve's overlay pointer textures are authored at 32 px. At the
           1600-line Steam Frame scanout they remain 32 px instead of being
           doubled into the oversized glow used by the old fallback. */
        const int pointer_size = height * 2 / 100 < 20 ?
                                     20 : height * 2 / 100;
        SDL_Rect pointer_rect = {target_x - pointer_size / 2,
                                 target_y - pointer_size / 2,
                                 pointer_size, pointer_size};
        video_draw(pointer, ui->renderer, &pointer_rect, SDL_GetTicks(), 0);
        drew_pointer_texture = pointer->texture != NULL;
    }
    if (!drew_pointer_texture) {
        draw_cursor_circle(ui->renderer, target_x, target_y, 12,
                           (SDL_Color){26, 159, 255, 48});
        draw_cursor_circle(ui->renderer, target_x, target_y, 7,
                           (SDL_Color){67, 184, 231, 230});
        draw_cursor_circle(ui->renderer, target_x, target_y, 3,
                           (SDL_Color){214, 249, 255, 235});
    }
}
#endif

static void draw_scene(svrt_ui *ui) {
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
    int rendering_left_eye_scene = 0;
    if (ui->left_eye_scene &&
        !SDL_SetRenderTarget(ui->renderer, ui->left_eye_scene)) {
        rendering_left_eye_scene = 1;
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ui->renderer);
    }
#endif
    if (!SVRT_UI_MINIMAL_STEAMOS)
        draw_environment(ui);
    const int eye_count = SVRT_ENABLE_DEBUG_LEFT_EYE_UI ? 1 : 2;
    /* During the standalone SteamOS transition, the local panel is hidden
       while Steam is still negotiating its surface.  SEARCHING is different:
       it owns loop.mkv and must be presented after boot.mkv until Steam's
       first real frame is ready. */
    const int show_panel = !(SVRT_UI_MINIMAL_STEAMOS && !ui->client_frame &&
                             ui->state != SVRT_UI_SEARCHING);
    if (show_panel)
        for (int eye = 0; eye < eye_count; ++eye)
            draw_curved_panel_eye(ui, eye * (width / 2),
                                  eye ? width - width / 2 : width / 2,
                                  height, eye);
#if SVRT_SHOW_STEAM_STARTING_FALLBACK
    if (SVRT_UI_MINIMAL_STEAMOS && !ui->client_frame) {
        /* Keep a non-black native-Steam transition on scanout until the
           client has produced its first X11 frame. */
        for (int eye = 0; eye < eye_count; ++eye)
            draw_steam_starting_eye(ui, eye * (width / 2),
                                    eye ? width - width / 2 : width / 2,
                                    height, SDL_GetTicks() - ui->state_started_ms);
    }
#endif
    if (ui->state == SVRT_UI_HOME &&
        (!SVRT_UI_MINIMAL_STEAMOS || SVRT_UI_SHOW_NAV_CHROME))
        for (int eye = 0; eye < eye_count; ++eye)
            draw_dashboard_chrome_eye(ui, eye * (width / 2),
                                      eye ? width - width / 2 : width / 2,
                                      height);
#if !SVRT_UI_NATIVE_DASHBOARD
    for (int eye = 0; eye < eye_count; ++eye)
        draw_laser_cursor_eye(ui, eye * (width / 2),
                              eye ? width - width / 2 : width / 2, height);
#endif
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
    if (rendering_left_eye_scene) {
        SDL_SetRenderTarget(ui->renderer, NULL);
        const SDL_Rect source = {0, 0, width / 2, height};
        const int zoomed_width = (int)lroundf(
            source.w * SVRT_DEBUG_LEFT_EYE_UI_SCALE);
        const int zoomed_height = (int)lroundf(
            source.h * SVRT_DEBUG_LEFT_EYE_UI_SCALE);
        const SDL_Rect destination = {
            (width - zoomed_width) / 2,
            (height - zoomed_height) / 2,
            zoomed_width,
            zoomed_height,
        };
        SDL_RenderCopy(ui->renderer, ui->left_eye_scene, &source,
                       &destination);
    }
#endif
}

static int configure_display_mode(SDL_Window *window) {
#if SVRT_ENFORCE_DISPLAY_MODE
    if (!window) return -1;
    int display = SDL_GetWindowDisplayIndex(window);
    if (display < 0) display = 0;

    SDL_DisplayMode selected = {0};
    const int mode_count = SDL_GetNumDisplayModes(display);
    for (int index = 0; index < mode_count; ++index) {
        SDL_DisplayMode candidate = {0};
        if (SDL_GetDisplayMode(display, index, &candidate)) continue;
        if (candidate.w == SVRT_DISPLAY_WIDTH &&
            candidate.h == SVRT_DISPLAY_HEIGHT &&
            candidate.refresh_rate == SVRT_DISPLAY_REFRESH_HZ) {
            selected = candidate;
            break;
        }
    }
    if (!selected.w || !selected.h) {
        /* X11 virtual displays do not always advertise the modeline through
         * SDL even when the Xorg server is running at the requested virtual
         * size.  Keep the desktop scanout in that case; the output-size
         * validation below still rejects a wrong framebuffer size. */
        fprintf(stderr,
                "SVRT UI: %dx%d@%dHz is not enumerated; using the Xorg desktop mode\n",
                SVRT_DISPLAY_WIDTH, SVRT_DISPLAY_HEIGHT,
                SVRT_DISPLAY_REFRESH_HZ);
        if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP)) {
            fprintf(stderr, "SVRT UI: cannot enter the desktop fullscreen mode: %s\n",
                    SDL_GetError());
            return -1;
        }
        return 0;
    }
    if (SDL_SetWindowDisplayMode(window, &selected)) {
        fprintf(stderr, "SVRT UI: cannot select %dx%d@%dHz: %s\n",
                selected.w, selected.h, selected.refresh_rate,
                SDL_GetError());
        return -1;
    }
    if (SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN)) {
        fprintf(stderr, "SVRT UI: cannot enter the required fullscreen mode: %s\n",
                SDL_GetError());
        return -1;
    }
#else
    (void)window;
#endif
    return 0;
}

int svrt_ui_open(svrt_ui *ui) {
    memset(ui, 0, sizeof(*ui));
    ui->gamepad_instance_id = -1;
    ui->dashboard_pressed_hit = SVRT_DASHBOARD_HIT_NONE;
    const char *trace_fps = getenv("SVRT_TRACE_UI_FPS");
    ui->trace_ui_fps = trace_fps && trace_fps[0] && strcmp(trace_fps, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER)) {
        fprintf(stderr, "SVRT UI: SDL video initialization failed: %s\n",
                SDL_GetError());
        return -1;
    }
    ui->owns_sdl = 1;
    if (TTF_Init()) {
        fprintf(stderr, "SVRT UI: font initialization failed: %s\n",
                TTF_GetError());
        SDL_Quit(); memset(ui, 0, sizeof(*ui)); return -1;
    }
    disable_local_input();
    const int requested_width = SVRT_ENFORCE_DISPLAY_MODE ?
                                SVRT_DISPLAY_WIDTH : 1280;
    const int requested_height = SVRT_ENFORCE_DISPLAY_MODE ?
                                 SVRT_DISPLAY_HEIGHT : 720;
    const Uint32 window_flags = SVRT_ENFORCE_DISPLAY_MODE ?
                                 SDL_WINDOW_BORDERLESS :
                                 SDL_WINDOW_FULLSCREEN_DESKTOP;
    ui->window = SDL_CreateWindow("SVRT", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, requested_width, requested_height,
        window_flags);
    if (ui->window && configure_display_mode(ui->window)) {
        SDL_DestroyWindow(ui->window);
        ui->window = NULL;
    }
    ui->renderer = ui->window ? SDL_CreateRenderer(ui->window, -1,
                                                    SDL_RENDERER_ACCELERATED) : NULL;
    /* The timing test runs with SDL's dummy/offscreen backend.  It has no
       accelerated renderer, while the real Pi display still takes the fast
       path above. */
    if (ui->window && !ui->renderer)
        ui->renderer = SDL_CreateRenderer(ui->window, -1,
                                          SDL_RENDERER_SOFTWARE);
    if (ui->renderer) {
        SDL_RendererInfo renderer_info;
        if (!SDL_GetRendererInfo(ui->renderer, &renderer_info))
            fprintf(stderr, "SVRT UI: renderer=%s flags=0x%x\n",
                    renderer_info.name ? renderer_info.name : "unknown",
                    renderer_info.flags);
    }
    /* Open controllers only after the renderer exists.  SDL exposes a
       controller at process startup before it emits a DEVICEADDED event;
       opening it earlier used to be skipped by the renderer guard in the
       input path, leaving a paired controller unable to move the laser until
       it was unplugged and reconnected. */
    SDL_GameControllerEventState(SDL_ENABLE);
    for (int device = 0; device < SDL_NumJoysticks(); ++device) {
        if (SDL_IsGameController(device)) {
            open_game_controller(ui, device);
            break;
        }
    }
    int output_width = 0, output_height = 0;
    SDL_DisplayMode output_mode = {0};
    if (ui->renderer)
        SDL_GetRendererOutputSize(ui->renderer, &output_width, &output_height);
    if (ui->window)
        SDL_GetWindowDisplayMode(ui->window, &output_mode);
    int output_refresh = output_mode.refresh_rate;
    if (!output_refresh && ui->window) {
        const int display = SDL_GetWindowDisplayIndex(ui->window);
        SDL_DisplayMode current_mode = {0};
        if (display >= 0 && !SDL_GetCurrentDisplayMode(display, &current_mode))
            output_refresh = current_mode.refresh_rate;
    }
#if SVRT_ENFORCE_DISPLAY_MODE
    if (output_width != SVRT_DISPLAY_WIDTH ||
        output_height != SVRT_DISPLAY_HEIGHT ||
        (output_refresh && output_refresh != SVRT_DISPLAY_REFRESH_HZ)) {
        fprintf(stderr,
                "SVRT UI: active mode is %dx%d@%dHz; required %dx%d@%dHz\n",
                output_width, output_height, output_refresh,
                SVRT_DISPLAY_WIDTH, SVRT_DISPLAY_HEIGHT,
                SVRT_DISPLAY_REFRESH_HZ);
        svrt_ui_close(ui);
        return -1;
    }
#endif
    ui->panel = ui->renderer ? SDL_CreateTexture(
        ui->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        SVRT_PANEL_WIDTH, SVRT_PANEL_HEIGHT) : NULL;
    if (ui->panel) SDL_SetTextureBlendMode(ui->panel, SDL_BLENDMODE_BLEND);
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
    ui->left_eye_scene = ui->renderer ? SDL_CreateTexture(
        ui->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        output_width, output_height) : NULL;
#endif
    ui->font = TTF_OpenFont(SVRT_GUI_FONT_PATH, 42);
    ui->code_font = TTF_OpenFont(SVRT_GUI_FONT_PATH, 120);
    ui->small_font = TTF_OpenFont(SVRT_GUI_FONT_PATH, 24);
    if (SVRT_GUI_DASHBOARD_FONT_PATH[0])
        /* Steam Frame's dock labels are intentionally compact; 24px made the
           clock wider than its column on the 1600px VM scanout. */
        ui->dashboard_font = TTF_OpenFont(SVRT_GUI_DASHBOARD_FONT_PATH, 20);
    /* Only one animation decoder should be active at a time.  The Pi's
       stateless HEVC block has limited capture contexts; opening boot, loop,
       and Steam loading decoders together makes them starve each other. */
    ui->boot = video_open(SVRT_GUI_BOOT_PATH);
    if (!ui->boot)
        fprintf(stderr, "SVRT UI: boot animation unavailable at %s; continuing\n",
                SVRT_GUI_BOOT_PATH);
    if (!SVRT_UI_MINIMAL_STEAMOS)
        ui->background = video_open(SVRT_GUI_BACKGROUND_PATH);
    /* These are Valve's tiny dashboard pointer textures from the supplied
       SteamVR Linux dump. They are optional so a stripped developer image
       still falls back to the native vector cursor below. */
    if (SVRT_GUI_POINTER_PATH[0])
        ui->pointer = video_open(SVRT_GUI_POINTER_PATH);
    /* The default, circle, and I-beam images are distinct states in the
       SteamVR overlay. Load them independently: the circle is selected over
       dashboard chrome even when the default pointer loaded successfully. */
    if (SVRT_GUI_POINTER_CIRCLE_PATH[0])
        ui->pointer_circle = video_open(SVRT_GUI_POINTER_CIRCLE_PATH);
    if (SVRT_GUI_POINTER_IBEAM_PATH[0])
        ui->pointer_ibeam = video_open(SVRT_GUI_POINTER_IBEAM_PATH);
    if (SVRT_GUI_STEAMVR_ICON_DIR_PATH[0]) {
        static const char *const icon_names[SVRT_DASHBOARD_ICON_COUNT] = {
            "icon_home.png", "svr_library.png", "svr_store.png",
            "svr_people_a.png", "svr_items.png", "svr_desktop.png",
            "svr_settings.png", "svr_eye.png", "svr_power.png",
            "svr_controller.png", "vr_steam.png", "icon_add.png",
            "steamframe_vr.png", "svr_volume.png"};
        for (int icon = 0; icon < SVRT_DASHBOARD_ICON_COUNT; ++icon) {
            snprintf(ui->dashboard_icon_paths[icon],
                     sizeof(ui->dashboard_icon_paths[icon]), "%s/%s",
                     SVRT_GUI_STEAMVR_ICON_DIR_PATH, icon_names[icon]);
            ui->dashboard_icons[icon] =
                video_open(ui->dashboard_icon_paths[icon]);
        }
    }
    if (!ui->renderer || !ui->panel || !ui->font || !ui->code_font ||
        !ui->small_font ||
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
        !ui->left_eye_scene ||
#endif
        (!SVRT_UI_MINIMAL_STEAMOS && !ui->background)) {
        fprintf(stderr, "SVRT UI: window setup failed: %s\n", SDL_GetError());
        svrt_ui_close(ui); return -1;
    }
    fprintf(stderr, "SVRT UI READY %dx%d @ %dHz (target %dx%d @ %dHz)\n",
            output_width, output_height, output_refresh,
            SVRT_DISPLAY_WIDTH, SVRT_DISPLAY_HEIGHT,
            SVRT_DISPLAY_REFRESH_HZ);
    ui->state = SVRT_UI_SEARCHING;
    ui->state_started_ms = SDL_GetTicks();
    ui->boot_started_ms = ui->state_started_ms;
    return 0;
}

void svrt_ui_close(svrt_ui *ui) {
    if (!ui) return;
    if (ui->game_controller)
        SDL_GameControllerClose(ui->game_controller);
    video_close(&ui->boot); video_close(&ui->loop);
    video_close(&ui->steam_loading); video_close(&ui->background);
    video_close(&ui->avatar);
    video_close(&ui->pointer); video_close(&ui->pointer_circle);
    video_close(&ui->pointer_ibeam);
    for (int icon = 0; icon < SVRT_DASHBOARD_ICON_COUNT; ++icon)
        video_close(&ui->dashboard_icons[icon]);
    if (ui->code_font) TTF_CloseFont(ui->code_font);
    if (ui->small_font) TTF_CloseFont(ui->small_font);
    if (ui->dashboard_font) TTF_CloseFont(ui->dashboard_font);
    if (ui->font) TTF_CloseFont(ui->font);
    if (ui->left_eye_scene) SDL_DestroyTexture(ui->left_eye_scene);
    if (ui->panel) SDL_DestroyTexture(ui->panel);
    if (ui->renderer) SDL_DestroyRenderer(ui->renderer);
    if (ui->window) SDL_DestroyWindow(ui->window);
    if (ui->owns_sdl) { TTF_Quit(); SDL_Quit(); }
    memset(ui, 0, sizeof(*ui));
}

void svrt_ui_draw(svrt_ui *ui, svrt_ui_state state, const char code[5],
                  const char *hostname, const char *detail, uint32_t now_ms) {
    (void)hostname;
    if (!ui || !ui->renderer) return;
    if (state != ui->state) {
        ui->state = state; ui->state_started_ms = now_ms;
        if (state == SVRT_UI_SEARCHING) {
            ui->loop_first_frame_ms = 0;
            ui->loop_presented = 0;
            video_close(&ui->steam_loading);
            video_rewind(ui->loop);
        }
        if (state == SVRT_UI_AUTHORIZING || state == SVRT_UI_STARTING ||
            state == SVRT_UI_FAILED) {
            ui->loop_first_frame_ms = 0;
            video_close(&ui->loop);
            video_rewind(ui->steam_loading);
            if (ui->steam_loading) ui->steam_loading->loops_completed = 0;
        }
        if (state == SVRT_UI_HOME) {
            /* The first captured Steam frame replaces the transition scene;
               release its decoder before the steady-state UI continues. */
            video_close(&ui->loop);
            video_close(&ui->steam_loading);
        }
    }
    /* Apply the new state before hit-testing queued pointer events. This
       makes the rail/dock responsive on the first frame Steam becomes
       capturable instead of forwarding that click into the old transition
       state. */
    poll_ui_actions(ui);
    uint32_t elapsed = now_ms - ui->state_started_ms;
    const uint32_t boot_elapsed = now_ms - ui->boot_started_ms;
    /* Boot is global to the UI, not tied to the pairing state.  Discovery or
       authorization may advance state while it is still playing, but the
       boot movie must always run to its final frame before anything replaces
       it. */
    if (ui->boot && !ui->boot->ended) {
        /* Boot predates the dashboard scene and intentionally remains the
           original flat, full per-eye animation. */
        SDL_SetRenderTarget(ui->renderer, NULL);
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ui->renderer);
        draw_video_per_eye(ui, ui->boot, boot_elapsed,
                           ui->boot->codec->width, ui->boot->codec->height,
                           0, 0,
                           SVRT_BOOT_SCALE, SVRT_UI_HORIZONTAL_ASPECT);
        SDL_RenderPresent(ui->renderer);
        trace_render_present(ui, "boot");
        return;
    } else {
        if (ui->boot) {
            fprintf(stderr, "STEARLIGHT BOOT COMPLETE %s\n",
                    SVRT_GUI_BOOT_PATH);
            video_close(&ui->boot);
        }
        draw_panel_content(ui, state, code, detail, elapsed, 255);
    }
    /* Keep the previous status frame on scanout while the loop decoder is
       opened/primed. */
    if (state == SVRT_UI_SEARCHING &&
        (!ui->loop || !ui->loop->texture))
        return;
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ui->renderer);
    draw_scene(ui);
    if (state == SVRT_UI_SEARCHING && ui->loop && ui->loop->texture &&
        !ui->loop_presented) {
        ui->loop_presented = 1;
        fprintf(stderr, "STEARLIGHT LOOP PRESENTED %s\n",
                SVRT_GUI_LOOP_PATH);
    }
    SDL_RenderPresent(ui->renderer);
    trace_render_present(ui, "scene");
}

SDL_Window *svrt_ui_window(svrt_ui *ui) { return ui ? ui->window : NULL; }
SDL_Renderer *svrt_ui_renderer(svrt_ui *ui) { return ui ? ui->renderer : NULL; }
int svrt_ui_boot_finished(const svrt_ui *ui) {
    return !ui || !ui->boot || ui->boot->ended;
}
int svrt_ui_loop_transition_ready(const svrt_ui *ui, uint32_t now_ms) {
#if SVRT_UI_MINIMAL_STEAMOS
    return ui && ui->loop_presented && ui->loop_first_frame_ms &&
           (uint32_t)(now_ms - ui->loop_first_frame_ms) >=
               SVRT_STEAM_LOOP_HOLD_MS;
#else
    (void)ui;
    (void)now_ms;
    return 1;
#endif
}
void svrt_ui_set_client_frame(svrt_ui *ui, SDL_Texture *frame) {
    if (ui) ui->client_frame = frame;
}

void svrt_ui_set_streaming_mode(svrt_ui *ui, int enabled) {
    if (ui) ui->streaming_mode = enabled != 0;
}

void svrt_ui_set_pointer_style(svrt_ui *ui, svrt_ui_pointer_style style) {
    if (ui) ui->pointer_style = style;
}

void svrt_ui_set_input_callback(svrt_ui *ui, svrt_ui_input_callback callback,
                                void *opaque) {
    if (!ui) return;
    ui->input_callback = callback;
    ui->input_callback_opaque = opaque;
}

int svrt_ui_take_connection_request(svrt_ui *ui) {
    if (!ui) return 0;
    poll_ui_actions(ui);
    const int requested = ui->connection_requested;
    ui->connection_requested = 0;
    return requested;
}

svrt_ui_action svrt_ui_take_action(svrt_ui *ui) {
    if (!ui) return SVRT_UI_ACTION_NONE;
    poll_ui_actions(ui);
    const svrt_ui_action action = ui->pending_action;
    ui->pending_action = SVRT_UI_ACTION_NONE;
    return action;
}
