#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  const char *shim = getenv("STEARLIGHT_VULKAN_WSI_SHIM");
  if (!shim || !*shim)
    shim = "/opt/steamvr/lib/linuxarm64/libstearlight-vulkan-wsi-shim.so";

  const char *shared_setting = getenv("STEARLIGHT_XCB_SHM_PRESENT");
  const char *shared =
      "/opt/steamvr/lib/linuxarm64/libstearlight-xcb-shm-present.so";
  if (shared_setting && !strcmp(shared_setting, "1") &&
      access(shared, R_OK) == 0) {
    const char *inherited = getenv("LD_PRELOAD");
    size_t length = strlen(shared) + (inherited ? strlen(inherited) : 0) + 2;
    char *combined = malloc(length);
    if (!combined) return 126;
    snprintf(combined, length, "%s%s%s", shared,
             inherited && *inherited ? ":" : "", inherited ? inherited : "");
    int result = setenv("LD_PRELOAD", combined, 1);
    free(combined);
    if (result != 0) return 126;
  }

  const char *current = getenv("LD_PRELOAD");
  if (!current || !*current) {
    if (setenv("LD_PRELOAD", shim, 1) != 0) {
      perror("stearlight compositor launcher: setenv LD_PRELOAD");
      return 126;
    }
  } else if (!strstr(current, shim)) {
    size_t length = strlen(current) + strlen(shim) + 2;
    char *combined = malloc(length);
    if (!combined) {
      perror("stearlight compositor launcher: allocate LD_PRELOAD");
      return 126;
    }
    snprintf(combined, length, "%s:%s", current, shim);
    if (setenv("LD_PRELOAD", combined, 1) != 0) {
      perror("stearlight compositor launcher: setenv LD_PRELOAD");
      free(combined);
      return 126;
    }
    free(combined);
  }

  fprintf(stderr, "STEARLIGHT WSI launcher: preload=%s\n", shim);
  argv[0] = "/opt/steamvr/bin/linuxarm64/vrcompositor";
  const char *prefix = getenv("STEARLIGHT_VULKAN_LIBRARY_PATH");
  if (prefix && *prefix) {
    /* Keep glibc ICD dependencies out of the Alpine launcher scripts. */
    const char *existing = getenv("LD_LIBRARY_PATH");
    size_t length = strlen(prefix) + (existing ? strlen(existing) : 0) + 2;
    char *path = malloc(length);
    char **arguments = calloc((size_t)argc + 4, sizeof(*arguments));
    if (!path || !arguments) {
      perror("stearlight compositor launcher: allocate loader arguments");
      free(path);
      free(arguments);
      return 126;
    }
    snprintf(path, length, "%s%s%s", prefix, existing && *existing ? ":" : "",
             existing ? existing : "");
    arguments[0] = "/opt/frame-gamescope/lib/ld-linux-aarch64.so.1";
    arguments[1] = "--library-path";
    arguments[2] = path;
    arguments[3] = "/opt/steamvr/bin/linuxarm64/vrcompositor.bin";
    for (int i = 1; i < argc; i++) arguments[i + 3] = argv[i];
    execv(arguments[0], arguments);
    fprintf(stderr, "stearlight compositor loader: exec failed: %s\n", strerror(errno));
    free(arguments);
    free(path);
    return 127;
  }
  execv("/opt/steamvr/bin/linuxarm64/vrcompositor.bin", argv);
  fprintf(stderr, "stearlight compositor launcher: exec failed: %s\n",
          strerror(errno));
  return 127;
}
