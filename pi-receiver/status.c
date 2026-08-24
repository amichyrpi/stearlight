#include "status.h"
#include <stearlight_protocol.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_us(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * 1000000u +
           (uint64_t)now.tv_nsec / 1000u;
}

void svrt_status_server_get_pose(svrt_status_server *server, int state,
                                 svrt_synthetic_pose *pose) {
    if (!server || !pose) return;
    memset(pose, 0, sizeof(*pose));
    pose->sequence = atomic_fetch_add(&server->pose_sequence, 1) + 1;
    pose->timestamp_us = monotonic_us();
    /* Video recovery must not make SteamVR treat the physical HMD as
       unplugged, but an unapproved Steam device must publish no pose. */
    pose->connected = state != SVRT_RECEIVER_UNAUTHORIZED;
    pose->valid = pose->connected;
    pose->result = pose->valid ? 200 /* TrackingResult_Running_OK */
                               : 101 /* TrackingResult_Calibrating_OutOfRange */;
    const char *disabled = getenv("SVRT_DISABLE_SYNTHETIC_POSE");
    if (disabled && disabled[0] && strcmp(disabled, "0")) {
        pose->valid = 0;
        pose->result = 101;
    }
    if (!pose->valid) {
        pose->quaternion[3] = 1.0;
        return;
    }

    /* Small, slow, deterministic motion exercises all six pose degrees of
       freedom while remaining safe for a seated test session. */
    const double t = (double)pose->timestamp_us / 1000000.0;
    const double x_frequency = 0.45;
    const double y_frequency = 0.35;
    const double z_frequency = 0.30;
    const double yaw_frequency = 0.25;
    const double pitch_frequency = 0.31;
    const double roll_frequency = 0.37;
    const double x_phase = x_frequency * t;
    const double y_phase = y_frequency * t + 0.7;
    const double z_phase = z_frequency * t + 1.3;
    const double yaw_phase = yaw_frequency * t;
    const double pitch_phase = pitch_frequency * t + 0.4;
    const double roll_phase = roll_frequency * t + 1.1;
    const double x_amplitude = 0.025;
    const double y_amplitude = 0.018;
    const double z_amplitude = 0.020;
    const double yaw_amplitude = 0.12;
    const double pitch_amplitude = 0.06;
    const double roll_amplitude = 0.05;

    pose->position[0] = x_amplitude * sin(x_phase);
    // Raw driver space uses Y=0 as the floor.  Keep the synthetic head at a
    // plausible standing height while retaining a small vertical movement.
    pose->position[1] = 1.65 + y_amplitude * sin(y_phase);
    pose->position[2] = z_amplitude * sin(z_phase);
    pose->velocity[0] = x_amplitude * x_frequency * cos(x_phase);
    pose->velocity[1] = y_amplitude * y_frequency * cos(y_phase);
    pose->velocity[2] = z_amplitude * z_frequency * cos(z_phase);

    const double yaw = yaw_amplitude * sin(yaw_phase);
    const double pitch = pitch_amplitude * sin(pitch_phase);
    const double roll = roll_amplitude * sin(roll_phase);
    const double yaw_rate = yaw_amplitude * yaw_frequency * cos(yaw_phase);
    const double pitch_rate = pitch_amplitude * pitch_frequency * cos(pitch_phase);
    const double roll_rate = roll_amplitude * roll_frequency * cos(roll_phase);
    pose->angular_velocity[0] = roll_rate;
    pose->angular_velocity[1] = pitch_rate;
    pose->angular_velocity[2] = yaw_rate;

    const double cy = cos(yaw * 0.5);
    const double sy = sin(yaw * 0.5);
    const double cp = cos(pitch * 0.5);
    const double sp = sin(pitch * 0.5);
    const double cr = cos(roll * 0.5);
    const double sr = sin(roll * 0.5);
    pose->quaternion[0] = sr * cp * cy - cr * sp * sy;
    pose->quaternion[1] = cr * sp * cy + sr * cp * sy;
    pose->quaternion[2] = cr * cp * sy - sr * sp * cy;
    pose->quaternion[3] = cr * cp * cy + sr * sp * sy;
}

