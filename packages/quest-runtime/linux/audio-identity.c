#define _GNU_SOURCE
#include <unistd.h>
#include <errno.h>

// PulseAudio repeats its existing IDs when dropping privileges. Android's
// app seccomp policy traps setresuid/setresgid even for these no-op calls.
// Accept only unchanged IDs; never request or simulate a privilege change.
int setresuid(uid_t real, uid_t effective, uid_t saved) {
    if ((real == (uid_t)-1 || real == getuid()) &&
        (effective == (uid_t)-1 || effective == geteuid()) &&
        (saved == (uid_t)-1 || saved == geteuid())) return 0;
    errno = EPERM;
    return -1;
}
int setresgid(gid_t real, gid_t effective, gid_t saved) {
    if ((real == (gid_t)-1 || real == getgid()) &&
        (effective == (gid_t)-1 || effective == getegid()) &&
        (saved == (gid_t)-1 || saved == getegid())) return 0;
    errno = EPERM;
    return -1;
}
