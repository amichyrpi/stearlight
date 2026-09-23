#include "dashboard_layout.h"

#include <math.h>
#include <string.h>

static int clamp_int(int value, int minimum, int maximum) {
    if (maximum < minimum) return maximum > 0 ? maximum : minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int rect_contains(const SDL_Rect *rect, int x, int y) {
    return rect && x >= rect->x && x < rect->x + rect->w &&
           y >= rect->y && y < rect->y + rect->h;
}

static SDL_Rect centered_rect(int center_x, int center_y, int width,
                              int height) {
    SDL_Rect rect = {
        center_x - width / 2,
        center_y - height / 2,
        width,
        height};
    return rect;
}

void svrt_dashboard_layout_make(int eye_x, int eye_width, int height,
                                float rail_width_ratio,
                                float rail_height_ratio,
                                float dock_y_ratio,
                                float dock_width_ratio,
                                svrt_dashboard_layout *layout) {
    if (!layout) return;
    memset(layout, 0, sizeof(*layout));
    if (eye_width <= 0 || height <= 0) return;

    const int rail_width = clamp_int(
        (int)lroundf(eye_width * rail_width_ratio), 48, eye_width / 3);
    const int rail_height = clamp_int(
        (int)lroundf(height * rail_height_ratio), 160, height - 24);
    layout->rail = (SDL_Rect){
        eye_x + (int)lroundf(eye_width * 0.060f),
        (height - rail_height) / 2 - height / 20,
        rail_width,
        rail_height};

    const int rail_padding = clamp_int(rail_width / 7, 6, 16);
    const int expanded_rail = rail_width_ratio > 0.10f;
    const int rail_slot_count = SVRT_DASHBOARD_NAV_ITEMS;
    const int item_height = (rail_height - rail_padding * 2) /
                            rail_slot_count;
    for (int item = 0; item < SVRT_DASHBOARD_NAV_ITEMS; ++item) {
        layout->rail_items[item] = (SDL_Rect){
            layout->rail.x + rail_padding / 2,
            layout->rail.y + rail_padding + item * item_height,
            layout->rail.w - rail_padding,
            item_height};
    }
    /* Steam Frame keeps Power in its own tile below the navigation rail.  It
       does not consume a ninth slot or squeeze the eight navigation icons. */
    const int power_gap = clamp_int(rail_width / 8, 6, 14);
    layout->power_button = (SDL_Rect){
        layout->rail.x,
        layout->rail.y + layout->rail.h + power_gap,
        layout->rail.w,
        expanded_rail ? clamp_int(item_height, 44, height / 8) : rail_width};

    const int dock_height = clamp_int(
        (int)lroundf(height * SVRT_STEAMFRAME_DOCK_HEIGHT_RATIO),
        48, height / 6);
    const int status_width = clamp_int(
        (int)lroundf(eye_width * dock_width_ratio), dock_height * 3,
        eye_width - dock_height * 2 - 24);
    /* Valve leaves roughly one fifth of a control-bar height between the
       Steam tile and the long status group (36 px at the native 180 px
       control-bar scale). */
    const int dock_gap = clamp_int(dock_height / 5, 10, 28);
    const int dock_y = clamp_int(
        (int)lroundf(height * dock_y_ratio), 0, height - dock_height);
    /* Center the complete dock, not only the long status section. This is
       the small rightward offset visible in the reference: the Steam tile
       hangs to the left of the status section. */
    const int status_left = eye_x +
                            (eye_width - status_width + dock_gap +
                             dock_height) / 2;
    layout->steam_tile = (SDL_Rect){
        status_left - dock_gap - dock_height,
        dock_y,
        dock_height,
        dock_height};
    layout->status_bar = (SDL_Rect){
        status_left,
        dock_y,
        status_width,
        dock_height};

    const int button_size = clamp_int(
        (int)lroundf((float)dock_height *
                     SVRT_STEAMFRAME_SMALL_BUTTON_SIZE_PX /
                     SVRT_STEAMFRAME_CONTROL_BAR_HEIGHT_PX),
        32, dock_height - 8);
    /* Steam Frame's status panel uses the dashboard side/component padding
       (1rem + 21px at the source 180px scale), rather than the 7px inner
       ControlBarGroup padding.  Applying that metric here keeps the add
       control and avatar away from the rounded panel edges at headset scale. */
    const int inset = clamp_int(
        (int)lroundf((float)dock_height * (16.0f + 21.0f) /
                     SVRT_STEAMFRAME_CONTROL_BAR_HEIGHT_PX),
        8, 24);
    const int inner = dock_height - inset * 2;
    const int avatar_size = clamp_int((int)lroundf(dock_height * 0.68f),
                                      28, inner);
    layout->plus_button = centered_rect(
        layout->status_bar.x + (int)lroundf(layout->status_bar.w * 0.05f),
        layout->status_bar.y + dock_height / 2,
        button_size,
        button_size);
    layout->avatar = (SDL_Rect){
        layout->status_bar.x + layout->status_bar.w - inset - avatar_size,
        layout->status_bar.y + (dock_height - avatar_size) / 2,
        avatar_size,
        avatar_size};

    const int dock_center_y = layout->status_bar.y + dock_height / 2;
    /* Leave deliberate columns for the clock, battery, Wi-Fi and avatar.  The
       hit regions remain generous, while ui.c renders the status glyphs at a
       smaller optical size inside them. */
    const int clock_width = (int)lroundf(layout->status_bar.w * 0.18f);
    layout->clock_region = (SDL_Rect){
        layout->status_bar.x + (int)lroundf(layout->status_bar.w * 0.65f),
        layout->status_bar.y + inset,
        clock_width,
        inner / 2};

    const int frame_center = layout->status_bar.x +
                             (int)lroundf(layout->status_bar.w * 0.37f);
    const int view_center = layout->status_bar.x +
                            (int)lroundf(layout->status_bar.w * 0.46f);
    layout->frame_button = centered_rect(frame_center,
                                         layout->status_bar.y + dock_height / 2,
                                         button_size, button_size);
    layout->view_button = centered_rect(view_center,
                                        layout->status_bar.y + dock_height / 2,
                                        button_size, button_size);
    /* The Add control belongs to the left section.  Only the Frame and View
       controls sit in Valve's rounded center group. */
    const int group_left = layout->frame_button.x - inset;
    const int group_right = layout->view_button.x + layout->view_button.w +
                            inset;
    layout->control_group = (SDL_Rect){
        group_left,
        layout->status_bar.y + inset,
        group_right - group_left,
        dock_height - inset * 2};
    layout->device_region = centered_rect(
        layout->status_bar.x + (int)lroundf(layout->status_bar.w * 0.56f),
        dock_center_y,
        inner * 2 / 3, inner);
    layout->battery_region = centered_rect(
        layout->status_bar.x + (int)lroundf(layout->status_bar.w * 0.69f),
        layout->status_bar.y + dock_height * 3 / 4,
        inner, inner / 2);
    layout->volume_region = centered_rect(
        layout->status_bar.x + (int)lroundf(layout->status_bar.w * 0.76f),
        layout->status_bar.y + dock_height * 3 / 4,
        inner / 2, inner / 2);
    layout->wifi_region = centered_rect(
        layout->status_bar.x + (int)lroundf(layout->status_bar.w * 0.80f),
        layout->status_bar.y + dock_height * 3 / 4,
        inner / 2, inner / 2);
    layout->notification_region = centered_rect(
        layout->status_bar.x + (int)lroundf(layout->status_bar.w * 0.84f),
        layout->status_bar.y + dock_height * 3 / 4,
        inner / 2, inner / 2);
}

int svrt_dashboard_layout_hit(const svrt_dashboard_layout *layout,
                             int x, int y) {
    if (!layout) return SVRT_DASHBOARD_HIT_NONE;
    for (int item = 0; item < SVRT_DASHBOARD_NAV_ITEMS; ++item)
        if (rect_contains(&layout->rail_items[item], x, y)) return item;
    if (rect_contains(&layout->steam_tile, x, y))
        return SVRT_DASHBOARD_HIT_STEAM;
    if (rect_contains(&layout->plus_button, x, y))
        return SVRT_DASHBOARD_HIT_ADD;
    if (rect_contains(&layout->frame_button, x, y))
        return SVRT_DASHBOARD_HIT_FRAME;
    if (rect_contains(&layout->battery_region, x, y))
        return SVRT_DASHBOARD_HIT_BATTERY;
    if (rect_contains(&layout->wifi_region, x, y))
        return SVRT_DASHBOARD_HIT_WIFI;
    if (rect_contains(&layout->view_button, x, y))
        return SVRT_DASHBOARD_HIT_VIEW;
    if (rect_contains(&layout->power_button, x, y))
        return SVRT_DASHBOARD_HIT_POWER;
    if (rect_contains(&layout->avatar, x, y))
        return SVRT_DASHBOARD_HIT_AVATAR;
    /* Chrome occludes the captured Steam surface. Consume gaps as well as
       buttons so XTest never clicks an invisible control underneath it. */
    if (rect_contains(&layout->rail, x, y) ||
        rect_contains(&layout->status_bar, x, y) ||
        rect_contains(&layout->power_button, x, y))
        return SVRT_DASHBOARD_HIT_CHROME;
    return SVRT_DASHBOARD_HIT_NONE;
}
