#!/usr/bin/env bash
set -euo pipefail

# Compile and optionally inspect/exercise the same OpenXR loader/runtime
# shipped by the image. Compilation is safe on any host with openxr-dev
# installed. Device discovery is also safe in a container; starting a real
# OpenXR service is reserved for hardware/CI with a compositor and is opt-in.
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
if ! command -v pkg-config >/dev/null 2>&1 ||
   ! pkg-config --exists openxr; then
  echo 'OpenXR runtime probe skipped (openxr-dev/pkg-config not installed).'
  exit 0
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/stearlight-openxr.XXXXXX")
service_pid=
xvfb_pid=
runtime_dir=${STEARLIGHT_XR_TEST_RUNTIME_DIR:-$tmp/runtime}
runtime_socket=${STEARLIGHT_XR_RUNTIME_SOCKET:-$runtime_dir/monado_comp_ipc}
cleanup() {
  if [ -n "$service_pid" ] && kill -0 "$service_pid" 2>/dev/null; then
    kill -TERM "$service_pid" 2>/dev/null || true
    wait "$service_pid" 2>/dev/null || true
  fi
  if [ -n "$xvfb_pid" ] && kill -0 "$xvfb_pid" 2>/dev/null; then
    kill -TERM "$xvfb_pid" 2>/dev/null || true
    wait "$xvfb_pid" 2>/dev/null || true
  fi
  rm -rf "$tmp"
}
trap cleanup EXIT INT TERM

cc=${CC:-cc}
"$cc" -std=c11 -Wall -Wextra -Werror \
  "$base/tests/openxr-runtime-test.c" -o "$tmp/openxr-runtime-test" \
  $(pkg-config --cflags --libs openxr)
echo 'OpenXR runtime probe compiled.'

if [ "${STEARLIGHT_XR_TEST_DISCOVERY:-0}" = 1 ]; then
  mkdir -p "$runtime_dir"
  monado_cli=${STEARLIGHT_XR_MONADO_CLI:-}
  if [ -z "$monado_cli" ]; then
    monado_cli=$(command -v monado-cli 2>/dev/null || true)
  fi
  if [ -z "$monado_cli" ] || [ ! -x "$monado_cli" ]; then
    echo 'Monado CLI is not installed; discovery probe cannot run.' >&2
    exit 127
  fi
  discovery_log="$tmp/monado-info.log"
  if ! QWERTY_ENABLE=1 XRT_DEBUG_GUI=1 OXR_DEBUG_GUI=1 XRT_NO_STDIN=1 \
       P_OVERRIDE_ACTIVE_CONFIG=qwerty XDG_RUNTIME_DIR="$runtime_dir" \
       HOME="${HOME:-$tmp}" "$monado_cli" info >"$discovery_log" 2>&1; then
    cat "$discovery_log" >&2
    exit 1
  fi
  if ! grep -Eiq 'qwerty|simulated' "$discovery_log"; then
    cat "$discovery_log" >&2
    echo 'Monado discovery did not expose a keyboard/simulated XR builder.' >&2
    exit 1
  fi
  printf '%s\n' 'OpenXR Monado device-discovery probe passed.'
  exit 0
fi

if [ "${STEARLIGHT_XR_TEST_START_MONADO:-0}" != 1 ]; then
  exit 0
fi

runtime_json=${XR_RUNTIME_JSON:-/usr/share/openxr/1/openxr_monado.json}
if [ ! -r "$runtime_json" ]; then
  echo "OpenXR runtime manifest is missing: $runtime_json" >&2
  exit 1
fi
mkdir -p "$runtime_dir"
if [ ! -w "$runtime_dir" ]; then
  echo "OpenXR runtime directory is not writable: $runtime_dir" >&2
  exit 1
fi

service=${STEARLIGHT_XR_MONADO_SERVICE:-}
if [ -z "$service" ]; then
  service=$(command -v monado-service 2>/dev/null || true)
fi
if [ -z "$service" ] || [ ! -x "$service" ]; then
  echo 'Monado service is not installed.' >&2
  exit 127
fi

test_config=${STEARLIGHT_XR_TEST_CONFIG:-qwerty}
debug_gui=${STEARLIGHT_XR_TEST_DEBUG_GUI:-0}
if [ "$test_config" = qwerty ] &&
   [ -z "${STEARLIGHT_XR_TEST_DEBUG_GUI+x}" ]; then
  debug_gui=1
fi
if [ "$test_config" = qwerty ]; then
  if ! command -v Xvfb >/dev/null 2>&1; then
    echo 'Xvfb is required for the Monado qwerty probe.' >&2
    exit 127
  fi
  display=${STEARLIGHT_XR_TEST_DISPLAY:-}
  if [ -z "$display" ]; then
    display=:$((100 + ($$ % 400)))
  fi
  Xvfb "$display" -screen 0 1280x720x24 -ac -nolisten tcp >/dev/null 2>&1 &
  xvfb_pid=$!
  export DISPLAY="$display"
  for attempt in $(seq 1 50); do
    if kill -0 "$xvfb_pid" 2>/dev/null &&
       [ -S "/tmp/.X11-unix/X${display#:}" ]; then
      break
    fi
    sleep 0.1
  done
  if ! kill -0 "$xvfb_pid" 2>/dev/null; then
    echo 'Xvfb did not start for the Monado qwerty probe.' >&2
    exit 1
  fi
fi

service_log="$tmp/monado-service.log"
if [ ! -S "$runtime_socket" ]; then
  IPC_EXIT_ON_DISCONNECT=1 \
  XRT_NO_STDIN=1 \
  QWERTY_ENABLE=$([ "$test_config" = qwerty ] && echo 1 || echo 0) \
  XRT_DEBUG_GUI="$debug_gui" \
  OXR_DEBUG_GUI="$debug_gui" \
  XDG_RUNTIME_DIR="$runtime_dir" \
  P_OVERRIDE_ACTIVE_CONFIG="$test_config" \
  HOME="${HOME:-$tmp}" "$service" >"$service_log" 2>&1 &
  service_pid=$!
  ready=0
  for attempt in $(seq 1 100); do
    if [ -S "$runtime_socket" ]; then
      ready=1
      break
    fi
    if ! kill -0 "$service_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [ "$ready" -ne 1 ]; then
    echo 'Monado did not create its IPC socket.' >&2
    tail -n 80 "$service_log" >&2 2>/dev/null || true
    exit 1
  fi
fi

set +e
XDG_RUNTIME_DIR="$runtime_dir" \
XR_RUNTIME_JSON="$runtime_json" \
P_OVERRIDE_ACTIVE_CONFIG="$test_config" \
  "$tmp/openxr-runtime-test"
probe_status=$?
set -e
if [ "$probe_status" -ne 0 ] && [ -s "$service_log" ]; then
  tail -n 80 "$service_log" >&2 || true
fi
exit "$probe_status"
