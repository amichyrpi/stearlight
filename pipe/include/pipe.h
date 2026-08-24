#ifndef SVRT_PIPE_H
#define SVRT_PIPE_H
#include <libavcodec/packet.h>
#include <libavcodec/codec_par.h>
#include <libavutil/rational.h>
#include <stdint.h>
typedef struct svrt_pipe svrt_pipe;
typedef int (*svrt_pipe_interrupt)(void *opaque);
typedef void (*svrt_pipe_idle)(void *opaque, uint64_t now_ms);
typedef struct svrt_pipe_config {
    const char *bind_address;
    uint16_t port;
    svrt_pipe_interrupt interrupt;
    svrt_pipe_idle idle;
    void *opaque;
} svrt_pipe_config;
typedef struct svrt_pipe_stats { uint64_t invalid_packets,recovered_shards,expired_frames; } svrt_pipe_stats;
int svrt_pipe_make_url(char *out,unsigned out_size,const svrt_pipe_config *config);
int svrt_pipe_listen(svrt_pipe **out,const svrt_pipe_config *config,char *error,unsigned error_size);
const AVCodecParameters *svrt_pipe_video_parameters(const svrt_pipe *pipe);
AVRational svrt_pipe_time_base(const svrt_pipe *pipe);
int svrt_pipe_read(svrt_pipe *pipe,AVPacket *packet);
void svrt_pipe_get_stats(const svrt_pipe *pipe,svrt_pipe_stats *stats);
int svrt_pipe_take_discontinuity(svrt_pipe *pipe);
int svrt_pipe_take_decoder_reset(svrt_pipe *pipe);
int svrt_pipe_take_control(svrt_pipe *pipe);
void svrt_pipe_close(svrt_pipe **pipe);
#endif
