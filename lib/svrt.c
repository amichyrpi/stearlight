#include <svrt.h>
#include <pipe.h>
#include <stearlight_protocol.h>
#include "drm_presenter.h"
#include <errno.h>
#include <libavcodec/avcodec.h>
#include <libavutil/dict.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <SDL.h>

enum { SVRT_EXTRA_SLOTS = 128 };

struct svrt_context {
    svrt_config cfg; atomic_int stopping; svrt_end_reason end_reason;
    svrt_pipe *pipe;
    AVCodecContext *decoder; AVBufferRef *hw_device; AVFrame *frame; AVPacket *packet;
    svrt_pipe *extra_pipe;
    AVCodecContext *extra_decoder; AVFrame *extra_decode_frame; AVPacket *extra_packet;
    AVFrame *extra_slots[SVRT_EXTRA_SLOTS]; uint64_t extra_ids[SVRT_EXTRA_SLOTS], extra_sequence;
    pthread_t extra_thread; pthread_mutex_t extra_mutex; pthread_cond_t extra_cond;
    atomic_int extra_started, extra_connected, extra_finished;
    pthread_t present_thread; pthread_mutex_t present_mutex; pthread_cond_t present_cond;
    AVFrame *present_main,*present_extra; uint64_t present_pts_us; atomic_int present_started;
    SDL_Window *window; SDL_Renderer *renderer; SDL_Texture *texture;
    int owns_display;
    svrt_drm *drm;
    struct {
        atomic_uint_fast64_t access_units, decoded_frames, presented_frames;
        atomic_uint_fast64_t dropped_frames, bytes_received, last_pts_us;
    } stats;
    uint64_t first_frame_us, last_report_us, last_report_frame;
    char error[256];
};
static void set_error(svrt_context *c,const char *fmt,...){va_list ap;va_start(ap,fmt);vsnprintf(c->error,sizeof(c->error),fmt,ap);va_end(ap);}
static uint64_t monotonic_us(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
static void packet_event(svrt_context *c,svrt_packet_event event,uint64_t pts_us){if(c->cfg.packet_event)c->cfg.packet_event(c->cfg.packet_event_opaque,event,pts_us,monotonic_us());}
static enum AVPixelFormat choose_format(AVCodecContext *unused,const enum AVPixelFormat *fmts){(void)unused;for(const enum AVPixelFormat *p=fmts;*p!=AV_PIX_FMT_NONE;p++)if(*p==AV_PIX_FMT_DRM_PRIME)return *p;return fmts[0];}
static const AVCodec *find_decoder(int require_hw){(void)require_hw;return avcodec_find_decoder(AV_CODEC_ID_HEVC);}
static void disable_local_input(void){
    const Uint32 events[]={SDL_KEYDOWN,SDL_KEYUP,SDL_TEXTEDITING,SDL_TEXTINPUT,
        SDL_KEYMAPCHANGED,SDL_MOUSEMOTION,SDL_MOUSEBUTTONDOWN,
        SDL_MOUSEBUTTONUP,SDL_MOUSEWHEEL,SDL_FINGERDOWN,SDL_FINGERUP,
        SDL_FINGERMOTION};
    SDL_ShowCursor(SDL_DISABLE);
    for(size_t i=0;i<sizeof(events)/sizeof(events[0]);i++)SDL_EventState(events[i],SDL_IGNORE);
}
static int open_video(svrt_context *c,const AVCodecParameters *parameters){
    const AVCodec *codec=find_decoder(c->cfg.require_hardware);if(!codec){set_error(c,"HEVC decoder not found");return -1;}
    if(parameters)fprintf(stderr,"SVRT: stream=%dx%d codec=%s\n",parameters->width,parameters->height,avcodec_get_name(parameters->codec_id));
    c->decoder=avcodec_alloc_context3(codec);if(!c->decoder){set_error(c,"avcodec_alloc_context3 failed");return -1;}if(parameters&&avcodec_parameters_to_context(c->decoder,parameters)<0){set_error(c,"invalid HEVC stream parameters");return -1;}if(c->cfg.require_hardware){int hw=av_hwdevice_ctx_create(&c->hw_device,AV_HWDEVICE_TYPE_DRM,NULL,NULL,0);if(hw<0){set_error(c,"could not open FFmpeg DRM hardware device: %d",hw);return -1;}c->decoder->hw_device_ctx=av_buffer_ref(c->hw_device);}c->decoder->flags|=AV_CODEC_FLAG_LOW_DELAY;c->decoder->thread_count=1;c->decoder->get_format=choose_format;
    /* Native presentation can legitimately retain the current scanout, one
       atomic commit in flight, and the latest queued frame.  extra_hw_frames
       is the libavcodec control used by the stateless V4L2 HEVC hwaccel;
       num_capture_buffers is a v4l2m2m option and was silently leaving rpivid
       at its minimum pool, which stalled decode once KMS ownership was fixed. */
    c->decoder->extra_hw_frames=8;int rc=avcodec_open2(c->decoder,codec,NULL);if(rc<0){char e[128];av_strerror(rc,e,sizeof(e));set_error(c,"opening decoder %s failed: %s",codec->name,e);return -1;}
    c->frame=av_frame_alloc();c->packet=av_packet_alloc();if(!c->frame||!c->packet){set_error(c,"FFmpeg allocation failed");return -1;}fprintf(stderr,"SVRT: decoder=%s\n",codec->name);return 0;
}
static int open_display(svrt_context *c){
    if (c->cfg.display_window) {
        c->window = c->cfg.display_window;
        c->renderer = c->cfg.display_renderer;
    } else {
        SDL_setenv("SDL_VIDEODRIVER","kmsdrm",0);SDL_SetHint(SDL_HINT_RENDER_VSYNC,"0");if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS)){set_error(c,"SDL_Init: %s",SDL_GetError());return -1;}
        disable_local_input();
        fprintf(stderr,"SVRT: SDL video=%s displays=%d\n",SDL_GetCurrentVideoDriver(),SDL_GetNumVideoDisplays());
        uint32_t flags=SDL_WINDOW_SHOWN|SDL_WINDOW_BORDERLESS;if(c->cfg.fullscreen)flags|=SDL_WINDOW_FULLSCREEN_DESKTOP;c->window=SDL_CreateWindow("SVRT HEVC",SDL_WINDOWPOS_UNDEFINED,SDL_WINDOWPOS_UNDEFINED,640,480,flags);if(!c->window){set_error(c,"SDL_CreateWindow: %s",SDL_GetError());return -1;}
    }
    /* Direct KMS video uses overlay planes. Paint the SDL-owned primary plane
       black once so square video leaves black borders instead of revealing
       boot or login-console text underneath. */
    if (!c->cfg.display_window) {
        SDL_Surface *background=SDL_GetWindowSurface(c->window);
        if(background){SDL_FillRect(background,NULL,SDL_MapRGB(background->format,0,0,0));SDL_UpdateWindowSurface(c->window);}
    }
    int drm_rc=svrt_drm_open(&c->drm,c->window,c->error,sizeof(c->error));if(drm_rc&&c->cfg.require_zero_copy)return -1;
    if(!c->drm&&!c->renderer){c->renderer=SDL_CreateRenderer(c->window,-1,SDL_RENDERER_ACCELERATED);if(!c->renderer){set_error(c,"SDL_CreateRenderer: %s",SDL_GetError());return -1;}c->error[0]='\0';}
    return 0;
}
static int interrupted(void *opaque){return atomic_load(&((svrt_context*)opaque)->stopping);}
int svrt_open(svrt_context **out,const svrt_config *cfg){if(!out)return -1;*out=NULL;svrt_context *c=calloc(1,sizeof(*c));if(!c)return -1;if(cfg)c->cfg=*cfg;else c->cfg=(svrt_config){.port=9944,.require_hardware=1,.require_zero_copy=1,.fullscreen=1};if(!c->cfg.port)c->cfg.port=9944;if(!c->cfg.extra_port)c->cfg.extra_port=(uint16_t)(c->cfg.port+3);pthread_mutex_init(&c->extra_mutex,NULL);pthread_cond_init(&c->extra_cond,NULL);pthread_mutex_init(&c->present_mutex,NULL);pthread_cond_init(&c->present_cond,NULL);if(!c->cfg.headless&&open_display(c)){fprintf(stderr,"SVRT: %s\n",c->error);svrt_close(&c);return -1;}if(!c->cfg.headless&&!c->cfg.display_window)c->owns_display=1;*out=c;return 0;}
static int software_present(svrt_context *c,AVFrame *f){uint32_t fmt;if(f->format==AV_PIX_FMT_YUV420P)fmt=SDL_PIXELFORMAT_IYUV;else if(f->format==AV_PIX_FMT_NV12)fmt=SDL_PIXELFORMAT_NV12;else{set_error(c,"unsupported software pixel format %s",av_get_pix_fmt_name(f->format));return -1;}if(c->texture){Uint32 texture_fmt=0;int texture_w=0,texture_h=0;if(SDL_QueryTexture(c->texture,&texture_fmt,NULL,&texture_w,&texture_h)){set_error(c,"SDL_QueryTexture: %s",SDL_GetError());return -1;}if(texture_fmt!=fmt||texture_w!=f->width||texture_h!=f->height){SDL_DestroyTexture(c->texture);c->texture=NULL;}}if(!c->texture)c->texture=SDL_CreateTexture(c->renderer,fmt,SDL_TEXTUREACCESS_STREAMING,f->width,f->height);if(!c->texture){set_error(c,"SDL_CreateTexture: %s",SDL_GetError());return -1;}int rc=f->format==AV_PIX_FMT_YUV420P?SDL_UpdateYUVTexture(c->texture,NULL,f->data[0],f->linesize[0],f->data[1],f->linesize[1],f->data[2],f->linesize[2]):SDL_UpdateNVTexture(c->texture,NULL,f->data[0],f->linesize[0],f->data[1],f->linesize[1]);if(rc||SDL_RenderClear(c->renderer)||SDL_RenderCopy(c->renderer,c->texture,NULL,NULL)){set_error(c,"SDL render: %s",SDL_GetError());return -1;}SDL_RenderPresent(c->renderer);return 0;}
static int64_t frame_id(const AVFrame *frame,AVRational time_base,uint64_t fallback){
    int64_t pts=frame->best_effort_timestamp;
    if(pts==AV_NOPTS_VALUE)return fallback==UINT64_MAX?INT64_MIN:(int64_t)fallback;
    return av_rescale_q(pts,time_base,AV_TIME_BASE_Q);
}
static int publish_extra(svrt_context *c,AVFrame *frame,int64_t pts_id){
    /* Do not retain a reference to a bcm2835-codec capture surface here.
       The timestamp matcher can retain more frames than v4l2m2m's eight
       capture buffers. Keeping AVFrame clones can therefore exhaust the
       decoder pool (or expose a surface to KMS while firmware recycles it),
       producing transient grey frames and a corrupt companion strip.  The
       960x1080 tile is small enough to detach once into ordinary ARM memory;
       the V4L2 buffer is then returned as soon as decode_extra unrefs it. */
    AVFrame *copy=av_frame_alloc();if(!copy)return -1;
    copy->format=frame->format;copy->width=frame->width;copy->height=frame->height;
    if(av_frame_get_buffer(copy,64)<0||av_frame_copy(copy,frame)<0||av_frame_copy_props(copy,frame)<0){av_frame_free(&copy);return -1;}
    pthread_mutex_lock(&c->extra_mutex);uint64_t sequence=++c->extra_sequence,id=pts_id==INT64_MIN?sequence:(uint64_t)pts_id;unsigned slot=(unsigned)(sequence%SVRT_EXTRA_SLOTS);
    av_frame_free(&c->extra_slots[slot]);c->extra_slots[slot]=copy;c->extra_ids[slot]=id;
    if(sequence==1||sequence%60==0)fprintf(stderr,"SVRT: extra frame=%llu pts_id=%lld\n",(unsigned long long)sequence,(long long)pts_id);
    pthread_cond_broadcast(&c->extra_cond);pthread_mutex_unlock(&c->extra_mutex);return 0;
}
static int open_extra_video(svrt_context *c,const AVCodecParameters *parameters){
    const AVCodec *codec=avcodec_find_decoder_by_name("h264_v4l2m2m");
    if(!codec){set_error(c,"H.264 V4L2 decoder not found");return -1;}
    c->extra_decoder=avcodec_alloc_context3(codec);if(!c->extra_decoder)return -1;
    if(parameters&&avcodec_parameters_to_context(c->extra_decoder,parameters)<0)return -1;
    c->extra_decoder->flags|=AV_CODEC_FLAG_LOW_DELAY;c->extra_decoder->thread_count=1;
    AVDictionary *opts=NULL;av_dict_set(&opts,"num_capture_buffers","8",0);
    int rc=avcodec_open2(c->extra_decoder,codec,&opts);av_dict_free(&opts);if(rc<0){set_error(c,"opening H.264 hardware decoder failed: %d",rc);return -1;}
    c->extra_decode_frame=av_frame_alloc();c->extra_packet=av_packet_alloc();
    if(!c->extra_decode_frame||!c->extra_packet)return -1;
    fprintf(stderr,"SVRT: extra stream=%dx%d decoder=%s\n",parameters?parameters->width:0,parameters?parameters->height:0,codec->name);return 0;
}
static int decode_extra(svrt_context *c,AVRational tb,int flush){
    int rc=avcodec_send_packet(c->extra_decoder,flush?NULL:c->extra_packet);
    if(rc<0&&rc!=AVERROR(EAGAIN))return -1;
    while((rc=avcodec_receive_frame(c->extra_decoder,c->extra_decode_frame))>=0){
        int64_t id=frame_id(c->extra_decode_frame,tb,UINT64_MAX);
        if((c->extra_decode_frame->format!=AV_PIX_FMT_YUV420P&&c->extra_decode_frame->format!=AV_PIX_FMT_YUVJ420P)||
           c->extra_decode_frame->width!=960||c->extra_decode_frame->height!=1080||publish_extra(c,c->extra_decode_frame,id))return -1;
        av_frame_unref(c->extra_decode_frame);
    }
    return rc==AVERROR(EAGAIN)||rc==AVERROR_EOF?0:-1;
}
static void *extra_receiver(void *opaque){
    svrt_context *c=opaque;char error[256]={0};
    svrt_pipe_config pc={.bind_address=c->cfg.bind_address,.port=c->cfg.extra_port,.interrupt=interrupted,.opaque=c};
    fprintf(stderr,"SVRT: listening for native H.264 tiles on TCP %u\n",c->cfg.extra_port);
    if(svrt_pipe_listen(&c->extra_pipe,&pc,error,sizeof(error))||atomic_load(&c->stopping))goto done;
    if(open_extra_video(c,svrt_pipe_video_parameters(c->extra_pipe)))goto done;
    AVRational tb=svrt_pipe_time_base(c->extra_pipe);c->extra_decoder->pkt_timebase=tb;atomic_store(&c->extra_connected,1);
    while(!atomic_load(&c->stopping)){
        int rc=svrt_pipe_read(c->extra_pipe,c->extra_packet);if(rc<0)break;
        if(decode_extra(c,tb,0)){av_packet_unref(c->extra_packet);break;}av_packet_unref(c->extra_packet);
    }
done:
    atomic_store(&c->extra_connected,0);atomic_store(&c->extra_finished,1);
    pthread_mutex_lock(&c->extra_mutex);pthread_cond_broadcast(&c->extra_cond);pthread_mutex_unlock(&c->extra_mutex);
    if(error[0]&&!atomic_load(&c->stopping))fprintf(stderr,"SVRT: extra receiver: %s\n",error);return NULL;
}
static AVFrame *matching_extra(svrt_context *c,uint64_t wanted){
    if(c->frame&&c->frame->best_effort_timestamp!=AV_NOPTS_VALUE)wanted=(uint64_t)av_rescale_q(c->frame->best_effort_timestamp,c->decoder->pkt_timebase,AV_TIME_BASE_Q);
    /* Both streams share a frame ID, but the separate hardware decoders can
       complete several milliseconds apart.  Stay below one 45 Hz frame
       period while allowing the H.264 companion to finish instead of
       needlessly dropping an otherwise valid reconstructed frame. */
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_nsec+=18000000;
    if(until.tv_nsec>=1000000000){until.tv_sec++;until.tv_nsec-=1000000000;}
    pthread_mutex_lock(&c->extra_mutex);
    while(!atomic_load(&c->stopping)){
        for(unsigned i=0;i<SVRT_EXTRA_SLOTS;i++)if(c->extra_slots[i]&&c->extra_ids[i]<wanted)av_frame_free(&c->extra_slots[i]);
        for(unsigned slot=0;slot<SVRT_EXTRA_SLOTS;slot++)if(c->extra_slots[slot]&&c->extra_ids[slot]==wanted){AVFrame *result=c->extra_slots[slot];c->extra_slots[slot]=NULL;pthread_mutex_unlock(&c->extra_mutex);return result;}
        if(pthread_cond_timedwait(&c->extra_cond,&c->extra_mutex,&until)==ETIMEDOUT)break;
    }
    pthread_mutex_unlock(&c->extra_mutex);return NULL;
}
static int queue_dual_present(svrt_context *c,const AVFrame *main,const AVFrame *extra,uint64_t pts_us){
    AVFrame *main_copy=av_frame_clone(main),*extra_copy=av_frame_clone(extra);
    if(!main_copy||!extra_copy){av_frame_free(&main_copy);av_frame_free(&extra_copy);return -1;}
    pthread_mutex_lock(&c->present_mutex);
    if(c->present_main){av_frame_free(&c->present_main);av_frame_free(&c->present_extra);atomic_fetch_add(&c->stats.dropped_frames,1);}
    c->present_main=main_copy;c->present_extra=extra_copy;c->present_pts_us=pts_us;
    pthread_cond_signal(&c->present_cond);pthread_mutex_unlock(&c->present_mutex);return 0;
}
static void *present_worker(void *opaque){
    svrt_context *c=opaque;
    while(!atomic_load(&c->stopping)){
        pthread_mutex_lock(&c->present_mutex);
        while(!atomic_load(&c->stopping)&&!c->present_main)pthread_cond_wait(&c->present_cond,&c->present_mutex);
        AVFrame *main=c->present_main,*extra=c->present_extra;uint64_t pts=c->present_pts_us;
        c->present_main=NULL;c->present_extra=NULL;pthread_mutex_unlock(&c->present_mutex);
        if(!main)continue;char error[256]={0};int rc=svrt_drm_present_dual(c->drm,main,extra,error,sizeof(error));
        if(rc<0)fprintf(stderr,"SVRT: native presentation failed: %s\n",error[0]?error:"KMS error");
        else packet_event(c,SVRT_PACKET_PROCESSED,pts);
        av_frame_free(&main);av_frame_free(&extra);
    }
    return NULL;
}
static int decode(svrt_context *c,AVRational time_base,uint64_t packet_pts_us,int flush){
    int rc=avcodec_send_packet(c->decoder,flush?NULL:c->packet);
    if(rc<0&&rc!=AVERROR(EAGAIN)){set_error(c,"avcodec_send_packet failed: %d",rc);return -1;}
    while((rc=avcodec_receive_frame(c->decoder,c->frame))>=0){
        /* Authorization can be revoked while the hardware decoder owns a
           backlog. Never display those already-decoded frames after stop. */
        if(atomic_load(&c->stopping)){av_frame_unref(c->frame);return 0;}
        uint64_t decoded=atomic_fetch_add(&c->stats.decoded_frames,1)+1;
        int64_t pts_id=frame_id(c->frame,time_base,packet_pts_us);int shown;
        if(c->frame->format!=AV_PIX_FMT_DRM_PRIME&&(c->cfg.require_hardware||c->cfg.require_zero_copy))shown=-1;
        else if(c->cfg.headless)shown=0;
        else if(c->frame->format==AV_PIX_FMT_DRM_PRIME&&c->drm)shown=svrt_drm_present(c->drm,c->frame,c->error,sizeof(c->error));
        else if(!c->cfg.require_hardware&&!c->cfg.require_zero_copy&&c->renderer)shown=software_present(c,c->frame);
        else shown=-1;
        if(shown<0&&!c->error[0])set_error(c,"decoder returned %s instead of DRM PRIME",av_get_pix_fmt_name(c->frame->format));
        if(shown)atomic_fetch_add(&c->stats.dropped_frames,1);else atomic_fetch_add(&c->stats.presented_frames,1);
        uint64_t now=monotonic_us();
        if(decoded==1){c->first_frame_us=c->last_report_us=now;c->last_report_frame=decoded;}
        if(decoded==1||decoded%60==0){
            double fps=0.0,average=0.0;
            if(now>c->last_report_us)fps=(double)(decoded-c->last_report_frame)*1000000.0/(double)(now-c->last_report_us);
            if(now>c->first_frame_us)average=(double)(decoded-1)*1000000.0/(double)(now-c->first_frame_us);
            fprintf(stderr,"SVRT: frame_id=%llu pts_id=%lld format=%s queued=%llu dropped=%llu decode_fps=%.2f average_fps=%.2f\n",(unsigned long long)decoded,(long long)pts_id,av_get_pix_fmt_name(c->frame->format),(unsigned long long)atomic_load(&c->stats.presented_frames),(unsigned long long)atomic_load(&c->stats.dropped_frames),fps,average);
            c->last_report_us=now;c->last_report_frame=decoded;
        }
        av_frame_unref(c->frame);if(shown<0)return -1;
    }
    return rc==AVERROR(EAGAIN)||rc==AVERROR_EOF?0:-1;
}
int svrt_run(svrt_context *c){
    if(!c)return -1;
    svrt_pipe_config pc={.bind_address=c->cfg.bind_address,.port=c->cfg.port,
                         .interrupt=interrupted,
                         .idle=(svrt_pipe_idle)c->cfg.ui_idle,
                         .opaque=c->cfg.ui_idle_opaque ? c->cfg.ui_idle_opaque : c};
    fprintf(stderr,"SVRT: listening for Stearlight HEVC/FEC on UDP %u\n",c->cfg.port);
    int rc=svrt_pipe_listen(&c->pipe,&pc,c->error,sizeof(c->error));
    if(!rc)rc=open_video(c,svrt_pipe_video_parameters(c->pipe));
    if(!rc){
        AVRational time_base=svrt_pipe_time_base(c->pipe);c->decoder->pkt_timebase=time_base;
        while(!atomic_load(&c->stopping)){
            SDL_Event e;while(!c->cfg.headless&&SDL_PollEvent(&e))if(e.type==SDL_QUIT)atomic_store(&c->stopping,1);
            rc=svrt_pipe_read(c->pipe,c->packet);if(rc<0){int control=svrt_pipe_take_control(c->pipe);if(control==STEARLIGHT_CONTROL_SHUTDOWN)c->end_reason=SVRT_END_SHUTDOWN;else if(control==STEARLIGHT_CONTROL_DISCONNECTED)c->end_reason=SVRT_END_DISCONNECTED;break;}if(svrt_pipe_take_decoder_reset(c->pipe)){avcodec_flush_buffers(c->decoder);fprintf(stderr,"SVRT: new video session; decoder reset at keyframe\n");}else if(svrt_pipe_take_discontinuity(c->pipe))fprintf(stderr,"SVRT: video continuity restored at keyframe\n");
            atomic_fetch_add(&c->stats.access_units,1);atomic_fetch_add(&c->stats.bytes_received,(uint64_t)c->packet->size);
            uint64_t pts_us=c->packet->pts==AV_NOPTS_VALUE?UINT64_MAX:(uint64_t)av_rescale_q(c->packet->pts,time_base,AV_TIME_BASE_Q);
            if(pts_us!=UINT64_MAX){atomic_store(&c->stats.last_pts_us,pts_us);packet_event(c,SVRT_PACKET_RECEIVED,pts_us);}
            if(decode(c,time_base,pts_us,0)){av_packet_unref(c->packet);rc=-1;break;}av_packet_unref(c->packet);
        }
        if(!atomic_load(&c->stopping)&&rc>=0&&decode(c,time_base,UINT64_MAX,1))rc=-1;
    }
    atomic_store(&c->stopping,1);
    if(rc<0&&!atomic_load(&c->stats.decoded_frames)&&!c->error[0]&&
       c->end_reason==SVRT_END_ERROR)
        set_error(c,"video stream ended: %d",rc);
    return c->error[0]?-1:0;
}
void svrt_stop(svrt_context *c){if(c)atomic_store(&c->stopping,1);}
void svrt_get_stats(const svrt_context *c,svrt_stats *out){if(c&&out){svrt_pipe_stats network={0};svrt_pipe_get_stats(c->pipe,&network);*out=(svrt_stats){.access_units=atomic_load(&c->stats.access_units),.decoded_frames=atomic_load(&c->stats.decoded_frames),.presented_frames=atomic_load(&c->stats.presented_frames),.dropped_frames=atomic_load(&c->stats.dropped_frames),.bytes_received=atomic_load(&c->stats.bytes_received),.last_pts_us=atomic_load(&c->stats.last_pts_us),.invalid_packets=network.invalid_packets,.fec_recovered_shards=network.recovered_shards,.network_dropped_frames=network.expired_frames};}}
const char *svrt_last_error(const svrt_context *c){return c?c->error:"invalid context";}
svrt_end_reason svrt_get_end_reason(const svrt_context *c){return c?c->end_reason:SVRT_END_ERROR;}
void svrt_close(svrt_context **ptr){if(!ptr||!*ptr)return;svrt_context *c=*ptr;*ptr=NULL;atomic_store(&c->stopping,1);svrt_pipe_close(&c->pipe);svrt_pipe_close(&c->extra_pipe);svrt_drm_close(&c->drm);if(c->texture)SDL_DestroyTexture(c->texture);if(c->owns_display&&c->renderer)SDL_DestroyRenderer(c->renderer);if(c->owns_display&&c->window)SDL_DestroyWindow(c->window);if(c->owns_display)SDL_Quit();av_packet_free(&c->packet);av_frame_free(&c->frame);avcodec_free_context(&c->decoder);av_buffer_unref(&c->hw_device);av_packet_free(&c->extra_packet);av_frame_free(&c->extra_decode_frame);for(unsigned i=0;i<SVRT_EXTRA_SLOTS;i++)av_frame_free(&c->extra_slots[i]);av_frame_free(&c->present_main);av_frame_free(&c->present_extra);avcodec_free_context(&c->extra_decoder);pthread_cond_destroy(&c->extra_cond);pthread_mutex_destroy(&c->extra_mutex);pthread_cond_destroy(&c->present_cond);pthread_mutex_destroy(&c->present_mutex);free(c);}
