#include "steam_client.h"

#include <X11/Xutil.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SVRT_STEAM_HOME "/var/lib/svrt-receiver"
#define SVRT_STEAM_DISPLAY ":8"
#define SVRT_STEAM_WIDTH 1024
#define SVRT_STEAM_HEIGHT 640

static const char *steam_binary(void) {
    static const char path[] =
        SVRT_STEAM_HOME "/.local/share/Steam/steamrtarm64/steam";
    return path;
}

static const char *steam_launcher(void) {
    static const char path[] =
        SVRT_STEAM_HOME "/.local/share/Steam/launch-steam.sh";
    return path;
}

static void child_environment(void) {
    setenv("HOME", SVRT_STEAM_HOME, 1);
    setenv("USER", "svrt-receiver", 1);
    setenv("LOGNAME", "svrt-receiver", 1);
    setenv("DISPLAY", SVRT_STEAM_DISPLAY, 1);
    setenv("XDG_SESSION_TYPE", "x11", 1);
    setenv("XDG_CURRENT_DESKTOP", "SVRT", 1);
    /* The receiver itself uses SDL's direct KMS backend.  Never leak that
       choice into Steam: its UI lives in the private Xvfb display. */
    setenv("SDL_VIDEODRIVER", "x11", 1);
}

static pid_t start_display(void) {
    pid_t pid = fork();
    if (pid) return pid;
    setpgid(0, 0);
    child_environment();
    execlp("Xvfb", "Xvfb", SVRT_STEAM_DISPLAY, "-screen", "0",
           "1024x640x24", "-nolisten", "tcp", "-noreset", NULL);
    _exit(127);
}

static pid_t start_steam(void) {
    pid_t pid = fork();
    if (pid) return pid;
    setpgid(0, 0);
    child_environment();
    if (access(steam_launcher(), X_OK) == 0)
        execl(steam_launcher(), steam_launcher(), NULL);
    execl(steam_binary(), steam_binary(), "-gamepadui", "-720p",
          "-noverifyfiles", "-nocrashmonitor", "-no-cef-sandbox",
          "-cef-disable-sandbox", NULL);
    _exit(127);
}

static pid_t start_window_manager(void) {
    pid_t pid = fork();
    if (pid) return pid;
    setpgid(0, 0);
    child_environment();
    execlp("matchbox-window-manager", "matchbox-window-manager",
           "-use_titlebar", "no", "-use_cursor", "no", NULL);
    _exit(127);
}

int svrt_steam_client_start(svrt_steam_client *client,
                            SDL_Renderer *renderer) {
    (void)renderer;
    if (!client) return -1;
    memset(client, 0, sizeof(*client));
    if (access(steam_binary(), X_OK)) {
        client->state = SVRT_STEAM_CLIENT_MISSING;
        snprintf(client->detail, sizeof(client->detail),
                 "Install the Steam ARM64 client");
        return 0;
    }
    client->display_pid = start_display();
    if (client->display_pid <= 0) {
        client->state = SVRT_STEAM_CLIENT_FAILED;
        snprintf(client->detail, sizeof(client->detail),
                 "Cannot start Steam display");
        return -1;
    }
    client->state = SVRT_STEAM_CLIENT_STARTING;
    client->next_connect_ms = SDL_GetTicks() + 100;
    snprintf(client->detail, sizeof(client->detail),
             "Starting Steam Big Picture");
    return 0;
}

static void connect_display(svrt_steam_client *client, uint32_t now_ms) {
    if (client->display || now_ms < client->next_connect_ms) return;
    client->display = XOpenDisplay(SVRT_STEAM_DISPLAY);
    if (!client->display) {
        client->next_connect_ms = now_ms + 100;
        return;
    }
    client->root = DefaultRootWindow(client->display);
    client->window_manager_pid = start_window_manager();
    client->steam_pid = start_steam();
    if (client->steam_pid <= 0) {
        client->state = SVRT_STEAM_CLIENT_FAILED;
        snprintf(client->detail, sizeof(client->detail),
                 "Cannot launch Steam ARM64");
    }
}

static int child_exited(pid_t pid) {
    if (pid <= 0) return 0;
    int status = 0;
    return waitpid(pid, &status, WNOHANG) == pid;
}

void svrt_steam_client_update(svrt_steam_client *client,
                              SDL_Renderer *renderer, uint32_t now_ms) {
    if (!client || !renderer || client->state == SVRT_STEAM_CLIENT_MISSING ||
        client->state == SVRT_STEAM_CLIENT_FAILED)
        return;
    connect_display(client, now_ms);
    if (!client->display || !client->steam_pid) return;
    if (child_exited(client->steam_pid)) {
        client->steam_pid = 0;
        client->state = SVRT_STEAM_CLIENT_FAILED;
        snprintf(client->detail, sizeof(client->detail),
                 "Steam ARM64 exited unexpectedly");
        return;
    }
    if (now_ms < client->next_capture_ms) return;
    client->next_capture_ms = now_ms + 33;
    XImage *image = XGetImage(client->display, client->root, 0, 0,
                              SVRT_STEAM_WIDTH, SVRT_STEAM_HEIGHT,
                              AllPlanes, ZPixmap);
    if (!image) return;
    if (!client->frame)
        client->frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          SVRT_STEAM_WIDTH,
                                          SVRT_STEAM_HEIGHT);
    if (client->frame && image->bits_per_pixel == 32) {
        SDL_UpdateTexture(client->frame, NULL, image->data,
                          image->bytes_per_line);
        client->state = SVRT_STEAM_CLIENT_RUNNING;
        client->detail[0] = '\0';
    }
    XDestroyImage(image);
}

SDL_Texture *svrt_steam_client_frame(const svrt_steam_client *client) {
    return client ? client->frame : NULL;
}

const char *svrt_steam_client_detail(const svrt_steam_client *client) {
    return client && client->detail[0] ? client->detail : NULL;
}

static void stop_process_group(pid_t pid) {
    if (pid <= 0) return;
    kill(-pid, SIGTERM);
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (waitpid(pid, NULL, WNOHANG) == pid) return;
        SDL_Delay(20);
    }
    kill(-pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

void svrt_steam_client_open_uri(const svrt_steam_client *client,
                                const char *uri) {
    if (!client || client->steam_pid <= 0 || !uri || !uri[0]) return;
    pid_t child = fork();
    if (child < 0) return;
    if (!child) {
        pid_t grandchild = fork();
        if (grandchild < 0) _exit(127);
        if (grandchild) _exit(0);
        setsid();
        child_environment();
        execl(steam_launcher(), steam_launcher(), uri, NULL);
        _exit(127);
    }
    waitpid(child, NULL, 0);
}

void svrt_steam_client_stop(svrt_steam_client *client) {
    if (!client) return;
    stop_process_group(client->steam_pid);
    stop_process_group(client->window_manager_pid);
    if (client->display) XCloseDisplay(client->display);
    stop_process_group(client->display_pid);
    if (client->frame) SDL_DestroyTexture(client->frame);
    memset(client, 0, sizeof(*client));
}
