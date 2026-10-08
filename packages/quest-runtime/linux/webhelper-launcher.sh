#!/bin/bash
set -eu
directory=/opt/steamvr/bin/vrwebhelper/linuxarm64
cd "$directory"
# The VM's Vulkan preload emulates features for software drivers. Quest uses
# real KGSL memory and shared timeline semaphores instead.
unset LD_PRELOAD
export LD_PRELOAD=/opt/quest/libquest_socket.so
export LD_LIBRARY_PATH="$directory:/usr/lib/aarch64-linux-gnu"
export VK_ICD_FILENAMES=/opt/quest/quest_icd.json
. /opt/quest/cef-arguments.sh
exec "$directory/vrwebhelper" "${quest_cef_args[@]}" --use-gl=angle --use-angle=vulkan \
 --no-zygote --renderer-process-limit=1 \
 --disk-cache-dir="$HOME/.cache/stearlight-vr-controls-v1"
