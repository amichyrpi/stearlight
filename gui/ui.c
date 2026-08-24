#include "ui.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/mathematics.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>

#define SVRT_PANEL_WIDTH 1024
#define SVRT_PANEL_HEIGHT 640
#define SVRT_EYE_HEIGHT_METERS 1.70f
#define SVRT_EYE_SEPARATION_METERS 0.064f
#define SVRT_VERTICAL_FOV_DEGREES 100.0f
#define SVRT_UI_HORIZONTAL_ASPECT 1.10f
#define SVRT_BOOT_SCALE 0.78f
#define SVRT_PI 3.14159265358979323846f

struct svrt_ui_video {
    AVFormatContext *format;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    SDL_Texture *texture;
    AVBufferRef *hw_device;
    AVRational time_base;
    int stream;
    int have_frame;
    int64_t frame_ms;
    uint32_t frame_duration_ms;
    uint32_t duration_ms;
    uint32_t playback_start_ms;
    int playback_started;
    uint32_t loops_completed;
    int ended;
    const char *path;
};

static svrt_ui_video *video_open(const char *path);

static void disable_local_input(void) {
    static const Uint32 events[] = {
        SDL_KEYUP, SDL_TEXTEDITING, SDL_TEXTINPUT,
        SDL_KEYMAPCHANGED, SDL_MOUSEMOTION,
        SDL_MOUSEBUTTONUP, SDL_MOUSEWHEEL, SDL_FINGERDOWN, SDL_FINGERUP,
        SDL_FINGERMOTION};
    SDL_ShowCursor(SDL_DISABLE);
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); ++i)
        SDL_EventState(events[i], SDL_IGNORE);
}

static void poll_ui_actions(svrt_ui *ui) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F9)
            ui->connection_requested = 1;
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            int width = 0, height = 0;
            SDL_GetRendererOutputSize(ui->renderer, &width, &height);
            float x = event.button.x, y = event.button.y;
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
            const float scene_width = width * 0.5f *
                                      SVRT_DEBUG_LEFT_EYE_UI_SCALE;
            const float scene_height = height *
                                       SVRT_DEBUG_LEFT_EYE_UI_SCALE;
            x = (x - (width - scene_width) * 0.5f) /
                SVRT_DEBUG_LEFT_EYE_UI_SCALE;
            y = (y - (height - scene_height) * 0.5f) /
                SVRT_DEBUG_LEFT_EYE_UI_SCALE;
#endif
            const int eye_width = width / 2;
            const int sidebar_w = (int)(eye_width * 0.065f);
            const int sidebar_h = (int)(height * 0.48f);
            const SDL_Rect sidebar = {
                (int)(eye_width * 0.055f),
                (height - sidebar_h) / 2 - height / 20,
                sidebar_w, sidebar_h};
            if (x >= sidebar.x && x < sidebar.x + sidebar.w &&
                y >= sidebar.y && y < sidebar.y + sidebar.h) {
                int item = (int)((y - sidebar.y) * 6 / sidebar.h);
                if (item < 0) item = 0;
                if (item > 5) item = 5;
                ui->selected_page = item;
                ui->pending_action = (svrt_ui_action)(SVRT_UI_ACTION_HOME + item);
            }
            const int bar_h = (int)(height * 0.072f);
            const int bar_w = (int)(eye_width * 0.48f);
            const int bar_y = (int)(height * 0.745f);
            const SDL_Rect steam = {(eye_width - bar_w) / 2 - bar_h - 8,
                                    bar_y, bar_h, bar_h};
            const SDL_Rect bar = {steam.x + steam.w + 8, bar_y,
                                  bar_w, bar_h};
            const SDL_Rect connection = {bar.x + 8, bar.y + 7,
                                         bar.h - 14, bar.h - 14};
            const SDL_Rect avatar = {bar.x + bar.w - bar.h + 7, bar.y + 7,
                                     bar.h - 14, bar.h - 14};
            if (x >= connection.x && x < connection.x + connection.w &&
                y >= connection.y && y < connection.y + connection.h) {
                ui->connection_requested = 1;
                ui->pending_action = SVRT_UI_ACTION_CONNECTION;
            } else if (x >= steam.x && x < steam.x + steam.w &&
                       y >= steam.y && y < steam.y + steam.h) {
                ui->selected_page = 0;
                ui->pending_action = SVRT_UI_ACTION_HOME;
            } else if (x >= avatar.x && x < avatar.x + avatar.w &&
                       y >= avatar.y && y < avatar.y + avatar.h) {
                ui->pending_action = SVRT_UI_ACTION_PROFILE;
            }
        }
    }
}

static int read_integer_file(const char *path, int *value) {
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    const int valid = fscanf(file, "%d", value) == 1;
    fclose(file);
    return valid;
}

static int wifi_connected(void) {
    static uint32_t next_check_ms;
    static int cached;
    const uint32_t now_ms = SDL_GetTicks();
    if (now_ms < next_check_ms) return cached;
    next_check_ms = now_ms + 2000;
    FILE *file = fopen("/sys/class/net/wlan0/operstate", "r");
    if (!file) return cached = 0;
    char state[16] = {0};
    const int connected = fgets(state, sizeof(state), file) &&
                          !strncmp(state, "up", 2);
    fclose(file);
    return cached = connected;
}

static int system_battery_percent(void) {
    static uint32_t next_check_ms;
    static int cached = -1;
    const uint32_t now_ms = SDL_GetTicks();
    if (now_ms < next_check_ms) return cached;
    next_check_ms = now_ms + 5000;
    glob_t matches = {0};
    int percent = -1;
    if (!glob("/sys/class/power_supply/*/capacity", 0, NULL, &matches)) {
        for (size_t i = 0; i < matches.gl_pathc; ++i) {
            if (read_integer_file(matches.gl_pathv[i], &percent)) break;
        }
        globfree(&matches);
    }
    cached = percent >= 0 && percent <= 100 ? percent : -1;
    return cached;
}

