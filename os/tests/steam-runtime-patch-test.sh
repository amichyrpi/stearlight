#!/usr/bin/env bash
set -euo pipefail

# Keep the generated Valve runtime wrapper valid with the awk implementation
# shipped by Debian (mawk) and with Alpine's /bin/sh invocation path.
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/stearlight-steam-runtime.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

runtime="$tmp/runtime"
mkdir -p "$runtime"
cat > "$runtime/run.sh" <<'EOF'
#!/bin/bash
set -e
STEAM_RUNTIME=$(CDPATH= cd -- "${0%/*}" && pwd)
host_library_paths=
case "${DEBUGGER-}" in
    (valgrind*) ;;
esac
steam_runtime_library_paths="$host_library_paths$STEAM_RUNTIME/lib"
if [ "$1" = "--print-steam-runtime-library-paths" ]; then
    echo "$steam_runtime_library_paths"
    exit 0
fi
exec "$@"
EOF
chmod 0755 "$runtime/run.sh"

"$base/patch-steam-runtime.sh" "$runtime/run.sh"

if grep -nF '\$(' "$runtime/run.sh"; then
  echo 'The runtime patch left an escaped command substitution.' >&2
  exit 1
fi
bash -n "$runtime/run.sh"
paths=$(STEAM_RUNTIME="$runtime" /bin/sh "$runtime/run.sh" \
  --print-steam-runtime-library-paths)
case "$paths" in
  "$runtime/stearlight_libs_32:"*) ;;
  *)
    echo "Unexpected runtime library path: $paths" >&2
    exit 1
    ;;
esac
echo 'Steam runtime patch checks passed'
