#pragma once

#include <SDL.h>

/*
 * The Steam Frame dashboard is a small piece of shell chrome around the
 * captured Steam surface. Keep its geometry independent from the renderer so
 * pointer hit-testing and drawing always use the same rectangles.
 */
/* These are the dimensions used by Valve's shipped dashboard web UI before
 * the compositor applies meters_per_pixel.  Keeping them here makes the SDL
 * fallback use the same control-bar contract instead of inventing a second
 * set of proportions for the dock. */
#define SVRT_STEAMFRAME_CONTROL_BAR_WIDTH_PX 1800
#define SVRT_STEAMFRAME_STATUS_BAR_WIDTH_PX 1860
#define SVRT_STEAMFRAME_CONTROL_BAR_HEIGHT_PX 180
#define SVRT_STEAMFRAME_CONTROL_BUTTON_SIZE_PX 96
#define SVRT_STEAMFRAME_SMALL_BUTTON_SIZE_PX 85
#define SVRT_STEAMFRAME_GROUP_PADDING_PX 7
#define SVRT_STEAMFRAME_GROUP_RADIUS_PX 21
#define SVRT_STEAMFRAME_BUTTON_RADIUS_PX 16
#define SVRT_STEAMFRAME_METERS_PER_PIXEL (2.667f / 1694.0f)
#define SVRT_STEAMFRAME_DOCK_HEIGHT_RATIO 0.070f

#define SVRT_DASHBOARD_NAV_ITEMS 8

#define SVRT_DASHBOARD_HIT_NONE (-1)
#define SVRT_DASHBOARD_HIT_STEAM 100
#define SVRT_DASHBOARD_HIT_FRAME 101
#define SVRT_DASHBOARD_HIT_AVATAR 102
#define SVRT_DASHBOARD_HIT_BATTERY 103
#define SVRT_DASHBOARD_HIT_WIFI 104
#define SVRT_DASHBOARD_HIT_VIEW 105
#define SVRT_DASHBOARD_HIT_POWER 106
#define SVRT_DASHBOARD_HIT_ADD 107
#define SVRT_DASHBOARD_HIT_CHROME 108
#define SVRT_DASHBOARD_HIT_POPOVER 109

typedef struct svrt_dashboard_layout {
    SDL_Rect rail;
    SDL_Rect rail_items[SVRT_DASHBOARD_NAV_ITEMS];
    SDL_Rect power_button;
    SDL_Rect steam_tile;
    SDL_Rect status_bar;
    SDL_Rect control_group;
    SDL_Rect plus_button;
    SDL_Rect frame_button;
    SDL_Rect view_button;
    SDL_Rect clock_region;
    SDL_Rect device_region;
    SDL_Rect battery_region;
    SDL_Rect volume_region;
    SDL_Rect wifi_region;
    SDL_Rect notification_region;
    SDL_Rect avatar;
} svrt_dashboard_layout;

void svrt_dashboard_layout_make(int eye_x, int eye_width, int height,
                                float rail_width_ratio,
                                float rail_height_ratio,
                                float dock_y_ratio,
                                float dock_width_ratio,
                                svrt_dashboard_layout *layout);

int svrt_dashboard_layout_hit(const svrt_dashboard_layout *layout,
                             int x, int y);
