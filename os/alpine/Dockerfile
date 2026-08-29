# syntax=docker/dockerfile:1.7
ARG ALPINE_TAG=edge
ARG STEARLIGHT_BUILD_DATE=unknown

FROM --platform=$TARGETPLATFORM debian:bookworm-slim AS steam-runtime
ARG STEARLIGHT_BAKE_STEAM=1
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl dbus libasound2 libgbm1 libgl1-mesa-dri \
    libgtk2.0-0 libnss3 libopenal1 libpipewire-0.3-0 libvulkan1 \
    libx11-6 libxcomposite1 libxdamage1 libxfixes3 libxrandr2 \
    libxss1 libxtst6 mesa-vulkan-drivers unzip xz-utils xvfb \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -u 1000 -s /bin/bash stearlight
COPY --chmod=0755 os/alpine/scripts/install-steam-arm64.sh /usr/local/bin/
USER stearlight
RUN if [ "$STEARLIGHT_BAKE_STEAM" = 1 ]; then install-steam-arm64.sh; fi
USER root

FROM --platform=$TARGETPLATFORM alpine:${ALPINE_TAG} AS receiver-build
RUN apk add --no-cache alsa-lib-dev build-base cmake ffmpeg-dev git \
    libdrm-dev libx11-dev mbedtls3-dev ninja openssl-dev pkgconf protobuf-c-dev \
    sdl2-dev sdl2_ttf-dev
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja \
      -DSVRT_BUILD_DRIVER=OFF -DSVRT_BUILD_UTILITY=OFF \
      -DSVRT_BUILD_TESTS=OFF -DSVRT_BUILD_VENDORED_SDL=OFF \
      -DSVRT_STEARLIGHT_OS=ON -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
    && cmake --build /build --parallel \
    && DESTDIR=/staging cmake --install /build

FROM --platform=$TARGETPLATFORM alpine:${ALPINE_TAG} AS rootfs
RUN printf '%s\n' \
      https://dl-cdn.alpinelinux.org/alpine/edge/main \
      https://dl-cdn.alpinelinux.org/alpine/edge/community \
      > /etc/apk/repositories \
    && apk add --no-cache \
      alpine-base alsa-lib avahi bash bubblewrap ca-certificates chrony dbus dhcpcd \
      doas e2fsprogs-extra ffmpeg gamescope iwd linux-rpi \
      linux-firmware-cypress mesa-dri-gallium \
      mesa-egl mesa-gbm mesa-gles mesa-vulkan-broadcom mbedtls3 monado openrc \
      openssh pipewire pipewire-alsa pipewire-pulse protobuf-c raspberrypi-bootloader \
      sdl2 sdl2_ttf shadow su-exec util-linux-misc wireplumber xauth xvfb \
      xwayland
RUN for group in audio input render video wheel; do \
      grep -q "^${group}:" /etc/group || addgroup -S "${group}"; \
    done \
    && addgroup -S stearlight \
    && adduser -D -u 1000 -G stearlight -s /bin/ash stearlight \
    && addgroup stearlight audio \
    && addgroup stearlight input \
    && addgroup stearlight render \
    && addgroup stearlight tty \
    && addgroup stearlight video \
    && addgroup stearlight wheel \
    && printf 'stearlight:stearlight\n' | chpasswd \
    && printf 'root:stearlight\n' | chpasswd \
    && mkdir -p /home/stearlight/.local/share /opt/stearlight/steam-runtime \
                /run/openrc \
    && touch /run/openrc/softlevel
COPY --from=receiver-build /staging/ /
COPY --from=steam-runtime / /opt/stearlight/steam-runtime/
COPY --from=steam-runtime --chown=1000:1000 /home/stearlight/ /home/stearlight/
COPY os/alpine/overlay/ /
COPY os/alpine/generated/ /etc/stearlight/
RUN chown -R stearlight:stearlight /home/stearlight \
    && chmod 0755 /etc/init.d/stearlight-session \
                  /etc/init.d/stearlight-wifi \
                  /usr/local/libexec/stearlight/session \
                  /usr/local/libexec/stearlight/provision-wifi \
                  /home/stearlight/.local/share/Steam/launch-steam.sh \
    && rc-update add devfs sysinit \
    && rc-update add dmesg sysinit \
    && rc-update add mdev sysinit \
    && rc-update add hwdrivers sysinit \
    && rc-update add modules boot \
    && rc-update add localmount boot \
    && rc-update add sysctl boot \
    && rc-update add dbus default \
    && rc-update add avahi-daemon default \
    && rc-update add chronyd default \
    && rc-update add dhcpcd default \
    && rc-update add iwd default \
    && rc-update add local default \
    && rc-update add sshd default \
    && rc-update add stearlight-wifi default \
    && rc-update add stearlight-session default \
    && chmod 0755 /etc/local.d/stearlight-performance.start \
    && if [ -f /etc/stearlight/wifi.env ]; then \
         chmod 0600 /etc/stearlight/wifi.env; \
       fi \
    && if [ -d /etc/stearlight/steam-tools/fex ]; then \
         mkdir -p /home/stearlight/.local/share/Steam/steamapps/common; \
         cp -a /etc/stearlight/steam-tools/fex \
           /home/stearlight/.local/share/Steam/steamapps/common/FEX; \
         cp -a /etc/stearlight/steam-tools/proton11 \
           '/home/stearlight/.local/share/Steam/steamapps/common/Proton 11.0 ARM64'; \
         cp /etc/stearlight/steam-tools/appmanifest_*.acf \
           /home/stearlight/.local/share/Steam/steamapps/; \
         chown -R stearlight:stearlight \
           /home/stearlight/.local/share/Steam/steamapps; \
         rm -rf /etc/stearlight/steam-tools; \
       fi

LABEL org.opencontainers.image.title="Stearlight OS" \
      org.opencontainers.image.description="Alpine aarch64 Steam/OpenXR appliance root filesystem"

FROM --platform=$BUILDPLATFORM alpine:${ALPINE_TAG} AS image-build
ARG STEARLIGHT_BUILD_DATE
RUN apk add --no-cache dosfstools e2fsprogs genimage mtools
COPY --from=rootfs / /work/rootfs/
COPY os/alpine/boot/ /work/boot-config/
COPY os/alpine/genimage.cfg /work/genimage.cfg
RUN mkdir -p /work/input/boot /work/output /work/tmp \
    && cp -a /work/rootfs/boot/. /work/input/boot/ \
    && cp /work/boot-config/config.txt /work/input/boot/config.txt \
    && cp /work/boot-config/cmdline.txt /work/input/boot/cmdline.txt \
    && rm -rf /work/rootfs/boot \
    && mkdir -p /work/rootfs/boot \
    && genimage --config /work/genimage.cfg --rootpath /work/rootfs \
         --inputpath /work/input --outputpath /work/output \
         --tmppath /work/tmp \
    && image="stearlight-os-${STEARLIGHT_BUILD_DATE}rp4.img.gz" \
    && gzip -9 -c /work/output/stearlight-os-rpi4.img > "/work/output/${image}" \
    && sha256sum "/work/output/${image}" > "/work/output/${image}.sha256" \
    && rm -f /work/output/boot.vfat /work/output/root.ext4 \
             /work/output/stearlight-os-rpi4.img

FROM scratch AS image
COPY --from=image-build /work/output/ /