static void update_steam_avatar(svrt_ui *ui) {
    if (!ui || ui->avatar) return;
    const uint32_t now_ms = SDL_GetTicks();
    if (now_ms < ui->next_avatar_scan_ms) return;
    ui->next_avatar_scan_ms = now_ms + 5000;
    glob_t matches = {0};
    if (glob("/var/lib/svrt-receiver/.local/share/Steam/config/avatarcache/*",
             0, NULL, &matches))
        return;
    for (size_t i = 0; i < matches.gl_pathc && !ui->avatar; ++i) {
        snprintf(ui->avatar_path, sizeof(ui->avatar_path), "%s",
                 matches.gl_pathv[i]);
        ui->avatar = video_open(ui->avatar_path);
    }
    globfree(&matches);
}

static void video_close(svrt_ui_video **video) {
    if (!video || !*video) return;
    svrt_ui_video *v = *video;
    if (v->texture) SDL_DestroyTexture(v->texture);
    av_packet_free(&v->packet);
    av_frame_free(&v->frame);
    avcodec_free_context(&v->codec);
    av_buffer_unref(&v->hw_device);
    avformat_close_input(&v->format);
    free(v);
    *video = NULL;
}

static enum AVPixelFormat video_choose_format(AVCodecContext *unused,
                                               const enum AVPixelFormat *formats) {
    (void)unused;
    for (const enum AVPixelFormat *format = formats;
         *format != AV_PIX_FMT_NONE; ++format)
        if (*format == AV_PIX_FMT_DRM_PRIME) return *format;
    return formats[0];
}

static svrt_ui_video *video_open(const char *path) {
    svrt_ui_video *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    v->path = path;
    if (avformat_open_input(&v->format, path, NULL, NULL) < 0 ||
        avformat_find_stream_info(v->format, NULL) < 0) {
        video_close(&v); return NULL;
    }
    v->stream = av_find_best_stream(v->format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                    NULL, 0);
    if (v->stream < 0) { video_close(&v); return NULL; }
    const AVStream *stream = v->format->streams[v->stream];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) { video_close(&v); return NULL; }
    v->codec = avcodec_alloc_context3(decoder);
    if (!v->codec || avcodec_parameters_to_context(v->codec, stream->codecpar) < 0) {
        video_close(&v); return NULL;
    }
    /* The Pi's ARM cores cannot decode these high-resolution HEVC UI movies
       at real time.  Use the same DRM PRIME HEVC path as the live receiver;
       frames are transferred to ordinary YUV only for the SDL texture upload. */
    if (av_hwdevice_ctx_create(&v->hw_device, AV_HWDEVICE_TYPE_DRM,
                               NULL, NULL, 0) >= 0) {
        v->codec->hw_device_ctx = av_buffer_ref(v->hw_device);
        v->codec->get_format = video_choose_format;
        v->codec->extra_hw_frames = 4;
    }
    if (avcodec_open2(v->codec, decoder, NULL) < 0) {
        video_close(&v); return NULL;
    }
    v->packet = av_packet_alloc(); v->frame = av_frame_alloc();
    if (!v->packet || !v->frame) { video_close(&v); return NULL; }
    v->time_base = stream->time_base;
    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        int64_t frame_duration = av_rescale_q(
            1, av_inv_q(stream->avg_frame_rate), (AVRational){1, 1000});
        if (frame_duration > 0) v->frame_duration_ms = (uint32_t)frame_duration;
    }
    if (!v->frame_duration_ms) v->frame_duration_ms = 33;
    if (stream->duration > 0) {
        int64_t duration = av_rescale_q(stream->duration, stream->time_base,
                                        (AVRational){1, 1000});
        if (duration > 0) v->duration_ms = (uint32_t)duration;
    }
    if (!v->duration_ms && v->format->duration > 0)
        v->duration_ms = (uint32_t)av_rescale_q(v->format->duration,
                                                AV_TIME_BASE_Q,
                                                (AVRational){1, 1000});
    return v;
}

static void video_rewind(svrt_ui_video *v) {
    if (!v || !v->format) return;
    av_seek_frame(v->format, v->stream, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(v->codec);
    v->have_frame = 0; v->frame_ms = 0; v->ended = 0;
    v->playback_started = 0;
}

static int video_upload_frame(svrt_ui_video *v, const AVFrame *frame,
                              SDL_Renderer *renderer) {
    if (!v || !frame || !renderer) return -1;
    Uint32 format = SDL_PIXELFORMAT_UNKNOWN;
    int packed = 0;
    switch (frame->format) {
        case AV_PIX_FMT_NV12: format = SDL_PIXELFORMAT_NV12; break;
        case AV_PIX_FMT_YUV420P:
        case AV_PIX_FMT_YUVJ420P: format = SDL_PIXELFORMAT_IYUV; break;
        case AV_PIX_FMT_RGB24:
            format = SDL_PIXELFORMAT_RGB24; packed = 1; break;
        case AV_PIX_FMT_RGBA:
            format = SDL_PIXELFORMAT_RGBA32; packed = 1; break;
        case AV_PIX_FMT_BGRA:
            format = SDL_PIXELFORMAT_BGRA32; packed = 1; break;
        default: return -1;
    }
    int width = frame->width, height = frame->height;
    if (v->texture) {
        Uint32 old_format; int old_width, old_height;
        SDL_QueryTexture(v->texture, &old_format, NULL, &old_width, &old_height);
        if (old_format != format || old_width != width || old_height != height) {
            SDL_DestroyTexture(v->texture); v->texture = NULL;
        }
    }
    if (!v->texture)
        v->texture = SDL_CreateTexture(renderer, format,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       width, height);
    if (!v->texture) return -1;
    if (packed)
        return SDL_UpdateTexture(v->texture, NULL, frame->data[0],
                                 frame->linesize[0]);
    if (format == SDL_PIXELFORMAT_NV12)
        return SDL_UpdateNVTexture(v->texture, NULL, frame->data[0],
                                   frame->linesize[0], frame->data[1],
                                   frame->linesize[1]);
    return SDL_UpdateYUVTexture(v->texture, NULL, frame->data[0],
                                frame->linesize[0], frame->data[1],
                                frame->linesize[1], frame->data[2],
                                frame->linesize[2]);
}

static int video_decode_next(svrt_ui_video *v, SDL_Renderer *renderer) {
    for (;;) {
        int rc = avcodec_receive_frame(v->codec, v->frame);
        if (rc == 0) {
            int64_t pts = v->frame->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE) pts = 0;
            v->frame_ms = av_rescale_q(pts, v->time_base, (AVRational){1, 1000});
            AVFrame *upload = v->frame;
            AVFrame *transferred = NULL;
            if (v->frame->format == AV_PIX_FMT_DRM_PRIME) {
                transferred = av_frame_alloc();
                if (!transferred || av_hwframe_transfer_data(transferred,
                                                              v->frame, 0) < 0) {
                    av_frame_free(&transferred);
                    v->have_frame = 0;
                    av_frame_unref(v->frame);
                    return -1;
                }
                upload = transferred;
            }
            v->have_frame = video_upload_frame(v, upload, renderer) == 0;
            av_frame_free(&transferred);
            av_frame_unref(v->frame);
            return v->have_frame ? 0 : -1;
        }
        if (rc != AVERROR(EAGAIN) && rc != AVERROR_EOF) return -1;
        if (rc == AVERROR_EOF) return 1;
        rc = av_read_frame(v->format, v->packet);
        if (rc < 0) {
            avcodec_send_packet(v->codec, NULL);
            continue;
        }
        if (v->packet->stream_index == v->stream)
            avcodec_send_packet(v->codec, v->packet);
        av_packet_unref(v->packet);
    }
}

