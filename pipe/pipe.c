#include <pipe.h>
#include <stearlight_protocol.h>

#include <libavutil/error.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define FRAME_SLOTS 16
#define FRAME_DEADLINE_US 80000u

typedef struct fec_group {
    uint8_t *parity[STEARLIGHT_FEC_PARITY_SHARDS];
    uint8_t parity_present[STEARLIGHT_FEC_PARITY_SHARDS];
} fec_group;

typedef struct video_frame {
    uint32_t session_id, frame_id, frame_size, shard_count, received;
    uint16_t flags;
    uint64_t timestamp_us, first_packet_us;
    uint8_t *data, *present;
    fec_group *groups;
    unsigned group_count;
} video_frame;

struct svrt_pipe {
    int socket;
    svrt_pipe_interrupt interrupt;
    svrt_pipe_idle idle;
    void *opaque;
    AVCodecParameters *parameters;
    video_frame frames[FRAME_SLOTS];
    uint32_t session_id, newest_frame, next_frame;
    uint64_t next_wait_us;
    int have_newest, have_next, need_keyframe;
    int discontinuity;
    int decoder_reset;
    uint64_t invalid_packets, recovered_shards, expired_frames;
    int control_code;
    int saw_video;
    uint64_t last_video_us;
};

static uint64_t monotonic_us(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

static void free_frame(video_frame *frame) {
    if (!frame) return;
    if (frame->groups)
        for (unsigned group = 0; group < frame->group_count; ++group)
            for (unsigned parity = 0; parity < STEARLIGHT_FEC_PARITY_SHARDS; ++parity)
                free(frame->groups[group].parity[parity]);
    free(frame->groups); free(frame->present); free(frame->data);
    memset(frame, 0, sizeof(*frame));
}

static int newer(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }

static video_frame *find_frame(svrt_pipe *pipe,
                               const stearlight_video_info *info,
                               uint64_t now) {
    for (unsigned i = 0; i < FRAME_SLOTS; ++i)
        if (pipe->frames[i].data && pipe->frames[i].session_id == info->session_id &&
            pipe->frames[i].frame_id == info->frame_id) return &pipe->frames[i];
    unsigned slot = FRAME_SLOTS;
    for (unsigned i = 0; i < FRAME_SLOTS; ++i) {
        if (!pipe->frames[i].data) { slot = i; break; }
    }
    if (slot == FRAME_SLOTS) return NULL;
    video_frame *frame = &pipe->frames[slot];
    frame->session_id = info->session_id; frame->frame_id = info->frame_id;
    frame->frame_size = info->frame_size; frame->timestamp_us = info->timestamp_us;
    frame->flags = info->flags & STEARLIGHT_FLAG_KEYFRAME;
    frame->first_packet_us = now;
    frame->shard_count = (info->frame_size + STEARLIGHT_VIDEO_SHARD_SIZE - 1) /
                         STEARLIGHT_VIDEO_SHARD_SIZE;
    frame->group_count = (frame->shard_count + STEARLIGHT_FEC_DATA_SHARDS - 1) /
                         STEARLIGHT_FEC_DATA_SHARDS;
    frame->data = calloc(frame->shard_count, STEARLIGHT_VIDEO_SHARD_SIZE);
    frame->present = calloc(frame->shard_count, 1);
    frame->groups = calloc(frame->group_count, sizeof(*frame->groups));
    if (!frame->data || !frame->present || !frame->groups) {
        free_frame(frame); return NULL;
    }
    return frame;
}

static int recover_group(svrt_pipe *pipe, video_frame *frame, unsigned group) {
    const unsigned first = group * STEARLIGHT_FEC_DATA_SHARDS;
    const unsigned count = frame->shard_count - first < STEARLIGHT_FEC_DATA_SHARDS
                               ? frame->shard_count - first : STEARLIGHT_FEC_DATA_SHARDS;
    uint8_t *data[STEARLIGHT_FEC_DATA_SHARDS];
    uint8_t present[STEARLIGHT_FEC_DATA_SHARDS];
    unsigned missing = 0;
    for (unsigned i = 0; i < count; ++i) {
        data[i] = frame->data + (size_t)(first + i) * STEARLIGHT_VIDEO_SHARD_SIZE;
        present[i] = frame->present[first + i]; if (!present[i]) ++missing;
    }
    if (!missing) return 0;
    fec_group *fec = &frame->groups[group];
    int recovered = stearlight_fec_recover(data, present, count,
        STEARLIGHT_VIDEO_SHARD_SIZE, fec->parity[0], fec->parity[1],
        fec->parity_present[0], fec->parity_present[1]);
    if (recovered < 0) return -1;
    for (unsigned i = 0; i < count; ++i) if (!frame->present[first + i]) {
        frame->present[first + i] = 1; ++frame->received; ++pipe->recovered_shards;
    }
    return recovered;
}

static void accept_datagram(svrt_pipe *pipe, const uint8_t *packet, size_t size) {
    stearlight_control_info control;
    if (!stearlight_control_decode(&control, packet, size) &&
        pipe->session_id && pipe->session_id == control.session_id) {
        pipe->control_code = (int)control.code;
        return;
    }
    stearlight_video_info info;
    if (stearlight_video_header_decode(&info, packet, size)) { ++pipe->invalid_packets; return; }
    if (pipe->session_id && pipe->session_id != info.session_id) {
        for (unsigned i = 0; i < FRAME_SLOTS; ++i) free_frame(&pipe->frames[i]);
        pipe->have_newest = 0;
        pipe->have_next = 0;
        pipe->need_keyframe = 1;
        pipe->discontinuity = 1;
        pipe->decoder_reset = 1;
    }
    if (!pipe->session_id) {
        pipe->need_keyframe = 1;
        pipe->discontinuity = 1;
    }
    pipe->session_id = info.session_id;
    const uint64_t now = monotonic_us();
    pipe->saw_video = 1;
    pipe->last_video_us = now;
    if (!pipe->have_next) {
        pipe->next_frame = info.frame_id;
        pipe->next_wait_us = now;
        pipe->have_next = 1;
    } else if (newer(pipe->next_frame, info.frame_id)) {
        return;
    }
    video_frame *frame = find_frame(pipe, &info, now);
    if (!frame || frame->frame_size != info.frame_size ||
        frame->timestamp_us != info.timestamp_us || info.fec_group >= frame->group_count) {
        ++pipe->invalid_packets; return;
    }
    const unsigned first = info.fec_group * STEARLIGHT_FEC_DATA_SHARDS;
    const unsigned expected = frame->shard_count - first < STEARLIGHT_FEC_DATA_SHARDS
                                  ? frame->shard_count - first : STEARLIGHT_FEC_DATA_SHARDS;
    if (info.data_shards != expected) { ++pipe->invalid_packets; return; }
    const uint8_t *payload = packet + STEARLIGHT_VIDEO_HEADER_SIZE;
    if (info.shard_id < info.data_shards) {
        const unsigned index = first + info.shard_id;
        const size_t offset = (size_t)index * STEARLIGHT_VIDEO_SHARD_SIZE;
        const unsigned expected_size = frame->frame_size - offset < STEARLIGHT_VIDEO_SHARD_SIZE
                ? frame->frame_size - (unsigned)offset : STEARLIGHT_VIDEO_SHARD_SIZE;
        if (info.payload_size != expected_size) { ++pipe->invalid_packets; return; }
        if (!frame->present[index]) {
            memcpy(frame->data + offset, payload, info.payload_size);
            frame->present[index] = 1; ++frame->received;
        }
    } else {
        const unsigned parity = info.shard_id - info.data_shards;
        if (parity >= STEARLIGHT_FEC_PARITY_SHARDS ||
            info.payload_size != STEARLIGHT_VIDEO_SHARD_SIZE) return;
        fec_group *fec = &frame->groups[info.fec_group];
        if (!fec->parity_present[parity]) {
            fec->parity[parity] = malloc(STEARLIGHT_VIDEO_SHARD_SIZE);
            if (!fec->parity[parity]) return;
            memcpy(fec->parity[parity], payload, STEARLIGHT_VIDEO_SHARD_SIZE);
            fec->parity_present[parity] = 1;
        }
        recover_group(pipe, frame, info.fec_group);
    }
    if (!pipe->have_newest || newer(info.frame_id, pipe->newest_frame)) {
        pipe->newest_frame = info.frame_id; pipe->have_newest = 1;
    }
}

static video_frame *ready_frame(svrt_pipe *pipe, uint64_t now) {
    while (pipe->have_next) {
        video_frame *expected = NULL;
        for (unsigned i = 0; i < FRAME_SLOTS; ++i)
            if (pipe->frames[i].data && pipe->frames[i].frame_id == pipe->next_frame) {
                expected = &pipe->frames[i]; break;
            }
        if (expected && expected->received == expected->shard_count) {
            if (pipe->need_keyframe && !(expected->flags & STEARLIGHT_FLAG_KEYFRAME)) {
                free_frame(expected); ++pipe->next_frame;
                pipe->next_wait_us = now; continue;
            }
            pipe->need_keyframe = 0; ++pipe->next_frame; pipe->next_wait_us = now;
            return expected;
        }
        if (pipe->have_newest && newer(pipe->newest_frame, pipe->next_frame) &&
            now - (expected ? expected->first_packet_us : pipe->next_wait_us) >=
                FRAME_DEADLINE_US) {
            /* Do not spend another full deadline on every frame behind the
               live edge. At 60/90 Hz that lets the backlog grow faster than
               it can be expired, fills all FRAME_SLOTS, and makes every new
               datagram look invalid. Drop the stale run in one operation and
               resume reassembly at the newest frame. */
            const uint32_t resume = pipe->newest_frame;
            uint32_t skipped = resume - pipe->next_frame;
            if (!skipped) skipped = 1;
            for (unsigned i = 0; i < FRAME_SLOTS; ++i)
                if (pipe->frames[i].data &&
                    newer(resume, pipe->frames[i].frame_id))
                    free_frame(&pipe->frames[i]);
            pipe->expired_frames += skipped;
            pipe->next_frame = resume;
            pipe->next_wait_us = now;
            pipe->need_keyframe = 1; pipe->discontinuity = 1; continue;
        }
        return NULL;
    }
    return NULL;
}

int svrt_pipe_make_url(char *out, unsigned size, const svrt_pipe_config *config) {
    if (!out || !size || !config) return -1;
    int used = snprintf(out, size, "udp://%s:%u", config->bind_address
                            ? config->bind_address : "0.0.0.0",
                        config->port ? config->port : 9944);
    return used < 0 || (unsigned)used >= size ? -1 : 0;
}

int svrt_pipe_listen(svrt_pipe **out, const svrt_pipe_config *config,
                     char *error, unsigned error_size) {
    if (!out || !config) return -1;
    *out = NULL;
    svrt_pipe *pipe = calloc(1, sizeof(*pipe));
    if (!pipe) return -1;
    pipe->socket = -1; pipe->interrupt = config->interrupt;
    pipe->idle = config->idle; pipe->opaque = config->opaque;
    char service[16]; snprintf(service, sizeof(service), "%u", config->port ? config->port : 9944);
    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_DGRAM,
                             .ai_flags = AI_PASSIVE};
    struct addrinfo *addresses = NULL;
    if (getaddrinfo(config->bind_address, service, &hints, &addresses)) goto fail;
    for (struct addrinfo *it = addresses; it; it = it->ai_next) {
        pipe->socket = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (pipe->socket < 0) continue;
        int one = 1, buffer = 4 * 1024 * 1024;
        setsockopt(pipe->socket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(pipe->socket, SOL_SOCKET, SO_RCVBUF, &buffer, sizeof(buffer));
        if (!bind(pipe->socket, it->ai_addr, it->ai_addrlen)) break;
        close(pipe->socket); pipe->socket = -1;
    }
    freeaddrinfo(addresses); addresses = NULL;
    if (pipe->socket < 0) goto fail;
    pipe->parameters = avcodec_parameters_alloc();
    if (!pipe->parameters) goto fail;
    pipe->parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    pipe->parameters->codec_id = AV_CODEC_ID_HEVC;
    pipe->parameters->width = 2880; pipe->parameters->height = 1600;
    *out = pipe; return 0;
fail:
    if (addresses) freeaddrinfo(addresses);
    if (error && error_size) snprintf(error, error_size,
        "could not open Stearlight UDP listener: %s", strerror(errno));
    svrt_pipe_close(&pipe); return -1;
}

const AVCodecParameters *svrt_pipe_video_parameters(const svrt_pipe *pipe) {
    return pipe ? pipe->parameters : NULL;
}
AVRational svrt_pipe_time_base(const svrt_pipe *pipe) {
    (void)pipe; return (AVRational){1, 1000000};
}

int svrt_pipe_read(svrt_pipe *pipe, AVPacket *packet) {
    if (!pipe || !packet) return AVERROR(EINVAL);
    uint8_t datagram[STEARLIGHT_DATAGRAM_SIZE];
    while (!pipe->interrupt || !pipe->interrupt(pipe->opaque)) {
        struct pollfd wait = {.fd = pipe->socket, .events = POLLIN};
        int ready = poll(&wait, 1, 20);
        if (ready < 0 && errno != EINTR) return AVERROR(errno);
        if (ready > 0) {
            ssize_t size = recv(pipe->socket, datagram, sizeof(datagram), 0);
            if (size > 0) accept_datagram(pipe, datagram, (size_t)size);
        }
        const uint64_t now = monotonic_us();
        if (pipe->idle) pipe->idle(pipe->opaque, now / 1000u);
        if (pipe->control_code) return AVERROR_EXIT;
        if (pipe->saw_video && now - pipe->last_video_us >= 3000000u) {
            pipe->control_code = STEARLIGHT_CONTROL_DISCONNECTED;
            return AVERROR(EPIPE);
        }
        video_frame *complete = ready_frame(pipe, monotonic_us());
        if (!complete) continue;
        if (av_new_packet(packet, (int)complete->frame_size) < 0) return AVERROR(ENOMEM);
        memcpy(packet->data, complete->data, complete->frame_size);
        packet->pts = packet->dts = (int64_t)complete->timestamp_us;
        free_frame(complete); return 0;
    }
    return AVERROR_EXIT;
}
void svrt_pipe_get_stats(const svrt_pipe *pipe,svrt_pipe_stats *stats){if(pipe&&stats)*stats=(svrt_pipe_stats){pipe->invalid_packets,pipe->recovered_shards,pipe->expired_frames};}
int svrt_pipe_take_discontinuity(svrt_pipe *pipe){if(!pipe)return 0;int changed=pipe->discontinuity;pipe->discontinuity=0;return changed;}
int svrt_pipe_take_decoder_reset(svrt_pipe *pipe){if(!pipe)return 0;int reset=pipe->decoder_reset;pipe->decoder_reset=0;return reset;}
int svrt_pipe_take_control(svrt_pipe *pipe){if(!pipe)return 0;int code=pipe->control_code;pipe->control_code=0;return code;}

void svrt_pipe_close(svrt_pipe **pointer) {
    if (!pointer || !*pointer) return;
    svrt_pipe *pipe = *pointer; *pointer = NULL;
    if (pipe->socket >= 0) close(pipe->socket);
    for (unsigned i = 0; i < FRAME_SLOTS; ++i) free_frame(&pipe->frames[i]);
    avcodec_parameters_free(&pipe->parameters);
    if (pipe->invalid_packets || pipe->expired_frames || pipe->recovered_shards)
        fprintf(stderr, "SVRT UDP: invalid=%llu expired_frames=%llu recovered_shards=%llu\n",
                (unsigned long long)pipe->invalid_packets,
                (unsigned long long)pipe->expired_frames,
                (unsigned long long)pipe->recovered_shards);
    free(pipe);
}
