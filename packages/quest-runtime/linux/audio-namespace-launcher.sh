#!/bin/sh
# quest-audio-namespace-launcher
# Quest's guest audio sockets already belong to this app. Retain Steam's
# actual game reaper without an additional namespace/reaper process.
[ "$1" = -- ] || exit 64
shift
exec "$@"
