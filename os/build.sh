#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "${script_dir}/.." && pwd)
output_dir=${STEARLIGHT_ARMADA_OUTPUT_DIR:-"${repo_dir}/out/stearlight-armada"}
image_tag=${STEARLIGHT_ARMADA_IMAGE_TAG:-stearlight-armada:dev}
version=${STEARLIGHT_ARMADA_VERSION:-"$(date -u +%Y%m%d).$(git -C "${repo_dir}" rev-parse --short HEAD)"}

command -v docker >/dev/null || { echo 'Docker is required' >&2; exit 1; }
docker buildx version >/dev/null
mkdir -p "${output_dir}"
output_dir=$(CDPATH= cd -- "${output_dir}" && pwd)
rm -rf "${output_dir}/image" "${output_dir}/qcow2"
mkdir -p "${output_dir}/image" "${output_dir}/qcow2"

docker buildx build --platform linux/arm64 --load \
  --build-arg "ARMADA_VERSION=${version}" \
  --tag "${image_tag}" \
  --file "${script_dir}/Containerfile" "${repo_dir}"

bib_image=${BOOTC_IMAGE_BUILDER:-quay.io/centos-bootc/bootc-image-builder:latest}
cat > "${output_dir}/disk.toml" <<'EOF'
[[customizations.filesystem]]
mountpoint = "/"
minsize = "8 GiB"
EOF

docker run --rm --privileged \
  -v "${output_dir}/disk.toml:/config.toml:ro" \
  -v "${output_dir}/image:/output" \
  "${bib_image}" --type raw --target-arch arm64 \
  --rootfs btrfs --use-librepo=True \
  --config /config.toml "${image_tag}"

docker run --rm --privileged \
  -v "${output_dir}/disk.toml:/config.toml:ro" \
  -v "${output_dir}/qcow2:/output" \
  "${bib_image}" --type qcow2 --target-arch arm64 \
  --rootfs btrfs --use-librepo=True \
  --config /config.toml "${image_tag}"

raw_image=$(find "${output_dir}/image" -type f \( -name '*.raw' -o -name 'disk.raw' \) -print -quit)
if [[ -z "${raw_image}" ]]; then
  echo "bootc-image-builder did not produce a raw image under ${output_dir}/image" >&2
  exit 1
fi
artifact_date=$(date -u +%Y%m%d)
artifact="${output_dir}/stearlight-os-${artifact_date}rp4.img.gz"
gzip -c "${raw_image}" > "${artifact}"
echo "Built Fedora bootc Pi 4 image: ${artifact}"
