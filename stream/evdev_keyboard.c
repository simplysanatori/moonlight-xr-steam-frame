// Moonlight XR Native: evdev keyboards to the host. GPLv3.
#include "evdev_keyboard.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "../xr/mlxr_log.h"

#define MAX_DEVS 8
#define BITS_PER_LONG (sizeof(long) * 8)
#define NLONGS(x) (((x) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define TEST_BIT(b, a) ((a[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1)

// Windows virtual-key codes for Linux key codes (as moonlight-qt maps SDL scancodes).
static const unsigned char kVk[256] = {
    [KEY_ESC] = 0x1B, [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4', [KEY_5] = '5',
    [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8', [KEY_9] = '9', [KEY_0] = '0', [KEY_MINUS] = 0xBD,
    [KEY_EQUAL] = 0xBB, [KEY_BACKSPACE] = 0x08, [KEY_TAB] = 0x09, [KEY_Q] = 'Q', [KEY_W] = 'W',
    [KEY_E] = 'E', [KEY_R] = 'R', [KEY_T] = 'T', [KEY_Y] = 'Y', [KEY_U] = 'U', [KEY_I] = 'I',
    [KEY_O] = 'O', [KEY_P] = 'P', [KEY_LEFTBRACE] = 0xDB, [KEY_RIGHTBRACE] = 0xDD, [KEY_ENTER] = 0x0D,
    [KEY_LEFTCTRL] = 0xA2, [KEY_A] = 'A', [KEY_S] = 'S', [KEY_D] = 'D', [KEY_F] = 'F', [KEY_G] = 'G',
    [KEY_H] = 'H', [KEY_J] = 'J', [KEY_K] = 'K', [KEY_L] = 'L', [KEY_SEMICOLON] = 0xBA,
    [KEY_APOSTROPHE] = 0xDE, [KEY_GRAVE] = 0xC0, [KEY_LEFTSHIFT] = 0xA0, [KEY_BACKSLASH] = 0xDC,
    [KEY_Z] = 'Z', [KEY_X] = 'X', [KEY_C] = 'C', [KEY_V] = 'V', [KEY_B] = 'B', [KEY_N] = 'N', [KEY_M] = 'M',
    [KEY_COMMA] = 0xBC, [KEY_DOT] = 0xBE, [KEY_SLASH] = 0xBF, [KEY_RIGHTSHIFT] = 0xA1,
    [KEY_KPASTERISK] = 0x6A, [KEY_LEFTALT] = 0xA4, [KEY_SPACE] = 0x20, [KEY_CAPSLOCK] = 0x14,
    [KEY_F1] = 0x70, [KEY_F2] = 0x71, [KEY_F3] = 0x72, [KEY_F4] = 0x73, [KEY_F5] = 0x74, [KEY_F6] = 0x75,
    [KEY_F7] = 0x76, [KEY_F8] = 0x77, [KEY_F9] = 0x78, [KEY_F10] = 0x79, [KEY_NUMLOCK] = 0x90,
    [KEY_SCROLLLOCK] = 0x91, [KEY_KP7] = 0x67, [KEY_KP8] = 0x68, [KEY_KP9] = 0x69, [KEY_KPMINUS] = 0x6D,
    [KEY_KP4] = 0x64, [KEY_KP5] = 0x65, [KEY_KP6] = 0x66, [KEY_KPPLUS] = 0x6B, [KEY_KP1] = 0x61,
    [KEY_KP2] = 0x62, [KEY_KP3] = 0x63, [KEY_KP0] = 0x60, [KEY_KPDOT] = 0x6E, [KEY_102ND] = 0xE2,
    [KEY_F11] = 0x7A, [KEY_F12] = 0x7B, [KEY_KPENTER] = 0x0D, [KEY_RIGHTCTRL] = 0xA3,
    [KEY_KPSLASH] = 0x6F, [KEY_SYSRQ] = 0x2C, [KEY_RIGHTALT] = 0xA5, [KEY_HOME] = 0x24, [KEY_UP] = 0x26,
    [KEY_PAGEUP] = 0x21, [KEY_LEFT] = 0x25, [KEY_RIGHT] = 0x27, [KEY_END] = 0x23, [KEY_DOWN] = 0x28,
    [KEY_PAGEDOWN] = 0x22, [KEY_INSERT] = 0x2D, [KEY_DELETE] = 0x2E, [KEY_MUTE] = 0xAD,
    [KEY_VOLUMEDOWN] = 0xAE, [KEY_VOLUMEUP] = 0xAF, [KEY_KPEQUAL] = 0xBB, [KEY_PAUSE] = 0x13,
    [KEY_LEFTMETA] = 0x5B, [KEY_RIGHTMETA] = 0x5C, [KEY_COMPOSE] = 0x5D, [KEY_NEXTSONG] = 0xB0,
    [KEY_PLAYPAUSE] = 0xB3, [KEY_PREVIOUSSONG] = 0xB1, [KEY_STOPCD] = 0xB2, [KEY_F13] = 0x7C,
    [KEY_F14] = 0x7D, [KEY_F15] = 0x7E, [KEY_F16] = 0x7F, [KEY_F17] = 0x80, [KEY_F18] = 0x81,
    [KEY_F19] = 0x82, [KEY_F20] = 0x83, [KEY_F21] = 0x84, [KEY_F22] = 0x85, [KEY_F23] = 0x86,
    [KEY_F24] = 0x87,
};

int evkbd_vk(int k, int* extended) {
    if (k < 0 || k >= 256) return 0;
    if (extended) {
        switch (k) {
        case KEY_RIGHTCTRL: case KEY_RIGHTALT: case KEY_KPENTER: case KEY_KPSLASH: case KEY_HOME:
        case KEY_UP: case KEY_PAGEUP: case KEY_LEFT: case KEY_RIGHT: case KEY_END: case KEY_DOWN:
        case KEY_PAGEDOWN: case KEY_INSERT: case KEY_DELETE: case KEY_LEFTMETA: case KEY_RIGHTMETA:
        case KEY_COMPOSE: case KEY_SYSRQ:
            *extended = 1;
            break;
        default:
            *extended = 0;
        }
    }
    return kVk[k];
}

typedef struct {
    int fd;
    char path[64];
    char name[128];
    int grabbed;
} Dev;

static struct {
    pthread_t thread;
    atomic_int stop, active;
    EvKbdSend send;
    void* user;
    Dev devs[MAX_DEVS];
    int n;
    unsigned char held[KEY_MAX / 8 + 1];
    int mods;
    void (*disconnect)(void*);
    void* disconnectUser;
} g;

static int isKeyboard(int fd) {
    unsigned long ev[NLONGS(EV_MAX + 1)] = {0}, keys[NLONGS(KEY_MAX + 1)] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev)), ev) < 0 || !TEST_BIT(EV_KEY, ev)) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0) return 0;
    return TEST_BIT(KEY_A, keys) && TEST_BIT(KEY_Z, keys) && TEST_BIT(KEY_SPACE, keys) && TEST_BIT(KEY_ENTER, keys);
}

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
        if (!isKeyboard(fd)) {
            close(fd);
            continue;
        }
        Dev* dv = &g.devs[g.n++];
        memset(dv, 0, sizeof(*dv));
        dv->fd = fd;
        snprintf(dv->path, sizeof(dv->path), "%s", path);
        ioctl(fd, EVIOCGNAME(sizeof(dv->name)), dv->name);
        MLOG_I("keyboard found: %s (%s)", dv->name, path);
    }
    closedir(d);
}

