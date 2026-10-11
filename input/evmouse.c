// Moonlight XR Native: evdev mice to the host. GPLv3.
#include "evmouse.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "Limelight.h"

#define MAX_DEVS 8
#define BITS_PER_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define TEST_BIT(b, a) ((a[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1)

typedef struct {
    int fd;
    char path[64];
    char name[128];
    int grabbed;     // 1 yes, 0 no, -1 tried and failed
    int hiRes;       // has REL_WHEEL_HI_RES: REL_WHEEL (legacy notches) is then ignored
    int dropped;     // SYN_DROPPED: ignore until the next SYN_REPORT
    int dx, dy, wheel, hwheel; // this packet so far
    int fx, fy;      // scaled motion kept as 1/1024 pixel between packets
} Dev;

static struct {
    pthread_t thread;
    int threaded;
    atomic_int stop, active;
    EvMouseSink sink;
    void* user;
    EvMouseCfg cfg;
    Dev devs[MAX_DEVS];
    int n;
    unsigned sent;       // buttons whose press the host has (bit per BUTTON_*)
    int wasActive;
    struct timespec lastScan;
    float scale;
    char ignore[128];    // MLXR_MOUSE_IGNORE: a substring of device names to skip
} g;

static EvMouseLog g_log;
static atomic_llong g_lastUse; // ns of the last mouse activity that was forwarded

void evmouse_set_log(EvMouseLog log) { g_log = log; }

static void logf_(char level, const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (g_log) g_log(level, buf);
}

int64_t evmouse_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

// ---- classification ---------------------------------------------------------------

int evmouse_is_mouse(const unsigned long* evBits, const unsigned long* relBits, const unsigned long* keyBits) {
    if (!TEST_BIT(EV_REL, evBits) || !TEST_BIT(EV_KEY, evBits)) return 0;
    if (!TEST_BIT(REL_X, relBits) || !TEST_BIT(REL_Y, relBits)) return 0;
    return TEST_BIT(BTN_LEFT, keyBits) != 0;
}

int evmouse_button(int code) {
    switch (code) {
    case BTN_LEFT: return BUTTON_LEFT;
    case BTN_MIDDLE: return BUTTON_MIDDLE;
    case BTN_RIGHT: return BUTTON_RIGHT;
    case BTN_SIDE: case BTN_BACK: return BUTTON_X1;
    case BTN_EXTRA: case BTN_FORWARD: return BUTTON_X2;
    default: return 0;
    }
}

// ---- grab -------------------------------------------------------------------------

static int doGrab(int fd, int on) {
    if (g.cfg.grab) return g.cfg.grab(fd, on);
    return ioctl(fd, EVIOCGRAB, on ? 1 : 0);
}

static void setGrab(Dev* dv, int on) {
    if (on == (dv->grabbed == 1)) return;
    if (doGrab(dv->fd, on) == 0) {
        dv->grabbed = on;
        logf_('I', "mouse %s: %s", dv->name, on ? "grabbed for the host" : "released");
    } else if (on) {
        logf_('W', "mouse %s: grab failed (%s); it also reaches other apps", dv->name, strerror(errno));
        dv->grabbed = -1; // forward anyway, don't retry every loop
    }
}

// ---- forwarding -------------------------------------------------------------------

static short clamp16(int v) { return (short)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }

static void noteUse(void) { atomic_store(&g_lastUse, (long long)evmouse_now_ns()); }

static void sendMove(int dx, int dy) {
    // A short per call; bigger jumps go in pieces
    while (dx || dy) {
        int sx = dx > 32767 ? 32767 : (dx < -32768 ? -32768 : dx);
        int sy = dy > 32767 ? 32767 : (dy < -32768 ? -32768 : dy);
        if (g.sink.move) g.sink.move(sx, sy, g.user);
        dx -= sx;
        dy -= sy;
    }
}

static void releaseAll(void) {
    for (int b = 1; b <= 5; b++)
        if (g.sent & (1u << b)) {
            g.sent &= ~(1u << b);
            if (g.sink.button) g.sink.button(b, 0, g.user);
        }
    for (int i = 0; i < g.n; i++) {
        Dev* d = &g.devs[i];
        d->dx = d->dy = d->wheel = d->hwheel = d->fx = d->fy = 0;
    }
}

static void flush(Dev* d) {
    int active = atomic_load(&g.active);
    if (active && (d->dx || d->dy)) {
        // Scaled in 1/1024 pixel, the remainder carried to the next packet (high-resolution mice)
        int sx = (int)lroundf((float)d->dx * g.scale * 1024.0f) + d->fx;
        int sy = (int)lroundf((float)d->dy * g.scale * 1024.0f) + d->fy;
        int px = sx / 1024, py = sy / 1024;
        d->fx = sx - px * 1024;
        d->fy = sy - py * 1024;
        if (px || py) {
            sendMove(px, py);
            noteUse();
        }
    }
    if (active && d->wheel) {
        if (g.sink.scroll) g.sink.scroll(clamp16(d->wheel), g.user);
        noteUse();
    }
    if (active && d->hwheel) {
        if (g.sink.hscroll) g.sink.hscroll(clamp16(d->hwheel), g.user);
        noteUse();
    }
    d->dx = d->dy = d->wheel = d->hwheel = 0;
}

