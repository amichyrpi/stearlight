#!/bin/sh
set -eu
# Steam asks for the real owner of a local CEF transport socket. Inspect its
# client and network-service processes instead of every Android process.
# GNU lsof still reads the actual descriptors, endpoints, uid and parent pid.
if [ "$#" = 5 ] && [ "$1" = -P ] && [ "$2" = -F ] &&
   [ "$3" = upnR ] && [ "$4" = -i ]; then
    case "$5" in TCP@127.0.0.1:*)
        uid=$(id -u)
        network=$(pgrep -u "$uid" -f -- '(^| )--utility-sub-type=network.mojom.NetworkService' || true)
        if [ -n "$network" ]; then
            ids=$({ pgrep -u "$uid" -x steam || true; printf '%s\n' "$network"; } | paste -sd,)
            exec /opt/quest/lsof -w -n -a -p "$ids" "$@"
        fi
    esac
fi
exec /opt/quest/lsof "$@"
