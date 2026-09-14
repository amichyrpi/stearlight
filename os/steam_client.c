#define _POSIX_C_SOURCE 200809L

#include "steam_client.h"

#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/XTest.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define STEARLIGHT_STEAM_DISPLAY ":8"
#define STEARLIGHT_STEAM_WIDTH 1024
#define STEARLIGHT_STEAM_HEIGHT 640
#ifndef SVRT_STEAM_CAPTURE_INTERVAL_MS
#define SVRT_STEAM_CAPTURE_INTERVAL_MS 16
#endif

static const char *steam_home(void) {
    const char *configured = getenv("SVRT_STEAM_HOME");
    return configured && configured[0] ? configured : "/home/stearlight";
}

static const char *steam_binary(void) {
    const char *configured = getenv("SVRT_STEAM_BINARY");
    if (configured && configured[0]) return configured;
    static char path[512];
    snprintf(path, sizeof(path), "%s/.local/share/Steam/steamrtarm64/steam",
             steam_home());
    return path;
}

static const char *steam_launcher(void) {
    const char *configured = getenv("SVRT_STEAM_LAUNCHER");
    if (configured && configured[0]) return configured;
    /* Native Pi images use the Armada-compatible ARM64 launcher.  The VM
       overrides this with steam32-launch because its guest is x86_64. */
    return "/usr/local/libexec/stearlight/launch-steam";
}

/* The distro package contains Valve's small bootstrap archive, while the
 * Gamepad UI itself is installed into the per-user Steam tree on first run.
 * Keep that extraction outside of the renderer so an interrupted update can
 * be repaired before Steam is launched again. */
static const char *steam_prepare(void) {
    const char *configured = getenv("SVRT_STEAM_PREPARE");
    if (configured && configured[0]) return configured;
    return "/usr/local/libexec/stearlight/steam-firstboot";
}

static int steam_has_wayland_compositor(void) {
    const char *compositor = getenv("SVRT_STEAM_COMPOSITOR");
    return compositor && strcmp(compositor, "weston") == 0;
}

static int steam_uses_classic_ui(void) {
    const char *mode = getenv("SVRT_STEAM_UI_MODE");
    return mode && (strcmp(mode, "tenfoot") == 0 ||
                    strcmp(mode, "classic") == 0);
}

static int steam_frame_enabled(void) {
    const char *mode = getenv("STEARLIGHT_STEAM_FRAME");
    /* The native Stearlight session is a Steam Frame client by default. Keep
       the old X11 ten-foot mode usable as an explicit diagnostic fallback. */
    if (!mode || !mode[0]) return !steam_uses_classic_ui();
    return strcmp(mode, "0") != 0;
}

static void report_exec_failure(const char *path);
static void stop_process_group(pid_t pid);
static void dispatch_steam_uri(const char *uri);
static void flush_pending_uri(stearlight_steam_client *client);

static int valid_valve_uri(const char *uri) {
    if (!uri || !uri[0]) return 0;
    const size_t length = strnlen(uri, STEARLIGHT_STEAM_URI_MAX);
    if (!length || length >= STEARLIGHT_STEAM_URI_MAX) return 0;
    if (strncmp(uri, "steam://", 8) && strncmp(uri, "steamlink://", 12))
        return 0;
    for (size_t index = 0; index < length; ++index) {
        const unsigned char byte = (unsigned char)uri[index];
        if (byte < 0x21 || byte > 0x7e) return 0;
    }
    return 1;
}

static const char *steam_link_uri(void) {
    const char *configured = getenv("STEARLIGHT_STEAM_LINK_URI");
    return valid_valve_uri(configured) ? configured : STEARLIGHT_STEAM_LINK_URI;
}

static void exec_steam_with_uri(const char *uri) {
    if (!uri || !uri[0]) _exit(127);
    const char *launcher = steam_launcher();
    const char *binary = steam_binary();
    /* Pass the URI as the first argument, exactly like Valve's documented
       `steam steam://...` entry point.  Each architecture-specific launcher
       adds the normal Gamepad UI/Steam Frame flags only when it has to start
       a new client.  If Steam is already running, its own URI handler forwards
       the request to that client instead of starting a second UI mode. */
    if (access(launcher, X_OK) == 0)
        execl(launcher, launcher, uri, NULL);
    if (access(binary, X_OK) == 0) {
        /* The architecture-specific launcher normally owns this argument
           translation.  Keep the direct-binary fallback equivalent so a
           repaired or minimal Steam install cannot silently lose Gamepad UI
           or Steam Frame mode on the next restart. */
        if (steam_uses_classic_ui())
            execl(binary, binary, "-tenfoot", "-steam", uri, NULL);
        if (steam_frame_enabled())
            execl(binary, binary, "-gamepadui", "-steamos3", "-steampal",
                  "-steamdeck", "-steamframe", uri, NULL);
        execl(binary, binary, "-gamepadui", "-steamos3", "-steampal",
              "-steamdeck", uri, NULL);
    }
    _exit(127);
}

