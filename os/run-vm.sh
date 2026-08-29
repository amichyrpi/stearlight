#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
image=${1:-"${repo_dir}/out/stearlight-armada/qcow2/disk.qcow2"}
[[ -f "${image}" ]] || { echo "Missing ${image}; run os/build.sh first" >&2; exit 1; }

port=${STEARLIGHT_VM_PORT:-8006}
exec docker run --rm --privileged \
  --device=/dev/kvm \
  --publish "127.0.0.1:${port}:8006" \
  --env CPU_CORES=4 --env RAM_SIZE=8G --env DISK_SIZE=16G \
  --env TPM=Y --env GPU=Y \
  --volume "${image}:/boot.qcow2:ro" \
  docker.io/qemux/qemu
