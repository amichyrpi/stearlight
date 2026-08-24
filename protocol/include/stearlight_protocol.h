#ifndef STEARLIGHT_PROTOCOL_H
#define STEARLIGHT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STEARLIGHT_MAGIC 0x53544152u /* STAR */
#define STEARLIGHT_VERSION 2u
#define STEARLIGHT_DATAGRAM_SIZE 1200u
#define STEARLIGHT_VIDEO_HEADER_SIZE 36u
#define STEARLIGHT_VIDEO_SHARD_SIZE \
    (STEARLIGHT_DATAGRAM_SIZE - STEARLIGHT_VIDEO_HEADER_SIZE)
#define STEARLIGHT_FEC_DATA_SHARDS 10u
#define STEARLIGHT_FEC_PARITY_SHARDS 2u
#define STEARLIGHT_MAX_FRAME_SIZE (8u * 1024u * 1024u)

enum stearlight_packet_type {
    STEARLIGHT_PACKET_VIDEO = 1,
    STEARLIGHT_PACKET_POSE = 2,
    STEARLIGHT_PACKET_CONTROLLER = 3,
    STEARLIGHT_PACKET_AUDIO = 4,
    STEARLIGHT_PACKET_PING = 5,
    STEARLIGHT_PACKET_STATS = 6,
    STEARLIGHT_PACKET_CONTROL = 7
};

#define STEARLIGHT_CONTROL_HEADER_SIZE 24u
enum stearlight_control_code {
    STEARLIGHT_CONTROL_SHUTDOWN = 1,
    STEARLIGHT_CONTROL_DISCONNECTED = 2
};

enum stearlight_packet_flags {
    STEARLIGHT_FLAG_KEYFRAME = 1u << 0,
    STEARLIGHT_FLAG_FEC = 1u << 1
};

#pragma pack(push, 1)
typedef struct stearlight_video_header {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t session_id;
    uint32_t frame_id;
    uint64_t timestamp_us;
    uint32_t frame_size;
    uint16_t fec_group;
    uint8_t shard_id;
    uint8_t data_shards;
    uint8_t parity_shards;
    uint8_t reserved;
    uint16_t payload_size;
} stearlight_video_header;
#pragma pack(pop)

typedef struct stearlight_video_info {
    uint16_t flags;
    uint32_t session_id;
    uint32_t frame_id;
    uint64_t timestamp_us;
    uint32_t frame_size;
    uint16_t fec_group;
    uint8_t shard_id;
    uint8_t data_shards;
    uint8_t parity_shards;
    uint16_t payload_size;
} stearlight_video_info;

#pragma pack(push, 1)
typedef struct stearlight_control_header {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t session_id;
    uint32_t code;
    uint64_t timestamp_us;
} stearlight_control_header;
#pragma pack(pop)

typedef struct stearlight_control_info {
    uint16_t flags;
    uint32_t session_id;
    uint32_t code;
    uint64_t timestamp_us;
} stearlight_control_info;

#define STEARLIGHT_POSE_VALUE_COUNT 13u
#pragma pack(push, 1)
typedef struct stearlight_pose_packet {
    uint32_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t flags;
    uint32_t session_id;
    uint32_t sequence;
    uint64_t timestamp_us;
    uint32_t result;
    uint32_t values[STEARLIGHT_POSE_VALUE_COUNT];
} stearlight_pose_packet;
#pragma pack(pop)
typedef struct stearlight_pose_info {
    uint16_t flags;
    uint32_t session_id, sequence;
    uint64_t timestamp_us;
    uint32_t result;
    float values[STEARLIGHT_POSE_VALUE_COUNT];
} stearlight_pose_info;

/* Header fields are encoded in network byte order on the wire. */
int stearlight_video_header_encode(stearlight_video_header *wire,
                                   const stearlight_video_info *host);
int stearlight_video_header_decode(stearlight_video_info *host,
                                   const void *datagram, size_t size);
int stearlight_control_encode(stearlight_control_header *wire,
                               const stearlight_control_info *host);
int stearlight_control_decode(stearlight_control_info *host,
                               const void *datagram, size_t size);
int stearlight_pose_encode(stearlight_pose_packet *wire,
                           const stearlight_pose_info *host);
int stearlight_pose_decode(stearlight_pose_info *host,
                           const void *datagram, size_t size);

/* Systematic 10+2 Reed-Solomon over GF(256). All shards have equal size. */
void stearlight_fec_encode(const uint8_t *const *data, unsigned data_count,
                           size_t size, uint8_t *parity0, uint8_t *parity1);
int stearlight_fec_recover(uint8_t **data, const uint8_t *present,
                           unsigned data_count, size_t size,
                           const uint8_t *parity0, const uint8_t *parity1,
                           int have_parity0, int have_parity1);

#ifdef __cplusplus
}
#endif
#endif