static void child_environment(void) {
    const char *user = getenv("SVRT_STEAM_USER");
    if (!user || !user[0]) user = "stearlight";
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    char default_config[512];
    if (!xdg_config || !xdg_config[0]) {
        snprintf(default_config, sizeof(default_config), "%s/.config",
                 steam_home());
        xdg_config = default_config;
    }
    char bootstrap_config[512];
    snprintf(bootstrap_config, sizeof(bootstrap_config),
             "%s/gamescope/bootstrap.cfg", xdg_config);
    setenv("HOME", steam_home(), 1);
    setenv("USER", user, 1);
    setenv("LOGNAME", user, 1);
    /* Steam can rebuild its WebHelper environment after the initial
       launcher. Reassert the compatibility-safe Fontconfig file here so
       every child, including first-boot and the native client, keeps the
       complete Noto fallback set instead of parsing Alpine's conf.d files. */
    const char *fontconfig_file = getenv("SVRT_FONTCONFIG_FILE");
    if (fontconfig_file && fontconfig_file[0] &&
        access(fontconfig_file, R_OK) == 0) {
        setenv("FONTCONFIG_FILE", fontconfig_file, 1);
        const char *fontconfig_path = getenv("SVRT_FONTCONFIG_PATH");
        setenv("FONTCONFIG_PATH",
               fontconfig_path && fontconfig_path[0] ? fontconfig_path
                                                     : "/etc/fonts",
               1);
    }
    const char *xdg_cache = getenv("XDG_CACHE_HOME");
    char default_cache[512];
    if (!xdg_cache || !xdg_cache[0]) {
        snprintf(default_cache, sizeof(default_cache), "%s/.cache",
                 steam_home());
        setenv("XDG_CACHE_HOME", default_cache, 1);
    }
    setenv("DISPLAY", STEARLIGHT_STEAM_DISPLAY, 1);
    setenv("XDG_SESSION_TYPE", "x11", 1);
    setenv("XDG_CURRENT_DESKTOP", "gamescope", 1);
    /* Steam's SteamOS/Gamepad UI path skips the desktop-only host ABI check
       and uses the bundled Steam Runtime. */
    setenv("STEAMOS", "1", 1);
    setenv("STEAM_RUNTIME", "1", 1);
    /* Steam's setup wizard is selected by the image marker used by
       SteamOS/gamescope-session.  Keep the marker and the explicit hints in
       the environment so the first boot is owned by Valve's Gamepad UI. */
    if (access("/etc/steamos-oobe-image", F_OK) == 0) {
        setenv("STEAMOS_OOBE", "1", 1);
        setenv("STEAMOS_OOBE_IMAGE", "1", 1);
    }
    setenv("STEAM_GAMESCOPE", steam_uses_classic_ui() ? "0" : "1", 1);
    /* The client is rendered on the private X display below and then copied
       into the native stereo shell.  Advertising a Wayland gamescope
       compositor here makes recent Gamepad UI builds create a Wayland
       surface instead of an X11 window; with no compositor to own that
       surface the client stays alive but nothing can be captured.  The
       normal gamescope session can opt back in explicitly by setting
       SVRT_STEAM_FORCE_WAYLAND=1. */
    const char *force_wayland = getenv("SVRT_STEAM_FORCE_WAYLAND");
    const char *gamescope = getenv("SVRT_USE_GAMESCOPE");
    const int gamescope_enabled = gamescope && gamescope[0] &&
                                  strcmp(gamescope, "0") != 0 &&
                                  !steam_uses_classic_ui();
    if ((force_wayland && force_wayland[0] && strcmp(force_wayland, "0") != 0) ||
        gamescope_enabled || steam_has_wayland_compositor())
        setenv("STEAM_GAMESCOPE_WAYLAND", "1", 1);
    else
        setenv("STEAM_GAMESCOPE_WAYLAND", "0", 1);
    unsetenv("WAYLAND_DISPLAY");
    unsetenv("WAYLAND_SOCKET");
    setenv("STEAM_GAMEPADUI", steam_uses_classic_ui() ? "0" : "1", 1);
    setenv("STEAM_GAMESCOPE_VRR_SUPPORTED", "1", 1);
    setenv("STEAM_GAMESCOPE_HAS_TEARING_SUPPORT", "1", 1);
    setenv("STEAM_GAMESCOPE_TEARING_SUPPORTED", "1", 1);
    setenv("STEAM_GAMESCOPE_HDR_SUPPORTED", "1", 1);
    setenv("STEAM_GAMESCOPE_DYNAMIC_FPSLIMITER", "1", 1);
    setenv("STEAM_GAMESCOPE_NIS_SUPPORTED", "1", 1);
    setenv("STEAM_GAMESCOPE_FANCY_SCALING_SUPPORT", "1", 1);
    setenv("STEAM_GAMESCOPE_COLOR_MANAGED", "1", 1);
    setenv("STEAM_GAMESCOPE_VIRTUAL_WHITE", "1", 1);
    setenv("STEAM_MANGOAPP_PRESETS_SUPPORTED", "1", 1);
    setenv("STEAM_MANGOAPP_HORIZONTAL_SUPPORTED", "1", 1);
    setenv("STEAM_USE_MANGOAPP", "1", 1);
    setenv("STEAM_DISABLE_MANGOAPP_ATOM_WORKAROUND", "1", 1);
    setenv("STEAM_USE_DYNAMIC_VRS", "1", 1);
    setenv("STEAM_MULTIPLE_XWAYLANDS", "1", 1);
    setenv("STEAM_ENABLE_VOLUME_HANDLER", "1", 1);
    setenv("STEAM_ALLOW_DRIVE_UNMOUNT", "1", 1);
    setenv("SRT_URLOPEN_PREFER_STEAM", "1", 1);
    setenv("STEAM_DISABLE_AUDIO_DEVICE_SWITCHING", "1", 1);
    setenv("STEAM_BOOTSTRAP_CONFIG", bootstrap_config, 1);
    char cursor_file[768];
    snprintf(cursor_file, sizeof(cursor_file),
             "%s/.local/share/Steam/tenfoot/resource/images/cursors/arrow.png",
             steam_home());
    setenv("CURSOR_FILE", cursor_file, 1);
    const char *client_cmd = steam_uses_classic_ui() ?
        "steam -tenfoot -steam" :
        (steam_frame_enabled() ?
         "steam -gamepadui -steamos3 -steampal -steamdeck -steamframe" :
         "steam -gamepadui -steamos3 -steampal -steamdeck");
    setenv("CLIENTCMD", client_cmd, 1);
    setenv("STEAMOS_STEAM_REBOOT_SENTINEL", "/tmp/steamos-reboot-sentinel", 1);
    setenv("REBOOT_SENTINEL", "/tmp/steamos-reboot-sentinel", 1);
    setenv("STEAMOS_STEAM_SHUTDOWN_SENTINEL", "/tmp/steamos-shutdown-sentinel", 1);
    setenv("SHUTDOWN_SENTINEL", "/tmp/steamos-shutdown-sentinel", 1);
    setenv("QT_IM_MODULE", "steam", 1);
    setenv("GTK_IM_MODULE", "Steam", 1);
    setenv("SDL_VIDEODRIVER", "x11", 1);
    /* The VM's direct-X11 Steam process starts the 32-bit client before it
       launches the 64-bit WebHelper.  Mesa does not inherit the ABI-specific
       driver path from run.sh's command-substitution, so allow the VM
       supervisor to provide the 32-bit path explicitly.  steam-firstboot
       patches the WebHelper boundary to clear this variable before the
       64-bit helper starts; production DRM sessions leave it unset and use
       their native driver discovery. */
    const char *dri_path = getenv("SVRT_STEAM_DRI_PATH");
    if (dri_path && dri_path[0])
        setenv("LIBGL_DRIVERS_PATH", dri_path, 1);
    else
        unsetenv("LIBGL_DRIVERS_PATH");
    /* Software GL is a VM diagnostic fallback only.  For the Pi, forcing it
       disables Mesa/V3D and makes the Steam welcome surface both slow and
       fragile.  The VM session opts in with SVRT_STEAM_SOFTWARE_GL=1. */
    const char *software_gl = getenv("SVRT_STEAM_SOFTWARE_GL");
    if (software_gl && software_gl[0] && strcmp(software_gl, "0") != 0)
        setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
    else
        unsetenv("LIBGL_ALWAYS_SOFTWARE");
}

static void prepare_steam_home(void) {
    const char *prepare = steam_prepare();
    if (!prepare || access(prepare, X_OK) != 0) return;

    /* gamescope-session-steam uses this marker to make Valve's bootstrap
       extraction resumable.  Keep it in the user tree so the first-run
       language/network/account flow remains owned by Steam, while an
       interrupted power cycle can safely retry the archive step. */
    const char *xdg_config = getenv("XDG_CONFIG_HOME");
    char default_config[512];
    if (!xdg_config || !xdg_config[0]) {
        snprintf(default_config, sizeof(default_config), "%s/.config",
                 steam_home());
        xdg_config = default_config;
    }
    char config_dir[512];
    char bootstrap_config[512];
    snprintf(config_dir, sizeof(config_dir), "%s/gamescope", xdg_config);
    snprintf(bootstrap_config, sizeof(bootstrap_config), "%s/bootstrap.cfg",
             config_dir);
    if (mkdir(xdg_config, 0755) && errno != EEXIST)
        fprintf(stderr, "STEARLIGHT STEAM: cannot create %s: %s\n",
                xdg_config, strerror(errno));
    if (mkdir(config_dir, 0755) && errno != EEXIST)
        fprintf(stderr, "STEARLIGHT STEAM: cannot create %s: %s\n",
                config_dir, strerror(errno));
    int config_fd = open(bootstrap_config, O_CREAT | O_WRONLY, 0644);
    if (config_fd >= 0) close(config_fd);

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "STEARLIGHT STEAM: cannot run first-boot setup: %s\n",
                strerror(errno));
        return;
    }
    if (pid == 0) {
        child_environment();
        execl(prepare, prepare, "--prepare", (char *)NULL);
        report_exec_failure(prepare);
        _exit(127);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        fprintf(stderr, "STEARLIGHT STEAM: first-boot setup did not complete\n");
}

