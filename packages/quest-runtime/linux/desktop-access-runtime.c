#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

// Linux-PAM treats failure to open NETLINK_AUDIT as a failed login, even
// after verifying the password. Android reserves that channel for its OS.
// Report its unavailability only within the app's developer-access service.
int audit_open(void) {
    int (*original)(void) = (int (*)(void))dlsym(RTLD_NEXT, "audit_open");
    if (!original) { errno = EPROTONOSUPPORT; return -1; }
    int descriptor = original();
    int saved = errno;
    const char *enabled = getenv("STEARLIGHT_DESKTOP_ACCESS");
    if (descriptor < 0 && (saved == EPERM || saved == EACCES) &&
        enabled && !strcmp(enabled, "1")) saved = EPROTONOSUPPORT;
    errno = saved;
    return descriptor;
}
