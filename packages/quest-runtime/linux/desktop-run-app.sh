#!/bin/bash
set -eu
shift 2
exec /bin/bash /opt/quest/desktop-app-session.sh "$@"