static void report_exec_failure(const char *path) {
    fprintf(stderr, "STEARLIGHT STEAM: cannot execute %s: %s\n", path,
            strerror(errno));
}

static void start_runtime_watch(void) {
    const char *watcher = getenv("SVRT_STEAM_RUNTIME_WATCH");
    if (!watcher || !watcher[0] || access(watcher, X_OK) != 0) return;
    pid_t pid = fork();
    if (pid == 0) {
        execl(watcher, watcher, (char *)NULL);
        _exit(127);
    }
}

static pid_t start_display(void) {
    pid_t pid = fork();
    if (pid) return pid;
    setpgid(0, 0);
    /* Xvfb is the X server Steam connects to; it must not inherit the
       client-side Mesa search path.  In particular, LIBGL_DRIVERS_PATH is
       ABI-specific and points at Steam's 32-bit DRI directory.  Xvfb's GLX
       module is loaded by the server itself, and that variable can make the
       module loader silently fail, leaving a display with no GLX extension.
       Keep the server environment independent from Steam's renderer. */
    unsetenv("DISPLAY");
    unsetenv("LIBGL_DRIVERS_PATH");
    unsetenv("LIBGL_ALWAYS_SOFTWARE");
    unsetenv("MESA_LOADER_DRIVER_OVERRIDE");
    unsetenv("MESA_GL_VERSION_OVERRIDE");
    unsetenv("MESA_GLSL_VERSION_OVERRIDE");
    unsetenv("LD_LIBRARY_PATH");
    unsetenv("LD_PRELOAD");
    /* CEF's software compositor only needs the core X11, Composite and
       Damage extensions.  Xvfb's GLX implementation is unstable when the
       Steam WebHelper mixes 32-bit bootstrap clients with 64-bit CEF; a
       malformed GLX request can take the whole private server down.  Keep a
       switch for real hardware experiments; setting SVRT_STEAM_XVFB_GLX=0
       selects the diagnostic no-GLX mode (Steam itself then cannot create its
       legacy VGUI window). */
    const char *glx = getenv("SVRT_STEAM_XVFB_GLX");
    if (!glx || !glx[0] || strcmp(glx, "0") != 0) {
        execlp("Xvfb", "Xvfb", STEARLIGHT_STEAM_DISPLAY, "-screen", "0",
               "1024x640x24", "+extension", "GLX", "+extension",
               "Composite", "+extension", "XTEST", "-ac",
               "-nolisten", "tcp", "-noreset", NULL);
    } else {
        execlp("Xvfb", "Xvfb", STEARLIGHT_STEAM_DISPLAY, "-screen", "0",
               "1024x640x24", "-extension", "GLX", "+extension", "XTEST",
               "-ac", "-nolisten", "tcp", "-noreset", NULL);
    }
    report_exec_failure("Xvfb");
    _exit(127);
}

static pid_t start_steam(void) {
    pid_t pid = fork();
    if (pid) return pid;
    setpgid(0, 0);
    child_environment();
    start_runtime_watch();
    const char *gamescope = getenv("SVRT_USE_GAMESCOPE");
    const char *launcher = steam_launcher();
    const char *binary = steam_binary();
    if (steam_uses_classic_ui()) {
        /* The legacy Big Picture surface is a normal X11 window.  It is a
           useful VM fallback because Gamepad UI is an off-screen compositor
           client and needs a real gamescope/Wayland host to present pixels.
           Production Pi sessions leave this unset and use the native
           Gamepad UI path below. */
        if (access(launcher, X_OK) == 0) {
            execl(launcher, launcher, "-tenfoot", "-steam", NULL);
            report_exec_failure(launcher);
        }
        execl(binary, binary, "-tenfoot", "-steam", NULL);
        report_exec_failure(binary);
        _exit(127);
    }
    if (gamescope && gamescope[0] && strcmp(gamescope, "0") &&
        access(launcher, X_OK) == 0) {
        /* gamescope is an Alpine/musl host process.  Do not let it inherit
           the glibc ICD selected for Valve's 32/64-bit Steam children: the
           loader can open that library but then loses VK_KHR_surface during
           symbol/WSI initialisation.  The VM supplies a native Alpine LVP
           manifest; production DRM sessions leave this opt-in unset and use
           the board's native Vulkan discovery. */
        const char *gamescope_icd = getenv("SVRT_GAMESCOPE_VK_ICD");
        if (gamescope_icd && gamescope_icd[0])
            setenv("VK_ICD_FILENAMES", gamescope_icd, 1);
        if (steam_frame_enabled())
            execlp("gamescope", "gamescope", "-e", "--backend", "sdl", "-b",
                   "-W", "1024", "-H", "640", "-w", "1024", "-h", "640",
                   "-r", "60", "--expose-wayland", "--", launcher,
                   "-gamepadui", "-steamos3", "-steampal", "-steamdeck",
                   "-steamframe", NULL);
        else
            execlp("gamescope", "gamescope", "-e", "--backend", "sdl", "-b",
                   "-W", "1024", "-H", "640", "-w", "1024", "-h", "640",
                   "-r", "60", "--expose-wayland", "--", launcher,
                   "-gamepadui", "-steamos3", "-steampal", "-steamdeck", NULL);
        report_exec_failure("gamescope");
    }
    if (access(launcher, X_OK) == 0) {
        if (steam_frame_enabled())
            execl(launcher, launcher, "-gamepadui", "-steamos3", "-steampal",
                  "-steamdeck", "-steamframe", NULL);
        else
            execl(launcher, launcher, "-gamepadui", "-steamos3", "-steampal",
                  "-steamdeck", NULL);
        report_exec_failure(launcher);
    }
    if (steam_frame_enabled())
        execl(binary, binary, "-gamepadui", "-steamos3", "-steampal",
              "-steamdeck", "-steamframe", NULL);
    else
        execl(binary, binary, "-gamepadui", "-steamos3", "-steampal",
              "-steamdeck", NULL);
    report_exec_failure(binary);
    _exit(127);
}

int stearlight_steam_client_start(stearlight_steam_client *client,
                                   SDL_Renderer *renderer) {
    (void)renderer;
    if (!client) return -1;
    memset(client, 0, sizeof(*client));
    prepare_steam_home();
    if (access(steam_launcher(), X_OK) && access(steam_binary(), X_OK)) {
        client->state = STEARLIGHT_STEAM_CLIENT_MISSING;
        snprintf(client->detail, sizeof(client->detail),
                 "Install the Steam client");
        fprintf(stderr, "STEARLIGHT STEAM: launcher is not installed (%s)\n",
                steam_launcher());
        return 0;
    }
    client->display_pid = start_display();
    if (client->display_pid <= 0) {
        client->state = STEARLIGHT_STEAM_CLIENT_FAILED;
        snprintf(client->detail, sizeof(client->detail),
                 "Cannot start Steam display");
        return -1;
    }
    client->state = STEARLIGHT_STEAM_CLIENT_STARTING;
    client->next_connect_ms = SDL_GetTicks() + 100;
    snprintf(client->detail, sizeof(client->detail),
             "Starting Steam");
    fprintf(stderr, "STEARLIGHT STEAM: starting Valve native Gamepad UI client\n");
    return 0;
}

