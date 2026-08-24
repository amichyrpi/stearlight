#include <stearlight_protocol.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
#include <string.h>

_Static_assert(sizeof(stearlight_video_header) == STEARLIGHT_VIDEO_HEADER_SIZE,
               "unexpected Stearlight video header size");
_Static_assert(sizeof(stearlight_control_header) == STEARLIGHT_CONTROL_HEADER_SIZE,
               "unexpected Stearlight control header size");
_Static_assert(sizeof(stearlight_pose_packet) == 80,
               "unexpected Stearlight pose packet size");

static uint64_t swap64(uint64_t value) {
    const uint32_t high = htonl((uint32_t)(value >> 32));
    const uint32_t low = htonl((uint32_t)value);
    return ((uint64_t)low << 32) | high;
}

int stearlight_video_header_encode(stearlight_video_header *wire,
                                   const stearlight_video_info *host) {
    if (!wire || !host || !host->session_id || !host->frame_size ||
        host->frame_size > STEARLIGHT_MAX_FRAME_SIZE ||
        !host->data_shards || host->data_shards > STEARLIGHT_FEC_DATA_SHARDS ||
        host->parity_shards > STEARLIGHT_FEC_PARITY_SHARDS ||
        host->payload_size > STEARLIGHT_VIDEO_SHARD_SIZE ||
        host->shard_id >= host->data_shards + host->parity_shards)
        return -1;
    memset(wire, 0, sizeof(*wire));
    wire->magic = htonl(STEARLIGHT_MAGIC);
    wire->version = STEARLIGHT_VERSION;
    wire->type = STEARLIGHT_PACKET_VIDEO;
    wire->flags = htons(host->flags);
    wire->session_id = htonl(host->session_id);
    wire->frame_id = htonl(host->frame_id);
    wire->timestamp_us = swap64(host->timestamp_us);
    wire->frame_size = htonl(host->frame_size);
    wire->fec_group = htons(host->fec_group);
    wire->shard_id = host->shard_id;
    wire->data_shards = host->data_shards;
    wire->parity_shards = host->parity_shards;
    wire->payload_size = htons(host->payload_size);
    return 0;
}

int stearlight_video_header_decode(stearlight_video_info *host,
                                   const void *datagram, size_t size) {
    if (!host || !datagram || size < sizeof(stearlight_video_header)) return -1;
    stearlight_video_header wire;
    memcpy(&wire, datagram, sizeof(wire));
    if (ntohl(wire.magic) != STEARLIGHT_MAGIC ||
        wire.version != STEARLIGHT_VERSION ||
        wire.type != STEARLIGHT_PACKET_VIDEO)
        return -1;
    memset(host, 0, sizeof(*host));
    host->flags = ntohs(wire.flags);
    host->session_id = ntohl(wire.session_id);
    host->frame_id = ntohl(wire.frame_id);
    host->timestamp_us = swap64(wire.timestamp_us);
    host->frame_size = ntohl(wire.frame_size);
    host->fec_group = ntohs(wire.fec_group);
    host->shard_id = wire.shard_id;
    host->data_shards = wire.data_shards;
    host->parity_shards = wire.parity_shards;
    host->payload_size = ntohs(wire.payload_size);
    if (!host->session_id || !host->frame_size ||
        host->frame_size > STEARLIGHT_MAX_FRAME_SIZE ||
        !host->data_shards || host->data_shards > STEARLIGHT_FEC_DATA_SHARDS ||
        host->parity_shards > STEARLIGHT_FEC_PARITY_SHARDS ||
        host->shard_id >= host->data_shards + host->parity_shards ||
        host->payload_size > STEARLIGHT_VIDEO_SHARD_SIZE ||
        sizeof(wire) + host->payload_size != size)
        return -1;
    return 0;
}

int stearlight_control_encode(stearlight_control_header *wire,
                               const stearlight_control_info *host) {
    if (!wire || !host || !host->session_id || !host->code) return -1;
    memset(wire, 0, sizeof(*wire));
    wire->magic = htonl(STEARLIGHT_MAGIC);
    wire->version = STEARLIGHT_VERSION;
    wire->type = STEARLIGHT_PACKET_CONTROL;
    wire->flags = htons(host->flags);
    wire->session_id = htonl(host->session_id);
    wire->code = htonl(host->code);
    wire->timestamp_us = swap64(host->timestamp_us);
    return 0;
}

int stearlight_control_decode(stearlight_control_info *host,
                               const void *datagram, size_t size) {
    if (!host || !datagram || size != sizeof(stearlight_control_header)) return -1;
    stearlight_control_header wire;
    memcpy(&wire, datagram, sizeof(wire));
    if (ntohl(wire.magic) != STEARLIGHT_MAGIC ||
        wire.version != STEARLIGHT_VERSION ||
        wire.type != STEARLIGHT_PACKET_CONTROL) return -1;
    memset(host, 0, sizeof(*host));
    host->flags = ntohs(wire.flags);
    host->session_id = ntohl(wire.session_id);
    host->code = ntohl(wire.code);
    host->timestamp_us = swap64(wire.timestamp_us);
    return host->session_id &&
           (host->code == STEARLIGHT_CONTROL_SHUTDOWN ||
            host->code == STEARLIGHT_CONTROL_DISCONNECTED) ? 0 : -1;
}

