#include "ui.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int verify_stereo_scene(svrt_ui *ui) {
    int width = 0, height = 0;
    if (SDL_GetRendererOutputSize(ui->renderer, &width, &height) ||
        width < 4 || height < 4)
        return -1;
    SDL_Surface *capture = SDL_CreateRGBSurfaceWithFormat(
        0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!capture || SDL_RenderReadPixels(ui->renderer, NULL,
                                         SDL_PIXELFORMAT_RGBA32,
                                         capture->pixels, capture->pitch)) {
        SDL_FreeSurface(capture);
        return -1;
    }

    unsigned sky_pixels = 0, green_pixels = 0, stereo_pixels = 0;
    const int eye_width = width / 2;
    for (int y = 0; y < height / 2; y += 4) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)capture->pixels +
                                             y * capture->pitch);
        for (int x = 0; x < eye_width; x += 4) {
            Uint8 r, g, b;
            SDL_GetRGB(row[x], capture->format, &r, &g, &b);
            if (r > 8 || g > 8 || b > 8) ++sky_pixels;
            if (g > r + 25 && g > b + 25) ++green_pixels;
        }
    }
    for (int y = height / 2; y < height; y += 2) {
        const Uint32 *row = (const Uint32 *)((const Uint8 *)capture->pixels +
                                             y * capture->pitch);
        for (int x = 0; x < eye_width; x += 2)
            if (row[x] != row[x + eye_width]) ++stereo_pixels;
    }

    const char *screenshot = getenv("SVRT_UI_SCREENSHOT");
    if (screenshot && screenshot[0]) SDL_SaveBMP(capture, screenshot);
    SDL_FreeSurface(capture);
    fprintf(stderr,
            "SVRT_UI_TEST sky_pixels=%u green_pixels=%u stereo_pixels=%u\n",
            sky_pixels, green_pixels, stereo_pixels);
    return sky_pixels > 100 && green_pixels < 100 && stereo_pixels > 100
               ? 0 : -1;
}

int main(void) {
    if (!getenv("SDL_VIDEODRIVER")) setenv("SDL_VIDEODRIVER", "dummy", 1);
    if (!getenv("SDL_AUDIODRIVER")) setenv("SDL_AUDIODRIVER", "dummy", 1);

    svrt_ui ui;
    if (svrt_ui_open(&ui)) {
        fprintf(stderr, "SVRT UI scene test: ui open failed\n");
        return 1;
    }

    const uint32_t deadline = SDL_GetTicks() + 15000;
    while ((!ui.loop_first_frame_ms || ui.boot) && SDL_GetTicks() < deadline) {
        svrt_ui_draw(&ui, SVRT_UI_SEARCHING, NULL, NULL, NULL, SDL_GetTicks());
        SDL_Delay(16);
    }

    const uint32_t loop_start = ui.loop_first_frame_ms;
    const uint32_t loop_delay = loop_start ? loop_start - ui.boot_started_ms : 0;
    fprintf(stderr, "SVRT_UI_TEST shutdown_screen=removed loop_started=%u\n",
            loop_delay);

    int failed = 0;
    failed |= !loop_start;
    failed |= ui.state != SVRT_UI_SEARCHING;
    failed |= ui.steam_loading != NULL;
    failed |= SDL_GetTicks() >= deadline;
    failed |= verify_stereo_scene(&ui) != 0;

    /* Optional visual-preview state for target-display captures.  Normal test
       runs leave this unset and retain their existing timing. */
    const char *preview_state = getenv("SVRT_UI_PREVIEW_STATE");
    if (preview_state && preview_state[0]) {
        svrt_ui_state state = !strcmp(preview_state, "authorizing") ?
                                  SVRT_UI_AUTHORIZING :
                              !strcmp(preview_state, "failed") ?
                                  SVRT_UI_FAILED : SVRT_UI_STARTING;
        const uint32_t preview_deadline = SDL_GetTicks() + 2500;
        while (SDL_GetTicks() < preview_deadline) {
            svrt_ui_draw(&ui, state, "1234", NULL, "Pairing failed",
                         SDL_GetTicks());
            SDL_Delay(16);
        }
    }
    const char *hold_ms = getenv("SVRT_UI_HOLD_MS");
    if (hold_ms) SDL_Delay((Uint32)strtoul(hold_ms, NULL, 10));
    svrt_ui_close(&ui);
    if (failed) {
        fprintf(stderr, "SVRT UI scene test: FAILED\n");
        return 1;
    }
    puts("SVRT UI scene test: passed");
    return 0;
}
