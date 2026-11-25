#!/bin/sh
set -eu
# Gamescope uses glibc Turnip. Its musl Xwayland child needs Mesa's
# software driver and must never load the parent's Vulkan ICD.
unset GALLIUM_DRIVER MESA_LOADER_DRIVER_OVERRIDE LIBGL_KOPPER_DISABLE LIBGL_KOPPER_DRI2 VK_ADD_DRIVER_FILES
export LIBGL_ALWAYS_SOFTWARE=1
export VK_DRIVER_FILES=/dev/null VK_ICD_FILENAMES=/dev/null
exec /usr/bin/Xwayland -shm "$@"
