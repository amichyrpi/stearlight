include(GNUInstallDirs)

find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
find_library(SVRT_XTST_LIBRARY Xtst REQUIRED)
find_library(SVRT_XFIXES_LIBRARY Xfixes REQUIRED)
pkg_check_modules(SVRT_SDL_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SVRT_AVCODEC REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil)

set(SVRT_OS_BOOT_ASSET "boot.mkv")
add_executable(stearlight-shell
  shell.c
  steam_client.c steam_client.h
  ../gui/ui.c ../gui/ui.h
  ../gui/dashboard_layout.c ../gui/dashboard_layout.h)
target_include_directories(stearlight-shell PRIVATE
  . ../gui)
target_compile_definitions(stearlight-shell PRIVATE
  SVRT_GUI_FONT_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/D-DINExp-Bold.otf"
  SVRT_GUI_BOOT_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/${SVRT_OS_BOOT_ASSET}"
  SVRT_GUI_LOOP_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/loop.mkv"
  SVRT_GUI_STEAM_LOADING_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/steam_loading.mkv"
  SVRT_GUI_BACKGROUND_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/transition_background.png"
  SVRT_GUI_POINTER_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/steamframe-native/laser_pointer/overlay_pointer.png"
  SVRT_GUI_POINTER_CIRCLE_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/steamframe-native/laser_pointer/overlay_pointer_circle.png"
  SVRT_GUI_POINTER_IBEAM_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/steamframe-native/laser_pointer/overlay_pointer_ibeam.png"
  SVRT_GUI_DASHBOARD_FONT_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/steamvr-dashboard/motiva-sans-regular.ttf"
  SVRT_GUI_STEAMVR_ICON_DIR_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/steamvr-native/icons"
  SVRT_PANEL_WIDTH=${SVRT_STEAM_SURFACE_WIDTH}
  SVRT_PANEL_HEIGHT=${SVRT_STEAM_SURFACE_HEIGHT}
  STEARLIGHT_STEAM_WIDTH=${SVRT_STEAM_SURFACE_WIDTH}
  STEARLIGHT_STEAM_HEIGHT=${SVRT_STEAM_SURFACE_HEIGHT}
  SVRT_UI_PANEL_ARC_DEGREES=${SVRT_UI_PANEL_ARC_DEGREES}f
  SVRT_UI_PANEL_HEIGHT_METERS=${SVRT_UI_PANEL_HEIGHT_METERS}f
  SVRT_UI_DASHBOARD_RAIL_WIDTH_RATIO=${SVRT_UI_DASHBOARD_RAIL_WIDTH_RATIO}f
  SVRT_UI_DASHBOARD_RAIL_EXPANDED_WIDTH_RATIO=${SVRT_UI_DASHBOARD_RAIL_EXPANDED_WIDTH_RATIO}f
  SVRT_UI_DASHBOARD_RAIL_HEIGHT_RATIO=${SVRT_UI_DASHBOARD_RAIL_HEIGHT_RATIO}f
  SVRT_UI_DASHBOARD_DOCK_Y_RATIO=${SVRT_UI_DASHBOARD_DOCK_Y_RATIO}f
  SVRT_UI_DASHBOARD_DOCK_WIDTH_RATIO=${SVRT_UI_DASHBOARD_DOCK_WIDTH_RATIO}f
  SVRT_ENABLE_DEBUG_LEFT_EYE_UI=0
  SVRT_UI_MINIMAL_STEAMOS=1
  # Steam's -steamframe page is only the content surface.  The rail, bottom
  # dock, laser and their input state belong to Valve's VRDashboardBar,
  # created by vrserver/vrdashboard.  The standalone SDL shell must not draw
  # a competing approximation in the production image; the layout test below
  # remains useful for geometry checks but never owns the runtime overlay.
  SVRT_UI_SHOW_NAV_CHROME=0
  SVRT_UI_FORCE_PANEL_STRIPS=0
  SVRT_UI_NATIVE_DASHBOARD=1
  SVRT_SHOW_STEAM_STARTING_FALLBACK=0
  SVRT_STEAM_LOOP_HOLD_MS=1500
  SVRT_STEAM_CAPTURE_INTERVAL_MS=16
  SVRT_DISPLAY_WIDTH=2880
  SVRT_DISPLAY_HEIGHT=1600
  SVRT_DISPLAY_REFRESH_HZ=60
  SVRT_ENFORCE_DISPLAY_MODE=1)
target_compile_features(stearlight-shell PRIVATE c_std_11)
target_link_libraries(stearlight-shell PRIVATE
  X11::X11 Xcomposite ${SVRT_XTST_LIBRARY} ${SVRT_XFIXES_LIBRARY}
  PkgConfig::SVRT_SDL_TTF PkgConfig::SVRT_AVCODEC m)

if(SVRT_BUILD_TESTS)
  add_executable(svrt-dashboard-layout-test
    ../gui/tests/dashboard_layout_test.c ../gui/dashboard_layout.c)
  target_include_directories(svrt-dashboard-layout-test PRIVATE ../gui)
  target_compile_features(svrt-dashboard-layout-test PRIVATE c_std_11)
  target_link_libraries(svrt-dashboard-layout-test PRIVATE
    PkgConfig::SVRT_SDL_TTF m)
  add_test(NAME svrt-dashboard-layout COMMAND svrt-dashboard-layout-test)
endif()

install(TARGETS stearlight-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../assets/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/svrt/assets
  FILES_MATCHING PATTERN "*.mkv" PATTERN "*.otf"
                 PATTERN "transition_background.png")
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../assets/steamvr-dashboard/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/svrt/assets/steamvr-dashboard
  FILES_MATCHING PATTERN "*.png" PATTERN "*.ttf")
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../assets/steamframe-native/webinterface/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/svrt/assets/steamframe-native/webinterface
  FILES_MATCHING
    PATTERN "*.html" PATTERN "*.js" PATTERN "*.css" PATTERN "*.json"
    PATTERN "*.png" PATTERN "*.svg" PATTERN "*.ttf" PATTERN "*.woff"
    PATTERN "*.woff2")
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../assets/steamframe-native/laser_pointer/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/svrt/assets/steamframe-native/laser_pointer
  FILES_MATCHING PATTERN "*.png" PATTERN "*.tga" PATTERN "*.obj" PATTERN "*.mtl")
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../assets/D-DINExp-Bold.otf
        DESTINATION ${CMAKE_INSTALL_DATADIR}/fonts/stearlight)