static void video_draw(svrt_ui_video *v, SDL_Renderer *renderer,
                       const SDL_Rect *destination, uint32_t elapsed_ms,
                       int loop) {
    if (!v || !renderer || !destination) return;
    (void)elapsed_ms;
    uint32_t playback_ms = v->playback_started ?
        SDL_GetTicks() - v->playback_start_ms : 0;
    if (loop && v->duration_ms && playback_ms >= v->duration_ms) {
        ++v->loops_completed;
        video_rewind(v);
        playback_ms = 0;
    }
    if (!v->have_frame) {
        int rc = video_decode_next(v, renderer);
        if (rc == 1 && loop) {
            ++v->loops_completed;
            video_rewind(v);
            if (video_decode_next(v, renderer) == 0) {
                v->playback_start_ms = SDL_GetTicks();
                v->playback_started = 1;
            }
        } else if (rc == 1) {
            v->ended = 1;
        } else if (rc == 0 && !v->playback_started) {
            /* Do not count decoder/device initialization time against the
               animation.  Otherwise the first draw can try to catch up
               thousands of milliseconds of stale frames synchronously. */
            v->playback_start_ms = SDL_GetTicks();
            v->playback_started = 1;
            playback_ms = 0;
        }
    }
    int catchup = 0;
    while (v->have_frame &&
           (uint32_t)v->frame_ms + 2 < playback_ms) {
        if (++catchup >= 8) {
            /* If decoding falls behind, show the newest frame available and
               resynchronize the playback clock instead of blocking the UI
               while it decodes an unbounded backlog. */
            v->playback_start_ms = SDL_GetTicks() - (uint32_t)v->frame_ms;
            break;
        }
        int rc = video_decode_next(v, renderer);
        if (rc == 1) {
            if (loop) {
                /* Some Matroska files do not expose a container duration.
                   Derive it when the decoder reaches EOF, then use the
                   elapsed-time modulo so the loop does not rewind to frame 0
                   on every draw after the first cycle. */
                if (!v->duration_ms)
                    v->duration_ms = (uint32_t)v->frame_ms +
                                     v->frame_duration_ms;
                ++v->loops_completed;
                video_rewind(v);
                if (video_decode_next(v, renderer) == 0) {
                    v->playback_start_ms = SDL_GetTicks();
                    v->playback_started = 1;
                }
            } else {
                v->ended = 1;
            }
            break;
        }
        if (rc < 0) break;
    }
    if (v->texture) SDL_RenderCopy(renderer, v->texture, NULL, destination);
}

static void draw_text(SDL_Renderer *renderer, TTF_Font *font, const char *value,
                      int center_x, int y, uint8_t alpha) {
    if (!renderer || !font || !value || !value[0]) return;
    SDL_Color color = {255, 255, 255, alpha};
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, value, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(texture, alpha);
        SDL_Rect target = {center_x - surface->w / 2, y, surface->w, surface->h};
        SDL_RenderCopy(renderer, texture, NULL, &target);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static void fill_rounded_rect(SDL_Renderer *renderer, const SDL_Rect *rect,
                              int radius, SDL_Color color) {
    if (!renderer || !rect || rect->w <= 0 || rect->h <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > rect->w / 2) radius = rect->w / 2;
    if (radius > rect->h / 2) radius = rect->h / 2;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_Rect middle = {rect->x + radius, rect->y,
                       rect->w - radius * 2, rect->h};
    SDL_Rect center = {rect->x, rect->y + radius,
                       rect->w, rect->h - radius * 2};
    SDL_RenderFillRect(renderer, &middle);
    SDL_RenderFillRect(renderer, &center);
    for (int y = 0; y < radius; ++y) {
        const float dy = radius - y - 0.5f;
        const int inset = radius - (int)sqrtf(radius * radius - dy * dy);
        SDL_RenderDrawLine(renderer, rect->x + inset, rect->y + y,
                          rect->x + rect->w - inset - 1, rect->y + y);
        SDL_RenderDrawLine(renderer, rect->x + inset,
                          rect->y + rect->h - y - 1,
                          rect->x + rect->w - inset - 1,
                          rect->y + rect->h - y - 1);
    }
}

static void draw_round_texture(SDL_Renderer *renderer, SDL_Texture *texture,
                               const SDL_Rect *rect) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (!renderer || !texture || !rect) return;
    enum { segments = 24 };
    SDL_Vertex vertices[segments + 1];
    int indices[segments * 3];
    const SDL_Color white = {255, 255, 255, 255};
    vertices[0] = (SDL_Vertex){
        {rect->x + rect->w * 0.5f, rect->y + rect->h * 0.5f},
        white, {0.5f, 0.5f}};
    for (int i = 0; i < segments; ++i) {
        const float angle = i * 2.0f * SVRT_PI / segments;
        const float x = cosf(angle), y = sinf(angle);
        vertices[i + 1] = (SDL_Vertex){
            {rect->x + rect->w * (0.5f + x * 0.5f),
             rect->y + rect->h * (0.5f + y * 0.5f)},
            white, {0.5f + x * 0.5f, 0.5f + y * 0.5f}};
        indices[i * 3] = 0;
        indices[i * 3 + 1] = i + 1;
        indices[i * 3 + 2] = (i + 1) % segments + 1;
    }
    SDL_RenderGeometry(renderer, texture, vertices, segments + 1,
                       indices, segments * 3);
#else
    SDL_RenderCopy(renderer, texture, NULL, rect);
#endif
}