static void connect_display(stearlight_steam_client *client, uint32_t now_ms) {
    if (!client) return;
    if (!client->display) {
        if (now_ms < client->next_connect_ms) return;
        client->display = XOpenDisplay(STEARLIGHT_STEAM_DISPLAY);
        if (!client->display) {
            client->next_connect_ms = now_ms + 100;
            return;
        }
        client->root = DefaultRootWindow((Display *)client->display);
        int glx_event = 0;
        int glx_error = 0;
        int glx_opcode = 0;
        const Bool glx_available = XQueryExtension(
            (Display *)client->display, "GLX", &glx_opcode, &glx_event,
            &glx_error);
        fprintf(stderr, "STEARLIGHT STEAM: private display GLX %s (opcode %d)\n",
                glx_available ? "available" : "missing", glx_opcode);
    }
    if (client->steam_pid || now_ms < client->next_connect_ms) return;
    const pid_t steam_pid = start_steam();
    if (steam_pid <= 0) {
        /* fork(2) returns -1 on a transient process/resource failure. Do not
           leave that sentinel in the struct: a non-zero negative pid would
           make the next update believe Steam is still running and disable
           the retry path. */
        client->steam_pid = 0;
        if (client->launch_failures < 10U) ++client->launch_failures;
        const uint32_t retry_ms = client->launch_failures > 5U ? 10000U :
                                   1000U * client->launch_failures;
        client->state = STEARLIGHT_STEAM_CLIENT_STARTING;
        snprintf(client->detail, sizeof(client->detail),
                 "Steam launch failed; retrying");
        client->next_connect_ms = now_ms + retry_ms;
        fprintf(stderr,
                "STEARLIGHT STEAM: cannot launch client; retry in %u ms\n",
                retry_ms);
    } else {
        client->steam_pid = steam_pid;
        client->state = STEARLIGHT_STEAM_CLIENT_STARTING;
        client->next_connect_ms = now_ms + 1000;
    }
}

static int child_exited(pid_t pid) {
    if (pid <= 0) return 0;
    int status = 0;
    return waitpid(pid, &status, WNOHANG) == pid;
}

static void clear_captured_frame(stearlight_steam_client *client) {
    if (!client) return;
    client->content_window = 0;
    client->frame_announced = 0;
    client->x_modifiers = 0;
    if (client->frame) SDL_DestroyTexture(client->frame);
    client->frame = NULL;
    client->frame_width = 0;
    client->frame_height = 0;
}

static uint32_t schedule_restart(stearlight_steam_client *client,
                                 uint32_t now_ms) {
    if (!client) return 0;
    if (client->launch_failures < 10U) ++client->launch_failures;
    const uint32_t delay = client->launch_failures > 5U ? 10000U :
                           1000U * client->launch_failures;
    client->next_connect_ms = now_ms + delay;
    client->state = STEARLIGHT_STEAM_CLIENT_STARTING;
    return delay;
}

static int ensure_display(stearlight_steam_client *client, uint32_t now_ms) {
    if (!client || client->display_pid > 0 || now_ms < client->next_connect_ms)
        return 1;
    client->display_pid = start_display();
    if (client->display_pid > 0) {
        client->next_connect_ms = now_ms + 100;
        return 1;
    }
    client->display_pid = 0;
    const uint32_t retry_ms = schedule_restart(client, now_ms);
    snprintf(client->detail, sizeof(client->detail),
             "Steam display launch failed; retrying");
    fprintf(stderr,
            "STEARLIGHT STEAM: cannot relaunch display; retry in %u ms\n",
            retry_ms);
    return 0;
}

static int image_has_visible_content(const XImage *image) {
    if (!image || image->width <= 0 || image->height <= 0) return 0;
    const int step_x = image->width / 32 > 0 ? image->width / 32 : 1;
    const int step_y = image->height / 24 > 0 ? image->height / 24 : 1;
    int visible = 0;
    for (int y = 0; y < image->height; y += step_y) {
        for (int x = 0; x < image->width; x += step_x) {
            unsigned long pixel = XGetPixel((XImage *)image, x, y);
            if (pixel > 0x101010UL) {
                if (++visible >= 8) return 1;
            }
        }
    }
    return 0;
}

static uint32_t ximage_channel(unsigned long pixel, unsigned long mask) {
    if (!mask) return 0;
    unsigned int shift = 0;
    while (shift < sizeof(mask) * 8U && !(mask & (1UL << shift))) ++shift;
    const unsigned long range = mask >> shift;
    if (!range) return 0;
    const unsigned long value = (pixel & mask) >> shift;
    return (uint32_t)(value * 255UL / range);
}

/* XGetImage on the root window does not composite mapped child windows.  The
 * Steam Gamepad UI is an ordinary X11 top-level window on the private display,
 * so reading the root (the old implementation) always returned the black
 * background and left the shell on its loading card forever.  Walk the X11
 * window tree and select the largest viewable surface; Chromium's content
 * window is normally the largest child and therefore contains the actual
 * Valve welcome pages.  This deliberately avoids XComposite so the same
 * capture path works with the tiny X11 stack shipped on the Pi.
 */
typedef struct steam_window_candidate {
    unsigned long window;
    unsigned long parent;
    int width;
    int height;
    int area;
    int depth;
} steam_window_candidate;

/* A WebHelper window can be unmapped or destroyed between XQueryTree and
 * XGetImage while Steam transitions between its updater, login and Gamepad
 * UI pages.  Xlib's default error handler terminates the whole shell for that
 * harmless race (BadMatch), which in turn makes OpenRC restart the session
 * and leaves a stale X display behind.  Scope a non-fatal handler around the
 * image request instead and retry on the next frame. */
static int capture_x_error_code;

static int capture_x_error_handler(Display *display, XErrorEvent *event) {
    (void)display;
    capture_x_error_code = event ? event->error_code : 1;
    return 0;
}

/* XQueryTree returns handles that may be destroyed by Chromium before the
 * following XGetWindowAttributes call.  Xlib reports that race
 * asynchronously; using its default handler would terminate the stereo shell
 * and OpenRC would restart it, leaving a black framebuffer.  Keep each small
 * query behind a scoped handler so a stale child is simply skipped. */
static int query_tree_safe(Display *display, Window parent, Window *root,
                           Window *returned_parent, Window **children,
                           unsigned int *child_count) {
    if (!display || !root || !returned_parent || !children || !child_count)
        return 0;
    XSync(display, False);
    capture_x_error_code = 0;
    int (*previous_handler)(Display *, XErrorEvent *) =
        XSetErrorHandler(capture_x_error_handler);
    const Bool queried = XQueryTree(display, parent, root, returned_parent,
                                    children, child_count);
    XSync(display, False);
    XSetErrorHandler(previous_handler);
    if (capture_x_error_code) {
        if (*children) XFree(*children);
        *children = NULL;
        *child_count = 0;
        return 0;
    }
    return queried ? 1 : 0;
}

