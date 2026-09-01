#include "steam_client.h"

#include <X11/Xutil.h>
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

static int steam_uses_classic_ui(void) {
    const char *mode = getenv("SVRT_STEAM_UI_MODE");
    return mode && (strcmp(mode, "tenfoot") == 0 ||
                    strcmp(mode, "classic") == 0);
}

static void report_exec_failure(const char *path);

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
        gamescope_enabled)
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
    setenv("CLIENTCMD", "steam -gamepadui -steamos3 -steampal -steamdeck", 1);
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
               "1024x640x24", "+extension", "GLX", "-ac",
               "-nolisten", "tcp", "-noreset", NULL);
    } else {
        execlp("Xvfb", "Xvfb", STEARLIGHT_STEAM_DISPLAY, "-screen", "0",
               "1024x640x24", "-extension", "GLX", "-ac", "-nolisten",
               "tcp", "-noreset", NULL);
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
        execlp("gamescope", "gamescope", "-e", "--backend", "sdl", "-b",
               "-W", "1024", "-H", "640", "-w", "1024", "-h", "640",
               "-r", "60", "--expose-wayland", "--", launcher,
               "-gamepadui", "-steamos3", "-steampal", "-steamdeck",
               NULL);
        report_exec_failure("gamescope");
    }
    if (access(launcher, X_OK) == 0) {
        execl(launcher, launcher, "-gamepadui", "-steamos3", "-steampal",
              "-steamdeck", NULL);
        report_exec_failure(launcher);
    }
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
    fprintf(stderr, "STEARLIGHT STEAM: starting native SteamOS Gamepad UI\n");
    return 0;
}

static void connect_display(stearlight_steam_client *client, uint32_t now_ms) {
    if (client->display || now_ms < client->next_connect_ms) return;
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
    client->steam_pid = start_steam();
    if (client->steam_pid <= 0) {
        client->state = STEARLIGHT_STEAM_CLIENT_FAILED;
        snprintf(client->detail, sizeof(client->detail),
                 "Cannot launch Steam");
    }
}

static int child_exited(pid_t pid) {
    if (pid <= 0) return 0;
    int status = 0;
    return waitpid(pid, &status, WNOHANG) == pid;
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

static void find_content_window(Display *display, Window parent,
                                steam_window_candidate *best) {
    if (!display || !best) return;

    Window root = 0;
    Window returned_parent = 0;
    Window *children = NULL;
    unsigned int child_count = 0;
    if (!XQueryTree(display, parent, &root, &returned_parent, &children,
                    &child_count))
        return;

    static Window diagnostic_windows[128];
    static unsigned int diagnostic_count;
    const char *trace_x11 = getenv("SVRT_TRACE_STEAM_X11");

    for (unsigned int index = 0; index < child_count; ++index) {
        XWindowAttributes attributes;
        const int got_attributes =
            XGetWindowAttributes(display, children[index], &attributes);
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
        const Bool queried = XQueryTree(display, (Window)client->root,
                                         &tree_root, &tree_parent,
                                         &tree_children, &tree_count);
        fprintf(stderr,
                "STEARLIGHT STEAM: initial X11 tree query=%s children=%u\n",
                queried ? "ok" : "failed", queried ? tree_count : 0);
        if (queried) {
            for (unsigned int index = 0; index < tree_count; ++index) {
                XWindowAttributes attributes;
                if (!XGetWindowAttributes(display, tree_children[index],
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
        const Bool queried = XQueryTree(display, (Window)client->root, &root,
                                         &parent, &children, &count);
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
            if (XQueryTree(display, (Window)client->root, &root, &parent,
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
    /* X requests are asynchronous.  Flush the tree query before installing
       the scoped handler so an unrelated earlier error is not attributed to
       this capture. */
    XSync(display, False);
    capture_x_error_code = 0;
    int (*previous_handler)(Display *, XErrorEvent *) =
        XSetErrorHandler(capture_x_error_handler);
    XImage *image = XGetImage(display, (Window)best.window, 0, 0,
                              (unsigned int)best.width,
                              (unsigned int)best.height, AllPlanes, ZPixmap);
    XSync(display, False);
    XSetErrorHandler(previous_handler);
    if (capture_x_error_code) {
        static unsigned int error_reports;
        if (error_reports < 8U) {
            fprintf(stderr,
                    "STEARLIGHT STEAM: XGetImage rejected window 0x%lx (error=%d)\n",
                    best.window, capture_x_error_code);
            ++error_reports;
        }
        if (image) XDestroyImage(image);
        return NULL;
    }
    return image;
}

void stearlight_steam_client_update(stearlight_steam_client *client,
                                     SDL_Renderer *renderer,
                                     uint32_t now_ms) {
    if (!client || !renderer || client->state == STEARLIGHT_STEAM_CLIENT_MISSING ||
        client->state == STEARLIGHT_STEAM_CLIENT_FAILED)
        return;
    connect_display(client, now_ms);
    if (!client->display || !client->steam_pid) return;
    if (child_exited(client->steam_pid)) {
        client->steam_pid = 0;
        client->state = STEARLIGHT_STEAM_CLIENT_FAILED;
        snprintf(client->detail, sizeof(client->detail),
                 "Steam exited unexpectedly");
        fprintf(stderr, "STEARLIGHT STEAM: client exited\n");
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
        const int native_argb8888 =
            image->bits_per_pixel == 32 && image->byte_order == LSBFirst &&
            image->red_mask == 0x00ff0000UL &&
            image->green_mask == 0x0000ff00UL &&
            image->blue_mask == 0x000000ffUL;
        if (native_argb8888) {
            SDL_UpdateTexture(client->frame, NULL, image->data,
                              image->bytes_per_line);
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
            }
        }
        client->state = STEARLIGHT_STEAM_CLIENT_RUNNING;
        client->detail[0] = '\0';
    }
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

void stearlight_steam_client_open_uri(
    const stearlight_steam_client *client, const char *uri) {
    if (!client || client->steam_pid <= 0 || !uri || !uri[0]) return;
    pid_t child = fork();
    if (child < 0) return;
    if (!child) {
        pid_t grandchild = fork();
        if (grandchild < 0) _exit(127);
        if (grandchild) _exit(0);
        setsid();
        child_environment();
        execl(steam_launcher(), steam_launcher(), uri, NULL);
        execl(steam_binary(), steam_binary(), uri, NULL);
        _exit(127);
    }
    waitpid(child, NULL, 0);
}

void stearlight_steam_client_stop(stearlight_steam_client *client) {
    if (!client) return;
    stop_process_group(client->steam_pid);
    if (client->display) XCloseDisplay((Display *)client->display);
    stop_process_group(client->display_pid);
    if (client->frame) SDL_DestroyTexture(client->frame);
    memset(client, 0, sizeof(*client));
}