static int send_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t sent = send(fd, data, size, MSG_NOSIGNAL);
        if (sent <= 0) return -1;
        data += sent;
        size -= (size_t)sent;
    }
    return 0;
}

static int format_peer_ip(const struct sockaddr_storage *peer, char *out,
                          size_t out_size) {
    if (peer->ss_family == AF_INET)
        return inet_ntop(AF_INET, &((const struct sockaddr_in *)peer)->sin_addr,
                         out, (socklen_t)out_size) != NULL;
    if (peer->ss_family == AF_INET6) {
        const struct in6_addr *addr =
            &((const struct sockaddr_in6 *)peer)->sin6_addr;
        if (IN6_IS_ADDR_V4MAPPED(addr))
            return inet_ntop(AF_INET, &addr->s6_addr[12], out,
                             (socklen_t)out_size) != NULL;
        return inet_ntop(AF_INET6, addr, out, (socklen_t)out_size) != NULL;
    }
    return 0;
}

static int request_from_paired_host(svrt_status_server *server,
                                    const struct sockaddr_storage *peer) {
    char expected[64];
    pthread_mutex_lock(&server->tracking_lock);
    snprintf(expected, sizeof(expected), "%s", server->paired_address);
    pthread_mutex_unlock(&server->tracking_lock);
    if (!expected[0]) return 0;
    char actual[INET6_ADDRSTRLEN] = {0};
    if (!format_peer_ip(peer, actual, sizeof(actual))) return 0;
    return strcmp(expected, actual) == 0;
}

static int send_ack(int fd, unsigned long long nonce, const char *stage,
                    uint64_t pts_us, uint64_t receiver_time_us) {
    char response[192];
    int used = snprintf(response, sizeof(response),
                        "SVRT/1 ACK %llu %s %llu %llu\n", nonce, stage,
                        (unsigned long long)pts_us,
                        (unsigned long long)receiver_time_us);
    return used > 0 && (size_t)used < sizeof(response)
               ? send_all(fd, response, (size_t)used)
               : -1;
}

static void answer_trace(svrt_status_server *server, int fd,
                         unsigned long long nonce, uint64_t target_pts_us) {
    /* Never monopolize the single status listener for a stale trace request.
     * PING is the driver's connection/liveness path, so it must remain
     * responsive while a stream is reconnecting or has stopped. */
    struct timeval timeout = {.tv_sec = 1};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    int received_sent = 0;
    for (int waited_ms = 0; waited_ms < 1000; ++waited_ms) {
        if (atomic_load(&server->stopping)) return;
        uint64_t received_pts = atomic_load(&server->received_pts_us);
        uint64_t processed_pts = atomic_load(&server->processed_pts_us);
        if (!received_sent && received_pts == target_pts_us) {
            if (send_ack(fd, nonce, "RECEIVED", received_pts,
                         atomic_load(&server->received_time_us)))
                return;
            received_sent = 1;
        }
        if (processed_pts == target_pts_us) {
            if (!received_sent &&
                send_ack(fd, nonce, "RECEIVED", target_pts_us,
                         atomic_load(&server->received_time_us)))
                return;
            send_ack(fd, nonce, "PROCESSED", processed_pts,
                     atomic_load(&server->processed_time_us));
            return;
        }
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
        nanosleep(&delay, NULL);
    }
    send_ack(fd, nonce, "TIMEOUT", target_pts_us, 0);
}