static void draw_dashboard_chrome_eye(svrt_ui *ui, int eye_x,
                                       int eye_width, int height) {
    const SDL_Color surface = {22, 27, 35, 245};
    const SDL_Color selected = {17, 126, 188, 255};
    const SDL_Color muted = {166, 176, 188, 255};
    const int sidebar_w = (int)(eye_width * 0.065f);
    const int sidebar_h = (int)(height * 0.48f);
    SDL_Rect sidebar = {eye_x + (int)(eye_width * 0.055f),
                        (height - sidebar_h) / 2 - height / 20,
                        sidebar_w, sidebar_h};
    fill_rounded_rect(ui->renderer, &sidebar, sidebar_w / 5, surface);
    const int item_step = (sidebar.h - 14) / 6;
    SDL_Rect side_selected = {sidebar.x + 5,
                              sidebar.y + 7 + ui->selected_page * item_step,
                              sidebar.w - 10, sidebar.w - 10};
    fill_rounded_rect(ui->renderer, &side_selected, 8, selected);
    static const char *items[] = {"H", "L", "S", "F", "D", "G"};
    for (int item = 0; item < 6; ++item)
        draw_text(ui->renderer, ui->small_font, items[item],
                  sidebar.x + sidebar.w / 2,
                  sidebar.y + 11 + item * item_step,
                  item ? muted.a : 255);

    const int bar_h = (int)(height * 0.072f);
    const int bar_w = (int)(eye_width * 0.48f);
    /* The debug single-eye view is uniformly zoomed after this scene is
       composed. Keep the dashboard low without letting that crop its edge. */
    const int bar_y = (int)(height * 0.745f);
    SDL_Rect steam = {eye_x + (eye_width - bar_w) / 2 - bar_h - 8,
                      bar_y, bar_h, bar_h};
    SDL_Rect bar = {steam.x + steam.w + 8, bar_y, bar_w, bar_h};
    fill_rounded_rect(ui->renderer, &steam, 10, surface);
    fill_rounded_rect(ui->renderer, &bar, 10, surface);
    SDL_Rect steam_selected = {steam.x + 5, steam.y + steam.h - 6,
                               steam.w - 10, 3};
    fill_rounded_rect(ui->renderer, &steam_selected, 2, selected);
    draw_text(ui->renderer, ui->small_font, "S",
              steam.x + steam.w / 2, steam.y + 8, 255);

    SDL_Rect connection = {bar.x + 8, bar.y + 7,
                           bar.h - 14, bar.h - 14};
    fill_rounded_rect(ui->renderer, &connection, 8,
                      ui->streaming_mode ? selected :
                      (SDL_Color){31, 38, 48, 255});
    draw_text(ui->renderer, ui->small_font, "C",
              connection.x + connection.w / 2, connection.y + 3, 255);

    char clock_text[16] = {0};
    time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local)) strftime(clock_text, sizeof(clock_text),
                                             "%H:%M", &local);
    draw_text(ui->renderer, ui->small_font, clock_text,
              bar.x + bar.w - bar.h * 2,
              bar.y + 7, 255);
    draw_text(ui->renderer, ui->small_font,
              wifi_connected() ? "WiFi" : "Offline",
              bar.x + bar.w - bar.h * 3,
              bar.y + bar.h / 2, muted.a);
    const int battery = system_battery_percent();
    if (battery >= 0) {
        char battery_text[8];
        snprintf(battery_text, sizeof(battery_text), "%d%%", battery);
        draw_text(ui->renderer, ui->small_font, battery_text,
                  bar.x + bar.w - bar.h * 3,
                  bar.y + 4, muted.a);
    }
    SDL_Rect avatar = {bar.x + bar.w - bar.h + 7, bar.y + 7,
                       bar.h - 14, bar.h - 14};
    fill_rounded_rect(ui->renderer, &avatar, avatar.w / 2,
                      (SDL_Color){50, 62, 77, 255});
    update_steam_avatar(ui);
    if (ui->avatar && !ui->avatar->texture)
        video_decode_next(ui->avatar, ui->renderer);
    if (ui->avatar && ui->avatar->texture)
        draw_round_texture(ui->renderer, ui->avatar->texture, &avatar);
    else
        draw_text(ui->renderer, ui->small_font, "U",
                  avatar.x + avatar.w / 2, avatar.y + 2, 255);
}

static void fit_rect(SDL_Rect *out, int x, int y, int width, int height,
                     int source_width, int source_height, int margin) {
    int available_width = width - margin * 2, available_height = height - margin * 2;
    if ((int64_t)available_width * source_height >
        (int64_t)available_height * source_width) {
        out->h = available_height; out->w = (int64_t)available_height * source_width / source_height;
    } else {
        out->w = available_width; out->h = (int64_t)available_width * source_height / source_width;
    }
    out->x = x + (width - out->w) / 2; out->y = y + (height - out->h) / 2;
}

static void draw_panel_video(svrt_ui *ui, svrt_ui_video *video,
                             uint32_t elapsed_ms, int source_width,
                             int source_height, int margin, int loop) {
    SDL_Rect destination;
    fit_rect(&destination, 0, 0, SVRT_PANEL_WIDTH, SVRT_PANEL_HEIGHT,
             source_width, source_height, margin);
    video_draw(video, ui->renderer, &destination, elapsed_ms, loop);
}

