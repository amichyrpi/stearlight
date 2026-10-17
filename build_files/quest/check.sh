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
python3 packages/quest-runtime/tests/network-bridge.test.py
