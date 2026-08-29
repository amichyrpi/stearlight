#!/bin/bash
set -euo pipefail

# FEX's ARM64 Steam rootfs provides the x86 userspace used by Proton. Keep the
# URL and digest explicit so image builds are reproducible and fail closed.
FEX_ROOTFS_URL="https://rootfs.fex-emu.gg/ArchLinux/2026-08-11/ArchLinux.sqsh"
FEX_ROOTFS_SHA256="5d0c1a38590c68e5c2597c2c8a26d2f80170b1b738c857d63e1cdadada5f5f2a"
FEX_ROOTFS_DIR=/usr/share/fex-emu/RootFS
mkdir -p "$FEX_ROOTFS_DIR" /usr/share/guestos/fex-mesa
curl --retry 3 --retry-delay 2 -fsSL -o "$FEX_ROOTFS_DIR/ArchLinux.sqsh" "$FEX_ROOTFS_URL"
printf '%s  %s\n' "$FEX_ROOTFS_SHA256" "$FEX_ROOTFS_DIR/ArchLinux.sqsh" | sha256sum -c -
unsquashfs -cat "$FEX_ROOTFS_DIR/ArchLinux.sqsh" graphics_provider.json | python3 -m json.tool >/dev/null

# CachyOS Proton 11 ARM64 is the compatibility tool exposed to Steam. The
# release checksum is fetched alongside the archive and verified before it is
# installed into bootc's immutable /usr tree.
PROTON_VER="11.0-20260703-slr"
PROTON_ARCHIVE="proton-cachyos-${PROTON_VER}-arm64"
PROTON_TAR="${PROTON_ARCHIVE}.tar.xz"
PROTON_BASE="https://github.com/CachyOS/proton-cachyos/releases/download/cachyos-${PROTON_VER}"
curl --retry 6 --retry-delay 5 -fsSL -o "/tmp/${PROTON_TAR}" "${PROTON_BASE}/${PROTON_TAR}"
curl --retry 6 --retry-delay 5 -fsSL -o "/tmp/${PROTON_ARCHIVE}.sha512sum" "${PROTON_BASE}/${PROTON_ARCHIVE}.sha512sum"
(cd /tmp && sha512sum -c "${PROTON_ARCHIVE}.sha512sum")

PROTON_DIR=/usr/share/steam/compatibilitytools.d
mkdir -p "$PROTON_DIR"
tar -xJf "/tmp/${PROTON_TAR}" -C "$PROTON_DIR"
test -d "$PROTON_DIR/$PROTON_ARCHIVE"
rm -rf "$PROTON_DIR/proton-cachyos-11.0-arm64"
mv "$PROTON_DIR/$PROTON_ARCHIVE" "$PROTON_DIR/proton-cachyos-11.0-arm64"
sed -i '/require_tool_appid/d' "$PROTON_DIR/proton-cachyos-11.0-arm64/toolmanifest.vdf" || true
rm -f "/tmp/${PROTON_TAR}" "/tmp/${PROTON_ARCHIVE}.sha512sum"

echo "Installed FEX ARM64 rootfs and CachyOS Proton 11 ARM64"