static int get_window_attributes_safe(Display *display, Window window,
                                      XWindowAttributes *attributes) {
    if (!display || !window || !attributes) return 0;
    XSync(display, False);
    capture_x_error_code = 0;
    int (*previous_handler)(Display *, XErrorEvent *) =
        XSetErrorHandler(capture_x_error_handler);
    const int got_attributes =
        XGetWindowAttributes(display, window, attributes);
    XSync(display, False);
    XSetErrorHandler(previous_handler);
    return got_attributes && !capture_x_error_code;
}

static XImage *get_window_image_safe(Display *display, Window window,
                                     int width, int height, int depth) {
    if (!display || !window || width <= 0 || height <= 0) return NULL;
    /* XGetImage's plane mask is validated against the drawable depth by a
       few Xvfb/GLX combinations.  AllPlanes is normally equivalent, but the
       server rejects the 64-bit Xlib value for a depth-24 drawable with
       BadMatch.  Restrict the mask to the actual visual width. */
    unsigned long plane_mask = AllPlanes;
    if (depth > 0 && depth < (int)(sizeof(unsigned long) * 8U))
        plane_mask = (1UL << (unsigned int)depth) - 1UL;
    XSync(display, False);
    capture_x_error_code = 0;
    int (*previous_handler)(Display *, XErrorEvent *) =
        XSetErrorHandler(capture_x_error_handler);
    XImage *image = XGetImage(display, window, 0, 0, (unsigned int)width,
                              (unsigned int)height, plane_mask, ZPixmap);
    XSync(display, False);
    XSetErrorHandler(previous_handler);
    if (capture_x_error_code) {
        if (image) XDestroyImage(image);
        return NULL;
    }
    return image;
}

/* GLX/CEF top-level windows can be valid, viewable InputOutput drawables but
   still reject XGetImage because their pixels live in a compositor-owned
   buffer.  Name the server-side Composite pixmap and read that pixmap
   instead.  Xvfb exposes Composite even when it cannot present GLX directly,
   so this also keeps the VM path deterministic. */
static XImage *get_window_composite_image_safe(Display *display, Window window,
                                               int width, int height,
                                               int depth) {
    if (!display || !window || width <= 0 || height <= 0) return NULL;
    int event_base = 0;
    int error_base = 0;
    if (!XCompositeQueryExtension(display, &event_base, &error_base))
        return NULL;
    XSync(display, False);
    capture_x_error_code = 0;
    int (*previous_handler)(Display *, XErrorEvent *) =
        XSetErrorHandler(capture_x_error_handler);
    Pixmap pixmap = XCompositeNameWindowPixmap(display, window);
    XSync(display, False);
    if (capture_x_error_code || !pixmap) {
        XSetErrorHandler(previous_handler);
        if (pixmap) XFreePixmap(display, pixmap);
        return NULL;
    }
    unsigned long plane_mask = AllPlanes;
    if (depth > 0 && depth < (int)(sizeof(unsigned long) * 8U))
        plane_mask = (1UL << (unsigned int)depth) - 1UL;
    capture_x_error_code = 0;
    XImage *image = XGetImage(display, pixmap, 0, 0, (unsigned int)width,
                              (unsigned int)height, plane_mask, ZPixmap);
    XSync(display, False);
    XFreePixmap(display, pixmap);
    XSetErrorHandler(previous_handler);
    if (capture_x_error_code) {
        if (image) XDestroyImage(image);
        return NULL;
    }
    return image;
}

static void find_content_window(Display *display, Window parent,
                                steam_window_candidate *best) {
    if (!display || !best) return;

    Window root = 0;
    Window returned_parent = 0;
    Window *children = NULL;
    unsigned int child_count = 0;
    if (!query_tree_safe(display, parent, &root, &returned_parent, &children,
                         &child_count))
        return;

    static Window diagnostic_windows[128];
    static unsigned int diagnostic_count;
    const char *trace_x11 = getenv("SVRT_TRACE_STEAM_X11");

    for (unsigned int index = 0; index < child_count; ++index) {
        XWindowAttributes attributes;
        const int got_attributes = get_window_attributes_safe(
            display, children[index], &attributes);
        if (got_attributes && trace_x11 && trace_x11[0] != '0' &&
            diagnostic_count < 128U) {
            int already_reported = 0;
            for (unsigned int seen = 0; seen < diagnostic_count; ++seen) {
                if (diagnostic_windows[seen] == children[index]) {
                    already_reported = 1;
                    break;
                }
            }
            if (!already_reported) {
                fprintf(stderr,
                        "STEARLIGHT STEAM: X11 node=0x%lx map=%d class=%d size=%dx%d depth=%d x=%d y=%d\n",
                        (unsigned long)children[index], attributes.map_state,
                        attributes.class, attributes.width, attributes.height,
                        attributes.depth, attributes.x, attributes.y);
                diagnostic_windows[diagnostic_count++] = children[index];
            }
        }
        if (got_attributes && attributes.map_state == IsViewable &&
            attributes.class != InputOnly && attributes.depth >= 16 &&
            attributes.width >= 160 && attributes.height >= 100) {
            const int area = attributes.width * attributes.height;
            if (area > best->area ||
                (area == best->area && attributes.depth > best->depth)) {
                best->window = children[index];
                best->parent = parent;
                best->width = attributes.width;
                best->height = attributes.height;
                best->area = area;
                best->depth = attributes.depth;
            }
        }
        find_content_window(display, children[index], best);
    }
    if (children) XFree(children);
}

