#!/bin/bash
set -eu
browser=/opt/stearlight-browser/usr/lib/chromium/chromium
[ -x "$browser" ] || { echo 'Chromium is not installed in this payload.' >&2; exit 1; }
unset LD_LIBRARY_PATH EGL_PLATFORM QT_QPA_PLATFORM
export LD_PRELOAD=/opt/quest/libquest_socket.so
export GLIBC_TUNABLES=glibc.pthread.rseq=0
export FONTCONFIG_FILE=/opt/quest/desktop-fonts.conf
export FONTCONFIG_PATH=/opt/stearlight-desktop/etc/fonts
for argument in "$@";do
 case "$argument" in
  # Chromium does not forward this parent-only testing switch itself. Without
  # it, helpers claim the disabled crash handler's reused descriptor twice.
  --type=*) exec "$browser" --disable-crashpad-for-testing "$@" ;;
 esac
done
platform=x11
[ -z "${WAYLAND_DISPLAY:-}" ] || platform=wayland
site_options=()
disabled=AudioServiceOutOfProcess,SpareRendererForSitePerProcess,BackForwardCache,NativeNotifications,WebUIOmniboxPopup,WebUIOmniboxAimPopup,WebUIOmniboxFullPopup,OptimizationGuideModelExecution,LogOnDeviceMetricsOnStartup
if [[ "${XDG_RUNTIME_DIR:-}" = */stearlight-plasma || "${STEARLIGHT_DESKTOP_APP_WINDOW:-}" = 1 ]];then
 # The headset keeps SteamVR and its UI alive alongside KDE. Per-site
 # renderers exceed Android's unchanged process cap even with one tab.
 # Remote Desktop releases that stack and retains normal site isolation.
 site_options+=( --disable-site-isolation-trials )
 disabled+=,SiteIsolationForCrossOriginOpenerPolicy
fi
# PRoot cannot create Chromium's nested Linux namespaces. Android still
# isolates the entire APK. Keep page rendering in a separate process.
exec "$browser" --no-sandbox --no-zygote --renderer-process-limit=1 \
 "${site_options[@]}" \
 --browser-subprocess-path=/opt/quest/chromium-launcher.sh \
 --disable-breakpad --disable-crashpad-for-testing --in-process-gpu --disable-gpu \
 --use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader \
 --no-first-run --no-default-browser-check \
 --enable-features=NetworkServiceInProcess2 \
 --disable-dev-shm-usage \
 --disable-features="$disabled" \
 --ozone-platform="$platform" --user-data-dir="$HOME/.local/share/stearlight-chromium" "$@"