static void answer_client(svrt_status_server *server, int fd) {
    struct timeval timeout = {.tv_sec = 2};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char request[128] = {0};
    size_t length = 0;
    int complete = 0;
    while (length < sizeof(request) - 1) {
        ssize_t received =
            recv(fd, request + length, sizeof(request) - 1 - length, 0);
        if (received <= 0) return;
        char *newline = memchr(request + length, '\n', (size_t)received);
        length += (size_t)received;
        if (newline) {
            length = (size_t)(newline - request) + 1;
            request[length] = '\0';
            complete = 1;
            break;
        }
    }
    if (!complete) return;
    unsigned long long nonce = 0, target_pts = 0, client_time = 0;
    unsigned long long auth_device_id = 0;
    unsigned session = 0, tracking_port = 0, client_authorized = 1;
    if (sscanf(request, "SVRT/1 TRACE %llu %llu", &nonce, &target_pts) == 2) {
        answer_trace(server, fd, nonce, (uint64_t)target_pts);
        return;
    }
    int connect_fields = sscanf(request, "SVRT/3 CONNECT %u %u %llu %llx %u",
                                &session, &tracking_port, &client_time,
                                &auth_device_id, &client_authorized);
    if (connect_fields != 5)
        connect_fields = sscanf(request, "SVRT/2 CONNECT %u %u %llu", &session,
                                &tracking_port, &client_time);
    if ((connect_fields == 3 || connect_fields == 5) && session && tracking_port) {
        struct sockaddr_storage peer; socklen_t peer_size = sizeof(peer);
        const uint64_t received_us = monotonic_us();
        if (!getpeername(fd, (struct sockaddr *)&peer, &peer_size)) {
            if (peer.ss_family == AF_INET)
                ((struct sockaddr_in *)&peer)->sin_port = htons((uint16_t)tracking_port);
            else if (peer.ss_family == AF_INET6)
                ((struct sockaddr_in6 *)&peer)->sin6_port = htons((uint16_t)tracking_port);
            else return;
            const uint64_t device_id = atomic_load(&server->steam_device_id);
            const int proven = request_from_paired_host(server, &peer);
            if (proven && connect_fields == 5 && device_id &&
                auth_device_id == device_id && !client_authorized) {
                svrt_status_server_revoke_authorization(server);
            }
            const int receiver_state = atomic_load(&server->state);
            if (receiver_state != SVRT_RECEIVER_UNAUTHORIZED) {
                pthread_mutex_lock(&server->tracking_lock);
                server->tracking_address = peer;
                server->tracking_address_size = peer_size;
                server->tracking_session = session;
                pthread_mutex_unlock(&server->tracking_lock);
            }
            const uint64_t sent_us = monotonic_us(); char response[288];
            int used;
            if (proven && receiver_state != SVRT_RECEIVER_UNAUTHORIZED) {
                used = snprintf(response, sizeof(response),
                    "SVRT/2 ACCEPT %u %llu %llu %llu %d %llu %llu %llu %llu %llu %llu %llu %016llx\n",
                    session, client_time, (unsigned long long)received_us,
                    (unsigned long long)sent_us, receiver_state,
                    (unsigned long long)atomic_load(&server->decoded),
                    (unsigned long long)atomic_load(&server->presented),
                    (unsigned long long)atomic_load(&server->dropped),
                    (unsigned long long)atomic_load(&server->bytes),
                    (unsigned long long)atomic_load(&server->invalid_packets),
                    (unsigned long long)atomic_load(&server->fec_recovered),
                    (unsigned long long)atomic_load(&server->network_dropped),
                    (unsigned long long)device_id);
            } else {
                used = snprintf(response, sizeof(response),
                    "SVRT/2 ACCEPT %u %llu %llu %llu %d %llu %llu %llu %llu %llu %llu %llu\n",
                    session, client_time, (unsigned long long)received_us,
                    (unsigned long long)sent_us, receiver_state,
                    (unsigned long long)atomic_load(&server->decoded),
                    (unsigned long long)atomic_load(&server->presented),
                    (unsigned long long)atomic_load(&server->dropped),
                    (unsigned long long)atomic_load(&server->bytes),
                    (unsigned long long)atomic_load(&server->invalid_packets),
                    (unsigned long long)atomic_load(&server->fec_recovered),
                    (unsigned long long)atomic_load(&server->network_dropped));
            }
            if (used > 0 && (size_t)used < sizeof(response)) send_all(fd,response,(size_t)used);
        }
        return;
    }
    if (sscanf(request, "SVRT/1 PING %llu", &nonce) != 1) return;
    svrt_synthetic_pose pose;
    svrt_status_server_get_pose(server, atomic_load(&server->state), &pose);
    char response[768];
    int used = snprintf(response, sizeof(response),
                        "SVRT/1 STATUS %llu %d %llu %llu %llu %llu "
                        "%d %d %d %llu %llu "
                        "%.9f %.9f %.9f "
                        "%.9f %.9f %.9f %.9f "
                        "%.9f %.9f %.9f "
                        "%.9f %.9f %.9f\n",
                        nonce, atomic_load(&server->state),
                        (unsigned long long)atomic_load(&server->decoded),
                        (unsigned long long)atomic_load(&server->presented),
                        (unsigned long long)atomic_load(&server->dropped),
                        (unsigned long long)atomic_load(&server->bytes),
                        pose.valid, pose.connected, pose.result,
                        (unsigned long long)pose.sequence,
                        (unsigned long long)pose.timestamp_us,
                        pose.position[0], pose.position[1], pose.position[2],
                        pose.quaternion[0], pose.quaternion[1],
                        pose.quaternion[2], pose.quaternion[3],
                        pose.velocity[0], pose.velocity[1], pose.velocity[2],
                        pose.angular_velocity[0], pose.angular_velocity[1],
                        pose.angular_velocity[2]);
    if (used > 0 && (size_t)used < sizeof(response))
        send_all(fd, response, (size_t)used);
}

