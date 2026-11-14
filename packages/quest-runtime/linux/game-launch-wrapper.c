#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "performance-boost.h"
#include "standable-settings.h"

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
    int command_index = 0;
    for (int i = 1; i < argc; ++i) {
        if (options && !strcmp(argv[i], "--")) { options = 0; command_index = i + 1; }
        if (options && !strcmp(argv[i], "--oom-score-adjust") &&
            i + 1 < argc && !strcmp(argv[i + 1], "900")) {
            ++i;
            continue;
        }
        if (options && !strcmp(argv[i], "--oom-score-adjust=900")) continue;
        // Standable shares a resident Wine server with its SteamVR add-on.
        // Steam tracks the settings process directly; a subreaper waiting
        // for that shared server would remain after closing the settings.
        const char *name = strrchr(argv[i], '/');
        name = name ? name + 1 : argv[i];
        if (i == command_index && !strcmp(name, "reaper") && i + 4 < argc &&
            !strcmp(argv[i + 1], "SteamLaunch") &&
            !strcmp(argv[i + 2], "AppId=2370570") && !strcmp(argv[i + 3], "--")) {
            i += 3;
            continue;
        }
        arguments[count++] = argv[i];
    }
    /* Estimation belongs to the resident tracker host. Release the optional
     * settings process before a different game adds its Wine/FEX children. */
    const char *appid = getenv("SteamAppId");
    if (appid && *appid && strspn(appid, "0123456789") == strlen(appid) &&
        strtoull(appid, NULL, 10) > 0 && strcmp(appid, "2370570") && strcmp(appid, "250820"))
    {
        pid_t settings = quest_release_standable_settings();
        /* Let Steam retire the settings app and Frame retire its overlay
         * before the new game's compositor claims its window. No waiter
         * remains resident after this bounded handover. */
        if (settings) {
            for (int i = 0; i < 30 && kill(settings, 0) == 0; ++i) usleep(50000);
            usleep(250000);
        }
    }
    quest_request_loading_boost();
    execv(path, arguments);
    perror("Quest game launcher exec");
    return 127;
}
