#define _GNU_SOURCE
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* MangoHud's thermal discovery throws on Android's inaccessible sysfs
 * directory. An empty sensor directory means unavailable, not a zero reading.
 * This preload is used only by mangoapp, never by games or the compositor. */
DIR *opendir(const char *path) {
    DIR *(*next)(const char *) = dlsym(RTLD_NEXT, "opendir");
    DIR *directory = next(path);
    if (!directory && (errno == EACCES || errno == EPERM) &&
        (!strcmp(path, "/sys/class/thermal/") || !strcmp(path, "/sys/class/thermal") ||
         !strcmp(path, "/sys/class/drm/") || !strcmp(path, "/sys/class/drm"))) {
        const char *empty = getenv("QUEST_MANGO_EMPTY_SENSORS");
        if (empty) directory = next(empty);
    }
    return directory;
}
