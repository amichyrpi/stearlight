#!/bin/sh
set -eu

base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
launcher="$base/overlay/usr/local/libexec/stearlight/steamvr-native"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/stearlight-vrstartup.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

root="$tmp/steamvr"
case $(uname -m) in
  aarch64|arm64) platform=linuxarm64 ;;
  *) platform=linux64 ;;
esac
bindir="$root/bin/$platform"
registry="$tmp/registry"
runtime_log="$tmp/runtime.log"
mkdir -p "$bindir" "$root/drivers/vrlink" \
  "$root/drivers/frame_controller" "$root/drivers/stearlight-fake" \
  "$registry" "$tmp/home" "$tmp/run"

cat >"$root/bin/vrenv.sh" <<'EOF'
#!/bin/sh
exec "$@"
EOF
cat >"$bindir/vrpathreg" <<'EOF'
#!/bin/sh
set -eu
case "$1" in
  setruntime|setconfig|setlog) exit 0 ;;
  adddriver)
    directory=$2
    name=${directory##*/}
    printf '%s\n' "$directory" >"$TEST_REGISTRY/$name"
    ;;
  finddriver)
    [ -f "$TEST_REGISTRY/$2" ] || exit 1
    cat "$TEST_REGISTRY/$2"
    ;;
  *) exit 2 ;;
esac
EOF
cat >"$bindir/vrstartup" <<'EOF'
#!/bin/sh
printf 'vrstartup called %s\n' "$*" >"$TEST_RUNTIME_LOG"
cat "$TEST_RUNTIME_LOG"
EOF
for binary in vrserver vrcompositor vrdashboard; do
  printf '#!/bin/sh\nexit 0\n' >"$bindir/$binary"
done
chmod 0755 "$root/bin/vrenv.sh" "$bindir"/*

settings="$tmp/home/.steam/steam/config/steamvr.vrsettings"
mkdir -p "$(dirname -- "$settings")"
cat >"$settings" <<'EOF'
{
  "steamvr": {
    "disableAsync": true,
    "enableLinuxVulkanAsync": false,
    "mirrorViewGeometry": "0 0 2880 1600"
  },
  "direct_mode": {
    "enable": true
  }
}
EOF

output=$(
  HOME="$tmp/home" \
  XDG_RUNTIME_DIR="$tmp/run" \
  DBUS_SESSION_BUS_ADDRESS=disabled \
  STEARLIGHT_STEAMVR_ROOT="$root" \
  STEARLIGHT_FAKE_TRACKING=1 \
  STEARLIGHT_NATIVE_STEAMVR_WAIT_FOR_STEAM=0 \
  TEST_REGISTRY="$registry" \
  TEST_RUNTIME_LOG="$runtime_log" \
    "$launcher" startup
)
case "$output" in
  'vrstartup called '*) ;;
  *) printf '%s\n' "Unexpected SteamVR launcher result: $output" >&2; exit 1 ;;
esac
grep -qx "$root/drivers/vrlink" "$registry/vrlink"
grep -qx "$root/drivers/frame_controller" "$registry/frame_controller"
grep -qx "$root/drivers/stearlight-fake" "$registry/stearlight-fake"
grep -q '"disableAsync": false' "$settings"
grep -q '"enableLinuxVulkanAsync": true' \
  "$settings"
grep -q '"mirrorViewGeometry": "0 0 2880 1600"' \
  "$settings"
grep -q '"enable": false' "$settings"

printf '%s\n' 'Native SteamVR startup and VM driver registration passed.'
