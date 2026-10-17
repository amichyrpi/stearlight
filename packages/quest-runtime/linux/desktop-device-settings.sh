#!/bin/bash
set -eu
case "${1:-headset}" in
 headset|wifi|bluetooth) printf '%s\n' "${1:-headset}" > /run/stearlight/quest/desktop-device-settings.request ;;
 *) exit 2 ;;
esac
