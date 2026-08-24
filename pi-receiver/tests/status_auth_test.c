#include "status.h"

#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

int main(void) {
    svrt_status_server server;
    memset(&server, 0, sizeof(server));
    atomic_init(&server.state, SVRT_RECEIVER_READY);
    atomic_init(&server.authorization_revoked, 0);

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