typedef struct status_client {
    svrt_status_server *server;
    int fd;
} status_client;

static void *status_client_thread(void *opaque) {
    status_client *client = opaque;
    svrt_status_server *server = client->server;
    answer_client(server, client->fd);
    close(client->fd);
    pthread_mutex_lock(&server->clients_lock);
    --server->active_clients;
    pthread_cond_broadcast(&server->clients_done);
    pthread_mutex_unlock(&server->clients_lock);
    free(client);
    return NULL;
}

static void *tracking_thread(void *opaque) {
    svrt_status_server *server = opaque; int socket6 = socket(AF_INET6,SOCK_DGRAM,0);
    if (socket6 < 0) return NULL;int off=0,one=1;setsockopt(socket6,IPPROTO_IPV6,IPV6_V6ONLY,&off,sizeof(off));setsockopt(socket6,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));struct sockaddr_in6 bind_address={.sin6_family=AF_INET6,.sin6_addr=IN6ADDR_ANY_INIT,.sin6_port=htons(9947)};if(bind(socket6,(struct sockaddr*)&bind_address,sizeof(bind_address))){close(socket6);return NULL;}
    while(!atomic_load(&server->stopping)) {
        struct sockaddr_storage target; socklen_t target_size=0; uint32_t session=0;
        const int authorized = atomic_load(&server->state) != SVRT_RECEIVER_UNAUTHORIZED;
        pthread_mutex_lock(&server->tracking_lock);
        if (!authorized) {
            memset(&server->tracking_address,0,sizeof(server->tracking_address));
            server->tracking_address_size=0;server->tracking_session=0;
        }
        target=server->tracking_address;target_size=server->tracking_address_size;
        session=server->tracking_session;pthread_mutex_unlock(&server->tracking_lock);
        if(session&&target_size){svrt_synthetic_pose pose;svrt_status_server_get_pose(server,atomic_load(&server->state),&pose);
            stearlight_pose_info info={.flags=(uint16_t)((pose.valid?1:0)|(pose.connected?2:0)),.session_id=session,.sequence=(uint32_t)pose.sequence,.timestamp_us=pose.timestamp_us,.result=(uint32_t)pose.result};
            unsigned n=0;for(unsigned i=0;i<3;i++)info.values[n++]=(float)pose.position[i];for(unsigned i=0;i<4;i++)info.values[n++]=(float)pose.quaternion[i];for(unsigned i=0;i<3;i++)info.values[n++]=(float)pose.velocity[i];for(unsigned i=0;i<3;i++)info.values[n++]=(float)pose.angular_velocity[i];
            stearlight_pose_packet packet;
            if(!stearlight_pose_encode(&packet,&info)&&sendto(socket6,&packet,sizeof(packet),0,(struct sockaddr *)&target,target_size)<0){static int reported;if(!reported++){fprintf(stderr,"SVRT tracking: UDP send failed: %s family=%d\n",strerror(errno),target.ss_family);}}
        }
        struct pollfd wait={.fd=socket6,.events=POLLIN};if(poll(&wait,1,4)>0){char hello[64]={0};struct sockaddr_storage peer;socklen_t peer_size=sizeof(peer);ssize_t size=recvfrom(socket6,hello,sizeof(hello)-1,0,(struct sockaddr*)&peer,&peer_size);unsigned hello_session=0;if(authorized&&size>0&&sscanf(hello,"STEARLIGHT_TRACK %u",&hello_session)==1&&hello_session){pthread_mutex_lock(&server->tracking_lock);server->tracking_address=peer;server->tracking_address_size=peer_size;server->tracking_session=hello_session;pthread_mutex_unlock(&server->tracking_lock);}}
    }
    close(socket6);return NULL;
}