static XImage *capture_steam_window(stearlight_steam_client *client) {
    if (!client || !client->display || !client->root) return NULL;
    Display *display = (Display *)client->display;
    static int initial_tree_reported;
    if (!initial_tree_reported) {
        Window tree_root = 0;
        Window tree_parent = 0;
        Window *tree_children = NULL;
        unsigned int tree_count = 0;
        const Bool queried = query_tree_safe(
            display, (Window)client->root, &tree_root, &tree_parent,
            &tree_children, &tree_count);
        fprintf(stderr,
                "STEARLIGHT STEAM: initial X11 tree query=%s children=%u\n",
                queried ? "ok" : "failed", queried ? tree_count : 0);
        if (queried) {
            for (unsigned int index = 0; index < tree_count; ++index) {
                XWindowAttributes attributes;
                if (!get_window_attributes_safe(display, tree_children[index],
                                                &attributes))
                    continue;
                fprintf(stderr,
                        "STEARLIGHT STEAM: X11 child=0x%lx map=%d size=%dx%d depth=%d\n",
                        (unsigned long)tree_children[index],
                        attributes.map_state, attributes.width,
                        attributes.height, attributes.depth);
            }
        }
        if (tree_children) XFree(tree_children);
        initial_tree_reported = 1;
    }
    static unsigned int tree_reports;
    if ((++tree_reports % 120U) == 0U) {
        Window root = 0;
        Window parent = 0;
        Window *children = NULL;
        unsigned int count = 0;
        const Bool queried = query_tree_safe(display, (Window)client->root,
                                             &root, &parent, &children, &count);
        fprintf(stderr,
                "STEARLIGHT STEAM: X11 root=0x%lx query=%s children=%u\n",
                client->root, queried ? "ok" : "failed", queried ? count : 0);
        if (children) XFree(children);
    }
    steam_window_candidate best = {0};
    find_content_window(display, (Window)client->root, &best);
    if (!best.window) {
        /* Keep a low-rate diagnostic in the VM/serial log.  A valid X11
         * connection can still have an empty tree while Chromium is starting;
         * distinguishing that state from a failed capture is important when
         * debugging first-run Steam on a headless Xvfb display. */
        static unsigned int empty_tree_scans;
        if ((++empty_tree_scans % 120U) == 0U) {
            Window root = 0;
            Window parent = 0;
            Window *children = NULL;
            unsigned int count = 0;
            if (query_tree_safe(display, (Window)client->root, &root, &parent,
                                &children, &count)) {
                fprintf(stderr,
                        "STEARLIGHT STEAM: X11 content tree empty (root=0x%lx children=%u)\n",
                        (unsigned long)client->root, count);
            } else {
                fprintf(stderr,
                        "STEARLIGHT STEAM: X11 content tree query failed\n");
            }
            if (children) XFree(children);
        }
        if (client->content_window) {
            client->content_window = 0;
            client->frame_width = 0;
            client->frame_height = 0;
        }
        return NULL;
    }
    if (client->content_window != best.window) {
        fprintf(stderr,
                "STEARLIGHT STEAM: capturing window 0x%lx (%dx%d)\n",
                best.window, best.width, best.height);
        client->content_window = best.window;
    }
    XImage *image = get_window_image_safe(display, (Window)best.window,
                                          best.width, best.height, best.depth);
    const int initial_image_error = capture_x_error_code;
    if (!image) {
        XImage *composite_image = get_window_composite_image_safe(
            display, (Window)best.window, best.width, best.height, best.depth);
        if (composite_image) {
            fprintf(stderr,
                    "STEARLIGHT STEAM: using Composite pixmap for window 0x%lx (%dx%d)\n",
                    best.window, best.width, best.height);
            image = composite_image;
        }
    }
    /* Chromium frequently places its visible pixels in an ARGB child that
       has no server-side backing store.  XGetImage then returns BadMatch even
       though the mapped frame immediately above it is capturable.  Retry the
       nearest parent before giving up; this keeps the Steam welcome surface
       visible while WebHelper swaps its child windows. */
    if (!image && best.parent && best.parent != (unsigned long)client->root) {
        XWindowAttributes parent_attributes;
        if (get_window_attributes_safe(display, (Window)best.parent,
                                       &parent_attributes) &&
            parent_attributes.map_state == IsViewable &&
            parent_attributes.class != InputOnly &&
            parent_attributes.depth >= 16 && parent_attributes.width >= 160 &&
            parent_attributes.height >= 100) {
            XImage *parent_image = get_window_image_safe(
                display, (Window)best.parent, parent_attributes.width,
                parent_attributes.height, parent_attributes.depth);
            if (!parent_image)
                parent_image = get_window_composite_image_safe(
                    display, (Window)best.parent, parent_attributes.width,
                    parent_attributes.height, parent_attributes.depth);
            if (parent_image) {
                fprintf(stderr,
                        "STEARLIGHT STEAM: using capturable parent 0x%lx for child 0x%lx (%dx%d)\n",
                        best.parent, best.window, parent_attributes.width,
                        parent_attributes.height);
                best.window = best.parent;
                best.width = parent_attributes.width;
                best.height = parent_attributes.height;
                client->content_window = best.window;
                image = parent_image;
            }
        }
    }
    if (!image) {
        static unsigned int error_reports;
        if (error_reports < 8U) {
            XWindowAttributes failed_attributes;
            const int got_failed_attributes = get_window_attributes_safe(
                display, (Window)best.window, &failed_attributes);
            fprintf(stderr,
                    "STEARLIGHT STEAM: XGetImage rejected window 0x%lx parent=0x%lx (%dx%d depth=%d map=%d class=%d attrs=%s error=%d)\n",
                    best.window, best.parent, best.width, best.height,
                    got_failed_attributes ? failed_attributes.depth : 0,
                    got_failed_attributes ? failed_attributes.map_state : 0,
                    got_failed_attributes ? failed_attributes.class : 0,
                    got_failed_attributes ? "ok" : "failed",
                    initial_image_error ? initial_image_error
                                        : capture_x_error_code);
            ++error_reports;
        }
    }
    return image;
}

