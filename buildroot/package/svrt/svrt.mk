SVRT_VERSION = local
SVRT_SITE = $(if $(call qstrip,$(BR2_PACKAGE_SVRT_LOCAL_PATH)),$(call qstrip,$(BR2_PACKAGE_SVRT_LOCAL_PATH)),/svrt)
SVRT_SITE_METHOD = local
SVRT_LICENSE = FSL-1.1-ALv2, GPL-2.0
SVRT_LICENSE_FILES = LICENSE vanilla-master/LICENSE
SVRT_DEPENDENCIES = rpi-ffmpeg libdrm sdl2 sdl2_ttf host-pkgconf
SVRT_CONF_OPTS = -DSVRT_BUILD_DRIVER=OFF -DSVRT_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
define SVRT_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_SVRT_PATH)/board/raspberrypi4/S50svrt $(TARGET_DIR)/etc/init.d/S50svrt
endef
$(eval $(cmake-package))
