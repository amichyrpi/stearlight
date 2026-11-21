#pragma once
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Release only the optional settings GUI, never the shared Wine server or
 * vrserver.exe tracker host. Refuse ambiguous process matches. */
static pid_t quest_release_standable_settings(void) {
    DIR *processes = opendir("/proc");
    if (!processes) return 0;
    pid_t target = 0;
    struct dirent *entry;
    while ((entry = readdir(processes))) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        char path[512], name[64];
        struct stat info;
        snprintf(path, sizeof(path), "/proc/%s", entry->d_name);
        if (stat(path, &info) || info.st_uid != getuid()) continue;
        snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
        FILE *file = fopen(path, "r");
        if (!file) continue;
        int read = fgets(name, sizeof(name), file) != NULL;
        fclose(file);
        if (!read || strcmp(name, "Standable.exe\n")) continue;
        if (target) { target = 0; break; }
        target = (pid_t)strtol(entry->d_name, NULL, 10);
    }
    closedir(processes);
    if (target && kill(target, SIGTERM)) target = 0;
    return target;
}
