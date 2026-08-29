#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "${script_dir}/../.." && pwd)
output_dir=${STEARLIGHT_OUTPUT_DIR:-"${repo_dir}/out/stearlight-os"}
image_tag=${STEARLIGHT_IMAGE_TAG:-stearlight-os-rootfs:dev}
bake_steam=${STEARLIGHT_BAKE_STEAM:-1}
build_date=${STEARLIGHT_BUILD_DATE:-$(date -u +%Y%m%d)}

command -v docker >/dev/null 2>&1 || {
  echo "Docker with buildx is required (Docker Desktop with WSL2 is supported)." >&2
  exit 1
}
docker buildx version >/dev/null

mkdir -p "${output_dir}"
output_dir=$(CDPATH= cd -- "${output_dir}" && pwd)
case "${output_dir}" in
  /|"${repo_dir}"|"${repo_dir}/os"|"${repo_dir}/os/alpine")
    echo "Refusing unsafe output directory: ${output_dir}" >&2
    exit 1
    ;;
esac
rm -rf "${output_dir}/image"
mkdir -p "${output_dir}/image"

if [[ -n ${STEAM_USERNAME:-} ]]; then
  echo "Downloading FEX and Proton from Steam with the supplied account..."
  bash "${script_dir}/scripts/download-steam-compat.sh" \
    "${repo_dir}/os/alpine/generated/steam-tools"
fi

echo "Building the aarch64 Alpine root filesystem..."
docker buildx build --platform linux/arm64 \
  --build-arg "STEARLIGHT_BAKE_STEAM=${bake_steam}" \
  --build-arg "STEARLIGHT_BUILD_DATE=${build_date}" \
  --target image --tag "${image_tag}" \
  --output "type=local,dest=${output_dir}/image" \
  --file "${script_dir}/Dockerfile" "${repo_dir}"
echo "Built ${output_dir}/image/stearlight-os-${build_date}rp4.img.gz"
echo "The image is not flashed automatically. Verify the target device before writing it."
