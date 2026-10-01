#!/bin/sh
set -eu

# Exercise the same resumable extraction path used by the image. The fixture
# is deliberately tiny: it verifies the archive transaction, Steam's expected
# per-user links/metadata, pressure-vessel directories, and the WebHelper ABI
# boundary without downloading Valve's client in CI.
base=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/stearlight-steam-firstboot.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

if ! command -v xz >/dev/null 2>&1; then
  printf '%s\n' 'Steam first-boot test skipped (xz is unavailable).'
  exit 0
fi

home="$tmp/home"
archive="$tmp/bootstrap.tar.xz"
seed="$tmp/seed"
pressure="$tmp/pressure-vessel"
fixture="$tmp/fixture"
mkdir -p "$home" "$seed" "$pressure" "$fixture/ubuntu12_32" \
         "$fixture/ubuntu12_64" "$fixture/steamui" \
         "$fixture/ubuntu12_32/steam-runtime/lib/i386-linux-gnu" \
         "$seed/stearlight_libs_32/dri" \
         "$seed/stearlight_libs_64/dri"

cat > "$fixture/ubuntu12_32/steam" <<'EOF'
#!/bin/sh
exit 0
EOF
chmod 0755 "$fixture/ubuntu12_32/steam"
printf '#!/bin/sh\nexec "$@"\n' \
  > "$fixture/ubuntu12_32/steam-runtime/run.sh"
chmod 0755 "$fixture/ubuntu12_32/steam-runtime/run.sh"
printf 'runtime libgcc\n' \
  > "$fixture/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libgcc_s.so.1"
printf 'runtime libstdc++\n' \
  > "$fixture/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libstdc++.so.6"
printf 'runtime xcb\n' \
  > "$fixture/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libxcb.so.1"
printf 'runtime xcb-dri3\n' \
  > "$fixture/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libxcb-dri3.so.0"
printf 'runtime xcb-dri2\n' \
  > "$fixture/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libxcb-dri2.so.0"
cat > "$fixture/ubuntu12_64/steamwebhelper.sh" <<'EOF'
#!/bin/bash
echo webhelper
EOF
chmod 0755 "$fixture/ubuntu12_64/steamwebhelper.sh"
printf 'fake Steam Gamepad UI\n' > "$fixture/steamui/steamui.so"
printf 'fake 32-bit driver\n' > "$seed/stearlight_libs_32/dri/swrast_dri.so"
printf 'fake 64-bit driver\n' > "$seed/stearlight_libs_64/dri/swrast_dri.so"
printf 'seed libgcc\n' > "$seed/stearlight_libs_32/libgcc_s.so.1"
printf 'seed libgcc\n' > "$seed/stearlight_libs_64/libgcc_s.so.1"
printf 'seed libstdc++\n' > "$seed/stearlight_libs_32/libstdc++.so.6"
printf 'seed libstdc++\n' > "$seed/stearlight_libs_64/libstdc++.so.6"
printf 'seed xcb\n' > "$seed/stearlight_libs_32/libxcb.so.1"
printf 'seed xcb-dri3\n' > "$seed/stearlight_libs_32/libxcb-dri3.so.0"
printf 'seed xcb-dri2\n' > "$seed/stearlight_libs_32/libxcb-dri2.so.0"
tar -cJf "$archive" -C "$fixture" .

STEARLIGHT_STEAM_HOME="$home" \
STEARLIGHT_STEAM_BOOTSTRAP="$archive" \
STEARLIGHT_STEAM_RUNTIME_SEED="$seed" \
STEARLIGHT_STEAM_PRESSURE_DIR="$pressure" \
STEARLIGHT_STEAM_BETA=steamdeck_publicbeta \
  "$base/steam-firstboot" --prepare >/dev/null

steam_root="$home/.local/share/Steam"
test -x "$steam_root/ubuntu12_32/steam"
if case "$(uname -s)" in
     MINGW*|MSYS*|CYGWIN*) true ;;
     *) false ;;
   esac