static void setGrab(Dev* dv, int on) {
    if (dv->grabbed == on) return;
    if (ioctl(dv->fd, EVIOCGRAB, on ? 1 : 0) == 0) {
        dv->grabbed = on;
        MLOG_I("keyboard %s %s", dv->name, on ? "grabbed for the host" : "released");
    } else if (on) {
        MLOG_W("keyboard %s: grab failed (%s); keys also reach other apps", dv->name, strerror(errno));
        dv->grabbed = -1; // forward anyway, don't retry every loop
    }
}

static int modBit(int k) {
    switch (k) {
    case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: return 0x01;
    case KEY_LEFTCTRL: case KEY_RIGHTCTRL: return 0x02;
    case KEY_LEFTALT: case KEY_RIGHTALT: return 0x04;
    case KEY_LEFTMETA: case KEY_RIGHTMETA: return 0x08;
    default: return 0;
    }
}

static void releaseAll(void) {
    for (int k = 0; k < KEY_MAX; k++)
        if (g.held[k / 8] & (1 << (k % 8))) {
            g.held[k / 8] &= (unsigned char)~(1 << (k % 8));
            int ext = 0, vk = evkbd_vk(k, &ext);
            g.mods &= ~modBit(k);
            if (vk) g.send((short)vk, 0, (char)(g.mods | (ext ? 0x10 : 0)), g.user);
        }
    g.mods = 0;
}

