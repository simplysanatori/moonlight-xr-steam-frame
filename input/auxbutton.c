// Native (lane a11y-2): see auxbutton.h. GPLv3.
#define _GNU_SOURCE
#include "auxbutton.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/input.h>

_Static_assert(AUXBTN_KEY == KEY_SELECT, "the button is KEY_SELECT");

static int readLine(const char* path, char* buf, size_t cap) {
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return 0;
    }
    int ok = fgets(buf, (int)cap, f) != NULL;
    fclose(f);
    if (ok) {
        buf[strcspn(buf, "\n")] = 0;
    }
    return ok;
}

int auxDevKeyInCaps(const char* caps, int key) {
    // the words are unsigned longs as the kernel prints them, the highest first
    const int bits = (int)(sizeof(unsigned long) * 8);
    unsigned long words[64];
    int n = 0;
    const char* p = caps;
    while (*p && n < 64) {
        while (*p == ' ') p++;
        if (!*p) break;
        char* end = NULL;
        words[n++] = strtoul(p, &end, 16);
        if (end == p) break;
        p = end;
    }
    int word = key / bits, bit = key % bits;
    if (word >= n) {
        return 0;
    }
    return (words[n - 1 - word] >> bit) & 1ul ? 1 : 0;
}

int auxDevFind(const char* sysRoot, const char* devRoot, AuxDevInfo* out) {
    memset(out, 0, sizeof(*out));
    char dirPath[256];
    snprintf(dirPath, sizeof dirPath, "%s/class/input", sysRoot);
    DIR* dir = opendir(dirPath);
    if (dir == NULL) {
        return 0;
    }
    struct dirent* e;
    char best[64] = "";
    while ((e = readdir(dir)) != NULL) {
        if (strncmp(e->d_name, "event", 5) != 0) {
            continue;
        }
        char path[512], name[128], caps[1024];
        snprintf(path, sizeof path, "%s/%s/device/name", dirPath, e->d_name);
        if (!readLine(path, name, sizeof name) || strcmp(name, AUXBTN_DEVICE_NAME) != 0) {
            continue;
        }
        snprintf(path, sizeof path, "%s/%s/device/capabilities/key", dirPath, e->d_name);
        if (!readLine(path, caps, sizeof caps) || !auxDevKeyInCaps(caps, AUXBTN_KEY)) {
            continue;
        }
        // the lowest event number of any that match, so the answer does not hang on readdir's order
        if (best[0] == 0 || strlen(e->d_name) < strlen(best)
                || (strlen(e->d_name) == strlen(best) && strcmp(e->d_name, best) < 0)) {
            snprintf(best, sizeof best, "%s", e->d_name);
        }
    }
    closedir(dir);
    if (best[0] == 0) {
        return 0;
    }
    out->found = 1;
    snprintf(out->sysName, sizeof out->sysName, "%s", best);
    snprintf(out->path, sizeof out->path, "%s/%s", devRoot, best);
    int fd = open(out->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) {
        out->readable = 1;
        close(fd);
    }
    else {
        out->err = errno;
    }
    return 1;
}

void auxDevInit(AuxDev* d) {
    d->fd = -1;
    d->grabbed = 0;
    d->grab = NULL;
}

static int doGrab(AuxDev* d, int on) {
    if (d->grab != NULL) {
        return d->grab(d->fd, on);
    }
    return ioctl(d->fd, EVIOCGRAB, on ? 1 : 0) == 0 ? 0 : -1;
}

void auxDevAdopt(AuxDev* d, int fd, int grab) {
    d->fd = fd;
    d->grabbed = grab && doGrab(d, 1) == 0;
}

int auxDevOpen(AuxDev* d, const char* path, int grab) {
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    auxDevAdopt(d, fd, grab);
    return 0;
}

void auxDevClose(AuxDev* d) {
    if (d->fd < 0) {
        return;
    }
    if (d->grabbed) {
        doGrab(d, 0);
        d->grabbed = 0;
    }
    close(d->fd);
    d->fd = -1;
}

int auxDevParse(const struct input_event* ev, int n, int* edges, int cap) {
    int k = 0;
    for (int i = 0; i < n && k < cap; i++) {
        if (ev[i].type == EV_KEY && ev[i].code == AUXBTN_KEY && (ev[i].value == 0 || ev[i].value == 1)) {
            edges[k++] = ev[i].value;
        }
    }
    return k;
}

int auxDevRead(AuxDev* d, int* edges, int cap) {
    if (d->fd < 0) {
        return -1;
    }
    int k = 0;
    struct input_event ev[32];
    for (;;) {
        ssize_t got = read(d->fd, ev, sizeof ev);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return k;
            }
            auxDevClose(d);
            return -1;
        }
        if (got == 0) {
            // a pipe's writer gone, or the device: nothing more will come
            auxDevClose(d);
            return k > 0 ? k : -1;
        }
        k += auxDevParse(ev, (int)(got / (ssize_t)sizeof(struct input_event)), edges + k, cap - k);
        if (k >= cap || (size_t)got < sizeof ev) {
            return k;
        }
    }
}
