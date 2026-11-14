#!/usr/bin/env python3
"""Build the optional Standable bridge; no Standable binaries are bundled."""
import argparse
from pathlib import Path
import shutil
import subprocess

REVISION = "6af5d9ae36a5385161d7e0cc0d0b7b08b8bc6bcc"
REPO = Path(__file__).resolve().parents[2]


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("native", "windows"))
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    source, build, output = (path.resolve() for path in (args.source, args.build, args.output))
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != REVISION:
        parser.error("Ignition checkout must be pinned to " + REVISION)
    patch = REPO / "build_files/quest/ignition-arm64.patch"
    applied = subprocess.run(["git", "-C", str(source), "apply", "--reverse", "--check", str(patch)],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if applied.returncode:
        run("git", "-C", source, "apply", "--check", patch)
        run("git", "-C", source, "apply", patch)
    build.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    common = ["-DIGNITION_QUEST_FILE_BRIDGE=ON", "-DCMAKE_BUILD_TYPE=Release"]
    tests = REPO / "packages/quest-runtime/tests"
    common += [f"-DQUEST_IPC_PROBE_SOURCE={(tests / 'standable-ipc.cpp').as_posix()}"]
    if args.target == "native":
        toolchain = build / "quest-aarch64.cmake"
        toolchain.write_text("set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR aarch64)\n"
                             "set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)\n"
                             "set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)\n")
        run(args.cmake, "-S", source, "-B", build, "-G", "Ninja", *common,
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}",
            f"-DQUEST_LAZY_PROVIDER_SOURCE={(REPO / 'packages/quest-runtime/linux/standable-provider.cpp').as_posix()}")
        run(args.cmake, "--build", build, "--target", "driver_ignition", "standable_ipc", "-j", "4")
        shutil.copyfile(build / "Linux/lib/libdriver_ignition.so", output / "driver_standable.so")
        symbols = subprocess.check_output(["aarch64-linux-gnu-readelf", "--dyn-syms", "--wide",
                                           str(output / "driver_standable.so")], text=True)
        if "HmdDriverFactory" not in symbols or any(name in symbols for name in (
                "VRDriverContext", "VRServerDriverHost", "OpenVRInternal_ModuleContext")):
            raise RuntimeError("Driver exports must isolate the OpenVR context")
        shutil.copyfile(build / "Linux/bin/standable_ipc", output / "standable-ipc-arm64")
        run("aarch64-linux-gnu-g++", "-O2", "-Wall", "-Wextra", "-Werror", "-isystem",
            source / "externals/openvr", tests / "standable-native-probe.cpp", "-ldl",
            "-o", output / "standable-native-probe")
    else:
        run(args.cmake, "-S", source / "projects/ignition_server", "-B", build,
            "-G", "Visual Studio 17 2022", "-A", "x64", *common, f"-DIGNITION_ROOT={source.as_posix()}")
        run(args.cmake, "--build", build, "--config", "Release", "--target",
            "ignition_server", "standable_ipc", "-j", "4")
        for name in ("ignition_server.exe", "standable_ipc.exe"):
            shutil.copyfile(build / "Release" / name, output / name)
        shutil.copyfile(build / "ignition_bridge/Release/ignition_bridge.dll", output / "ignition_bridge.dll")
    shutil.copyfile(source / "LICENSE", output / "Ignition-LICENSE.txt")
    (output / "ignition-revision.txt").write_text(REVISION + "\n")


if __name__ == "__main__":
    main()
