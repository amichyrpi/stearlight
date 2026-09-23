#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>

/* The private Steam surface and the rendered panel deliberately share one
 * contract. The OS build can override these from CMake when a different
 * gamescope/Xvfb surface is needed for a headset profile. */
#ifndef SVRT_PANEL_WIDTH
#define SVRT_PANEL_WIDTH 1024
#endif
#ifndef SVRT_PANEL_HEIGHT
#define SVRT_PANEL_HEIGHT 640
#endif
#ifndef SVRT_GUI_POINTER_PATH
#define SVRT_GUI_POINTER_PATH ""
#endif
#ifndef SVRT_GUI_POINTER_CIRCLE_PATH
#define SVRT_GUI_POINTER_CIRCLE_PATH ""
#endif
#ifndef SVRT_GUI_POINTER_IBEAM_PATH
#define SVRT_GUI_POINTER_IBEAM_PATH ""
#endif
#ifndef SVRT_GUI_DASHBOARD_FONT_PATH
#define SVRT_GUI_DASHBOARD_FONT_PATH ""
#endif
#ifndef SVRT_GUI_STEAMVR_ICON_DIR_PATH
#define SVRT_GUI_STEAMVR_ICON_DIR_PATH ""
#endif

/* SteamVR's exported dashboard glyphs are optional for the generic receiver
 * build; the UI keeps a vector fallback when the private asset set is absent. */
#define SVRT_DASHBOARD_ICON_COUNT 14

/* Dashboard calibration is kept as ratios so the same composition scales
 * from the VM window to the 1440x1600-per-eye Pi scanout. The defaults are
 * measured from the Steam Frame dashboard reference: a narrow left rail,
 * a bottom control bar, and a little black breathing room around both. */
#ifndef SVRT_UI_DASHBOARD_RAIL_WIDTH_RATIO
#define SVRT_UI_DASHBOARD_RAIL_WIDTH_RATIO 0.047f
#endif
#ifndef SVRT_UI_DASHBOARD_RAIL_EXPANDED_WIDTH_RATIO
#define SVRT_UI_DASHBOARD_RAIL_EXPANDED_WIDTH_RATIO 0.185f
#endif
#ifndef SVRT_UI_DASHBOARD_RAIL_EXPAND_TIMEOUT_MS
#define SVRT_UI_DASHBOARD_RAIL_EXPAND_TIMEOUT_MS 1500u
#endif
#ifndef SVRT_UI_DASHBOARD_RAIL_HEIGHT_RATIO
#define SVRT_UI_DASHBOARD_RAIL_HEIGHT_RATIO 0.43f
#endif
#ifndef SVRT_UI_DASHBOARD_DOCK_Y_RATIO
#define SVRT_UI_DASHBOARD_DOCK_Y_RATIO 0.807f
#endif
#ifndef SVRT_UI_DASHBOARD_DOCK_WIDTH_RATIO
#define SVRT_UI_DASHBOARD_DOCK_WIDTH_RATIO 0.54f
#endif

/* The Steam Frame screenshots use a wider curved panel than the old
 * receiver scene. Keep the projection tunable without changing the private
 * Steam surface's 16:10 contract. */
#ifndef SVRT_UI_PANEL_ARC_DEGREES
#define SVRT_UI_PANEL_ARC_DEGREES 50.0f
#endif
#ifndef SVRT_UI_PANEL_HEIGHT_METERS
#define SVRT_UI_PANEL_HEIGHT_METERS 2.73f
#endif
#ifndef SVRT_UI_LASER_ORIGIN_X_RATIO
#define SVRT_UI_LASER_ORIGIN_X_RATIO 0.18f
#endif
#ifndef SVRT_UI_LASER_ORIGIN_Y_RATIO
#define SVRT_UI_LASER_ORIGIN_Y_RATIO 1.04f
#endif

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
#ifndef SVRT_UI_SHOW_NAV_CHROME
#define SVRT_UI_SHOW_NAV_CHROME 0
#endif
#ifndef SVRT_UI_FORCE_PANEL_STRIPS
#define SVRT_UI_FORCE_PANEL_STRIPS 0
#endif
/* When Valve's vrserver/vrdashboard owns the compositor overlay, the SDL
 * shell is only a Steam page-surface bridge and must not draw a second laser
 * or cursor on top of it. */
#ifndef SVRT_UI_NATIVE_DASHBOARD
#define SVRT_UI_NATIVE_DASHBOARD 0
#endif

/* The standalone OS must expose Valve's own first-run surface.  The generic
 * receiver UI may opt into a local transition, but it is disabled for the OS
 * target so a missing Steam frame never gets mistaken for a welcome screen. */
#ifndef SVRT_SHOW_STEAM_STARTING_FALLBACK
#define SVRT_SHOW_STEAM_STARTING_FALLBACK 1
#endif
#ifndef SVRT_STEAM_LOOP_HOLD_MS
#define SVRT_STEAM_LOOP_HOLD_MS 0
#endif

/* The headset scanout is a side-by-side stereo surface.  Keep the geometry
 * in one place so the renderer, the VM display and the Pi KMS session agree
 * on the per-eye size and refresh target. */
