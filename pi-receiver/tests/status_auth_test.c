#define _POSIX_C_SOURCE 200809L

#include "status.h"

#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    svrt_status_server server;
    memset(&server, 0, sizeof(server));
    atomic_init(&server.state, SVRT_RECEIVER_READY);
    atomic_init(&server.pose_sequence, 0);
    atomic_init(&server.authorization_revoked, 0);

    unsetenv("SVRT_ENABLE_SYNTHETIC_POSE");
    unsetenv("SVRT_DISABLE_SYNTHETIC_POSE");
    svrt_synthetic_pose pose;
    svrt_status_server_get_pose(&server, SVRT_RECEIVER_READY, &pose);
    if (pose.valid || !pose.connected || pose.result != 101) {
        fputs("synthetic pose was enabled without an explicit test opt-in\n",
              stderr);
        return 1;
    }
    if (setenv("SVRT_ENABLE_SYNTHETIC_POSE", "1", 1)) {
        fputs("could not enable synthetic pose test mode\n", stderr);
        return 1;
    }
    svrt_status_server_get_pose(&server, SVRT_RECEIVER_READY, &pose);
    unsetenv("SVRT_ENABLE_SYNTHETIC_POSE");
    if (!pose.valid || !pose.connected || pose.result != 200) {
        fputs("explicit synthetic pose test mode did not produce tracking\n",
              stderr);
        return 1;
    }

    svrt_status_server_revoke_authorization(&server);
    if (!svrt_status_server_authorization_revoked(&server) ||
        atomic_load(&server.state) != SVRT_RECEIVER_UNAUTHORIZED) {
        fputs("authorization revoke did not become visible immediately\n",
              stderr);
        return 1;
    }

    svrt_status_server_update(&server, SVRT_RECEIVER_READY, NULL);
    if (atomic_load(&server.state) != SVRT_RECEIVER_UNAUTHORIZED) {
        fputs("receiver update bypassed authorization revoke\n", stderr);
        return 1;
    }

    svrt_status_server_reset_authorization(&server);
    svrt_status_server_update(&server, SVRT_RECEIVER_READY, NULL);
    if (svrt_status_server_authorization_revoked(&server) ||
        atomic_load(&server.state) != SVRT_RECEIVER_READY) {
        fputs("authorization reset did not restore receiver state\n", stderr);
        return 1;
    }

    puts("SVRT status authorization test: passed");
    return 0;
}
