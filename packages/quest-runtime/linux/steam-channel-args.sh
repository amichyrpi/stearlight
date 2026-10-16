# Sourced by Valve's RUNSTEAM.sh after it assembles STEAM_ARGS.
# Preprocessing desktop shader archives competes with the dashboard and local
# games on Quest. Mesa compiles and caches the shaders actually used instead.
# Keep the real single-worker replay path available for explicit opt-in.
if [ "${STEARLIGHT_SHADER_PRECACHE:-0}" != 1 ];then
 STEAM_ARGS+=( -noshaders )
fi
# Use Valve's moving public feeds. A recovery image's linux_arm64_beta_* name
# identifies a snapshot and must not pin future Beta checks to that old build.
channel=$(cat "${STEAMROOT}/package/stearlight-channel" 2>/dev/null || true)
case "$channel" in
 rel|beta)
  # Frame injects its own -clientbeta earlier in this array. Steam uses the
  # first value, so appending our selection alone silently keeps the snapshot.
  selected_args=();skip_channel_value=0
  for argument in "${STEAM_ARGS[@]}";do
   if [ "$skip_channel_value" = 1 ];then skip_channel_value=0;continue;fi
   case "$argument" in
    -clientbeta) skip_channel_value=1 ;;
    -clientbeta=*) ;;
    *) selected_args+=( "$argument" ) ;;
   esac
  done
  STEAM_ARGS=( "${selected_args[@]}" ) ;;
esac
case "$channel" in
 rel) feed=steamdeck_stable ;;
 beta) feed=publicbeta ;;
 *) feed= ;;
esac
if [ -n "$feed" ];then
 # Steam also uses package/beta to identify its installed manifest. A stale
 # recovery branch here makes each launch reinstall the already-current feed.
 printf '%s\n' "$feed" >"${STEAMROOT}/package/beta"
 STEAM_ARGS+=( -clientbeta "$feed" )
fi
if [ -f "${STEAMROOT}/package/stearlight-channel-update" ];then
 STEAM_ARGS+=( -forcesteamupdate -forcepackagedownload )
 rm -f "${STEAMROOT}/package/stearlight-channel-update"
fi