static void draw_loading_content(svrt_ui *ui, svrt_ui_state state,
                                 const char code[5], const char *detail,
                                 uint32_t elapsed_ms, uint8_t alpha) {
    if (!ui->steam_loading)
        ui->steam_loading = video_open(SVRT_GUI_STEAM_LOADING_PATH);

    const int animation_width = 520;
    const int animation_height = animation_width * 1080 / 1920;
    const int text_height = TTF_FontHeight(ui->font);
    const int code_height = TTF_FontHeight(ui->code_font);
    const int animation_text_gap = state == SVRT_UI_AUTHORIZING ? 18 : 24;
    const int text_code_gap = 8;
    const int group_height = animation_height + animation_text_gap +
                             text_height +
                             (state == SVRT_UI_AUTHORIZING ?
                                  text_code_gap + code_height : 0);
    /* The movie has a small, symmetric black border around the logo.  Moving
       the logical group up by half of that border keeps the visible logo and
       its text centered together, rather than centering the logo alone. */
    int y = (SVRT_PANEL_HEIGHT - group_height) / 2 - 12;

    if (ui->steam_loading) {
        SDL_Rect destination = {
            (SVRT_PANEL_WIDTH - animation_width) / 2,
            y,
            animation_width,
            animation_height,
        };
        video_draw(ui->steam_loading, ui->renderer, &destination,
                   elapsed_ms, 1);
    }
    y += animation_height + animation_text_gap;

    const char *text = state == SVRT_UI_AUTHORIZING ?
                           "Enter this pin in Steam" :
                       state == SVRT_UI_STARTING ?
                           "Starting SteamVR stream" :
                           (detail ? detail : "Steam Link pairing failed");
    draw_text(ui->renderer, ui->font, text, SVRT_PANEL_WIDTH / 2, y, alpha);

    if (state == SVRT_UI_AUTHORIZING && code && code[0]) {
        y += text_height + text_code_gap;
        draw_text(ui->renderer, ui->code_font, code,
                  SVRT_PANEL_WIDTH / 2, y, alpha);
    }
}

static void draw_video_per_eye(svrt_ui *ui, svrt_ui_video *video,
                               uint32_t elapsed_ms, int source_width,
                               int source_height, int margin, int loop,
                               float scale, float horizontal_aspect) {
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
    for (int eye = 0; eye < 2; ++eye) {
        const int eye_x = eye * (width / 2);
        const int eye_width = eye ? width - width / 2 : width / 2;
        SDL_Rect destination;
        fit_rect(&destination, eye_x, 0, eye_width, height,
                 source_width, source_height, margin);
        destination.w = (int)lroundf(destination.w * scale * horizontal_aspect);
        destination.h = (int)lroundf(destination.h * scale);
        destination.x = eye_x + (eye_width - destination.w) / 2;
        destination.y = (height - destination.h) / 2;
        video_draw(video, ui->renderer, &destination, elapsed_ms, loop);
    }
}

static void draw_panel_content(svrt_ui *ui, svrt_ui_state state,
                               const char code[5], const char *detail,
                               uint32_t elapsed_ms, uint8_t alpha) {
    if (!ui->panel || SDL_SetRenderTarget(ui->renderer, ui->panel)) return;
    const int loading_state = state == SVRT_UI_AUTHORIZING ||
                              state == SVRT_UI_STARTING ||
                              state == SVRT_UI_FAILED;
    /* steam_loading.mkv has an opaque black background.  Match it while the
       animation is visible so its scaled frame never appears as a rectangle. */
    if (loading_state)
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
    else
        SDL_SetRenderDrawColor(ui->renderer, 5, 7, 10, 255);
    SDL_RenderClear(ui->renderer);
    if (state == SVRT_UI_SEARCHING) {
        if (!ui->loop) ui->loop = video_open(SVRT_GUI_LOOP_PATH);
        if (ui->loop)
            draw_panel_video(ui, ui->loop, elapsed_ms, 1440, 1600, 0, 1);
        if (ui->loop && ui->loop->playback_started &&
            !ui->loop_first_frame_ms)
            ui->loop_first_frame_ms = SDL_GetTicks();
    } else if (state == SVRT_UI_AUTHORIZING || state == SVRT_UI_STARTING) {
        draw_loading_content(ui, state, code, detail, elapsed_ms, alpha);
    } else if (state == SVRT_UI_FAILED) {
        draw_loading_content(ui, state, code, detail, elapsed_ms, alpha);
    } else if (state == SVRT_UI_HOME) {
        if (ui->client_frame) {
            SDL_Rect destination = {0, 0, SVRT_PANEL_WIDTH, SVRT_PANEL_HEIGHT};
            SDL_RenderCopy(ui->renderer, ui->client_frame, NULL, &destination);
        } else {
            draw_text(ui->renderer, ui->font, "Steam ARM64",
                      SVRT_PANEL_WIDTH / 2, SVRT_PANEL_HEIGHT / 2 - 42, 255);
            draw_text(ui->renderer, ui->small_font,
                      detail ? detail : "Starting Big Picture",
                      SVRT_PANEL_WIDTH / 2, SVRT_PANEL_HEIGHT / 2 + 12, 210);
        }
    }
    SDL_SetRenderTarget(ui->renderer, NULL);
}

static float scene_focal_length(int eye_height) {
    const float half_fov = SVRT_VERTICAL_FOV_DEGREES * SVRT_PI / 360.0f;
    return (float)eye_height * 0.5f / tanf(half_fov);
}

static SDL_FPoint project_ground(float world_x, float world_z, int eye_x,
                                 int eye_width, int height, int eye) {
    const float focal = scene_focal_length(height);
    const float eye_offset = eye ? SVRT_EYE_SEPARATION_METERS * 0.5f :
                                   -SVRT_EYE_SEPARATION_METERS * 0.5f;
    return (SDL_FPoint){
        eye_x + eye_width * 0.5f + focal * (world_x - eye_offset) / world_z,
        height * 0.5f + focal * SVRT_EYE_HEIGHT_METERS / world_z};
}

static SDL_FPoint distort_ring_point(SDL_FPoint point, int eye_x,
                                     int eye_width, int height) {
    const float center_x = eye_x + eye_width * 0.5f;
    const float center_y = height * 0.5f;
    const float nx = (point.x - center_x) / (eye_width * 0.5f);
    const float ny = (point.y - center_y) / (height * 0.5f);
    float radius_squared = nx * nx + ny * ny;
    if (radius_squared > 4.0f) radius_squared = 4.0f;
    const float scale = 1.0f + 0.065f * radius_squared;
    point.x = center_x + (point.x - center_x) * scale;
    point.y = center_y + (point.y - center_y) * scale;
    return point;
}

