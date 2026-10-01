#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
  echo 'Usage: build.sh /f/source /f/build (or /g paths)' >&2
  exit 2
fi
source_dir=$(realpath "$1")
build_dir=$(realpath -m "$2")
for path in "$source_dir" "$build_dir"; do
  case "$path" in
    /f/*|/g/*) ;;
    *) echo "Build paths must be on F: or G:: $path" >&2; exit 2 ;;
  esac
done
mkdir -p "$build_dir"
export TMPDIR="$build_dir/temp"
mkdir -p "$TMPDIR"
meson setup "$build_dir" "$source_dir" --buildtype=debugoptimized \
  --prefix=/ucrt64 -Dvenus=true -Dvideo=true -Dtests=false
meson compile -C "$build_dir" -j 6
