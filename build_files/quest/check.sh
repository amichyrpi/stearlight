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
python3 tests/recovery-test.py
python3 packages/quest-runtime/tests/rootfs-archive.test.py
python3 packages/quest-runtime/tests/audio-queue.test.py
python3 packages/quest-runtime/tests/mountinfo-cache.test.py
python3 packages/quest-runtime/tests/steam-input.test.py
python3 packages/quest-runtime/tests/game-launch-wrapper.test.py
python3 packages/quest-runtime/tests/fex-signal.test.py
test_binary=$(mktemp)
trap 'rm -f -- "$test_binary"' EXIT
cc -std=c11 -Wall -Wextra -Werror packages/quest-runtime/tests/syscall-fast-path.test.c -o "$test_binary"
"$test_binary"
python3 packages/quest-runtime/tests/network-bridge.test.py