#ifndef SVRT_DISPLAY_WIDTH
#define SVRT_DISPLAY_WIDTH 2880
#endif
#ifndef SVRT_DISPLAY_HEIGHT
#define SVRT_DISPLAY_HEIGHT 1600
#endif
#ifndef SVRT_DISPLAY_REFRESH_HZ
#define SVRT_DISPLAY_REFRESH_HZ 60
#endif
#ifndef SVRT_ENFORCE_DISPLAY_MODE
#define SVRT_ENFORCE_DISPLAY_MODE 0
#endif
#define SVRT_UI_FRAME_INTERVAL_NS \
    (1000000000L / SVRT_DISPLAY_REFRESH_HZ)

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
    SVRT_UI_ACTION_MEDIA,
    SVRT_UI_ACTION_DOWNLOADS,
    SVRT_UI_ACTION_SETTINGS,
    SVRT_UI_ACTION_FRAME,
    SVRT_UI_ACTION_POWER,
    SVRT_UI_ACTION_PROFILE,
    SVRT_UI_ACTION_CONNECTION,
    SVRT_UI_ACTION_ADD,
    SVRT_UI_ACTION_NETWORK,
    SVRT_UI_ACTION_SUSPEND,
    SVRT_UI_ACTION_RESTART,
    SVRT_UI_ACTION_SHUTDOWN
} svrt_ui_action;

typedef enum svrt_ui_pointer_style {
    SVRT_UI_POINTER_DEFAULT,
    SVRT_UI_POINTER_IBEAM
} svrt_ui_pointer_style;

/* Input generated by the SDL shell.  The screen coordinates are useful for
 * rendering the laser pointer, while surface_x/surface_y are the same
 * coordinates that the captured Steam surface uses.  Keeping this small
 * transport-neutral record in the UI layer lets the OS bridge forward input
 * to Valve's X11 client without making gui/ depend on Xlib. */
typedef enum svrt_ui_input_type {
    SVRT_UI_INPUT_MOUSE_MOTION,
    SVRT_UI_INPUT_MOUSE_BUTTON_DOWN,
    SVRT_UI_INPUT_MOUSE_BUTTON_UP,
    SVRT_UI_INPUT_MOUSE_WHEEL,
    SVRT_UI_INPUT_KEY_DOWN,
    SVRT_UI_INPUT_KEY_UP
} svrt_ui_input_type;

typedef struct svrt_ui_input {
    svrt_ui_input_type type;
    int screen_x;
    int screen_y;
    int screen_width;
    int screen_height;
    int delta_x;
    int delta_y;
    int button;
    int wheel_y;
    int keycode;
    int scancode;
    int modifiers;
    int on_surface;
    int surface_x;
    int surface_y;
    int surface_width;
    int surface_height;
} svrt_ui_input;

typedef void (*svrt_ui_input_callback)(const svrt_ui_input *input,
                                       void *opaque);

typedef struct svrt_ui_video svrt_ui_video;

typedef struct svrt_ui {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    TTF_Font *code_font;
    TTF_Font *small_font;
    TTF_Font *dashboard_font;
    svrt_ui_video *boot;
    svrt_ui_video *loop;
    svrt_ui_video *steam_loading;
    svrt_ui_video *background;
    svrt_ui_video *avatar;
    svrt_ui_video *pointer;
    svrt_ui_video *pointer_circle;
    svrt_ui_video *pointer_ibeam;
    svrt_ui_video *dashboard_icons[SVRT_DASHBOARD_ICON_COUNT];
    char dashboard_icon_paths[SVRT_DASHBOARD_ICON_COUNT][512];
    SDL_Texture *panel;
    SDL_Texture *left_eye_scene;
    SDL_Texture *client_frame;
    SDL_GameController *game_controller;
    SDL_JoystickID gamepad_instance_id;
    int connection_requested;
    int streaming_mode;
    int selected_page;
    int rail_expanded;
    uint32_t rail_expand_until_ms;
    int dashboard_popover;
    int performance_basic_view;
    int manual_gpu_clock;
    int performance_gpu_mhz;
    int dashboard_pressed_hit;
    int view_enabled;
    svrt_ui_pointer_style pointer_style;
    int cursor_x;
    int cursor_y;
    int cursor_visible;
    int16_t gamepad_left_x;
    int16_t gamepad_left_y;
    int16_t gamepad_right_y;
    uint32_t next_gamepad_cursor_ms;
    uint32_t next_gamepad_wheel_ms;
    svrt_ui_input_callback input_callback;
    void *input_callback_opaque;
    svrt_ui_action pending_action;
    uint32_t next_avatar_scan_ms;
    char avatar_path[512];
    svrt_ui_state state;
    uint32_t state_started_ms;
    uint32_t boot_started_ms;
    /* Monotonic UI trace points.  They are intentionally public so the Pi
       transition test and the receiver log can verify ordering/timing. */
    uint32_t loop_first_frame_ms;
    int loop_presented;
    /* Optional one-line-per-second render-rate trace for hardware/VM
       profiling. It is disabled unless SVRT_TRACE_UI_FPS is set. */
    uint32_t trace_window_start_ms;
    uint32_t trace_frame_count;
    int trace_ui_fps;
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
void svrt_ui_set_pointer_style(svrt_ui *ui, svrt_ui_pointer_style style);
void svrt_ui_set_input_callback(svrt_ui *ui, svrt_ui_input_callback callback,
                                void *opaque);
/* Return non-zero once the userspace boot movie has reached its final frame.
 * The standalone OS uses this to hand DRM ownership to gamescope without
 * skipping the stereo boot animation. */
int svrt_ui_boot_finished(const svrt_ui *ui);
/* Return non-zero after the loop movie has been presented for the configured
 * transition hold.  Steam may have a captured frame before boot.mkv ends; the
 * shell still needs to show loop.mkv after boot before handing off to Steam. */
int svrt_ui_loop_transition_ready(const svrt_ui *ui, uint32_t now_ms);
int svrt_ui_take_connection_request(svrt_ui *ui);
svrt_ui_action svrt_ui_take_action(svrt_ui *ui);
