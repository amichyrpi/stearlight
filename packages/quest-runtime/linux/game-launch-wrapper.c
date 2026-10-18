#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* quest-game-launch-wrapper: retain Android's inherited process priority.
 * Build statically: Steam supplies mixed-architecture overlay preloads that
 * cannot be loaded by the guest's musl shell. Preserve them for Valve's exec. */
int main(int argc, char **argv) {
    char path[PATH_MAX];
    const char suffix[] = ".quest-real";
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - sizeof(suffix));
    if (length < 0 || (size_t)length >= sizeof(path) - sizeof(suffix)) {
        fputs("quest-game-launch-wrapper: cannot resolve executable\n", stderr);
        return 127;
    }
    memcpy(path + length, suffix, sizeof(suffix));
    char **arguments = calloc((size_t)argc + 1, sizeof(*arguments));
    if (!arguments) return 127;
    arguments[0] = argv[0];
    int count = 1;
    int options = 1;
    for (int i = 1; i < argc; ++i) {
        if (options && !strcmp(argv[i], "--")) options = 0;
        if (options && !strcmp(argv[i], "--oom-score-adjust") &&
            i + 1 < argc && !strcmp(argv[i + 1], "900")) {
            ++i;
            continue;
        }
        if (options && !strcmp(argv[i], "--oom-score-adjust=900")) continue;
        arguments[count++] = argv[i];
    }
    execv(path, arguments);
    perror("Quest game launcher exec");
    return 127;
}
