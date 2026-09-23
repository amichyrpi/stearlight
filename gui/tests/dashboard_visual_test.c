/* Deterministic software-rendered dashboard snapshot used for visual
 * comparison with the supplied Steam Frame references. This intentionally
 * includes the renderer implementation so the private chrome drawing path
 * is exercised without booting Steam or a VM. */
#define _POSIX_C_SOURCE 200809L
#define SVRT_GUI_FONT_PATH ""
#define SVRT_GUI_BOOT_PATH ""
#define SVRT_GUI_LOOP_PATH ""
#define SVRT_GUI_BACKGROUND_PATH ""
#define SVRT_GUI_STEAM_LOADING_PATH ""
#define SVRT_UI_MINIMAL_STEAMOS 1
#define SVRT_UI_SHOW_NAV_CHROME 1
#define SVRT_ENABLE_DEBUG_LEFT_EYE_UI 0
#include "../ui.c"

int main(int argc, char **argv) {
    const char *output = argc > 1 ? argv[1] : "/tmp/dashboard.bmp";
    const char *icon_dir = argc > 2 ? argv[2] : "/ui-assets/icons";
    if (SDL_Init(SDL_INIT_VIDEO) || TTF_Init()) return 1;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, 2880, 1600, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *renderer = surface ? SDL_CreateSoftwareRenderer(surface) : NULL;
    if (!surface || !renderer) return 2;

    svrt_ui ui;
    memset(&ui, 0, sizeof(ui));
    ui.renderer = renderer;
    ui.state = SVRT_UI_HOME;
    ui.selected_page = 0;
    ui.cursor_visible = 1;
    ui.cursor_x = 500;
    ui.cursor_y = 620;
    if (argc > 3 && strcmp(argv[3], "performance") == 0)
        ui.dashboard_popover = SVRT_DASHBOARD_POPOVER_PERFORMANCE;
    else if (argc > 3 && strcmp(argv[3], "battery") == 0)
        ui.dashboard_popover = SVRT_DASHBOARD_POPOVER_BATTERY;
    else if (argc > 3 && strcmp(argv[3], "power") == 0)
        ui.dashboard_popover = SVRT_DASHBOARD_POPOVER_POWER;
    ui.panel = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_TARGET,
                                 SVRT_PANEL_WIDTH, SVRT_PANEL_HEIGHT);
    SDL_SetTextureBlendMode(ui.panel, SDL_BLENDMODE_BLEND);
    ui.dashboard_font = TTF_OpenFont(
        "/staging/usr/share/svrt/assets/steamvr-dashboard/"
        "motiva-sans-regular.ttf", 20);
    ui.small_font = TTF_OpenFont(
        "/staging/usr/share/fonts/stearlight/D-DINExp-Bold.otf", 24);
    ui.font = TTF_OpenFont(
        "/staging/usr/share/fonts/stearlight/D-DINExp-Bold.otf", 42);
    ui.pointer = video_open(
        "/staging/usr/share/svrt/assets/steamframe-native/laser_pointer/"
        "overlay_pointer.png");
    ui.pointer_circle = video_open(
        "/staging/usr/share/svrt/assets/steamframe-native/laser_pointer/"
        "overlay_pointer_circle.png");
    static const char *const icon_names[SVRT_DASHBOARD_ICON_COUNT] = {
        "icon_home.png", "svr_library.png", "svr_store.png",
        "svr_people_a.png", "svr_items.png", "svr_desktop.png",
        "svr_settings.png", "svr_eye.png", "svr_power.png",
        "svr_controller.png", "vr_steam.png", "icon_add.png",
        "steamframe_vr.png"};
    for (int icon = 0; icon < SVRT_DASHBOARD_ICON_COUNT; ++icon) {
        snprintf(ui.dashboard_icon_paths[icon],
                 sizeof(ui.dashboard_icon_paths[icon]), "%s/%s",
                 icon_dir, icon_names[icon]);
        ui.dashboard_icons[icon] = video_open(ui.dashboard_icon_paths[icon]);
        if (ui.dashboard_icons[icon])
            video_decode_next(ui.dashboard_icons[icon], renderer);
    }
    if (ui.pointer) video_decode_next(ui.pointer, renderer);
    if (ui.pointer_circle) video_decode_next(ui.pointer_circle, renderer);

    SDL_SetRenderTarget(renderer, ui.panel);
    SDL_SetRenderDrawColor(renderer, 14, 20, 27, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderTarget(renderer, NULL);
    /* The software backend lazily initializes texture formats the first time
       a font size is used. Render a warm-up frame so the saved frame matches
       the steady-state hardware renderer used by the appliance. */
    for (int frame = 0; frame < 2; ++frame) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        for (int eye = 0; eye < 2; ++eye) {
            const int eye_x = eye * 1440;
            draw_curved_panel_eye(&ui, eye_x, 1440, 1600, eye);
            draw_dashboard_chrome_eye(&ui, eye_x, 1440, 1600);
            draw_laser_cursor_eye(&ui, eye_x, 1440, 1600);
        }
        SDL_RenderPresent(renderer);
    }
    /* Read the renderer rather than saving its backing surface directly.
       Target-texture switches are otherwise not reflected consistently by
       SDL's software backend, which made the chrome backgrounds disappear
       from the visual-regression artifact. */
    SDL_Surface *capture = SDL_CreateRGBSurfaceWithFormat(
        0, 2880, 1600, 32, SDL_PIXELFORMAT_ARGB8888);
    int result = capture ? SDL_RenderReadPixels(
        renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
        capture->pixels, capture->pitch) : -1;
    if (!result) result = SDL_SaveBMP(capture, output);

    svrt_dashboard_layout layout;
    dashboard_make_layout(&ui, 0, 1440, 1600, &layout);
    ui.dashboard_popover = SVRT_DASHBOARD_POPOVER_NONE;
    ui.pending_action = SVRT_UI_ACTION_NONE;
    handle_local_dashboard_click(
        &ui, layout.frame_button.x + layout.frame_button.w / 2,
        layout.frame_button.y + layout.frame_button.h / 2, 2880, 1600);
    if (ui.dashboard_popover != SVRT_DASHBOARD_POPOVER_PERFORMANCE)
        result = -1;
    SDL_Rect popover = dashboard_popover_rect(
        &layout, ui.dashboard_popover);
    handle_local_dashboard_click(
        &ui, popover.x + popover.w * 7 / 10,
        popover.y + popover.h - 2, 2880, 1600);
    if (ui.dashboard_popover != SVRT_DASHBOARD_POPOVER_BATTERY)
        result = -1;
    ui.dashboard_popover = SVRT_DASHBOARD_POPOVER_NONE;
    handle_local_dashboard_click(
        &ui, layout.view_button.x + layout.view_button.w / 2,
        layout.view_button.y + layout.view_button.h / 2, 2880, 1600);
    if (!ui.view_enabled) result = -1;
    handle_local_dashboard_click(
        &ui, layout.power_button.x + layout.power_button.w / 2,
        layout.power_button.y + layout.power_button.h / 2, 2880, 1600);
    if (ui.dashboard_popover != SVRT_DASHBOARD_POPOVER_POWER)
        result = -1;
    popover = dashboard_popover_rect(&layout, ui.dashboard_popover);
    handle_local_dashboard_click(
        &ui, popover.x + popover.w / 2, popover.y + 60, 2880, 1600);
    if (ui.pending_action != SVRT_UI_ACTION_SUSPEND)
        result = -1;

    video_close(&ui.pointer);
    video_close(&ui.pointer_circle);
    for (int icon = 0; icon < SVRT_DASHBOARD_ICON_COUNT; ++icon)
        video_close(&ui.dashboard_icons[icon]);
    SDL_DestroyTexture(ui.panel);
    if (ui.dashboard_font) TTF_CloseFont(ui.dashboard_font);
    if (ui.small_font) TTF_CloseFont(ui.small_font);
    if (ui.font) TTF_CloseFont(ui.font);
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(capture);
    SDL_FreeSurface(surface);
    TTF_Quit();
    SDL_Quit();
    return result ? 3 : 0;
}