static SDL_FPoint polar_grid_point(float distance, float angle, int eye_x,
                                   int eye_width, int height, int eye) {
    const float center_x = eye_x + eye_width * 0.5f;
    const float origin_y = height * 1.025f;
    const float horizon_y = height * 0.5f;
    const float radial = 1.0f -
        expf(-distance / (SVRT_EYE_HEIGHT_METERS * 1.5f));
    const float curved_radial = powf(radial, 1.18f);
    const float stereo = (eye ? -1.0f : 1.0f) * radial * 2.0f;
    return (SDL_FPoint){
        center_x + eye_width * 0.68f * curved_radial * cosf(angle) + stereo,
        origin_y - (origin_y - horizon_y) * radial * sinf(angle)};
}

static void draw_floor_grid(svrt_ui *ui, int eye_x, int eye_width,
                            int height, int eye) {
    SDL_Rect clip = {eye_x, height / 2, eye_width, height - height / 2};
    SDL_RenderSetClipRect(ui->renderer, &clip);
    SDL_SetRenderDrawBlendMode(ui->renderer, SDL_BLENDMODE_NONE);
    /* SteamVR's visible floor-grid line color.  Keep it opaque so the final
       scanout is exactly #24282F instead of depending on blend rounding. */
    SDL_SetRenderDrawColor(ui->renderer, 36, 40, 47, 255);

    /* Rings are circles in world space. Perspective makes them the familiar
       SteamVR ellipses while the 1.70 m camera height fixes their scale. */
    for (float radius = 0.75f; radius <= 12.0f; radius += 0.75f) {
        SDL_FPoint points[65];
        int count = 0;
        for (int sample = 0; sample <= 64; ++sample) {
            const float angle = (8.0f + sample * 164.0f / 64.0f) *
                                SVRT_PI / 180.0f;
            const float z = radius * sinf(angle);
            if (z > 0.20f)
                points[count++] = distort_ring_point(
                    project_ground(radius * cosf(angle), z, eye_x, eye_width,
                                   height, eye), eye_x, eye_width, height);
        }
        if (count > 1) SDL_RenderDrawLinesF(ui->renderer, points, count);
    }
    for (int spoke = -8; spoke <= 8; ++spoke) {
        const float angle = (90.0f + spoke * 10.0f) * SVRT_PI / 180.0f;
        SDL_FPoint points[41];
        for (int sample = 0; sample <= 40; ++sample) {
            const float distance = 12.0f * sample / 40.0f;
            points[sample] = polar_grid_point(distance, angle, eye_x,
                                               eye_width, height, eye);
            if (spoke == 0)
                points[sample].x = eye_x + eye_width * 0.5f;
        }
        SDL_RenderDrawLinesF(ui->renderer, points, 41);
        if (spoke == 0) {
            for (int sample = 0; sample <= 40; ++sample)
                points[sample].x += 1.0f;
            SDL_RenderDrawLinesF(ui->renderer, points, 41);
        }
    }
    SDL_RenderSetClipRect(ui->renderer, NULL);
}

static void draw_circle_curved_environment_eye(svrt_ui *ui, int eye_x,
                                                int eye_width, int height,
                                                int eye, int draw_floor) {
#if SDL_VERSION_ATLEAST(2, 0, 18)
    enum { segments = 64 };
    SDL_Vertex sky[(segments + 1) * 2];
    SDL_Vertex floor[(segments + 1) * 2];
    int indices[segments * 6];
    const SDL_Color white = {255, 255, 255, 255};
    const SDL_Color black = {0, 0, 0, 255};
    for (int column = 0; column <= segments; ++column) {
        /* Numerically invert an actual 12.75 m world-space circle at this
           screen column. This is the same camera/lens projection as a ring,
           without feeding extreme off-screen vertices to SDL's sampler. */
        const float radius = 12.75f;
        const float u = (float)column / segments;
        const float target_x = eye_x + u * eye_width;
        float low = 8.0f * SVRT_PI / 180.0f;
        float high = 172.0f * SVRT_PI / 180.0f;
        SDL_FPoint boundary = {target_x, (float)height};
        for (int iteration = 0; iteration < 24; ++iteration) {
            const float angle = (low + high) * 0.5f;
            const float z = radius * sinf(angle);
            boundary = distort_ring_point(
                project_ground(radius * cosf(angle), z, eye_x, eye_width,
                               height, eye), eye_x, eye_width, height);
            if (boundary.x > target_x) low = angle;
            else high = angle;
        }
        boundary.x = target_x;
        sky[column * 2] =
            (SDL_Vertex){{boundary.x, 0.0f}, white, {u, 0.0f}};
        sky[column * 2 + 1] =
            (SDL_Vertex){{boundary.x, boundary.y}, white, {u, 0.5f}};
        floor[column * 2] =
            (SDL_Vertex){{boundary.x, boundary.y}, black, {0.0f, 0.0f}};
        floor[column * 2 + 1] =
            (SDL_Vertex){{boundary.x, (float)height}, black, {0.0f, 0.0f}};
    }
    for (int column = 0; column < segments; ++column) {
        const int vertex = column * 2, index = column * 6;
        indices[index] = vertex; indices[index + 1] = vertex + 2;
        indices[index + 2] = vertex + 1; indices[index + 3] = vertex + 1;
        indices[index + 4] = vertex + 2; indices[index + 5] = vertex + 3;
    }
    SDL_RenderGeometry(ui->renderer, ui->background->texture, sky,
                       (segments + 1) * 2, indices, segments * 6);
    if (draw_floor)
        SDL_RenderGeometry(ui->renderer, NULL, floor,
                           (segments + 1) * 2, indices, segments * 6);
#else
    SDL_Rect destination = {eye_x, 0, eye_width, height};
    SDL_RenderCopy(ui->renderer, ui->background->texture, NULL, &destination);
    if (draw_floor) {
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
        SDL_Rect floor = {eye_x, height / 2, eye_width, height - height / 2};
        SDL_RenderFillRect(ui->renderer, &floor);
    }
#endif
}

