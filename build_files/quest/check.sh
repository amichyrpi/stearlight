#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
while IFS= read -r file; do bash -n "$file"; done < <(find build_files/quest post_process/quest packages/quest-runtime/linux -name '*.sh')
python3 - <<'PY'
import ast
from pathlib import Path
for folder in ('build_files/quest', 'post_process/quest', 'packages/quest-runtime/linux'):
    for file in Path(folder).glob('*.py'):
        ast.parse(file.read_text(), filename=str(file))
PY
python3 tests/quest-build-test.py
python3 packages/quest-runtime/tests/clean-build.test.py
python3 tests/recovery-test.py
python3 packages/quest-runtime/tests/rootfs-archive.test.py
python3 packages/quest-runtime/tests/clock-bridge.test.py
python3 packages/quest-runtime/tests/battery-snapshot.test.py
python3 packages/quest-runtime/tests/steam-os-update.test.py
python3 packages/quest-runtime/tests/os-update-activation.test.py
python3 packages/quest-runtime/tests/audio-queue.test.py
python3 packages/quest-runtime/tests/audio-identity.test.py
python3 packages/quest-runtime/tests/loading-boost.test.py
python3 packages/quest-runtime/tests/mountinfo-cache.test.py
python3 packages/quest-runtime/tests/steam-input.test.py
python3 packages/quest-runtime/tests/steam-integrity.test.py
python3 packages/quest-runtime/tests/frame-menus.test.py
if command -v node >/dev/null 2>&1; then
    node packages/quest-runtime/tests/frame-menu-ui.test.cjs
else
    echo 'Frame menu JavaScript test skipped: Node is not installed.'
fi
python3 packages/quest-runtime/tests/steam-gamepad-launch.test.py
python3 packages/quest-runtime/tests/sdl-gamepad.test.py
python3 packages/quest-runtime/tests/sdl3-resolver.test.py
python3 packages/quest-runtime/tests/video-queue.test.py
python3 packages/quest-runtime/tests/video-codec.test.py
python3 packages/quest-runtime/tests/streaming-input.test.py
python3 packages/quest-runtime/tests/streaming-socket.test.py
python3 packages/quest-runtime/tests/video-format.test.py
python3 packages/quest-runtime/tests/game-launch-wrapper.test.py
python3 packages/quest-runtime/tests/game-runtime.test.py
python3 packages/quest-runtime/tests/proton-runtime.test.py
python3 packages/quest-runtime/tests/game-reaper.test.py
python3 packages/quest-runtime/tests/fex-signal.test.py
test_binary=$(mktemp)
trap 'rm -f -- "$test_binary"' EXIT
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ipackages/quest-runtime/app/src/main/cpp -isystem packages/openvr/headers packages/quest-runtime/tests/hand-skeleton.test.cpp -o "$test_binary"
"$test_binary"
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ipackages/quest-runtime/app/src/main/cpp packages/quest-runtime/tests/display-pulse.test.cpp -o "$test_binary"
"$test_binary"
g++ -O2 -std=c++17 -Wall -Wextra -Werror -pthread packages/quest-runtime/tests/gpu-frames.test.cpp -o "$test_binary"
"$test_binary"
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ipackages/quest-runtime/app/src/main/cpp packages/quest-runtime/tests/motion.test.cpp -o "$test_binary"
"$test_binary"
g++ -O2 -std=c++17 -Wall -Wextra -Werror -Ipackages/quest-runtime/app/src/main/cpp packages/quest-runtime/tests/external-gamepad.test.cpp -o "$test_binary"
"$test_binary"
g++ -std=c++17 -Wall -Wextra -Werror -fsyntax-only -Ipackages/quest-runtime/app/src/main/cpp packages/quest-runtime/linux/sdl3-gamepad.cpp
cc -std=c11 -Wall -Wextra -Werror packages/quest-runtime/tests/syscall-fast-path.test.c -o "$test_binary"
"$test_binary"
cc -O2 -std=c11 -Wall -Wextra -Werror packages/quest-runtime/tests/capture-matrix.test.c -o "$test_binary"
"$test_binary"
python3 packages/quest-runtime/tests/capture-matrix-abi.test.py
python3 packages/quest-runtime/tests/network-bridge.test.py
python3 packages/quest-runtime/tests/network-interfaces.test.py
python3 packages/quest-runtime/tests/socket-registry.test.py