static void removeDev(int i) {
    MLOG_I("keyboard gone: %s", g.devs[i].name);
    close(g.devs[i].fd);
    g.devs[i] = g.devs[--g.n];
}

static void* run(void* arg) {
    (void)arg;
    struct timespec last = {0};
    int wasActive = 0;
    while (!atomic_load(&g.stop)) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - last.tv_sec >= 2) { // hotplug: Bluetooth keyboards come and go
            scan();
            last = now;
        }
        int active = atomic_load(&g.active);
        for (int i = 0; i < g.n; i++)
            if (g.devs[i].grabbed != -1 || !active) setGrab(&g.devs[i], active);
        if (wasActive && !active) releaseAll();
        wasActive = active;

        struct pollfd pf[MAX_DEVS];
        for (int i = 0; i < g.n; i++) pf[i] = (struct pollfd){g.devs[i].fd, POLLIN, 0};
        if (poll(pf, (nfds_t)g.n, 250) <= 0) continue;
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
            for (size_t j = 0; j < (size_t)r / sizeof(ev[0]); j++) {
                if (ev[j].type != EV_KEY || ev[j].value == 2 || ev[j].code >= KEY_MAX) continue; // repeats: the host repeats
                int k = ev[j].code, down = ev[j].value == 1;
                int ext = 0, vk = evkbd_vk(k, &ext);
                if (down) {
                    g.mods |= modBit(k);
                    g.held[k / 8] |= (unsigned char)(1 << (k % 8));
                } else {
                    g.mods &= ~modBit(k);
                    g.held[k / 8] &= (unsigned char)~(1 << (k % 8));
                }
                if (!active || !vk) continue;
                // moonlight-qt's KeyComboQuit, matched by scancode as its fallback
                if (down && k == KEY_Q && (g.mods & 0x07) == 0x07 && g.disconnect) {
                    g.held[k / 8] &= (unsigned char)~(1 << (k % 8));
                    releaseAll();
                    MLOG_I("keyboard: Ctrl+Alt+Shift+Q (disconnect your session)");
                    g.disconnect(g.disconnectUser);
                    continue;
                }
                g.send((short)vk, down, (char)(g.mods | (ext ? 0x10 : 0)), g.user);
            }
        }
    }
    for (int i = 0; i < g.n; i++) {
        setGrab(&g.devs[i], 0);
        close(g.devs[i].fd);
    }
    g.n = 0;
    return NULL;
}

void evkbd_set_disconnect_hook(void (*fn)(void*), void* user) {
    g.disconnect = fn;
    g.disconnectUser = user;
}

int evkbd_start(EvKbdSend send, void* user) {
    void (*hook)(void*) = g.disconnect;
    void* hookUser = g.disconnectUser;
    memset(&g, 0, sizeof(g));
    g.disconnect = hook;
    g.disconnectUser = hookUser;
    g.send = send;
    g.user = user;
    return pthread_create(&g.thread, NULL, run, NULL);
}

void evkbd_set_active(int active) { atomic_store(&g.active, active ? 1 : 0); }

void evkbd_stop(void) {
    atomic_store(&g.stop, 1);
    pthread_join(g.thread, NULL);
}
