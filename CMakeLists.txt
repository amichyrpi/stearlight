cmake_minimum_required(VERSION 3.22)
project(svrt LANGUAGES C)
if(WIN32)
  enable_language(CXX)
endif()
enable_testing()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(SVRT_BUILD_DRIVER "Build the SteamVR virtual-HMD driver" ON)
option(SVRT_BUILD_RECEIVER "Build the Raspberry Pi receiver" ON)
option(SVRT_BUILD_OS_SHELL
  "Build the standalone SteamOS shell without Steam Link receiver logic" OFF)
option(SVRT_BUILD_PI_LIBRARY "Build the Raspberry Pi HEVC/DRM library" ON)
option(SVRT_BUILD_TESTS "Build unit and integration tests" ON)
option(SVRT_BUILD_UTILITY "Build the Windows SVRT Utility" ON)
option(SVRT_STEARLIGHT_OS
  "Build the stereo, black-environment Stearlight OS shell" OFF)
set(SVRT_VENDORED_SDL_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|armv[67].*)$")
  set(SVRT_VENDORED_SDL_DEFAULT ON)
endif()
option(SVRT_BUILD_VENDORED_SDL "Build SDL2 with KMSDRM support like Vanilla"
  ${SVRT_VENDORED_SDL_DEFAULT})
add_subdirectory(protocol)
add_subdirectory(pipe)
add_subdirectory(lib)
if(SVRT_BUILD_DRIVER)
  add_subdirectory(steamvr-driver)
endif()
if(SVRT_BUILD_UTILITY AND WIN32)
  add_subdirectory(utility)
endif()
if(SVRT_BUILD_RECEIVER AND UNIX)
  include(FetchContent)
  set(IHSLIB_SAMPLES OFF CACHE BOOL "" FORCE)
  set(IHSLIB_CODEGEN OFF CACHE BOOL "" FORCE)
  set(_svrt_build_shared_libs ${BUILD_SHARED_LIBS})
  set(BUILD_SHARED_LIBS ON)
  FetchContent_Declare(ihslib
    GIT_REPOSITORY https://github.com/mariotaku/ihslib.git
    GIT_TAG 560908104b1f884792f90d8f98e33ec8c61106f5)
  FetchContent_MakeAvailable(ihslib)
  set(BUILD_SHARED_LIBS ${_svrt_build_shared_libs})
  set_target_properties(ihslib ihs-protobuf ihs-platforms PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
  # IHSlib commit 5609081 calls sqrt() from its video statistics module
  # without including math.h. Inject the standard declaration without
  # carrying a modified LGPL source fork. Its POSIX UDP backend also uses
  # struct timeval without including sys/time.h, which musl exposes less
  # permissively than glibc.
  target_compile_options(ihslib PRIVATE -include math.h)
  target_compile_options(ihs-platforms PRIVATE -include sys/time.h)
  if(TARGET OpenSSL::Crypto)
    target_link_libraries(ihslib PRIVATE OpenSSL::Crypto)
  endif()
  install(FILES "${ihslib_SOURCE_DIR}/LICENSE"
    DESTINATION "share/licenses/svrt"
    RENAME "IHSlib-LICENSE")
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "share/doc/svrt")
endif()
if(SVRT_BUILD_RECEIVER)
  add_subdirectory(pi-receiver)
endif()
if(SVRT_BUILD_OS_SHELL AND UNIX)
  add_subdirectory(os)
endif()