static void draw_environment(svrt_ui *ui) {
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
    if (ui->background && !ui->background->texture)
        video_decode_next(ui->background, ui->renderer);
    const int eye_count = SVRT_ENABLE_DEBUG_LEFT_EYE_UI ? 1 : 2;
    for (int eye = 0; eye < eye_count; ++eye) {
        const int eye_x = eye * (width / 2);
        const int eye_width = eye ? width - width / 2 : width / 2;
        SDL_Rect eye_clip = {eye_x, 0, eye_width, height};
        SDL_RenderSetClipRect(ui->renderer, &eye_clip);
        if (ui->background && ui->background->texture)
            draw_circle_curved_environment_eye(ui, eye_x, eye_width, height,
                                                eye, 1);
        draw_floor_grid(ui, eye_x, eye_width, height, eye);
        /* Paint the sky once more to mask the portion of a spoke beyond the
           outer circle. The grid therefore terminates exactly at its curved
           sky boundary. */
        if (ui->background && ui->background->texture)
            draw_circle_curved_environment_eye(ui, eye_x, eye_width, height,
                                                eye, 0);
        SDL_RenderSetClipRect(ui->renderer, NULL);
    }
}

static float rounded_corner_inset(float u) {
    const float radius = 0.035f;
    float edge = u < 0.5f ? u : 1.0f - u;
    if (edge >= radius) return 0.0f;
    const float x = edge - radius;
    return radius - sqrtf(radius * radius - x * x);
}

static void draw_curved_panel_eye(svrt_ui *ui, int eye_x, int eye_width,
                                  int height, int eye) {
    enum { slices = 32 };
#if SDL_VERSION_ATLEAST(2, 0, 18)
    SDL_Vertex vertices[(slices + 1) * 2];
    int indices[slices * 6];
    const float focal = scene_focal_length(height);
    const float eye_offset = eye ? SVRT_EYE_SEPARATION_METERS * 0.5f :
                                   -SVRT_EYE_SEPARATION_METERS * 0.5f;
    const float radius = 3.5f, distance = 2.15f;
    const float arc = 44.0f * SVRT_PI / 180.0f;
    const float panel_height = 1.50f;
    for (int column = 0; column <= slices; ++column) {
        const float u = (float)column / slices;
        const float angle = (u - 0.5f) * arc;
        const float world_x = radius * sinf(angle);
        const float z = distance - radius * (1.0f - cosf(angle));
        const float screen_x = eye_x + eye_width * 0.5f +
                               SVRT_UI_HORIZONTAL_ASPECT * focal *
                                   (world_x - eye_offset) / z;
        const float half_screen_height = focal * panel_height * 0.5f / z;
        const float inset = rounded_corner_inset(u);
        const float screen_inset = inset * half_screen_height * 2.0f;
        SDL_Color white = {255, 255, 255, 255};
        vertices[column * 2] = (SDL_Vertex){
            {screen_x, height * 0.5f - half_screen_height + screen_inset},
            white, {u, inset}};
        vertices[column * 2 + 1] = (SDL_Vertex){
            {screen_x, height * 0.5f + half_screen_height - screen_inset},
            white, {u, 1.0f - inset}};
    }
    for (int column = 0; column < slices; ++column) {
        const int vertex = column * 2, index = column * 6;
        indices[index] = vertex; indices[index + 1] = vertex + 1;
        indices[index + 2] = vertex + 2; indices[index + 3] = vertex + 2;
        indices[index + 4] = vertex + 1; indices[index + 5] = vertex + 3;
    }
    SDL_RenderGeometry(ui->renderer, ui->panel, vertices,
                       (slices + 1) * 2, indices, slices * 6);
#else
    (void)eye;
    const int corrected_width = (int)lroundf(
        eye_width * 8.0f / 10.0f * SVRT_UI_HORIZONTAL_ASPECT);
    SDL_Rect destination = {eye_x + (eye_width - corrected_width) / 2,
                            height / 4, corrected_width, height / 2};
    SDL_RenderCopy(ui->renderer, ui->panel, NULL, &destination);
#endif
}

static void draw_scene(svrt_ui *ui) {
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(ui->renderer, &width, &height);
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
    int rendering_left_eye_scene = 0;
    if (ui->left_eye_scene &&
        !SDL_SetRenderTarget(ui->renderer, ui->left_eye_scene)) {
        rendering_left_eye_scene = 1;
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ui->renderer);
    }
#endif
    draw_environment(ui);
    const int eye_count = SVRT_ENABLE_DEBUG_LEFT_EYE_UI ? 1 : 2;
    for (int eye = 0; eye < eye_count; ++eye)
        draw_curved_panel_eye(ui, eye * (width / 2),
                              eye ? width - width / 2 : width / 2,
                              height, eye);
    if (ui->state == SVRT_UI_HOME)
        for (int eye = 0; eye < eye_count; ++eye)
            draw_dashboard_chrome_eye(ui, eye * (width / 2),
                                      eye ? width - width / 2 : width / 2,
                                      height);
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
    if (rendering_left_eye_scene) {
        SDL_SetRenderTarget(ui->renderer, NULL);
        const SDL_Rect source = {0, 0, width / 2, height};
        const int zoomed_width = (int)lroundf(
            source.w * SVRT_DEBUG_LEFT_EYE_UI_SCALE);
        const int zoomed_height = (int)lroundf(
            source.h * SVRT_DEBUG_LEFT_EYE_UI_SCALE);
        const SDL_Rect destination = {
            (width - zoomed_width) / 2,
            (height - zoomed_height) / 2,
            zoomed_width,
            zoomed_height,
        };
        SDL_RenderCopy(ui->renderer, ui->left_eye_scene, &source,
                       &destination);
    }
#endif
}