static void *discovery_thread(void *opaque){svrt_status_server *server=opaque;int fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0)return NULL;int one=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof(one));setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&one,sizeof(one));struct sockaddr_in address={.sin_family=AF_INET,.sin_addr.s_addr=INADDR_ANY,.sin_port=htons(9757)};if(bind(fd,(struct sockaddr*)&address,sizeof(address))){close(fd);return NULL;}while(!atomic_load(&server->stopping)){struct pollfd wait={.fd=fd,.events=POLLIN};if(poll(&wait,1,250)<=0)continue;char request[64];struct sockaddr_storage peer;socklen_t peer_size=sizeof(peer);ssize_t size=recvfrom(fd,request,sizeof(request),0,(struct sockaddr*)&peer,&peer_size);if(atomic_load(&server->state)!=SVRT_RECEIVER_UNAUTHORIZED&&size==20&&!memcmp(request,"STEARLIGHT_DISCOVERY",20)){const char response[]="STEARLIGHT/2 9945 9944 9946 9947";sendto(fd,response,sizeof(response)-1,0,(struct sockaddr*)&peer,peer_size);}}close(fd);return NULL;}

static void *status_thread(void *opaque) {
    svrt_status_server *server = opaque;
    int listener = socket(AF_INET6, SOCK_STREAM, 0);
    if (listener < 0) return NULL;
    int one = 1, off = 0;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    struct sockaddr_in6 address = {.sin6_family = AF_INET6,
                                   .sin6_addr = IN6ADDR_ANY_INIT,
                                   .sin6_port = htons(server->port)};
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
        listen(listener, 4)) {
        close(listener);
        return NULL;
    }
    while (!atomic_load(&server->stopping)) {
        fd_set reads;
        FD_ZERO(&reads);
        FD_SET(listener, &reads);
        struct timeval wait = {.tv_sec = 1};
        int ready = select(listener + 1, &reads, NULL, NULL, &wait);
        if (ready <= 0) continue;
        int client = accept(listener, NULL, NULL);
        if (client >= 0) {
            status_client *connection = malloc(sizeof(*connection));
            if (!connection) {
                close(client);
                continue;
            }
            connection->server = server;
            connection->fd = client;
            pthread_mutex_lock(&server->clients_lock);
            ++server->active_clients;
            pthread_mutex_unlock(&server->clients_lock);
            pthread_t thread;
            if (pthread_create(&thread, NULL, status_client_thread, connection)) {
                pthread_mutex_lock(&server->clients_lock);
                --server->active_clients;
                pthread_cond_broadcast(&server->clients_done);
                pthread_mutex_unlock(&server->clients_lock);
                close(client);
                free(connection);
            } else {
                pthread_detach(thread);
            }
        }
    }
    close(listener);
    return NULL;
}

int svrt_status_server_start(svrt_status_server *server, uint16_t port) {
    if (!server) return -1;
    memset(server, 0, sizeof(*server));
    server->port = port ? port : 9945;
    pthread_mutex_init(&server->clients_lock, NULL);
    pthread_mutex_init(&server->tracking_lock, NULL);
    pthread_cond_init(&server->clients_done, NULL);
    atomic_store(&server->state, SVRT_RECEIVER_UNAUTHORIZED);
    pthread_t *thread = malloc(sizeof(*thread));
    if (!thread || pthread_create(thread, NULL, status_thread, server)) {
        free(thread);
        pthread_cond_destroy(&server->clients_done);
        pthread_mutex_destroy(&server->clients_lock);
        pthread_mutex_destroy(&server->tracking_lock);
        return -1;
    }
    server->thread = thread;
    if (!pthread_create(&server->tracking_thread,NULL,tracking_thread,server))
        server->tracking_started=1;
    if (!pthread_create(&server->discovery_thread,NULL,discovery_thread,server))
        server->discovery_started=1;
    return 0;
}

