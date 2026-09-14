#!/bin/sh
set -eu

base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/stearlight-xr-bridge.XXXXXX")
cleanup() {
    rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

runtime_dir=$tmp/runtime
runtime_socket=$runtime_dir/monado_comp_ipc
runtime_pid_file=$runtime_dir/stearlight-monado.pid
runtime_json=$tmp/openxr.json
service=$tmp/fake-monado-service
application=$tmp/fake-openxr-application
bridge=$tmp/xrizer
registry=$tmp/config/openvr/openvrpaths.vrpath
python=${STEARLIGHT_XR_TEST_PYTHON:-python3}

# Windows Python builds used by the host test runner may not provide Unix
# domain sockets. The target Alpine image does, so keep this test a clean skip
# on that host instead of reporting a false XR failure.
if ! "$python" -c 'import socket; s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.close()' \
    >/dev/null 2>&1; then
    printf '%s\n' 'OpenVR bridge environment test skipped (Unix sockets unavailable)'
    exit 0
fi

printf '%s\n' '{}' >"$runtime_json"
mkdir -p "$bridge/bin/linuxarm64"
printf '%s\n' 'ARM64 bridge test marker' >"$bridge/bin/linuxarm64/vrclient.so"

cat >"$service" <<'EOF'
#!/bin/sh
set -eu
exec "$STEARLIGHT_XR_TEST_PYTHON" - "$STEARLIGHT_XR_RUNTIME_SOCKET" <<'PY'
import os
import signal
import socket
import sys
import time

path = sys.argv[1]
os.makedirs(os.path.dirname(path), exist_ok=True)
try:
    os.unlink(path)
except FileNotFoundError:
    pass
server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
server.bind(path)
server.listen(1)

def stop(_signum, _frame):
    server.close()
    try:
        os.unlink(path)
    except FileNotFoundError:
        pass
    raise SystemExit(0)

signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)
while True:
    time.sleep(1)
PY
EOF
cat >"$application" <<'EOF'
#!/bin/sh
set -eu
test -S "$STEARLIGHT_XR_RUNTIME_SOCKET"
printf 'VR_OVERRIDE=%s\nXR_RUNTIME_JSON=%s\nPRESSURE_VESSEL_FILESYSTEMS_RW=%s\n' \
    "${VR_OVERRIDE:-}" "${XR_RUNTIME_JSON:-}" \
    "${PRESSURE_VESSEL_FILESYSTEMS_RW:-}" >"$STEARLIGHT_XR_TEST_APP_OUTPUT"
EOF
chmod 0755 "$service" "$application"

run_bridge_application() {
    output=$1
    STEARLIGHT_XR_ENABLE=1 \
    STEARLIGHT_XR_MODE=standalone \
    STEARLIGHT_XR_STEAM_LAUNCH=1 \
    STEARLIGHT_XR_RUNTIME_DIR="$runtime_dir" \
    STEARLIGHT_XR_RUNTIME_SOCKET="$runtime_socket" \
    STEARLIGHT_XR_RUNTIME_PID_FILE="$runtime_pid_file" \
    STEARLIGHT_XR_RUNTIME_JSON="$runtime_json" \
    STEARLIGHT_XR_CONTAINER_RUNTIME_JSON=/run/host/usr/share/openxr/1/openxr_monado.json \
    STEARLIGHT_XR_OPENVR_RUNTIME="$bridge" \
    STEARLIGHT_XR_OPENVR_CONTAINER_RUNTIME=/run/host/home/stearlight/xrizer \
    STEARLIGHT_XR_OPENVR_REQUIRED=1 \
    STEARLIGHT_XR_MONADO_SERVICE="$service" \
    STEARLIGHT_XR_AUTOSTART=1 \
    STEARLIGHT_XR_NO_STDIN=1 \
    PRESSURE_VESSEL_FILESYSTEMS_RW=/tmp/existing \
    XDG_CONFIG_HOME="$tmp/config" \
    XDG_CACHE_HOME="$tmp/cache" \
    STEARLIGHT_XR_TEST_APP_OUTPUT="$output" \
    STEARLIGHT_XR_TEST_PYTHON="$python" \
    "$base/overlay/usr/local/libexec/stearlight/xr-run" "$application"
}

run_bridge_application "$tmp/first-output"
grep -q '^VR_OVERRIDE=/run/host/home/stearlight/xrizer$' "$tmp/first-output"
grep -q '^XR_RUNTIME_JSON=/run/host/usr/share/openxr/1/openxr_monado.json$' "$tmp/first-output"
grep -q "^PRESSURE_VESSEL_FILESYSTEMS_RW=/tmp/existing:$runtime_socket$" "$tmp/first-output"
test -s "$registry"
cp "$registry" "$tmp/registry-before"

# A second launch must preserve the user's OpenVR registry rather than
# rewriting it, while still applying the Steam container environment.
run_bridge_application "$tmp/second-output"
cmp "$registry" "$tmp/registry-before"
grep -q '^VR_OVERRIDE=/run/host/home/stearlight/xrizer$' "$tmp/second-output"
test ! -e "$runtime_pid_file"
test ! -e "$runtime_socket"

# Required OpenVR games fail clearly when no ARM64 bridge was installed.
if STEARLIGHT_XR_ENABLE=1 \
   STEARLIGHT_XR_MODE=standalone \
   STEARLIGHT_XR_STEAM_LAUNCH=1 \
   STEARLIGHT_XR_RUNTIME_DIR="$runtime_dir" \
   STEARLIGHT_XR_RUNTIME_JSON="$runtime_json" \
   STEARLIGHT_XR_OPENVR_RUNTIME= \
   STEARLIGHT_XR_OPENVR_REQUIRED=1 \
   "$base/overlay/usr/local/libexec/stearlight/xr-run" "$application" \
   >"$tmp/missing-output" 2>&1; then
    printf '%s\n' 'required OpenVR bridge unexpectedly accepted an empty runtime' >&2
    exit 1
fi
grep -q 'OpenVR bridge is required' "$tmp/missing-output"

printf '%s\n' 'OpenVR bridge environment and repeatability checks passed'
