#!/bin/sh
set -eu
root=${STEARLIGHT_STEAM_ROOT:-/opt/steam}
quest=${STEARLIGHT_QUEST_ROOT:-/opt/quest}
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
tool="$root/steamapps/common/FEX-Emu/fex-compat-tool"
if [ -f "$tool" ] && [ -f "$quest/fex-game-launcher.py" ] && ! cmp -s "$tool" "$quest/fex-game-launcher.py";then
 cp "$quest/fex-game-launcher.py" "$tool.quest-next"
 chmod 755 "$tool.quest-next"
 mv "$tool.quest-next" "$tool"
fi
