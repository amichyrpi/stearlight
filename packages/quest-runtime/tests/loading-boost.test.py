from pathlib import Path
import subprocess
import tempfile

header = Path(__file__).resolve().parents[1] / 'app/src/main/cpp/loading_boost.h'
with tempfile.TemporaryDirectory() as folder:
    source, binary = Path(folder) / 'check.cpp', Path(folder) / 'check'
    source.write_text('#include "' + str(header) + '"\n#include <cassert>\nint main(){LoadingBoost b;assert(!b.update(0,true,false));assert(b.update(1,true,true));assert(b.update(15,true,true));assert(!b.update(16,true,true));assert(!b.update(20,false,true));assert(!b.update(21,true,true));assert(!b.update(76,false,true));assert(b.update(77,true,true));assert(!b.update(78,true,false));assert(!b.update(79,true,true));}\n')
    subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    print('Loading boost duration, cooldown and focus-loss checks passed')
