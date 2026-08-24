#include <stearlight_protocol.h>
#include <assert.h>
#include <string.h>

int main(void) {
    uint8_t original[10][257], work[10][257], p0[257], p1[257];
    const uint8_t *source[10]; uint8_t *target[10]; uint8_t present[10];
    for (unsigned shard = 0; shard < 10; ++shard) {
        for (unsigned i = 0; i < 257; ++i)
            original[shard][i] = (uint8_t)(shard * 37u + i * 13u);
        source[shard] = original[shard]; target[shard] = work[shard];
    }
    stearlight_fec_encode(source, 10, 257, p0, p1);
    memcpy(work, original, sizeof(work)); memset(present, 1, sizeof(present));
    memset(work[2], 0, 257); memset(work[8], 0, 257); present[2] = present[8] = 0;
    assert(stearlight_fec_recover(target, present, 10, 257, p0, p1, 1, 1) == 2);
    assert(!memcmp(work, original, sizeof(work)));
    memcpy(work, original, sizeof(work)); memset(present, 1, sizeof(present));
    memset(work[4], 0, 257); present[4] = 0;
    assert(stearlight_fec_recover(target, present, 10, 257, p0, NULL, 1, 0) == 1);
    assert(!memcmp(work, original, sizeof(work)));
    stearlight_video_info info = {.session_id=9,.frame_id=12,.timestamp_us=123456789,
        .frame_size=12345,.fec_group=2,.shard_id=3,.data_shards=10,
        .parity_shards=2,.payload_size=100};
    stearlight_video_header wire = {0}; assert(!stearlight_video_header_encode(&wire,&info));
    uint8_t packet[STEARLIGHT_VIDEO_HEADER_SIZE+100]; memcpy(packet,&wire,sizeof(wire));
    stearlight_video_info decoded; assert(!stearlight_video_header_decode(&decoded,packet,sizeof(packet)));
    assert(decoded.session_id==info.session_id&&decoded.timestamp_us==info.timestamp_us&&decoded.payload_size==100);
    stearlight_control_info control = {.session_id=9,
        .code=STEARLIGHT_CONTROL_SHUTDOWN, .timestamp_us=987654321};
    stearlight_control_header control_wire = {0};
    assert(!stearlight_control_encode(&control_wire, &control));
    stearlight_control_info control_decoded = {0};
    assert(!stearlight_control_decode(&control_decoded, &control_wire,
                                      sizeof(control_wire)));
    assert(control_decoded.session_id == control.session_id &&
           control_decoded.code == control.code &&
           control_decoded.timestamp_us == control.timestamp_us);
    return 0;
}