int stearlight_pose_encode(stearlight_pose_packet *wire,
                           const stearlight_pose_info *host) {
    if (!wire || !host || !host->session_id) return -1;
    memset(wire, 0, sizeof(*wire)); wire->magic=htonl(STEARLIGHT_MAGIC);
    wire->version=STEARLIGHT_VERSION; wire->type=STEARLIGHT_PACKET_POSE;
    wire->flags=htons(host->flags); wire->session_id=htonl(host->session_id);
    wire->sequence=htonl(host->sequence); wire->timestamp_us=swap64(host->timestamp_us);
    wire->result=htonl(host->result);
    for(unsigned i=0;i<STEARLIGHT_POSE_VALUE_COUNT;++i){uint32_t bits;memcpy(&bits,&host->values[i],4);wire->values[i]=htonl(bits);}
    return 0;
}
int stearlight_pose_decode(stearlight_pose_info *host,
                           const void *datagram, size_t size) {
    if(!host||!datagram||size!=sizeof(stearlight_pose_packet))return -1;
    stearlight_pose_packet wire;memcpy(&wire,datagram,sizeof(wire));
    if(ntohl(wire.magic)!=STEARLIGHT_MAGIC||wire.version!=STEARLIGHT_VERSION||wire.type!=STEARLIGHT_PACKET_POSE)return -1;
    memset(host,0,sizeof(*host));host->flags=ntohs(wire.flags);host->session_id=ntohl(wire.session_id);host->sequence=ntohl(wire.sequence);host->timestamp_us=swap64(wire.timestamp_us);host->result=ntohl(wire.result);
    if(!host->session_id)return -1;
    for(unsigned i=0;i<STEARLIGHT_POSE_VALUE_COUNT;++i){uint32_t bits=ntohl(wire.values[i]);memcpy(&host->values[i],&bits,4);}return 0;
}

static uint8_t gf_mul(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    while (b) {
        if (b & 1) result ^= a;
        const uint8_t high = a & 0x80;
        a <<= 1;
        if (high) a ^= 0x1d; /* x^8+x^4+x^3+x^2+1 */
        b >>= 1;
    }
    return result;
}

static uint8_t gf_pow(uint8_t value, unsigned power) {
    uint8_t result = 1;
    while (power) {
        if (power & 1) result = gf_mul(result, value);
        value = gf_mul(value, value);
        power >>= 1;
    }
    return result;
}

static uint8_t gf_div(uint8_t a, uint8_t b) {
    return !a ? 0 : gf_mul(a, gf_pow(b, 254));
}

void stearlight_fec_encode(const uint8_t *const *data, unsigned count,
                           size_t size, uint8_t *p0, uint8_t *p1) {
    if (!data || !count || !p0 || !p1) return;
    memset(p0, 0, size);
    memset(p1, 0, size);
    for (unsigned shard = 0; shard < count; ++shard) {
        const uint8_t coefficient = (uint8_t)(shard + 1);
        for (size_t byte = 0; byte < size; ++byte) {
            p0[byte] ^= data[shard][byte];
            p1[byte] ^= gf_mul(coefficient, data[shard][byte]);
        }
    }
}

int stearlight_fec_recover(uint8_t **data, const uint8_t *present,
                           unsigned count, size_t size, const uint8_t *p0,
                           const uint8_t *p1, int have0, int have1) {
    if (!data || !present || !count || count > STEARLIGHT_FEC_DATA_SHARDS)
        return -1;
    int missing[2] = {-1, -1};
    unsigned missing_count = 0;
    for (unsigned i = 0; i < count; ++i)
        if (!present[i]) {
            if (missing_count == 2) return -1;
            missing[missing_count++] = (int)i;
        }
    if (!missing_count) return 0;
    if (!have0 || !p0 || (missing_count == 2 && (!have1 || !p1))) return -1;
    uint8_t *a = data[missing[0]];
    uint8_t *b = missing_count == 2 ? data[missing[1]] : NULL;
    if (!a || (missing_count == 2 && !b)) return -1;
    for (size_t byte = 0; byte < size; ++byte) {
        uint8_t sum0 = p0[byte];
        uint8_t sum1 = have1 ? p1[byte] : 0;
        for (unsigned i = 0; i < count; ++i) if (present[i]) {
            sum0 ^= data[i][byte];
            if (have1) sum1 ^= gf_mul((uint8_t)(i + 1), data[i][byte]);
        }
        if (missing_count == 1) {
            a[byte] = sum0;
        } else {
            const uint8_t ca = (uint8_t)(missing[0] + 1);
            const uint8_t cb = (uint8_t)(missing[1] + 1);
            a[byte] = gf_div((uint8_t)(sum1 ^ gf_mul(cb, sum0)),
                             (uint8_t)(ca ^ cb));
            b[byte] = (uint8_t)(sum0 ^ a[byte]);
        }
    }
    return (int)missing_count;
}
