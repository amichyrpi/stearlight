#!/bin/sh
set -eu

base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/stearlight-xr-lifecycle.XXXXXX")
first_pid=
second_pid=
external_pid=
cleanup() {
    for pid in "$first_pid" "$second_pid" "$external_pid"; do
        case "$pid" in
            ''|*[!0-9]*) ;;
            *)
                if kill -0 "$pid" 2>/dev/null; then
                    kill -TERM "$pid" 2>/dev/null || true
                    wait "$pid" 2>/dev/null || true
                fi
                ;;
        esac
    done
    rm -rf "$tmp"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

runtime_dir=$tmp/runtime
runtime_socket=$runtime_dir/monado_comp_ipc
runtime_pid_file=$runtime_dir/stearlight-monado.pid
runtime_lease_dir=$runtime_pid_file.leases
runtime_json=$tmp/openxr.json
service=$tmp/fake-monado-service
application=$tmp/fake-openxr-application
count_file=$tmp/service-starts
python=${STEARLIGHT_XR_TEST_PYTHON:-python3}

# Windows Python builds used by the host test runner may not provide Unix
# domain sockets. The target Alpine image does, so keep this test a clean
# skip on that host instead of reporting a false XR failure.
if ! "$python" -c 'import socket; s=socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.close()' \
    >/dev/null 2>&1; then
    printf '%s\n' 'OpenXR lifecycle repeatability skipped (Unix sockets unavailable)'
    exit 0
fi

printf '%s\n' '{}' >"$runtime_json"
cat >"$service" <<'EOF'
#!/bin/sh
set -eu
printf 'start\n' >>"$STEARLIGHT_XR_TEST_STARTS"
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
delay = float(os.environ.get("STEARLIGHT_XR_TEST_DELAY_SOCKET", "0"))
if delay:
    time.sleep(delay)
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
if [ ! -S "$STEARLIGHT_XR_RUNTIME_SOCKET" ]; then
    printf '%s\n' 'runtime socket disappeared while the application was running' >&2
    exit 2
fi
sleep "${STEARLIGHT_XR_TEST_APP_SLEEP:-0}"
if [ ! -S "$STEARLIGHT_XR_RUNTIME_SOCKET" ]; then
    printf '%s\n' 'runtime socket was terminated by another launcher' >&2
    exit 2
fi
printf '%s\n' "XRT_NO_STDIN=${XRT_NO_STDIN:-}" >"$STEARLIGHT_XR_TEST_APP_OUTPUT"
EOF
chmod 0755 "$service" "$application"

run_application() {
    output=$1
    STEARLIGHT_XR_ENABLE=1 \
    STEARLIGHT_XR_MODE=standalone \
    STEARLIGHT_XR_RUNTIME_DIR="$runtime_dir" \
    STEARLIGHT_XR_RUNTIME_SOCKET="$runtime_socket" \
    STEARLIGHT_XR_RUNTIME_PID_FILE="$runtime_pid_file" \
    STEARLIGHT_XR_RUNTIME_JSON="$runtime_json" \
    STEARLIGHT_XR_MONADO_SERVICE="$service" \
    STEARLIGHT_XR_AUTOSTART=1 \
    STEARLIGHT_XR_NO_STDIN=1 \
    XDG_CACHE_HOME="$tmp/cache" \
    STEARLIGHT_XR_TEST_STARTS="$count_file" \
    STEARLIGHT_XR_TEST_APP_OUTPUT="$output" \
    STEARLIGHT_XR_TEST_PYTHON="$python" \
    "$base/overlay/usr/local/libexec/stearlight/xr-run" "$application"
}

run_application "$tmp/app-one"
test -s "$tmp/app-one"
grep -q '^XRT_NO_STDIN=1$' "$tmp/app-one"
test ! -e "$runtime_pid_file"
test ! -e "$runtime_socket"
test ! -e "$runtime_lease_dir"

run_application "$tmp/app-two"
test -s "$tmp/app-two"
grep -q '^XRT_NO_STDIN=1$' "$tmp/app-two"
test ! -e "$runtime_pid_file"
test ! -e "$runtime_socket"
test ! -e "$runtime_lease_dir"
test "$(wc -l <"$count_file")" -eq 2

# Two applications starting together must share one newly-started Monado.
# The delayed socket makes the second invocation exercise the startup lock
# instead of merely observing an already-ready socket. The first invocation
# owns the service; the second must not terminate it when it exits.
STEARLIGHT_XR_TEST_DELAY_SOCKET=0.4 \
STEARLIGHT_XR_TEST_APP_SLEEP=1.2 \
run_application "$tmp/app-three" &
first_pid=$!
sleep 0.05
STEARLIGHT_XR_TEST_DELAY_SOCKET=0.4 \
STEARLIGHT_XR_TEST_APP_SLEEP=0.2 \
run_application "$tmp/app-four" &
second_pid=$!
wait "$first_pid"
wait "$second_pid"
test -s "$tmp/app-three"
test -s "$tmp/app-four"
test "$(wc -l <"$count_file")" -eq 3
test ! -e "$runtime_pid_file"
test ! -e "$runtime_socket"
test ! -e "$runtime_lease_dir"

# A Monado process supervised outside xr-run must remain alive after the
# application exits, even while its startup socket is still being created.
rm -f "$runtime_socket" "$runtime_pid_file" "$runtime_pid_file.owner"
STEARLIGHT_XR_RUNTIME_SOCKET="$runtime_socket" \
STEARLIGHT_XR_TEST_STARTS="$count_file" \
STEARLIGHT_XR_TEST_PYTHON="$python" \
STEARLIGHT_XR_TEST_DELAY_SOCKET=0.4 \
"$service" &
external_pid=$!
printf '%s\n' "$external_pid" >"$runtime_pid_file"
STEARLIGHT_XR_ENABLE=1 \
STEARLIGHT_XR_MODE=standalone \
STEARLIGHT_XR_RUNTIME_DIR="$runtime_dir" \
STEARLIGHT_XR_RUNTIME_SOCKET="$runtime_socket" \
STEARLIGHT_XR_RUNTIME_PID_FILE="$runtime_pid_file" \
STEARLIGHT_XR_RUNTIME_JSON="$runtime_json" \
STEARLIGHT_XR_MONADO_SERVICE="$service" \
STEARLIGHT_XR_AUTOSTART=1 \
STEARLIGHT_XR_NO_STDIN=1 \
STEARLIGHT_XR_TEST_APP_SLEEP=0.2 \
XDG_CACHE_HOME="$tmp/cache" \
STEARLIGHT_XR_TEST_STARTS="$count_file" \
STEARLIGHT_XR_TEST_APP_OUTPUT="$tmp/app-external" \
STEARLIGHT_XR_TEST_PYTHON="$python" \
"$base/overlay/usr/local/libexec/stearlight/xr-run" "$application"
test -s "$tmp/app-external"
kill -0 "$external_pid"
kill -TERM "$external_pid" 2>/dev/null || true
wait "$external_pid" 2>/dev/null || true
rm -f "$runtime_socket" "$runtime_pid_file"

printf '%s\n' 'OpenXR lifecycle repeatability passed'
