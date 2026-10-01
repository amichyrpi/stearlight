#!/bin/sh
# ELF inspection is CPU-independent; docker-desktop includes scanelf.
extract_frame_gamescope() (
  set -eu
  frame_root=$1
  stage=$2
  command -v scanelf >/dev/null 2>&1 || {
    echo 'Gamescope extraction requires scanelf (pax-utils) in the WSL distribution.' >&2
    exit 1
  }
  mkdir -p "$stage/bin" "$stage/lib" "$stage/share"
  cp "$frame_root/usr/bin/gamescope" "$stage/bin/gamescope"
  pending="$stage/dependencies.pending"
  seen="$stage/dependencies.txt"
  : >"$seen"
  scanelf -n -B -F '%n' "$frame_root/usr/bin/gamescope" |
    awk '{print $1}' | tr ',' '\n' >"$pending"
  # Vulkan is loaded with dlopen, so it is absent from DT_NEEDED.
  printf '%s\n' libvulkan.so.1 >>"$pending"
  while [ -s "$pending" ]; do
    dependency=$(head -n 1 "$pending")
    sed -i '1d' "$pending"
    # scanelf prints the filename alone for ELFs without DT_NEEDED.
    case "$dependency" in ''|-|*/*) continue ;; esac
    if grep -Fxq "$dependency" "$seen"; then continue; fi
    source="$frame_root/usr/lib/$dependency"
    if [ ! -f "$source" ]; then
      printf 'Missing Gamescope dependency: %s\n' "$source" >&2
      exit 1
    fi
    # Flatten SONAME symlinks to avoid absolute links escaping the bundle.
    cp -L "$source" "$stage/lib/$dependency"
    printf '%s\n' "$dependency" >>"$seen"
    scanelf -n -B -F '%n' "$source" |
      awk '{print $1}' | tr ',' '\n' >>"$pending"
  done
  for resource in gamescope libdrm X11/xkb; do
    if [ -d "$frame_root/usr/share/$resource" ]; then
      mkdir -p "$stage/share/$resource"
      cp -a "$frame_root/usr/share/$resource/." "$stage/share/$resource/"
    fi
  done
  rm "$pending"
  chmod 0755 "$stage/bin/gamescope" "$stage/lib/ld-linux-aarch64.so.1"
)
