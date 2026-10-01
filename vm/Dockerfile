# syntax=docker/dockerfile:1.7
ARG ALPINE_TAG=edge

# The full Bookworm image is used here because Docker Desktop's WSL/containerd
# importer can omit the compressed layer from bookworm-slim on this host.
# Keeping the glibc stages on the complete image makes /bin/sh and the Steam
# bootstrap files available deterministically.
FROM --platform=$TARGETPLATFORM debian:bookworm AS steam-bootstrap
RUN dpkg --add-architecture i386 \
    && apt-get -o Acquire::Retries=5 update \
    && apt-get -o Acquire::Retries=5 install -y --no-install-recommends \
         ca-certificates curl dbus-x11 libc-bin libc6:i386 \
         libdrm2 libdrm2:i386 libegl1 libegl1:i386 libgl1 libgl1:i386 \
         libgl1-mesa-dri libgl1-mesa-dri:i386 \
         libglx-mesa0 libglx-mesa0:i386 libglx0 libglx0:i386 \
         libvulkan1 libvulkan1:i386 mesa-vulkan-drivers mesa-vulkan-drivers:i386 \
         libglvnd0 libglvnd0:i386 \
         libllvm15 libllvm15:i386 \
         libglib2.0-0 libglib2.0-0:i386 \
         libx11-6:i386 libx11-xcb1:i386 libxdamage1:i386 libxext6:i386 \
         libxfixes3:i386 libxrandr2:i386 libxrender1:i386 libxcb1:i386 \
         libxcomposite1 libxcomposite1:i386 libxcursor1 libxcursor1:i386 \
         libxi6 libxi6:i386 libxinerama1 libxinerama1:i386 \
         libxss1 libxss1:i386 libxtst6 libxtst6:i386 \
         libxcb-dri2-0:i386 libxcb-dri3-0:i386 libxcb-glx0:i386 \
         libxcb-present0:i386 libxcb-sync1:i386 libxshmfence1:i386 \
         libxxf86vm1:i386 xz-utils xvfb bubblewrap \
     && rm -rf /var/lib/apt/lists/* \
     && chmod 4755 /usr/bin/bwrap \
     && mkdir -p /out

# Keep the multi-architecture Debian install cacheable. Runtime patch changes
# must not force another several-minute i386/amd64 apt transaction.
RUN curl -fL --retry 8 --retry-all-errors \
         -o /tmp/steam.deb \
         https://repo.steampowered.com/steam/archive/stable/steam_latest.deb \
     && dpkg-deb -x /tmp/steam.deb /out \
     && mkdir -p /out/lib /out/lib64 /out/sbin

RUN cp -L /lib/ld-linux.so.2 /out/lib/ \
     && cp -L /lib64/ld-linux-x86-64.so.2 /out/lib64/ \
     && cp -L /sbin/ldconfig /out/sbin/ldconfig.real \
     && mkdir -p /out/lib/i386-linux-gnu /out/usr/lib/i386-linux-gnu \
                /out/lib/x86_64-linux-gnu /out/usr/lib/x86_64-linux-gnu \
     && cp -a /lib/i386-linux-gnu/. /out/lib/i386-linux-gnu/ \
    && cp -a /usr/lib/i386-linux-gnu/. /out/usr/lib/i386-linux-gnu/ \
    && cp -a /lib/x86_64-linux-gnu/. /out/lib/x86_64-linux-gnu/ \
    && cp -a /usr/lib/x86_64-linux-gnu/. /out/usr/lib/x86_64-linux-gnu/ \
    && if [ -d /usr/share/vulkan/icd.d ]; then \
         mkdir -p /out/usr/share/vulkan/icd.d \
         && cp -a /usr/share/vulkan/icd.d/. /out/usr/share/vulkan/icd.d/; \
       fi \
    && mkdir -p /out/etc /out/var/cache/ldconfig \
    && printf '%s\n' \
         '/lib' '/lib64' '/lib/i386-linux-gnu' '/lib/x86_64-linux-gnu' \
          '/usr/lib' '/usr/lib/i386-linux-gnu' '/usr/lib/x86_64-linux-gnu' \
          > /out/etc/ld.so.conf \
     && ldconfig -r /out \
     && cp -L /out/etc/ld.so.cache /out/var/cache/ldconfig/ld.so.cache

COPY os/patch-steam-runtime.sh /tmp/patch-steam-runtime.sh
RUN mkdir -p /tmp/steam-bootstrap \
    && tar -xJf /out/usr/lib/steam/bootstraplinux_ubuntu12_32.tar.xz \
         -C /tmp/steam-bootstrap \
    && mkdir -p /tmp/steam-bootstrap/ubuntu12_32/steam-runtime/stearlight_libs_32 \
                /tmp/steam-bootstrap/ubuntu12_32/steam-runtime/stearlight_libs_64 \
    && for arch in i386-linux-gnu x86_64-linux-gnu; do \
         if [ "$arch" = i386-linux-gnu ]; then \
           runtime_libs=/tmp/steam-bootstrap/ubuntu12_32/steam-runtime/stearlight_libs_32; \
         else \
           runtime_libs=/tmp/steam-bootstrap/ubuntu12_32/steam-runtime/stearlight_libs_64; \
         fi; \
         find "/out/usr/lib/$arch" -maxdepth 1 \
           \( -type f -o -type l \) \
           \( -name 'libGL*' -o -name 'libEGL*' -o -name 'libGLES*' \
              -o -name 'libGLX*' -o -name 'libGLdispatch*' -o -name 'libdrm*' \
              -o -name 'libglapi*' -o -name 'libgbm*' -o -name 'libpciaccess*' \
              -o -name 'libatomic*' -o -name 'libelf*' -o -name 'libzstd*' \
              -o -name 'libexpat*' -o -name 'libffi*' -o -name 'libz*' \
              -o -name 'libsensors*' -o -name 'libedit*' -o -name 'libtinfo*' \
              -o -name 'libz3*' -o -name 'libxml2*' -o -name 'libicu*' \
              -o -name 'liblzma*' -o -name 'libbsd*' -o -name 'libmd*' \
              -o -name 'libwayland*' \
              -o -name 'libvulkan*' \
              -o -name 'libglib*.so*' -o -name 'libgobject*.so*' \
              -o -name 'libgio*.so*' -o -name 'libgmodule*.so*' \
              -o -name 'libgthread*.so*' -o -name 'libpcre*.so*' \
              -o -name 'libmount*.so*' -o -name 'libblkid*.so*' \
              -o -name 'libselinux*.so*' -o -name 'libsepol*.so*' \
              -o -name 'libuuid*.so*' -o -name 'libcap*.so*' \
              -o -name 'libLLVM*' -o -name 'libstdc++*' -o -name 'libgcc_s*' \
              -o -name 'libxcb*.so*' -o -name 'libX11*.so*' \
              -o -name 'libXau*.so*' -o -name 'libXdmcp*.so*' \
              -o -name 'libXdamage*.so*' -o -name 'libXext*.so*' \
              -o -name 'libXfixes*.so*' -o -name 'libXrandr*.so*' \
              -o -name 'libXrender*.so*' -o -name 'libXxf86vm*.so*' \
              -o -name 'libXcomposite*.so*' -o -name 'libXcursor*.so*' \
              -o -name 'libXi*.so*' -o -name 'libXinerama*.so*' \
              -o -name 'libXss*.so*' -o -name 'libXtst*.so*' \
              -o -name 'libxshmfence*.so*' \) \
           -exec cp -a {} "$runtime_libs"/ \; ; \
       if [ -d "/out/usr/lib/$arch/dri" ]; then \
           cp -a "/out/usr/lib/$arch/dri" "$runtime_libs"/; \
         fi; \
       done \
    && for abi in 32 64; do \
         runtime_libs=/tmp/steam-bootstrap/ubuntu12_32/steam-runtime/stearlight_libs_${abi}; \
         printf '%s\n' \
           '{' \
           '  "ICD": {' \
           '    "api_version": "1.1.230",' \
           '    "library_path": "./libvulkan_lvp.so"' \
           '  },' \
           '  "file_format_version": "1.0.0"' \
           '}' > "$runtime_libs/lvp_icd.json"; \
       done \
    && mkdir -p /out/usr/lib/steam/stearlight_libs_32 \
                /out/usr/lib/steam/stearlight_libs_64 \
    && cp -a /tmp/steam-bootstrap/ubuntu12_32/steam-runtime/stearlight_libs_32/. \
          /out/usr/lib/steam/stearlight_libs_32/ \
    && cp -a /tmp/steam-bootstrap/ubuntu12_32/steam-runtime/stearlight_libs_64/. \
          /out/usr/lib/steam/stearlight_libs_64/ \
    && chmod 0755 /tmp/patch-steam-runtime.sh \
     && /tmp/patch-steam-runtime.sh \
          /tmp/steam-bootstrap/ubuntu12_32/steam-runtime/run.sh \
     && tar -cJf /out/usr/lib/steam/bootstraplinux_ubuntu12_32.tar.xz \
          -C /tmp/steam-bootstrap .

# The ABI-specific Mesa seed is copied outside Valve's replaceable runtime so
# the runtime watcher can restore matching 32/64-bit DRI modules after an
# update. Extract Valve's current Gamepad UI once while building the VM.  The clean
# per-user tree is copied into the guest below; Steam still owns the first-run
# language, timezone, network, update, sign-in and tour screens.
COPY os/scripts/preextract-steam-client.sh /usr/local/bin/preextract-steam-client.sh
COPY os/scripts/sanitize-steam-profile.sh /usr/local/bin/sanitize-steam-profile.sh
RUN useradd --create-home --uid 1000 --shell /bin/bash steam \
    && chown -R steam:steam /home/steam \
    && chmod 0755 /usr/local/bin/preextract-steam-client.sh \
                  /usr/local/bin/sanitize-steam-profile.sh
USER steam
ENV HOME=/home/steam
RUN /usr/local/bin/preextract-steam-client.sh /out/usr/bin/steam
USER root

# SteamVR is a glibc process even though the OS shell is Alpine/musl. Build
# the VM-only OpenVR provider against glibc so vrserver can dlopen it; a musl
# shared object would carry libc.musl-x86_64.so.1 and be rejected by Valve's
# runtime loader.
FROM debian:bookworm AS fake-tracking-build
RUN apt-get -o Acquire::Retries=5 update \
    && apt-get -o Acquire::Retries=5 install -y --no-install-recommends build-essential ca-certificates git \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY os/driver/fake_tracking.cpp /src/fake_tracking.cpp
COPY os/driver/stearlight-fake /src/stearlight-fake
RUN for attempt in 1 2 3 4 5; do \
      rm -rf /tmp/openvr; \
      git -c http.version=HTTP/1.1 clone --depth 1 --branch v2.15.6 \
        https://github.com/ValveSoftware/openvr.git /tmp/openvr && exit 0; \
      echo "OpenVR source download failed (attempt $attempt/5); retrying" >&2; \
      sleep 5; \
    done; exit 1
RUN mkdir -p /out/stearlight-fake/bin/linux64 \
    && c++ -std=c++17 -fPIC -shared \
         -I/tmp/openvr/headers /src/fake_tracking.cpp \
         -static-libstdc++ -static-libgcc \
         -o /out/stearlight-fake/bin/linux64/driver_stearlight-fake.so \
    && cp -a /src/stearlight-fake/. /out/stearlight-fake/ \
    && chmod 0755 /out/stearlight-fake/bin/linux64/driver_stearlight-fake.so

FROM alpine:${ALPINE_TAG} AS rootfs
RUN for attempt in 1 2 3 4 5; do \
      apk add --no-cache \
        alpine-base alsa-lib bash bubblewrap coreutils dbus dbus-glib dhcpcd e2fsprogs eudev \
        ca-certificates fontconfig font-dejavu font-noto font-noto-all font-noto-cjk font-noto-emoji gcompat ibus linux-virt mesa-dri-gallium mesa-egl mesa-gbm mesa-gles \
        mesa-vulkan-swrast mesa-vulkan-virtio vulkan-loader dbus-x11 gamescope mbedtls3 openrc shadow \
        monado openxr pipewire \
        libxcb libxcomposite libxfixes libxscrnsaver libxtst \
        lsof pciutils python3 su-exec tar tzdata util-linux-misc xf86-input-libinput \
        xwayland \
        xz && exit 0; \
      echo "apk rootfs dependency download failed (attempt $attempt/5); retrying" >&2; \
      sleep 5; \
    done; exit 1
RUN for group in audio input render tty video wheel; do \
      grep -q "^${group}:" /etc/group || addgroup -S "${group}"; \
    done \
    && addgroup -S stearlight \
    && adduser -D -u 1000 -G stearlight -s /bin/ash stearlight \
    && for group in audio input render tty video wheel; do \
         addgroup stearlight "$group"; \
       done \
    && mkdir -p /var/lib/stearlight
COPY --from=steam-bootstrap /out/ /
COPY --from=steam-bootstrap --chown=1000:1000 \
     /home/steam/.local/share/Steam /home/stearlight/.local/share/Steam
# The VM is x86_64, so it can run the authorized Linux SteamVR payload
# directly. Keep Valve's binaries, web helper, dashboard resources and all
# shipped drivers together; vrstartup then owns vrserver/vrcompositor/
# vrdashboard and creates the real VRDashboardBar overlay.
COPY --from=steamvr /bin /opt/steamvr/bin
COPY --from=steamvr /drivers /opt/steamvr/drivers
COPY --from=steamvr /resources /opt/steamvr/resources
COPY --from=steamvr /content /opt/steamvr/content
COPY --from=steamvr /tools /opt/steamvr/tools
COPY --from=steamvr /steamxr_linux64.json /opt/steamvr/steamxr_linux64.json
# Replace the matching web UI/data bundle with the current authentic Steam
# Frame recovery assets. Recovery executables are ARM64 and are not copied
# into this x86_64 guest.
COPY --from=frame-ui /resources /opt/steamvr/resources
COPY --from=frame-ui /content /opt/steamvr/content
COPY --from=fake-tracking-build \
     /out/stearlight-fake \
     /opt/steamvr/drivers/stearlight-fake
RUN chmod -R a+rX /opt/steamvr \
    && find /opt/steamvr/bin -type f -exec chmod a+rx {} +
# The Valve runtime and Steam Frame recovery payload above are the only
# dashboard and laser resource sources; no duplicate UI assets are installed.
COPY os/steam-firstboot /usr/local/libexec/stearlight/steam-firstboot
COPY os/patch-steam-runtime.sh /usr/local/libexec/stearlight/patch-steam-runtime
COPY os/vm-overlay/ /
COPY os/overlay/usr/local/libexec/stearlight/patch-steamui-network /usr/local/libexec/stearlight/patch-steamui-network
COPY os/overlay/usr/bin/ /usr/bin/
COPY os/overlay/usr/share/gamescope-session-plus/ /usr/share/gamescope-session-plus/
COPY os/overlay/usr/share/wayland-sessions/ /usr/share/wayland-sessions/
COPY os/overlay/etc/steamos-oobe-image /etc/steamos-oobe-image
COPY os/overlay/etc/conf.d/stearlight-xr /etc/conf.d/stearlight-xr
COPY os/overlay/etc/init.d/stearlight-xr /etc/init.d/stearlight-xr
COPY os/overlay/usr/local/libexec/stearlight/xr-service /usr/local/libexec/stearlight/xr-service
COPY os/overlay/usr/local/libexec/stearlight/xr-run /usr/local/libexec/stearlight/xr-run
COPY os/overlay/usr/local/libexec/stearlight/launch-xr-game /usr/local/libexec/stearlight/launch-xr-game
COPY os/overlay/usr/local/libexec/stearlight/steamvr-native /usr/local/libexec/stearlight/steamvr-native
RUN fc-cache -f
RUN chown -R stearlight:stearlight /home/stearlight /var/lib/stearlight \
    && chmod 0700 /var/lib/stearlight \
    && mkdir -p /etc/first-boot \
    && if [ -f /usr/lib/steam/bootstraplinux_ubuntu12_32.tar.xz ]; then \
         ln -sfn /usr/lib/steam/bootstraplinux_ubuntu12_32.tar.xz \
           /etc/first-boot/bootstraplinux_ubuntu12_32.tar.xz; \
       fi \
    && mkdir -p /var/cache/ldconfig /var/pressure-vessel/ldso /var/pressure-vessel/tmp \
    && if [ ! -s /var/cache/ldconfig/ld.so.cache ] && [ -s /etc/ld.so.cache ]; then \
         cp -L /etc/ld.so.cache /var/cache/ldconfig/ld.so.cache; \
       fi \
    && chown -R stearlight:stearlight /var/pressure-vessel \
    && chmod 0755 /var/pressure-vessel /var/pressure-vessel/ldso \
    && chmod 0755 /etc/init.d/stearlight-vm-session \
                   /usr/local/libexec/stearlight/vm-session \
                   /usr/local/libexec/stearlight/vm-console \
                   /usr/local/libexec/stearlight/steam32-launch \
                   /usr/local/libexec/stearlight/steam-webhelper-env \
                   /usr/local/libexec/stearlight/direct-steam-runtime/_v2-entry-point \
                   /usr/local/libexec/stearlight/steamrt-webhelper/_v2-entry-point \
                   /usr/local/libexec/stearlight/steam-firstboot \
                   /usr/local/libexec/stearlight/patch-steamui-network \
                   /usr/local/libexec/stearlight/patch-steam-runtime \
                   /usr/local/libexec/stearlight/steam-runtime-watch \
                   /usr/local/libexec/stearlight/steamvr-native \
                   /etc/init.d/stearlight-xr \
                   /usr/local/libexec/stearlight/xr-service \
                   /usr/local/libexec/stearlight/xr-run \
                   /usr/local/libexec/stearlight/launch-xr-game \
                   /usr/bin/gamescope-session-plus \
                   /usr/share/gamescope-session-plus/gamescope-session-plus \
                   /usr/share/gamescope-session-plus/sessions.d/steam \
                   /usr/bin/jupiter-biosupdate \
                   /usr/bin/jupiter-dock-updater \
                   /usr/bin/pkexec \
                   /usr/bin/steamos-update \
                   /usr/bin/steamos-select-branch \
                   /usr/bin/steamos-session-select \
                   /usr/bin/timedatectl \
                   /usr/bin/localectl \
                   /usr/bin/steamos-set-timezone \
                   /usr/bin/steamos-devkit-mode \
                   /usr/bin/steamos-polkit-helpers/jupiter-biosupdate \
                   /usr/bin/steamos-polkit-helpers/jupiter-dock-updater \
                   /usr/bin/steamos-polkit-helpers/steamos-priv-write \
                   /usr/bin/steamos-polkit-helpers/steamos-set-timezone \
                   /usr/bin/steamos-polkit-helpers/steamos-devkit-mode \
    && rc-update add devfs sysinit \
    && rc-update add dmesg sysinit \
    && rc-update add mdev sysinit \
    && rc-update add hwdrivers sysinit \
    && rc-update add modules boot \
    && rc-update add localmount boot \
    && rc-update add sysctl boot \
    && rc-update add dbus default \
    && rc-update add dhcpcd default \
    && rc-update add stearlight-vm-session default

FROM alpine:${ALPINE_TAG} AS image-build
RUN for attempt in 1 2 3 4 5; do \
      apk add --no-cache dosfstools e2fsprogs genimage mtools systemd-boot && exit 0; \
      echo "apk image dependency download failed (attempt $attempt/5); retrying" >&2; \
      sleep 5; \
    done; exit 1
WORKDIR /work
COPY --from=rootfs / /work/rootfs/
COPY os/genimage-vm.cfg /work/genimage.cfg
COPY os/vm-systemd-loader.conf /work/loader.conf
COPY os/vm-systemd-entry.conf /work/stearlight.conf
# Keep the raw disk as the only image-build output. Converting it to VDI
# inside Docker duplicates a multi-gigabyte file on the Docker data volume
# and can exhaust its ext4 layer while the image is read back.
RUN mkdir -p /work/input/efi/EFI/BOOT /work/input/efi/loader/entries \
             /work/output /work/tmp \
    && cp /usr/lib/systemd/boot/efi/systemd-bootx64.efi \
         /work/input/efi/EFI/BOOT/BOOTX64.EFI \
    && cp /work/rootfs/boot/vmlinuz-virt /work/input/efi/vmlinuz-virt \
    && cp /work/rootfs/boot/initramfs-virt /work/input/efi/initramfs-virt \
    && cp /work/loader.conf /work/input/efi/loader/loader.conf \
    && cp /work/stearlight.conf /work/input/efi/loader/entries/stearlight.conf \
    && genimage --config /work/genimage.cfg --rootpath /work/rootfs \
         --inputpath /work/input --outputpath /work/output \
         --tmppath /work/tmp \
    && rm -f /work/output/efi.vfat /work/output/root.ext4

# Keep the BuildKit export small enough for Docker Desktop's client session.
# The raw disk is intentionally sparse, but a local exporter still walks every
# logical byte.  Compressing it inside the builder preserves the cacheable
# image-build stage and avoids the five-minute export-session timeout.
FROM alpine:${ALPINE_TAG} AS image-compress
RUN for attempt in 1 2 3 4 5; do \
      apk add --no-cache zstd && exit 0; \
      echo "apk image compression dependency download failed (attempt $attempt/5); retrying" >&2; \
      sleep 5; \
    done; exit 1
COPY --from=image-build /work/output/stearlight-vm.raw /work/output/stearlight-vm.raw
RUN zstd -T0 -1 --rm /work/output/stearlight-vm.raw \
      -o /work/output/stearlight-vm.raw.zst

FROM scratch AS image
COPY --from=image-compress /work/output/ /
