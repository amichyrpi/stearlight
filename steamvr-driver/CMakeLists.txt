if(NOT WIN32)
  return()
endif()

# Valve's SteamVR already contains the VRLink transport. A third-party
# resource-only package is optional: Steam Link selects its settings by the
# exact android/os/Build/PRODUCT value sent by the headset. Never ship a
# package with guessed aliases that can silently apply the wrong settings.
if(SVRT_BUILD_VRLINK_RESOURCES)
  if(NOT SVRT_VRLINK_PRODUCT)
    message(FATAL_ERROR
      "SVRT_VRLINK_PRODUCT must be set to the exact Steam Link headset product")
  endif()
  if(NOT SVRT_VRLINK_PRODUCT MATCHES "^[A-Za-z0-9._-]+$")
    message(FATAL_ERROR
      "SVRT_VRLINK_PRODUCT may contain only ASCII letters, digits, '.', '_' or '-'")
  endif()
  set(SVRT_VRLINK_PACKAGE_DIR "${CMAKE_BINARY_DIR}/svrt-vrlink")
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/default.vrsettings.in"
    "${CMAKE_CURRENT_BINARY_DIR}/svrt-vrlink-default.vrsettings" @ONLY)
  add_custom_target(svrt-vrlink-package ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "${SVRT_VRLINK_PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/svrt-vrlink"
            "${SVRT_VRLINK_PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${SVRT_VRLINK_PACKAGE_DIR}/resources/settings"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_CURRENT_BINARY_DIR}/svrt-vrlink-default.vrsettings"
            "${SVRT_VRLINK_PACKAGE_DIR}/resources/settings/default.vrsettings"
    COMMENT "Packaging optional Steam Frame/VRLink resources")
endif()

if(NOT SVRT_BUILD_DRIVER)
  return()
endif()

set(SVRT_OPENVR_INCLUDE "${CMAKE_SOURCE_DIR}/openvr/headers")
if(NOT EXISTS "${SVRT_OPENVR_INCLUDE}/openvr_driver.h")
  include(FetchContent)
  FetchContent_Declare(openvr_sdk
    GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
    GIT_TAG v2.15.6
    GIT_SHALLOW TRUE)
  FetchContent_GetProperties(openvr_sdk)
  if(NOT openvr_sdk_POPULATED)
    FetchContent_Populate(openvr_sdk)
  endif()
  set(SVRT_OPENVR_INCLUDE "${openvr_sdk_SOURCE_DIR}/headers")
endif()

add_library(driver_svrt SHARED
  driver.cpp
  audio.cpp audio.h
  direct_mode.cpp direct_mode.h
  receiver_link.cpp receiver_link.h
  ../common/mdns_discovery.cpp ../common/mdns_discovery.h)
target_compile_features(driver_svrt PRIVATE cxx_std_17)
target_include_directories(driver_svrt PRIVATE "${SVRT_OPENVR_INCLUDE}" ../common)
target_link_libraries(driver_svrt PRIVATE d3d11 dxgi ws2_32 dnsapi iphlpapi ole32 uuid advapi32 stearlight::protocol)
target_compile_definitions(driver_svrt PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(driver_svrt PROPERTIES
  PREFIX ""
  OUTPUT_NAME "driver_svrt"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/svrt/bin/win64>"
  LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/svrt/bin/win64>")
add_custom_command(TARGET driver_svrt POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E remove_directory
          ${CMAKE_BINARY_DIR}/svrt/resources
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/svrt
          ${CMAKE_BINARY_DIR}/svrt
  )
