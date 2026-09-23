#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
if [[ -z ${STEARLIGHT_OUTPUT_DIR:-} ]]; then
  echo "Set STEARLIGHT_OUTPUT_DIR to an F:/ or G:/ mounted path; C: output is disabled." >&2
  exit 1
fi
output_dir=${STEARLIGHT_OUTPUT_DIR}
image_tag=${STEARLIGHT_IMAGE_TAG:-stearlight-os-rootfs:dev}
bake_steam=${STEARLIGHT_BAKE_STEAM:-1}
build_date=${STEARLIGHT_BUILD_DATE:-$(date -u +%Y%m%d)}

command -v docker >/dev/null 2>&1 || {
  echo "Docker with buildx is required (Docker Desktop with WSL2 is supported)." >&2
  exit 1
}
docker buildx version >/dev/null

case "${output_dir}" in
  *..*)
    echo "Refusing an output path containing parent traversal: ${output_dir}" >&2
    exit 1
    ;;
  /mnt/f/*|/mnt/g/*) ;;
  /mnt/*)
    echo "Refusing a Windows-mounted output outside /mnt/f or /mnt/g: ${output_dir}" >&2
    exit 1
    ;;
esac
mkdir -p "${output_dir}"
output_dir=$(CDPATH= cd -- "${output_dir}" && pwd)
case "${output_dir}" in
  /mnt/f|/mnt/g)
    echo "Refusing to use an entire F: or G: volume as the output directory." >&2
    exit 1
    ;;
  /mnt/f/*|/mnt/g/*) ;;
  /mnt/*)
    echo "Refusing canonical output on an unapproved Windows-mounted drive: ${output_dir}" >&2
    exit 1
    ;;
  /|"${repo_dir}"|"${repo_dir}/os"|"${script_dir}"|"${repo_dir}/build"|"${repo_dir}/out")
    echo "Refusing unsafe output directory: ${output_dir}" >&2
    exit 1
    ;;
  *)
    # Native Linux CI/workstations do not expose Windows drive letters; keep
    # their explicit output directories usable while the WSL /mnt path stays
    # restricted to F: and G: above.
    ;;
esac
rm -rf "${output_dir}/image"
mkdir -p "${output_dir}/image"
# Keep the Docker build context valid even when a source archive omits the
# empty generated directory.  A subscribed build may populate it below with
# Valve's private FEX/Proton depots; an offline build still gets an empty
# directory and therefore a reproducible image without those optional tools.
mkdir -p "${script_dir}/generated"

if [[ -n ${STEAM_USERNAME:-} ]]; then
  echo "Downloading FEX and Proton from Steam with the supplied account..."
  bash "${script_dir}/scripts/download-steam-compat.sh" \
    "${script_dir}/generated/steam-tools"
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
