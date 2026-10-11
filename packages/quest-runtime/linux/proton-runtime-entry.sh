#!/bin/sh
# quest-proton-runtime-entry
# PRoot already provides the guest filesystem; pressure-vessel cannot create
# privileged namespaces inside an ordinary Quest application.
export PATH=/usr/bin:/bin
export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu:/opt/frame-gamescope/lib
exec /usr/bin/python3 /opt/quest/proton-game-launcher.py "$0" "$@"
