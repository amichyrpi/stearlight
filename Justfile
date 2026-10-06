set shell := ["bash", "-eu", "-c"]
set windows-shell := ["powershell.exe", "-NoLogo", "-NoProfile", "-Command"]

default:
    @just --list

# Run focused OS source and lifecycle checks.
check:
    bash build_files/check.sh

# Build the ARM64 image on Linux (requires root and Docker).
build output="dist":
    sudo bash build_files/build-image.sh "{{ output }}"

# Build the bundled Windows QEMU in MSYS2 UCRT64.
packages work="../stearlight-host-build" output="dist/qemu":
    bash packages/windows-runtime/build-runtime.sh "{{ work }}" "{{ output }}"

# Assemble a downloaded image/runtime into the runnable Windows ZIP.
package input="dist" output="stearlight-frame-windows.zip":
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File post_process/package-windows.ps1 -PackageDirectory "{{ input }}" -ArchivePath "{{ output }}"

# Run an extracted package on Windows.
run-vm directory="dist":
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File post_process/start-vm.ps1 -VmDirectory "{{ directory }}"
