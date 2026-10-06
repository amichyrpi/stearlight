#!/bin/sh
set -eu
directory=/opt/steamvr/bin/vrwebhelper/linuxarm64
cd "$directory"
# The VM's Vulkan preload emulates features for software drivers. Quest uses
# real KGSL memory and shared timeline semaphores instead.
unset LD_PRELOAD
export LD_LIBRARY_PATH="$directory:/usr/lib/aarch64-linux-gnu"
export VK_ICD_FILENAMES=/opt/quest/quest_icd.json
exec "$directory/vrwebhelper" "$@" --use-gl=angle --use-angle=vulkan
