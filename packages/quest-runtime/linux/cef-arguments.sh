# Sourced by the two Quest CEF launchers; retain Valve's existing features.
quest_cef_args=()
quest_cef_disable=0
for argument in "$@"; do
 case "$argument" in
  --disable-features=*) argument="$argument,AudioServiceOutOfProcess,SpareRendererForSitePerProcess";quest_cef_disable=1 ;;
 esac
 quest_cef_args+=("$argument")
done
if [ "$quest_cef_disable" = 0 ]; then quest_cef_args+=(--disable-features=AudioServiceOutOfProcess,SpareRendererForSitePerProcess);fi
