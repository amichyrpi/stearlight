if(NOT SVRT_BUILD_RECEIVER OR NOT UNIX)
  return()
endif()
include(GNUInstallDirs)
set(SVRT_RECEIVER_BOOT_ASSET "boot.mkv")
if(SVRT_STEARLIGHT_OS)
  set(SVRT_RECEIVER_BOOT_ASSET "os/os_boot.mp4")
endif()
add_executable(svrt-receiver
  main.c
  status.c status.h
  audio.c audio.h
  steam_client.c steam_client.h
  steam_link_pairing.c steam_link_pairing.h)
target_sources(svrt-receiver PRIVATE
  ../gui/pairing.c ../gui/pairing.h
  ../gui/ui.c ../gui/ui.h)
target_include_directories(svrt-receiver PRIVATE ../gui .)
target_compile_definitions(svrt-receiver PRIVATE
  SVRT_GUI_FONT_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/D-DINExp-Bold.otf"
  SVRT_GUI_BOOT_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/${SVRT_RECEIVER_BOOT_ASSET}"
  SVRT_GUI_LOOP_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/loop.mkv"
  SVRT_GUI_STEAM_LOADING_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/steam_loading.mkv"
  SVRT_GUI_BACKGROUND_PATH="${CMAKE_INSTALL_FULL_DATADIR}/svrt/assets/transition_background.png")
if(SVRT_STEARLIGHT_OS)
  target_compile_definitions(svrt-receiver PRIVATE
    SVRT_ENABLE_DEBUG_LEFT_EYE_UI=0
    SVRT_UI_MINIMAL_STEAMOS=1
    SVRT_STEAM_CAPTURE_INTERVAL_MS=16)
endif()
target_compile_features(svrt-receiver PRIVATE c_std_11)
find_package(Threads REQUIRED)
find_package(ALSA REQUIRED)
find_package(X11 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SVRT_SDL_TTF REQUIRED IMPORTED_TARGET SDL2_ttf)
target_link_libraries(svrt-receiver PRIVATE svrt::svrt Threads::Threads ALSA::ALSA X11::X11 PkgConfig::SVRT_SDL_TTF ihslib m)
if(SVRT_BUILD_TESTS)
  pkg_check_modules(SVRT_SDL REQUIRED IMPORTED_TARGET sdl2)
  add_executable(svrt-status-auth-test tests/status_auth_test.c status.c)
  target_include_directories(svrt-status-auth-test PRIVATE .)
  target_compile_features(svrt-status-auth-test PRIVATE c_std_11)
  target_link_libraries(svrt-status-auth-test PRIVATE
    svrt::svrt Threads::Threads m)
  add_test(NAME svrt-status-auth-test COMMAND svrt-status-auth-test)

  add_executable(svrt-gui-loadtest ../gui/tests/loadtest.c)
  target_include_directories(svrt-gui-loadtest PRIVATE
    ../gui ${SVRT_SDL_INCLUDE_DIRS} ${SVRT_SDL_TTF_INCLUDE_DIRS})
  target_compile_features(svrt-gui-loadtest PRIVATE c_std_11)
  target_link_libraries(svrt-gui-loadtest PRIVATE
    PkgConfig::SVRT_SDL PkgConfig::SVRT_SDL_TTF m)
  add_test(NAME svrt-gui-loadtest COMMAND svrt-gui-loadtest)

  add_executable(svrt-ui-transition-test
    ../gui/tests/transition_test.c ../gui/ui.c)
  target_include_directories(svrt-ui-transition-test PRIVATE
    ../gui ${SVRT_SDL_INCLUDE_DIRS} ${SVRT_SDL_TTF_INCLUDE_DIRS})
  target_compile_definitions(svrt-ui-transition-test PRIVATE
    SVRT_GUI_FONT_PATH="${CMAKE_SOURCE_DIR}/assets/D-DINExp-Bold.otf"
    SVRT_GUI_BOOT_PATH="${CMAKE_SOURCE_DIR}/assets/boot.mkv"
    SVRT_GUI_LOOP_PATH="${CMAKE_SOURCE_DIR}/assets/loop.mkv"
    SVRT_GUI_STEAM_LOADING_PATH="${CMAKE_SOURCE_DIR}/assets/steam_loading.mkv"
    SVRT_GUI_BACKGROUND_PATH="${CMAKE_SOURCE_DIR}/assets/transition_background.png")
  target_compile_features(svrt-ui-transition-test PRIVATE c_std_11)
  target_link_libraries(svrt-ui-transition-test PRIVATE
    svrt::svrt Threads::Threads PkgConfig::SVRT_SDL
    PkgConfig::SVRT_SDL_TTF m)
  add_test(NAME svrt-ui-transition-test COMMAND svrt-ui-transition-test)
  set_tests_properties(svrt-ui-transition-test PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()
install(TARGETS svrt-receiver RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS ihslib LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/svrt-receiver.service.in
  ${CMAKE_CURRENT_BINARY_DIR}/svrt-receiver.service
  @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/svrt-receiver.service
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/stearlight.service
  DESTINATION /etc/avahi/services)
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/force-5ghz.sh
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/svrt)
install(PROGRAMS ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/install-steam-arm64-pi.sh
                 ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/launch-steam-arm64-pi.sh
  DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/svrt)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../assets/
  DESTINATION ${CMAKE_INSTALL_DATADIR}/svrt/assets
  FILES_MATCHING PATTERN "*.mkv" PATTERN "*.mp4" PATTERN "*.otf"
                 PATTERN "transition_background.png")
