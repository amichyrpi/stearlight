#ifndef SVRT_STATUS_SERVER_H
#define SVRT_STATUS_SERVER_H

#include <stdatomic.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <sys/socket.h>
#include <svrt.h>

enum svrt_receiver_state {
    SVRT_RECEIVER_STARTING = 0,
    SVRT_RECEIVER_READY = 1,
    SVRT_RECEIVER_STREAMING = 2,
    SVRT_RECEIVER_ERROR = 3,
    SVRT_RECEIVER_UNAUTHORIZED = 4
};

/* Wire-compatible synthetic 6DoF pose returned with SVRT/1 STATUS. The
   Raspberry Pi currently has no lighthouse sensor, so this is deliberately
   bounded test motion rather than a claim of physical tracking. */
typedef struct svrt_synthetic_pose {
    int valid;
    int connected;
    int result;
    uint64_t sequence;
    uint64_t timestamp_us;
    double position[3];
    double quaternion[4]; /* x, y, z, w */
    double velocity[3];
    double angular_velocity[3];
} svrt_synthetic_pose;

typedef struct svrt_status_server {
    uint16_t port;
    atomic_int stopping;
    atomic_int state;
    atomic_uint_fast64_t decoded;
    atomic_uint_fast64_t presented;
    atomic_uint_fast64_t dropped;
    atomic_uint_fast64_t bytes;
    atomic_uint_fast64_t invalid_packets;
    atomic_uint_fast64_t fec_recovered;
    atomic_uint_fast64_t network_dropped;
    atomic_uint_fast64_t received_pts_us;
    atomic_uint_fast64_t received_time_us;
    atomic_uint_fast64_t processed_pts_us;
    atomic_uint_fast64_t processed_time_us;
    atomic_uint_fast64_t pose_sequence;
    atomic_uint_fast64_t steam_device_id;
    atomic_int authorization_revoked;
    pthread_mutex_t clients_lock;
    pthread_cond_t clients_done;
    unsigned active_clients;
    void *thread;
    pthread_t tracking_thread;
    int tracking_started;
    pthread_t discovery_thread;
    int discovery_started;
    pthread_mutex_t tracking_lock;
    struct sockaddr_storage tracking_address;
    socklen_t tracking_address_size;
    uint32_t tracking_session;
    char paired_address[64];
} svrt_status_server;

int svrt_status_server_start(svrt_status_server *server, uint16_t port);
void svrt_status_server_update(svrt_status_server *server, int state,
                               const svrt_stats *stats);
void svrt_status_server_get_pose(svrt_status_server *server, int state,
                                 svrt_synthetic_pose *pose);
void svrt_status_server_packet_event(void *opaque, svrt_packet_event event,
                                     uint64_t pts_us, uint64_t receiver_time_us);
void svrt_status_server_reset_trace(svrt_status_server *server);
void svrt_status_server_set_steam_device_id(svrt_status_server *server,
                                            uint64_t device_id);
void svrt_status_server_set_paired_host(svrt_status_server *server,
                                        const char *address);
void svrt_status_server_reset_authorization(svrt_status_server *server);
void svrt_status_server_revoke_authorization(svrt_status_server *server);
int svrt_status_server_authorization_revoked(svrt_status_server *server);
void svrt_status_server_stop(svrt_status_server *server);

#endif
