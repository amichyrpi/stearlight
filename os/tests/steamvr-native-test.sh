#!/usr/bin/env bash
set -euo pipefail

base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
launcher="${base}/overlay/usr/local/libexec/stearlight/steamvr-native"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

root="${tmp}/runtime"
bindir="${root}/bin/linux64"
mkdir -p "${bindir}" "${root}/drivers/vrlink" \
  "${root}/drivers/svrt-fake" "${root}/drivers/frame_controller" \
  "${tmp}/home/.config/openvr" "${tmp}/run" "${tmp}/steam"

for binary in vrstartup vrserver vrcompositor vrdashboard; do
  : >"${bindir}/${binary}"
  chmod +x "${bindir}/${binary}"
done

cat >"${root}/bin/vrenv.sh" <<'MOCK_ENV'
#!/bin/sh
exec "$@"
MOCK_ENV
chmod +x "${root}/bin/vrenv.sh"

cat >"${bindir}/vrpathreg" <<'MOCK_PATHREG'
#!/bin/sh
set -eu
printf '%s\n' "$*" >>"$MOCK_PATHREG_LOG"
case "$1" in
  setruntime|setconfig|setlog)
    exit 0
    ;;
  adddriver)
    printf '%s\n' "$2" >>"$MOCK_DRIVERS"
    ;;
  finddriver)
    expected="${MOCK_DRIVER_ROOT}/drivers/${2}"
    if grep -F -x -q "$expected" "$MOCK_DRIVERS"; then
      printf '%s\n' "$expected"
      exit 0
    fi
    exit 1
    ;;
  *)
    exit 2
    ;;
esac
MOCK_PATHREG
chmod +x "${bindir}/vrpathreg"

# A nonempty registry used to suppress registration altogether. The launcher
# must reconcile the runtime and required Valve/VM drivers even in this case.
printf '%s\n' '{"runtime":["/stale/runtime"],"external_drivers":[]}' \
  >"${tmp}/existing.vrpath"
: >"${tmp}/pathreg.log"
: >"${tmp}/drivers.log"

set +e
HOME="${tmp}/home" \
STEAM_BASE_FOLDER="${tmp}/steam" \
XDG_RUNTIME_DIR="${tmp}/run" \
VR_PATHREG_OVERRIDE="${tmp}/existing.vrpath" \
STEARLIGHT_STEAMVR_ROOT="$root" \
STEARLIGHT_NATIVE_STEAMVR_WAIT_FOR_STEAM=0 \
STEARLIGHT_FAKE_TRACKING=1 \
MOCK_PATHREG_LOG="${tmp}/pathreg.log" \
MOCK_DRIVERS="${tmp}/drivers.log" \
MOCK_DRIVER_ROOT="$root" \
  "$launcher" unsupported >"${tmp}/stdout" 2>"${tmp}/stderr"
status=$?
set -e

test "$status" -eq 2
grep -F -x -q "setruntime ${root}" "${tmp}/pathreg.log"
grep -F -x -q "setconfig ${tmp}/home/.steam/steam/config" \
  "${tmp}/pathreg.log"
grep -F -x -q "setlog ${tmp}/home/.steam/steam/logs" \
  "${tmp}/pathreg.log"
for driver in vrlink svrt-fake frame_controller; do
  grep -F -x -q "adddriver ${root}/drivers/${driver}" "${tmp}/pathreg.log"
  grep -F -x -q "finddriver ${driver}" "${tmp}/pathreg.log"
done
grep -q '^usage:' "${tmp}/stderr"

# Never start vrstartup after a timeout if Steam's API/WebHelper is absent.
set +e
HOME="${tmp}/home" \
STEAM_BASE_FOLDER="${tmp}/steam" \
XDG_RUNTIME_DIR="${tmp}/run" \
VR_PATHREG_OVERRIDE="${tmp}/existing.vrpath" \
STEARLIGHT_STEAMVR_ROOT="$root" \
STEARLIGHT_NATIVE_STEAMVR_WAIT_FOR_STEAM=1 \
STEARLIGHT_NATIVE_STEAM_WAIT_SECONDS=0 \
STEARLIGHT_FAKE_TRACKING=1 \
MOCK_PATHREG_LOG="${tmp}/pathreg.log" \
MOCK_DRIVERS="${tmp}/drivers.log" \
MOCK_DRIVER_ROOT="$root" \
  "$launcher" unsupported >"${tmp}/wait-stdout" 2>"${tmp}/wait-stderr"
wait_status=$?
set -e
test "$wait_status" -eq 1
grep -q 'Steam client/WebHelper did not become ready' "${tmp}/wait-stderr"
! grep -q '^usage:' "${tmp}/wait-stderr"

echo 'SteamVR native launcher registry test passed'