static void handle(Dev* d, const struct input_event* ev) {
    switch (ev->type) {
    case EV_SYN:
        if (ev->code == SYN_DROPPED) {
            d->dropped = 1;
            d->dx = d->dy = d->wheel = d->hwheel = 0;
        } else if (ev->code == SYN_REPORT) {
            if (d->dropped) d->dropped = 0;
            else flush(d);
        }
        break;
    case EV_REL:
        if (d->dropped) break;
        switch (ev->code) {
        case REL_X: d->dx += ev->value; break;
        case REL_Y: d->dy += ev->value; break;
        case REL_WHEEL_HI_RES: d->wheel += ev->value; break;
        case REL_WHEEL: if (!d->hiRes) d->wheel += ev->value * 120; break;
        case REL_HWHEEL_HI_RES: d->hwheel += ev->value; break;
        case REL_HWHEEL: if (!d->hiRes) d->hwheel += ev->value * 120; break;
        }
        break;
    case EV_KEY: {
        if (ev->value == 2 || d->dropped) break;
        int b = evmouse_button(ev->code);
        if (!b) break;
        int down = ev->value == 1;
        if (!atomic_load(&g.active)) break; // pressed while not forwarding: the host never hears it
        if (down) g.sent |= 1u << b;
        else if (!(g.sent & (1u << b))) break;
        else g.sent &= ~(1u << b);
        if (g.sink.button) g.sink.button(b, down, g.user);
        noteUse();
        break;
    }
    }
}

// ---- devices ----------------------------------------------------------------------

static void removeDev(int i) {
    logf_('I', "mouse gone: %s", g.devs[i].name);
    close(g.devs[i].fd);
    g.devs[i] = g.devs[--g.n];
}

int evmouse_add_fd(int fd, const char* name, int hiRes) {
    if (g.n >= MAX_DEVS) return -1;
    Dev* dv = &g.devs[g.n];
    memset(dv, 0, sizeof(*dv));
    dv->fd = fd;
    dv->hiRes = hiRes;
    snprintf(dv->name, sizeof(dv->name), "%s", name ? name : "mouse");
    return g.n++;
}

int evmouse_device_count(void) { return g.n; }
int evmouse_grabbed(int i) { return i >= 0 && i < g.n ? g.devs[i].grabbed : 0; }

static void scan(void) {
    DIR* d = opendir("/dev/input");
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) && g.n < MAX_DEVS) {
        if (strncmp(e->d_name, "event", 5)) continue;
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int known = 0;
        for (int i = 0; i < g.n; i++) known |= !strcmp(g.devs[i].path, path);
        if (known) continue;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        unsigned long ev[NLONGS(EV_MAX + 1)] = {0}, rel[NLONGS(REL_MAX + 1)] = {0}, key[NLONGS(KEY_MAX + 1)] = {0};
        char name[128] = "";
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !TEST_BIT(EV_REL, ev) ||
            ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel)), rel) < 0 ||
            ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key)), key) < 0 || !evmouse_is_mouse(ev, rel, key) ||
            !strncmp(name, "Steam", 5) || (g.ignore[0] && strstr(name, g.ignore))) {
            close(fd);
            continue;
        }
        int idx = evmouse_add_fd(fd, name, TEST_BIT(REL_WHEEL_HI_RES, rel) != 0);
        if (idx < 0) {
            close(fd);
            break;
        }
        snprintf(g.devs[idx].path, sizeof(g.devs[idx].path), "%s", path);
        logf_('I', "mouse found: %s (%s)%s", name, path, g.devs[idx].hiRes ? " hi-res wheel" : "");
    }
    closedir(d);
}

int evmouse_step(int timeoutMs) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (g.cfg.scan && now.tv_sec - g.lastScan.tv_sec >= 2) { // hotplug: Bluetooth mice come and go
        scan();
        g.lastScan = now;
    }
    int active = atomic_load(&g.active);
    for (int i = 0; i < g.n; i++)
        if (g.devs[i].grabbed != -1 || !active) setGrab(&g.devs[i], active);
    if (g.wasActive && !active) releaseAll();
    g.wasActive = active;

    if (g.n == 0) {
        if (timeoutMs > 0) usleep((useconds_t)timeoutMs * 1000);
        return 0;
    }
    struct pollfd pf[MAX_DEVS];
    for (int i = 0; i < g.n; i++) pf[i] = (struct pollfd){g.devs[i].fd, POLLIN, 0};
    if (poll(pf, (nfds_t)g.n, timeoutMs) <= 0) return 0;
    int handled = 0;
    for (int i = g.n - 1; i >= 0; i--) {
        if (pf[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            removeDev(i);
            continue;
        }
        if (!(pf[i].revents & POLLIN)) continue;
        struct input_event ev[64];
        ssize_t r = read(g.devs[i].fd, ev, sizeof(ev));
        if (r < 0) {
            if (errno == ENODEV) removeDev(i);
            continue;
        }
        size_t cnt = (size_t)r / sizeof(ev[0]);
        for (size_t j = 0; j < cnt; j++) handle(&g.devs[i], &ev[j]);
        handled += (int)cnt;
    }
    return handled;
}

