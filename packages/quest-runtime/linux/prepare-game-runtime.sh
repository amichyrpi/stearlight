#!/bin/sh
set -eu
root=${STEARLIGHT_STEAM_ROOT:-/opt/steam}
quest=${STEARLIGHT_QUEST_ROOT:-/opt/quest}
if [ -f "$quest/prepare-fex.py" ];then
 /usr/bin/python3 "$quest/prepare-fex.py"
fi
# Steam's desktop default marks games at 900, ahead of most cached Android
# apps for memory reclamation. Keep the foreground app's inherited priority.
wrapper="$root/linuxarm64/steam-launch-wrapper"
adapter="$quest/game-launch-wrapper"
if [ -f "$wrapper" ] && [ -f "$adapter" ] && ! cmp -s "$wrapper" "$adapter";then
 if grep -aq 'quest-game-launch-wrapper' "$wrapper";then
  [ -f "$wrapper.quest-real" ] || exit 1
 elif [ "$(od -An -tx1 -N4 "$wrapper" | tr -d ' \n')" = 7f454c46 ];then
  mv "$wrapper" "$wrapper.quest-real"
 else
  echo 'Quest game launcher adapter does not match this build.' >&2
  exit 1
 fi
 cp "$adapter" "$wrapper.quest-next"
 chmod 755 "$wrapper.quest-next" "$wrapper.quest-real"
 mv "$wrapper.quest-next" "$wrapper"
fi
player="$root/steamrtarm64/fossilize_replay"
launcher="$quest/shader-replay-launcher"
if [ -f "$player" ] && [ -f "$launcher" ] && ! cmp -s "$player" "$launcher";then
 if [ -f "$player.real" ] && grep -aq 'quest-shader-replay-launcher' "$player";then
  :
 else
  [ "$(od -An -tx1 -N4 "$player" | tr -d ' \n')" = 7f454c46 ] || exit 1
  mv "$player" "$player.real"
 fi
 cp "$launcher" "$player.quest-next"
 chmod 755 "$player.quest-next" "$player.real"
 mv "$player.quest-next" "$player"
fi

# ARM64 Proton needs the app's existing PRoot filesystem; pressure-vessel
# cannot create another namespace inside an unprivileged Android application.
entry="$root/steamapps/common/SteamLinuxRuntime_4-arm64/_v2-entry-point"
adapter="$quest/proton-runtime-entry.sh"
if [ -f "$entry" ] && [ -f "$adapter" ] && ! cmp -s "$entry" "$adapter";then
 if ! grep -aq 'quest-proton-runtime-entry' "$entry";then
  cp "$entry" "$entry.quest-original"
 fi
 cp "$adapter" "$entry.quest-next"
 chmod 755 "$entry.quest-next"
 mv "$entry.quest-next" "$entry"
fi

# Steam's actual reaper still owns game cleanup. The audio namespace helper
# otherwise keeps a second reaper alive despite namespaces being unavailable.
audio="$root/linuxarm64/pw-audio-namespace"
adapter="$quest/audio-namespace-launcher.sh"
if [ -f "$audio" ] && [ -f "$adapter" ] && ! cmp -s "$audio" "$adapter";then
 if ! grep -aq 'quest-audio-namespace-launcher' "$audio";then
  cp "$audio" "$audio.quest-original"
 fi
 cp "$adapter" "$audio.quest-next"
 chmod 755 "$audio.quest-next"
 mv "$audio.quest-next" "$audio"
fi
tool="$root/steamapps/common/FEX-Emu/fex-compat-tool"
if [ -f "$tool" ] && [ -f "$quest/fex-game-launcher.py" ] && ! cmp -s "$tool" "$quest/fex-game-launcher.py";then
 cp "$quest/fex-game-launcher.py" "$tool.quest-next"
 chmod 755 "$tool.quest-next"
 mv "$tool.quest-next" "$tool"
fi
