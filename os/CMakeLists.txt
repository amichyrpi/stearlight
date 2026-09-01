include(GNUInstallDirs)

find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SVRT_SDL_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
pkg_check_modules(SVRT_AVCODEC REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil)

set(SVRT_OS_BOOT_ASSET "os/os_boot.mp4")
add_executable(stearlight-shell
  shell.c
  steam_client.c steam_client.h
  ../gui/ui.c ../gui/ui.h)
target_include_directories(stearlight-shell PRIVATE
  . ../gui)
target_compile_definitions(stearlight-shell PRIVATE
  SVRT_GUI_FONT_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/D-DINExp-Bold.otf"
  SVRT_GUI_BOOT_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/${SVRT_OS_BOOT_ASSET}"
  SVRT_GUI_LOOP_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/loop.mkv"
  SVRT_GUI_STEAM_LOADING_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/steam_loading.mkv"
  SVRT_GUI_BACKGROUND_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/transition_background.png"
  SVRT_ENABLE_DEBUG_LEFT_EYE_UI=0
  SVRT_UI_MINIMAL_STEAMOS=1
  SVRT_UI_FORCE_PANEL_STRIPS=1
  SVRT_SHOW_STEAM_STARTING_FALLBACK=0
  SVRT_STEAM_CAPTURE_INTERVAL_MS=16
  SVRT_DISPLAY_WIDTH=2880
  SVRT_DISPLAY_HEIGHT=1600
  SVRT_DISPLAY_REFRESH_HZ=60
  SVRT_ENFORCE_DISPLAY_MODE=1)
target_compile_features(stearlight-shell PRIVATE c_std_11)
target_link_libraries(stearlight-shell PRIVATE
  X11::X11 PkgConfig::SVRT_SDL_TTF PkgConfig::SVRT_AVCODEC m)

install(TARGETS stearlight-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../assets/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/svrt/assets
  FILES_MATCHING PATTERN "*.mkv" PATTERN "*.mp4" PATTERN "*.otf"
                 PATTERN "transition_background.png")
