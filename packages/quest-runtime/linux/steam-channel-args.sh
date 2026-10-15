# Sourced by Valve's RUNSTEAM.sh after it assembles STEAM_ARGS.
# Preprocessing desktop shader archives competes with the dashboard and local
# games on Quest. Mesa compiles and caches the shaders actually used instead.
# Keep the real single-worker replay path available for explicit opt-in.
if [ "${STEARLIGHT_SHADER_PRECACHE:-0}" != 1 ];then
 STEAM_ARGS+=( -noshaders )
fi
# Frame supplies its own ARM64 Beta branch. Its client build can differ from
# the general Public Beta manifest; selecting that manifest repeatedly offers
# the same package. Preserve Valve's runtime-provided Frame branch across UI
# selections, and use Public Beta on runtimes without a Frame branch.
frame_beta=$(cat "${STEAMROOT}/package/beta" 2>/dev/null || true)
case "$frame_beta" in
 linux_arm64_beta_*) printf '%s\n' "$frame_beta" >"${STEAMROOT}/package/stearlight-frame-beta" ;;
 *) frame_beta=$(cat "${STEAMROOT}/package/stearlight-frame-beta" 2>/dev/null || true) ;;
esac
case "$frame_beta" in linux_arm64_beta_*) ;; *) frame_beta= ;; esac
case "$(cat "${STEAMROOT}/package/stearlight-channel" 2>/dev/null || true)" in
 rel) STEAM_ARGS+=( -clientbeta steamdeck_stable ) ;;
 beta) STEAM_ARGS+=( -clientbeta "${frame_beta:-publicbeta}" ) ;;
esac
if [ -f "${STEAMROOT}/package/stearlight-channel-update" ];then
 STEAM_ARGS+=( -forcesteamupdate -forcepackagedownload )
 rm -f "${STEAMROOT}/package/stearlight-channel-update"
fi