int svrt_ui_open(svrt_ui *ui) {
    memset(ui, 0, sizeof(*ui));
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return -1;
    ui->owns_sdl = 1;
    if (TTF_Init()) { SDL_Quit(); memset(ui, 0, sizeof(*ui)); return -1; }
    disable_local_input();
    ui->window = SDL_CreateWindow("SVRT", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_FULLSCREEN_DESKTOP);
    ui->renderer = ui->window ? SDL_CreateRenderer(ui->window, -1,
                                                    SDL_RENDERER_ACCELERATED) : NULL;
    /* The timing test runs with SDL's dummy/offscreen backend.  It has no
       accelerated renderer, while the real Pi display still takes the fast
       path above. */
    if (ui->window && !ui->renderer)
        ui->renderer = SDL_CreateRenderer(ui->window, -1,
                                          SDL_RENDERER_SOFTWARE);
    ui->panel = ui->renderer ? SDL_CreateTexture(
        ui->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        SVRT_PANEL_WIDTH, SVRT_PANEL_HEIGHT) : NULL;
    if (ui->panel) SDL_SetTextureBlendMode(ui->panel, SDL_BLENDMODE_BLEND);
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
    int output_width = 0, output_height = 0;
    if (ui->renderer)
        SDL_GetRendererOutputSize(ui->renderer, &output_width, &output_height);
    ui->left_eye_scene = ui->renderer ? SDL_CreateTexture(
        ui->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        output_width, output_height) : NULL;
#endif
    ui->font = TTF_OpenFont(SVRT_GUI_FONT_PATH, 42);
    ui->code_font = TTF_OpenFont(SVRT_GUI_FONT_PATH, 120);
    ui->small_font = TTF_OpenFont(SVRT_GUI_FONT_PATH, 24);
    /* Only one animation decoder should be active at a time.  The Pi's
       stateless HEVC block has limited capture contexts; opening boot, loop,
       and Steam loading decoders together makes them starve each other. */
    ui->boot = video_open(SVRT_GUI_BOOT_PATH);
    ui->background = video_open(SVRT_GUI_BACKGROUND_PATH);
    if (!ui->renderer || !ui->panel || !ui->font || !ui->code_font ||
        !ui->small_font ||
#if SVRT_ENABLE_DEBUG_LEFT_EYE_UI
        !ui->left_eye_scene ||
#endif
        !ui->boot || !ui->background) {
        svrt_ui_close(ui); return -1;
    }
    ui->state = SVRT_UI_SEARCHING;
    ui->state_started_ms = SDL_GetTicks();
    ui->boot_started_ms = ui->state_started_ms;
    return 0;
}

void svrt_ui_close(svrt_ui *ui) {
    if (!ui) return;
    video_close(&ui->boot); video_close(&ui->loop);
    video_close(&ui->steam_loading); video_close(&ui->background);
    video_close(&ui->avatar);
    if (ui->code_font) TTF_CloseFont(ui->code_font);
    if (ui->small_font) TTF_CloseFont(ui->small_font);
    if (ui->font) TTF_CloseFont(ui->font);
    if (ui->left_eye_scene) SDL_DestroyTexture(ui->left_eye_scene);
    if (ui->panel) SDL_DestroyTexture(ui->panel);
    if (ui->renderer) SDL_DestroyRenderer(ui->renderer);
    if (ui->window) SDL_DestroyWindow(ui->window);
    if (ui->owns_sdl) { TTF_Quit(); SDL_Quit(); }
    memset(ui, 0, sizeof(*ui));
}

void svrt_ui_draw(svrt_ui *ui, svrt_ui_state state, const char code[5],
                  const char *hostname, const char *detail, uint32_t now_ms) {
    (void)hostname;
    if (!ui || !ui->renderer) return;
    poll_ui_actions(ui);
    if (state != ui->state) {
        ui->state = state; ui->state_started_ms = now_ms;
        if (state == SVRT_UI_SEARCHING) {
            ui->loop_first_frame_ms = 0;
            video_close(&ui->steam_loading);
            video_rewind(ui->loop);
        }
        if (state == SVRT_UI_AUTHORIZING || state == SVRT_UI_STARTING ||
            state == SVRT_UI_FAILED) {
            ui->loop_first_frame_ms = 0;
            video_close(&ui->loop);
            video_rewind(ui->steam_loading);
            if (ui->steam_loading) ui->steam_loading->loops_completed = 0;
        }
    }
    uint32_t elapsed = now_ms - ui->state_started_ms;
    const uint32_t boot_elapsed = now_ms - ui->boot_started_ms;
    /* Boot is global to the UI, not tied to the pairing state.  Discovery or
       authorization may advance state while it is still playing, but the
       boot movie must always run to its final frame before anything replaces
       it. */
    if (ui->boot && !ui->boot->ended) {
        /* Boot predates the dashboard scene and intentionally remains the
           original flat, full per-eye animation. */
        SDL_SetRenderTarget(ui->renderer, NULL);
        SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ui->renderer);
        draw_video_per_eye(ui, ui->boot, boot_elapsed, 1440, 1600, 0, 0,
                           SVRT_BOOT_SCALE, SVRT_UI_HORIZONTAL_ASPECT);
        SDL_RenderPresent(ui->renderer);
        return;
    } else {
        if (ui->boot) video_close(&ui->boot);
        draw_panel_content(ui, state, code, detail, elapsed, 255);
    }
    /* Keep the previous status frame on scanout while the loop decoder is
       opened/primed. */
    if (state == SVRT_UI_SEARCHING &&
        (!ui->loop || !ui->loop->texture))
        return;
    SDL_SetRenderDrawColor(ui->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ui->renderer);
    draw_scene(ui);
    SDL_RenderPresent(ui->renderer);
}

SDL_Window *svrt_ui_window(svrt_ui *ui) { return ui ? ui->window : NULL; }
SDL_Renderer *svrt_ui_renderer(svrt_ui *ui) { return ui ? ui->renderer : NULL; }
void svrt_ui_set_client_frame(svrt_ui *ui, SDL_Texture *frame) {
    if (ui) ui->client_frame = frame;
}

void svrt_ui_set_streaming_mode(svrt_ui *ui, int enabled) {
    if (ui) ui->streaming_mode = enabled != 0;
}

int svrt_ui_take_connection_request(svrt_ui *ui) {
    if (!ui) return 0;
    poll_ui_actions(ui);
    const int requested = ui->connection_requested;
    ui->connection_requested = 0;
    return requested;
}

svrt_ui_action svrt_ui_take_action(svrt_ui *ui) {
    if (!ui) return SVRT_UI_ACTION_NONE;
    poll_ui_actions(ui);
    const svrt_ui_action action = ui->pending_action;
    ui->pending_action = SVRT_UI_ACTION_NONE;
    return action;
}
