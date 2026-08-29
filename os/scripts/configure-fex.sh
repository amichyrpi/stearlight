#!/usr/bin/env bash
set -euo pipefail

install -d -m 0755 /usr/share/fex-emu /usr/share/guestos/fex-mesa
cat > /usr/share/fex-emu/Config.json <<'EOF'
{
  "Config": {
    "RootFS": "/usr/share/guestos/fex-mesa",
    "TSOEnabled": "1",
    "HalfBarrierTSOEnabled": "1",
    "ThunkHostLibs": "/usr/lib64/fex-emu/HostThunks",
    "ThunkGuestLibs": "/usr/share/fex-emu/GuestThunks"
  },
  "ThunksDB": { "Vulkan": 1, "GL": 1, "drm": 1, "WaylandClient": 1, "asound": 1 }
}
EOF

