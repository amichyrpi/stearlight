#!/usr/bin/env python3
"""Build the SDL 2.0.14-compatible Celeste renderer with upstream pool reuse."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile


def repair_buffer_reuse(source):
    path = source / "src/FNA3D_Driver_Vulkan.c"
    text = path.read_text()
    before = "\t\t\twhile (CURIDX < vulkanBuffer->subBufferCount && SUBBUF->bound != -1)"
    after = "\t\t\t/* Search all available slots, including earlier completed draws. */\n\t\t\tCURIDX = 0;\n" + before
    if text.count(before) != 1:
        raise ValueError("Unrecognized FNA3D dynamic buffer search")
    if after not in text:
        path.write_text(text.replace(before, after))


def build(work, destination, archives):
    sources = work / "celeste-fna-sources"
    sources.mkdir(parents=True, exist_ok=True)
    folders = {}
    for name, archive_path in archives.items():
        with tarfile.open(archive_path) as archive:
            folder = archive.getnames()[0].split('/')[0]
            archive.extractall(sources, filter="data")
        folders[name] = sources / folder
    source = folders["renderer"]
    for name, target in (("mojoshader", "MojoShader"), ("vulkan-headers", "Vulkan-Headers")):
        shutil.copytree(folders[name], source / target, dirs_exist_ok=True)
    repair_buffer_reuse(source)
    sdk = work / "celeste-fna-sdk"
    sdk.mkdir(exist_ok=True)
    # Only an import library: Celeste supplies its real SDL at runtime. This
    # never enters the payload. Headers match that SDL's public ABI.
    (sdk / "sdl-import.c").write_text("void quest_sdl_import_library(void) {}\n")
    subprocess.run(["x86_64-linux-gnu-gcc", "-shared", "-fPIC",
                    "-Wl,-soname,libSDL2-2.0.so.0", str(sdk / "sdl-import.c"),
                    "-o", str(sdk / "libSDL2-2.0.so.0")], check=True)
    output = work / "celeste-fna-build"
    subprocess.run(["cmake", "-S", str(source), "-B", str(output), "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_SYSTEM_NAME=Linux",
                    "-DCMAKE_SYSTEM_PROCESSOR=x86_64", "-DCMAKE_C_COMPILER=x86_64-linux-gnu-gcc",
                    f"-DSDL2_INCLUDE_DIRS={folders['sdl-headers'] / 'include'}",
                    f"-DSDL2_LIBRARIES={sdk / 'libSDL2-2.0.so.0'}",
                    "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--no-as-needed"], check=True)
    subprocess.run(["cmake", "--build", str(output), "-j", "6"], check=True)
    destination.mkdir(parents=True, exist_ok=True)
    library = destination / "libFNA3D.so.0"
    shutil.copyfile(output / "libFNA3D.so.0.21.06", library)
    library.chmod(0o755)
    (destination / "profile.json").write_text(json.dumps({
        "version": "21.06-quest-reuse", "sha256": hashlib.sha256(library.read_bytes()).hexdigest()
    }, indent=2) + "\n")