void stearlight_steam_client_update(stearlight_steam_client *client,
                                     SDL_Renderer *renderer,
                                     uint32_t now_ms) {
    if (!client || !renderer || client->state == STEARLIGHT_STEAM_CLIENT_MISSING ||
        client->state == STEARLIGHT_STEAM_CLIENT_FAILED)
        return;
    if (client->display_pid > 0 && child_exited(client->display_pid)) {
        fprintf(stderr, "STEARLIGHT STEAM: private X display exited\n");
        stop_process_group(client->steam_pid);
        client->steam_pid = 0;
        if (client->display) XCloseDisplay((Display *)client->display);
        client->display = NULL;
        client->display_pid = 0;
        client->root = 0;
        clear_captured_frame(client);
        const uint32_t retry_ms = schedule_restart(client, now_ms);
        /* The old X server has already been reaped.  Recreate it as part of
           the same retry, otherwise connect_display() can only keep trying
           XOpenDisplay() against a display that will never come back. */
        client->display_pid = start_display();
        if (client->display_pid <= 0)
            client->display_pid = 0;
        snprintf(client->detail, sizeof(client->detail),
                 "Steam display exited; restarting");
        fprintf(stderr,
                 "STEARLIGHT STEAM: display restart in %u ms\n", retry_ms);
        return;
    }
    if (!client->display && !ensure_display(client, now_ms)) return;
    connect_display(client, now_ms);
    if (!client->display || !client->steam_pid) return;
    if (child_exited(client->steam_pid)) {
        const pid_t exited_pid = client->steam_pid;
        client->steam_pid = 0;
        (void)kill(-exited_pid, SIGTERM);
        clear_captured_frame(client);
        const uint32_t retry_ms = schedule_restart(client, now_ms);
        snprintf(client->detail, sizeof(client->detail),
                 "Steam exited; restarting");
        fprintf(stderr,
                "STEARLIGHT STEAM: client exited; restart in %u ms\n",
                retry_ms);
        return;
    }
    /* A low-rate heartbeat makes first-run failures diagnosable even when the
       VM's serial console is the only available log sink. */
    static uint32_t last_heartbeat_ms;
    if (now_ms - last_heartbeat_ms >= 2000U) {
        fprintf(stderr,
                "STEARLIGHT STEAM: bridge heartbeat display=%s root=0x%lx steam_pid=%ld frame=%s state=%d\n",
                client->display ? "open" : "closed",
                client->root,
                (long)client->steam_pid, client->frame ? "ready" : "none",
                (int)client->state);
        last_heartbeat_ms = now_ms;
    }
    if (now_ms < client->next_capture_ms) return;
    client->next_capture_ms = now_ms + SVRT_STEAM_CAPTURE_INTERVAL_MS;
    static uint32_t last_capture_log_ms;
    if (now_ms - last_capture_log_ms >= 2000U) {
        fprintf(stderr, "STEARLIGHT STEAM: capture tick\n");
        last_capture_log_ms = now_ms;
    }
    XImage *image = capture_steam_window(client);
    if (!image) return;
    /* Xvfb has a valid root image before Steam maps its Gamepad UI window.
       Do not publish that all-black bootstrap surface as the live frame or
       the VR shell would hide its own loading transition indefinitely. */
    if (!image_has_visible_content(image)) {
        static unsigned int blank_reports;
        if (blank_reports < 4U) {
            fprintf(stderr,
                    "STEARLIGHT STEAM: selected window 0x%lx is blank (%dx%d bpp=%d)\n",
                    client->content_window, image->width, image->height,
                    image->bits_per_pixel);
            ++blank_reports;
        }
        XDestroyImage(image);
        return;
    }
    if (!client->frame || client->frame_width != image->width ||
        client->frame_height != image->height)
    {
        if (client->frame) SDL_DestroyTexture(client->frame);
        client->frame = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          image->width, image->height);
        client->frame_width = image->width;
        client->frame_height = image->height;
    }
    if (client->frame) {
        int texture_updated = 0;
        const int native_argb8888 =
            image->bits_per_pixel == 32 && image->byte_order == LSBFirst &&
            image->red_mask == 0x00ff0000UL &&
            image->green_mask == 0x0000ff00UL &&
            image->blue_mask == 0x000000ffUL;
        if (native_argb8888) {
            texture_updated = SDL_UpdateTexture(client->frame, NULL,
                                                image->data,
                                                image->bytes_per_line) == 0;
        } else {
            /* Xvfb normally exposes 32 bpp for a depth-24 window.  Keep a
               mask-aware fallback for real Pi X servers that expose 16/24
               bpp so the Steam welcome surface cannot disappear solely due
               to a visual-format difference. */
            void *pixels = NULL;
            int pitch = 0;
            if (SDL_LockTexture(client->frame, NULL, &pixels, &pitch) == 0) {
                for (int y = 0; y < image->height; ++y) {
                    uint32_t *row = (uint32_t *)((uint8_t *)pixels +
                                                  (size_t)y * (size_t)pitch);
                    for (int x = 0; x < image->width; ++x) {
                        const unsigned long pixel = XGetPixel(image, x, y);
                        row[x] = 0xff000000U |
                                 (ximage_channel(pixel, image->red_mask) << 16) |
                                 (ximage_channel(pixel, image->green_mask) << 8) |
                                 ximage_channel(pixel, image->blue_mask);
                    }
                }
                SDL_UnlockTexture(client->frame);
                texture_updated = 1;
            }
        }
        if (texture_updated) {
            if (!client->frame_announced) {
                fprintf(stderr,
                        "STEARLIGHT STEAM FRAME READY %dx%d window=0x%lx\n",
                        image->width, image->height, client->content_window);
                client->frame_announced = 1;
            }
            client->launch_failures = 0;
            client->state = STEARLIGHT_STEAM_CLIENT_RUNNING;
            client->detail[0] = '\0';
        }
    }
    flush_pending_uri(client);
    XDestroyImage(image);
}

SDL_Texture *stearlight_steam_client_frame(
    const stearlight_steam_client *client) {
    return client ? client->frame : NULL;
}

const char *stearlight_steam_client_detail(
    const stearlight_steam_client *client) {
    return client && client->detail[0] ? client->detail : NULL;
}

static void stop_process_group(pid_t pid) {
    if (pid <= 0) return;
    kill(-pid, SIGTERM);
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (waitpid(pid, NULL, WNOHANG) == pid) return;
        SDL_Delay(20);
    }
    kill(-pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

static void dispatch_steam_uri(const char *uri) {
    if (!uri || !uri[0]) return;
    pid_t child = fork();
    if (child < 0) return;
    if (!child) {
        pid_t grandchild = fork();
        if (grandchild < 0) _exit(127);
        if (grandchild) _exit(0);
        setsid();
        child_environment();
        exec_steam_with_uri(uri);
    }
    waitpid(child, NULL, 0);
}

static void flush_pending_uri(stearlight_steam_client *client) {
    if (!client || !client->pending_uri[0] || client->steam_pid <= 0 ||
        client->state != STEARLIGHT_STEAM_CLIENT_RUNNING)
        return;
    char uri[STEARLIGHT_STEAM_URI_MAX];
    memcpy(uri, client->pending_uri, sizeof(uri));
    client->pending_uri[0] = '\0';
    fprintf(stderr, "STEARLIGHT STEAM: dispatching queued Valve URI (%s)\n",
            uri);
    dispatch_steam_uri(uri);
}

void stearlight_steam_client_open_uri(
    stearlight_steam_client *client, const char *uri) {
    if (!client || !valid_valve_uri(uri) ||
        client->state == STEARLIGHT_STEAM_CLIENT_MISSING ||
        client->state == STEARLIGHT_STEAM_CLIENT_FAILED)
        return;
    if (client->steam_pid <= 0 ||
        client->state != STEARLIGHT_STEAM_CLIENT_RUNNING) {
        snprintf(client->pending_uri, sizeof(client->pending_uri), "%s", uri);
        fprintf(stderr,
                "STEARLIGHT STEAM: queued Valve URI until client frame is ready (%s)\n",
                client->pending_uri);
        return;
    }
    dispatch_steam_uri(uri);
}

void stearlight_steam_client_open_steam_link(
    stearlight_steam_client *client) {
    if (!client) return;
    const char *uri = steam_link_uri();
    fprintf(stderr,
            "STEARLIGHT STEAM: handing Steam Link to Valve client (%s)\n",
            uri);
    stearlight_steam_client_open_uri(client, uri);
}

static int steam_pointer_target(const stearlight_steam_client *client,
                                int *screen, int *screen_width,
                                int *screen_height, int *origin_x,
                                int *origin_y, int *target_width,
                                int *target_height) {
    if (!client || !client->display || !screen_width || !screen_height ||
        !origin_x || !origin_y || !target_width || !target_height)
        return 0;
    Display *display = (Display *)client->display;
    const int selected_screen = DefaultScreen(display);
    const int width = DisplayWidth(display, selected_screen);
    const int height = DisplayHeight(display, selected_screen);
    if (width <= 0 || height <= 0) return 0;
    int x = 0, y = 0;
    if (client->content_window) {
        Window child = None;
        XSync(display, False);
        capture_x_error_code = 0;
        int (*previous_handler)(Display *, XErrorEvent *) =
            XSetErrorHandler(capture_x_error_handler);
        XTranslateCoordinates(display, (Window)client->content_window,
                              RootWindow(display, selected_screen), 0, 0,
                              &x, &y, &child);
        XSync(display, False);
        XSetErrorHandler(previous_handler);
        if (capture_x_error_code) return 0;
        XWindowAttributes attributes;
        if (get_window_attributes_safe(display,
                                       (Window)client->content_window,
                                       &attributes) &&
            attributes.width > 0 && attributes.height > 0) {
            *target_width = attributes.width;
            *target_height = attributes.height;
        } else {
            *target_width = width;
            *target_height = height;
        }
    } else {
        *target_width = width;
        *target_height = height;
    }
    if (screen) *screen = selected_screen;
    *screen_width = width;
    *screen_height = height;
    *origin_x = x;
    *origin_y = y;
    return 1;
}

void stearlight_steam_client_send_mouse_motion(
    const stearlight_steam_client *client, int surface_x, int surface_y,
    int surface_width, int surface_height) {
    int screen = 0, screen_width = 0, screen_height = 0;
    int origin_x = 0, origin_y = 0;
    int target_width = 0, target_height = 0;
    if (!steam_pointer_target(client, &screen, &screen_width, &screen_height,
                              &origin_x, &origin_y, &target_width,
                              &target_height) || surface_width <= 0 ||
        surface_height <= 0 || target_width <= 0 || target_height <= 0)
        return;
    int x = origin_x + surface_x * target_width / surface_width;
    int y = origin_y + surface_y * target_height / surface_height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= screen_width) x = screen_width - 1;
    if (y >= screen_height) y = screen_height - 1;
    XTestFakeMotionEvent((Display *)client->display, screen, x, y,
                         CurrentTime);
    XFlush((Display *)client->display);
}