then
    # Git Bash represents directory symlinks as directory junctions, so its
    # test(1) cannot report -L and readlink(1) cannot inspect the target.
    # The Linux/Alpine path below still verifies the actual symlink target.
    test -d "$home/.steam/root"
    test -d "$home/.steam/steam"
else
    test -L "$home/.steam/root"
    test -L "$home/.steam/steam"
    test "$(readlink "$home/.steam/root")" = "$steam_root"
fi
test "$(cat "$steam_root/package/beta")" = steamdeck_publicbeta
test -e "$steam_root/.stearlight-prepared"
test -s "$home/.config/gamescope/bootstrap.cfg"
grep -q '^set_bootstrap=1$' "$home/.config/gamescope/bootstrap.cfg"
test -d "$pressure/ldso"
test -d "$pressure/tmp"
test -s "$steam_root/ubuntu12_32/steam-runtime/stearlight_libs_32/dri/swrast_dri.so"
cmp "$seed/stearlight_libs_32/libgcc_s.so.1" \
    "$steam_root/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libgcc_s.so.1"
cmp "$seed/stearlight_libs_32/libstdc++.so.6" \
    "$steam_root/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libstdc++.so.6"
cmp "$seed/stearlight_libs_32/libxcb.so.1" \
    "$steam_root/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libxcb.so.1"
cmp "$seed/stearlight_libs_32/libxcb-dri3.so.0" \
    "$steam_root/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libxcb-dri3.so.0"
cmp "$seed/stearlight_libs_32/libxcb-dri2.so.0" \
    "$steam_root/ubuntu12_32/steam-runtime/lib/i386-linux-gnu/libxcb-dri2.so.0"
test "$(STEARLIGHT_STEAM_HOME="$home" "$base/steam-firstboot" --status)" = ready

# Valve verifies WebHelper wrappers by exact size/content. The first-boot
# transaction must leave those files byte-for-byte untouched; ABI selection is
# performed by the launcher environment instead of rewriting Steam payloads.
webhelper="$steam_root/ubuntu12_64/steamwebhelper.sh"
webhelper_before=$(sha256sum "$webhelper" | awk '{print $1}')
runtime_before=$(sha256sum "$steam_root/ubuntu12_32/steam-runtime/run.sh" |
  awk '{print $1}')
STEARLIGHT_STEAM_HOME="$home" STEARLIGHT_STEAM_BOOTSTRAP="$archive" \
STEARLIGHT_STEAM_RUNTIME_SEED="$seed" STEARLIGHT_STEAM_PRESSURE_DIR="$pressure" \
  "$base/steam-firstboot" --prepare >/dev/null
test "$(sha256sum "$webhelper" | awk '{print $1}')" = "$webhelper_before"
test "$(sha256sum "$steam_root/ubuntu12_32/steam-runtime/run.sh" |
  awk '{print $1}')" = "$runtime_before"
! grep -q 'STEARLIGHT_WEBHELPER_ABI_BOUNDARY' "$webhelper"

# Build-stage extraction must not bake the staging account into the release.
profile="$tmp/profile"
mkdir -p "$profile/config" "$profile/userdata/42"
printf 'account\n' > "$profile/config/loginusers.vdf"
printf 'local\n' > "$profile/config/localconfig.vdf"
printf 'keep\n' > "$profile/config/SteamAppData.vdf"
printf 'userdata\n' > "$profile/userdata/42/localconfig.vdf"
printf 'payload\n' > "$profile/steamui.so"
"$base/scripts/sanitize-steam-profile.sh" "$profile"
test ! -e "$profile/config/loginusers.vdf"
test ! -e "$profile/config/localconfig.vdf"
test ! -e "$profile/userdata"
test -s "$profile/config/SteamAppData.vdf"
test -s "$profile/steamui.so"
echo 'Steam first-boot extraction checks passed'