static void* run(void* arg) {
    (void)arg;
    while (!atomic_load(&g.stop)) evmouse_step(250);
    for (int i = 0; i < g.n; i++) {
        setGrab(&g.devs[i], 0);
        close(g.devs[i].fd);
    }
    g.n = 0;
    return NULL;
}

void evmouse_init(const EvMouseSink* sink, void* user, const EvMouseCfg* cfg) {
    memset(&g, 0, sizeof(g));
    g.sink = *sink;
    g.user = user;
    if (cfg) g.cfg = *cfg;
    else g.cfg.scan = 1;
    g.scale = 1.0f;
    const char* s = getenv("MLXR_MOUSE_SCALE");
    if (s && atof(s) > 0.0) g.scale = (float)atof(s);
    s = getenv("MLXR_MOUSE_IGNORE");
    if (s) snprintf(g.ignore, sizeof(g.ignore), "%s", s);
    atomic_store(&g_lastUse, 0);
    if (g.scale != 1.0f) logf_('I', "mouse: motion scale %.2f", (double)g.scale);
}

int evmouse_start(const EvMouseSink* sink, void* user) {
    const char* off = getenv("MLXR_MOUSE");
    if (off && !strcmp(off, "0")) {
        logf_('I', "mouse: off (MLXR_MOUSE=0)");
        return -1;
    }
    evmouse_init(sink, user, NULL);
    g.threaded = 1;
    return pthread_create(&g.thread, NULL, run, NULL);
}

void evmouse_set_active(int active) { atomic_store(&g.active, active ? 1 : 0); }

void evmouse_stop(void) {
    if (!g.threaded) return;
    atomic_store(&g.stop, 1);
    pthread_join(g.thread, NULL);
    g.threaded = 0;
}

// ---- the controller pointer yields to the mouse -----------------------------------

static struct {
    int inited, enabled, holdMs;
    float dist;
    int parked, refSet;
    long long seenUse;
    float refX, refY, curX, curY; // video fractions
} f;

static void filterInit(void) {
    if (f.inited) return;
    f.inited = 1;
    f.enabled = 1;
    f.holdMs = 1000;
    f.dist = 0.03f;
    const char* s = getenv("MLXR_MOUSE_PARK");
    if (s && !strcmp(s, "0")) f.enabled = 0;
    if ((s = getenv("MLXR_MOUSE_PARK_MS")) && atoi(s) >= 0) f.holdMs = atoi(s);
    if ((s = getenv("MLXR_MOUSE_PARK_DIST")) && atof(s) > 0.0) f.dist = (float)atof(s);
    logf_('I', "mouse: controller pointer %s (hold %d ms, resume after %.3f of the video width)",
          f.enabled ? "yields to the mouse" : "does not yield (MLXR_MOUSE_PARK=0)", f.holdMs, (double)f.dist);
}

void evmouse_filter_config(int enabled, int holdMs, float dist) {
    f.inited = 1;
    f.enabled = enabled;
    f.holdMs = holdMs;
    f.dist = dist;
}

void evmouse_filter_reset(void) {
    int inited = f.inited, en = f.enabled, hold = f.holdMs;
    float dist = f.dist;
    memset(&f, 0, sizeof(f));
    f.inited = inited;
    f.enabled = en;
    f.holdMs = hold;
    f.dist = dist;
    atomic_store(&g_lastUse, 0);
}

int evmouse_filter_parked(void) { return f.parked; }

void evmouse_note_use_at(int64_t nowNs) { atomic_store(&g_lastUse, (long long)nowNs); }

int evmouse_abs_filter(int x, int y, int w, int h, int64_t nowNs) {
    filterInit();
    if (!f.enabled || w <= 0 || h <= 0) return 0;
    f.curX = (float)x / (float)w;
    f.curY = (float)y / (float)w; // both in video widths so the distance is isotropic
    (void)h;
    long long use = atomic_load(&g_lastUse);
    if (use != 0 && use != f.seenUse) { // the mouse did something new
        f.seenUse = use;
        f.refSet = 0;
        if (!f.parked) logf_('I', "mouse used: the controller pointer is parked");
        f.parked = 1;
    }
    if (!f.parked) return 0;
    if (!f.refSet || nowNs - use < (int64_t)f.holdMs * 1000000LL) {
        f.refSet = 1;
        f.refX = f.curX; // where the aim was while the mouse was in use
        f.refY = f.curY;
        return 1;
    }
    float d = hypotf(f.curX - f.refX, f.curY - f.refY);
    if (d > f.dist) {
        f.parked = 0;
        logf_('I', "controller aim moved %.3f: its pointer resumes", (double)d);
        return 0;
    }
    return 1;
}