void stearlight_steam_client_send_mouse_button(
    const stearlight_steam_client *client, int button, int pressed) {
    if (!client || !client->display) return;
    int x_button = 0;
    if (button == 1) x_button = 1;
    else if (button == 2) x_button = 2;
    else if (button == 3) x_button = 3;
    if (!x_button) return;
    XTestFakeButtonEvent((Display *)client->display, x_button,
                         pressed ? True : False, CurrentTime);
    XFlush((Display *)client->display);
}

void stearlight_steam_client_send_mouse_wheel(
    const stearlight_steam_client *client, int delta_y) {
    if (!client || !client->display || !delta_y) return;
    const int button = delta_y > 0 ? 4 : 5;
    const int count = delta_y > 0 ? delta_y : -delta_y;
    for (int index = 0; index < count && index < 8; ++index) {
        XTestFakeButtonEvent((Display *)client->display, button, True,
                             CurrentTime);
        XTestFakeButtonEvent((Display *)client->display, button, False,
                             CurrentTime);
    }
    XFlush((Display *)client->display);
}

static KeySym x_keysym_from_sdl(int keycode) {
    switch (keycode) {
    case SDLK_BACKSPACE: return XK_BackSpace;
    case SDLK_TAB: return XK_Tab;
    case SDLK_RETURN: return XK_Return;
    case SDLK_ESCAPE: return XK_Escape;
    case SDLK_DELETE: return XK_Delete;
    case SDLK_HOME: return XK_Home;
    case SDLK_END: return XK_End;
    case SDLK_PAGEUP: return XK_Page_Up;
    case SDLK_PAGEDOWN: return XK_Page_Down;
    case SDLK_LEFT: return XK_Left;
    case SDLK_RIGHT: return XK_Right;
    case SDLK_UP: return XK_Up;
    case SDLK_DOWN: return XK_Down;
    case SDLK_INSERT: return XK_Insert;
    case SDLK_F1: return XK_F1;
    case SDLK_F2: return XK_F2;
    case SDLK_F3: return XK_F3;
    case SDLK_F4: return XK_F4;
    case SDLK_F5: return XK_F5;
    case SDLK_F6: return XK_F6;
    case SDLK_F7: return XK_F7;
    case SDLK_F8: return XK_F8;
    case SDLK_F9: return XK_F9;
    case SDLK_F10: return XK_F10;
    case SDLK_F11: return XK_F11;
    case SDLK_F12: return XK_F12;
    case SDLK_LSHIFT: return XK_Shift_L;
    case SDLK_RSHIFT: return XK_Shift_R;
    case SDLK_LCTRL: return XK_Control_L;
    case SDLK_RCTRL: return XK_Control_R;
    case SDLK_LALT: return XK_Alt_L;
    case SDLK_RALT: return XK_Alt_R;
    case SDLK_LGUI: return XK_Super_L;
    case SDLK_RGUI: return XK_Super_R;
    case SDLK_SPACE: return XK_space;
    default:
        /* SDL uses Unicode values for printable keycodes. Xlib accepts the
           corresponding keysym for the same keyboard entry. */
        return keycode >= 0x20 && keycode <= 0x10ffff ?
                   (KeySym)keycode : NoSymbol;
    }
}

static unsigned int x_modifier_mask(int keycode) {
    switch (keycode) {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT: return 1U;
    case SDLK_LCTRL:
    case SDLK_RCTRL: return 2U;
    case SDLK_LALT:
    case SDLK_RALT: return 4U;
    case SDLK_LGUI:
    case SDLK_RGUI: return 8U;
    default: return 0U;
    }
}

static KeySym x_modifier_keysym(unsigned int mask) {
    if (mask == 1U) return XK_Shift_L;
    if (mask == 2U) return XK_Control_L;
    if (mask == 4U) return XK_Alt_L;
    if (mask == 8U) return XK_Super_L;
    return NoSymbol;
}

static unsigned int x_required_modifiers(int modifiers) {
    unsigned int required = 0U;
    if (modifiers & KMOD_SHIFT) required |= 1U;
    if (modifiers & KMOD_CTRL) required |= 2U;
    if (modifiers & KMOD_ALT) required |= 4U;
    if (modifiers & KMOD_GUI) required |= 8U;
    return required;
}

void stearlight_steam_client_send_key(
    stearlight_steam_client *client, int keycode, int pressed,
    int modifiers) {
    if (!client || !client->display) return;
    Display *display = (Display *)client->display;
    const KeySym keysym = x_keysym_from_sdl(keycode);
    if (keysym == NoSymbol) return;
    const KeyCode x_keycode = XKeysymToKeycode(display, keysym);
    if (!x_keycode) return;
    const unsigned int modifier = x_modifier_mask(keycode);
    if (modifier) {
        XTestFakeKeyEvent(display, x_keycode, pressed ? True : False,
                          CurrentTime);
        if (pressed) client->x_modifiers |= modifier;
        else client->x_modifiers &= ~modifier;
        XFlush(display);
        return;
    }

    const unsigned int required = x_required_modifiers(modifiers);
    const unsigned int temporary = pressed ? required & ~client->x_modifiers : 0U;
    if (pressed) {
        for (unsigned int bit = 1U; bit <= 8U; bit <<= 1U) {
            if (!(temporary & bit)) continue;
            const KeyCode modifier_keycode = XKeysymToKeycode(
                display, x_modifier_keysym(bit));
            if (modifier_keycode)
                XTestFakeKeyEvent(display, modifier_keycode, True,
                                  CurrentTime);
        }
    }
    XTestFakeKeyEvent(display, x_keycode, pressed ? True : False,
                      CurrentTime);
    if (pressed) {
        for (unsigned int bit = 8U; bit > 0U; bit >>= 1U) {
            if (!(temporary & bit)) continue;
            const KeyCode modifier_keycode = XKeysymToKeycode(
                display, x_modifier_keysym(bit));
            if (modifier_keycode)
                XTestFakeKeyEvent(display, modifier_keycode, False,
                                  CurrentTime);
        }
    }
    XFlush(display);
}

void stearlight_steam_client_stop(stearlight_steam_client *client) {
    if (!client) return;
    stop_process_group(client->steam_pid);
    if (client->display) XCloseDisplay((Display *)client->display);
    stop_process_group(client->display_pid);
    if (client->frame) SDL_DestroyTexture(client->frame);
    memset(client, 0, sizeof(*client));
}
