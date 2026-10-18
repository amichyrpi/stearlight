#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

// Keep Gamescope's game lifetime isolated from the dashboard's process group.
static volatile sig_atomic_t stopping;
static void stop(int signal) { (void)signal; stopping = 1; }
static void children(int signal) {
    char path[80];
    snprintf(path, sizeof(path), "/proc/self/task/%d/children", getpid());
    FILE* file = fopen(path, "r");
    if (!file) return;
    int child;
    while (fscanf(file, "%d", &child) == 1) kill(child, signal);
    fclose(file);
}
int main(int argc, char** argv) {
    int command = 1;
    while (command < argc && strcmp(argv[command], "--")) ++command;
    if (++command >= argc) return 64;
    pid_t parent = getppid();
    struct sigaction action = {0};
    action.sa_handler = stop;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0);
    prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);
    if (getppid() != parent) return 1;
    pid_t primary = fork();
    if (primary < 0) return 1;
    if (!primary) {
        setsid();
        signal(SIGTERM, SIG_DFL); signal(SIGINT, SIG_DFL); signal(SIGHUP, SIG_DFL);
        execvp(argv[command], argv + command);
        perror("Quest Gamescope child");
        _exit(127);
    }
    int result = 0, status;
    while (!stopping) {
        pid_t dead = waitpid(-1, &status, 0);
        if (dead == primary) {
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            if (WIFSIGNALED(status))
                fprintf(stderr, "Quest game exited on signal %d\n", WTERMSIG(status));
            else if (result)
                fprintf(stderr, "Quest game exited with status %d\n", result);
            break;
        }
        if (dead < 0 && errno != EINTR) break;
    }
    kill(-primary, SIGTERM);
    children(SIGTERM);
    for (unsigned attempt = 0; attempt < 30; ++attempt) {
        pid_t dead;
        do { dead = waitpid(-1, &status, WNOHANG); } while (dead > 0);
        if (dead < 0 && errno == ECHILD) return result;
        usleep(100000);
    }
    kill(-primary, SIGKILL); children(SIGKILL);
    while (waitpid(-1, &status, 0) > 0 || errno == EINTR) {}
    return result;
}
