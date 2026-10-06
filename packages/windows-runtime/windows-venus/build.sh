#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
  echo 'Usage: build.sh SOURCE_DIRECTORY BUILD_DIRECTORY' >&2
  exit 2
fi
source_dir=$(realpath "$1")
build_dir=$(realpath -m "$2")
mkdir -p "$build_dir"
export TMPDIR="$build_dir/temp"
mkdir -p "$TMPDIR"
meson setup "$build_dir" "$source_dir" --buildtype=release \
  --prefix=/ucrt64 -Dvenus=true -Dvideo=true -Dtests=false
meson compile -C "$build_dir" -j 6