void svrt_status_server_update(svrt_status_server *server, int state,
                               const svrt_stats *stats) {
    if (!server) return;
    if (stats) {
        atomic_store(&server->decoded, stats->decoded_frames);
        atomic_store(&server->presented, stats->presented_frames);
        atomic_store(&server->dropped, stats->dropped_frames);
        atomic_store(&server->bytes, stats->bytes_received);
        atomic_store(&server->invalid_packets,stats->invalid_packets);
        atomic_store(&server->fec_recovered,stats->fec_recovered_shards);
        atomic_store(&server->network_dropped,stats->network_dropped_frames);
    }
    if (atomic_load(&server->authorization_revoked) &&
        state != SVRT_RECEIVER_UNAUTHORIZED)
        state = SVRT_RECEIVER_UNAUTHORIZED;
    atomic_store(&server->state, state);
}

void svrt_status_server_packet_event(void *opaque, svrt_packet_event event,
                                     uint64_t pts_us, uint64_t receiver_time_us) {
    svrt_status_server *server = opaque;
    if (!server || pts_us == UINT64_MAX) return;
    if (event == SVRT_PACKET_RECEIVED) {
        atomic_store(&server->received_time_us, receiver_time_us);
        atomic_store(&server->received_pts_us, pts_us);
    } else if (event == SVRT_PACKET_PROCESSED) {
        atomic_store(&server->processed_time_us, receiver_time_us);
        atomic_store(&server->processed_pts_us, pts_us);
    }
}

void svrt_status_server_reset_trace(svrt_status_server *server) {
    if (!server) return;
    atomic_store(&server->received_pts_us, UINT64_MAX);
    atomic_store(&server->received_time_us, 0);
    atomic_store(&server->processed_pts_us, UINT64_MAX);
    atomic_store(&server->processed_time_us, 0);
}

void svrt_status_server_stop(svrt_status_server *server) {
    if (!server || !server->thread) return;
    atomic_store(&server->stopping, 1);
    pthread_t *thread = server->thread;
    pthread_join(*thread, NULL);
    if(server->tracking_started)pthread_join(server->tracking_thread,NULL);
    if(server->discovery_started)pthread_join(server->discovery_thread,NULL);
    free(thread);
    server->thread = NULL;
    pthread_mutex_lock(&server->clients_lock);
    while (server->active_clients) pthread_cond_wait(&server->clients_done,
                                                       &server->clients_lock);
    pthread_mutex_unlock(&server->clients_lock);
    pthread_cond_destroy(&server->clients_done);
    pthread_mutex_destroy(&server->clients_lock);
    pthread_mutex_destroy(&server->tracking_lock);
}

void svrt_status_server_set_steam_device_id(svrt_status_server *server,
                                            uint64_t device_id) {
    if (server) atomic_store(&server->steam_device_id, device_id);
}

void svrt_status_server_set_paired_host(svrt_status_server *server,
                                        const char *address) {
    if (!server) return;
    pthread_mutex_lock(&server->tracking_lock);
    if (address && address[0])
        snprintf(server->paired_address, sizeof(server->paired_address), "%s",
                 address);
    else
        server->paired_address[0] = 0;
    pthread_mutex_unlock(&server->tracking_lock);
}

void svrt_status_server_reset_authorization(svrt_status_server *server) {
    if (!server) return;
    atomic_store(&server->authorization_revoked, 0);
}

void svrt_status_server_revoke_authorization(svrt_status_server *server) {
    if (!server) return;
    atomic_store(&server->authorization_revoked, 1);
    atomic_store(&server->state, SVRT_RECEIVER_UNAUTHORIZED);
}

int svrt_status_server_authorization_revoked(svrt_status_server *server) {
    return server && atomic_load(&server->authorization_revoked);
}
