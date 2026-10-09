# Sourced by Valve's RUNSTEAM.sh after it assembles STEAM_ARGS.
# Frame's bootstrapper otherwise redirects every ARM64 beta to its bundled
# development branch. Override it only after an explicit channel selection.
case "$(cat "${STEAMROOT}/package/stearlight-channel" 2>/dev/null || true)" in
 rel) STEAM_ARGS+=( -clientbeta steamdeck_stable ) ;;
 beta) STEAM_ARGS+=( -clientbeta publicbeta ) ;;
esac
if [ -f "${STEAMROOT}/package/stearlight-channel-update" ];then
 STEAM_ARGS+=( -forcesteamupdate -forcepackagedownload )
 rm -f "${STEAMROOT}/package/stearlight-channel-update"
fi
