#ifndef SVRT_H
#define SVRT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct svrt_context svrt_context;
typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;

typedef void (*svrt_ui_idle_cb)(void *opaque, uint32_t now_ms);
typedef enum svrt_end_reason {
    SVRT_END_ERROR = 0,
    SVRT_END_DISCONNECTED = 1,
    SVRT_END_SHUTDOWN = 2
} svrt_end_reason;

typedef enum svrt_packet_event {
    SVRT_PACKET_RECEIVED = 1,
    SVRT_PACKET_PROCESSED = 2,
} svrt_packet_event;

typedef void (*svrt_packet_event_cb)(void *opaque, svrt_packet_event event,
                                     uint64_t pts_us, uint64_t receiver_time_us);

typedef struct svrt_config {
    uint16_t port;                 /* TCP port; 9944 when zero */
    uint16_t extra_port;           /* reserved for compatibility; unused */
    const char *bind_address;      /* NULL means all interfaces */
    int require_hardware;          /* fail instead of software fallback */
    int require_zero_copy;         /* reject frames that are not DRM PRIME */
    int fullscreen;                /* create a KMSDRM fullscreen window */
    int headless;                  /* decode/measure without opening a display */
    svrt_packet_event_cb packet_event; /* optional latency instrumentation */
    void *packet_event_opaque;
    /* Optional display owned by the receiver UI. When supplied, svrt keeps
       the window/renderer alive and only owns the decoder and KMS planes. */
    SDL_Window *display_window;
    SDL_Renderer *display_renderer;
    svrt_ui_idle_cb ui_idle;
    void *ui_idle_opaque;
} svrt_config;

typedef struct svrt_stats {
    uint64_t access_units;
    uint64_t decoded_frames;
    uint64_t presented_frames;
    uint64_t dropped_frames;
    uint64_t bytes_received;
    uint64_t last_pts_us;
    uint64_t invalid_packets;
    uint64_t fec_recovered_shards;
    uint64_t network_dropped_frames;
} svrt_stats;

/* Open decoder, SDL KMSDRM display and listening socket. */
int svrt_open(svrt_context **out, const svrt_config *config);
/* Accept one sender and run until disconnect, quit event, or error. */
int svrt_run(svrt_context *ctx);
svrt_end_reason svrt_get_end_reason(const svrt_context *ctx);
void svrt_stop(svrt_context *ctx);
void svrt_get_stats(const svrt_context *ctx, svrt_stats *out);
const char *svrt_last_error(const svrt_context *ctx);
void svrt_close(svrt_context **ctx);

#ifdef __cplusplus
}
#endif
#endif
