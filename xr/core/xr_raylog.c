// The ray recorder's CSV rows, see xr_raylog.h
#include "xr_raylog.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The columns, in order: the frame, each hand's, then what was decided
#define RAYLOG_FRAME_COLS 10
#define RAYLOG_HAND_COLS 22
#define RAYLOG_OUT_COLS 4
#define RAYLOG_COLS (RAYLOG_FRAME_COLS + 2 * RAYLOG_HAND_COLS + RAYLOG_OUT_COLS)

static const char* const handCols[RAYLOG_HAND_COLS] = {
    "kind", "ok", "flags", "active", "x", "y", "z", "qx", "qy", "qz", "qw",
    "joints", "kx", "ky", "kz", "trigger", "grip", "sx", "sy", "buttons", "src", "awake"
};

// Appends to buf at *at, keeping track of whether it all fitted
static void put(char* buf, size_t size, size_t* at, int* fits, const char* fmt, ...)
        __attribute__((format(printf, 5, 6)));

static void put(char* buf, size_t size, size_t* at, int* fits, const char* fmt, ...) {
    if (!*fits) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *at, size - *at, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= size - *at) {
        *fits = 0;
        return;
    }
    *at += (size_t)n;
}

int rayLogHeader(char* buf, size_t size) {
    size_t at = 0;
    int fits = size > 0;
    put(buf, size, &at, &fits, "t_ns,to_head,head_flags,hx,hy,hz,hqx,hqy,hqz,hqw");
    static const char side[2] = { 'l', 'r' };
    for (int h = 0; h < 2; h++) {
        for (int c = 0; c < RAYLOG_HAND_COLS; c++) {
            put(buf, size, &at, &fits, ",%c_%s", side[h], handCols[c]);
        }
    }
    put(buf, size, &at, &fits, ",pointer,hand,ray,hover");
    return fits ? (int)at : 0;
}

int rayLogFormat(const RayLogRow* r, char* buf, size_t size) {
    size_t at = 0;
    int fits = size > 0;
    put(buf, size, &at, &fits, "%lld,%d,0x%x,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f",
        (long long)r->t, r->toHead, r->headFlags, r->head.position.x, r->head.position.y,
        r->head.position.z, r->head.orientation.x, r->head.orientation.y,
        r->head.orientation.z, r->head.orientation.w);
    for (int h = 0; h < 2; h++) {
        const RayLogHand* p = &r->hand[h];
        put(buf, size, &at, &fits,
            ",%d,%d,0x%x,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%d,%.4f,%.4f,%.4f"
            ",%.3f,%.3f,%.3f,%.3f,%u,%d,%d",
            p->kind, p->ok, p->flags, p->active, p->pose.position.x, p->pose.position.y,
            p->pose.position.z, p->pose.orientation.x, p->pose.orientation.y,
            p->pose.orientation.z, p->pose.orientation.w, p->joints, p->knuckle.x,
            p->knuckle.y, p->knuckle.z, p->trigger, p->grip, p->stick.x, p->stick.y,
            p->buttons, p->src, p->awake);
    }
    put(buf, size, &at, &fits, ",%d,%d,%d,%d", r->pointer, r->chosen, r->ray, r->hover);
    return fits ? (int)at : 0;
}

int rayLogParse(const char* line, RayLogRow* r) {
    if (line == NULL || line[0] < '0' || line[0] > '9') {
        // The header starts with a letter, a comment with #
        return 0;
    }
    double v[RAYLOG_COLS];
    long long t = 0;
    const char* p = line;
    for (int c = 0; c < RAYLOG_COLS; c++) {
        char* end = NULL;
        if (c == 0) {
            t = strtoll(p, &end, 10);
            v[c] = 0.0;
        }
        else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            v[c] = (double)strtoul(p, &end, 16);
        }
        else {
            v[c] = strtod(p, &end);
        }
        if (end == p) {
            return 0;
        }
        p = end;
        if (c < RAYLOG_COLS - 1) {
            if (*p != ',') {
                return 0;
            }
            p++;
        }
    }
    while (*p == '\r' || *p == '\n' || *p == ' ') {
        p++;
    }
    if (*p != '\0') {
        return 0;
    }

    memset(r, 0, sizeof(*r));
    r->t = t;
    r->toHead = (int)v[1];
    r->headFlags = (unsigned)v[2];
    r->head.position.x = (float)v[3];
    r->head.position.y = (float)v[4];
    r->head.position.z = (float)v[5];
    r->head.orientation.x = (float)v[6];
    r->head.orientation.y = (float)v[7];
    r->head.orientation.z = (float)v[8];
    r->head.orientation.w = (float)v[9];
    for (int h = 0; h < 2; h++) {
        const double* q = &v[RAYLOG_FRAME_COLS + h * RAYLOG_HAND_COLS];
        RayLogHand* o = &r->hand[h];
        o->kind = (int)q[0];
        o->ok = (int)q[1];
        o->flags = (unsigned)q[2];
        o->active = (int)q[3];
        o->pose.position.x = (float)q[4];
        o->pose.position.y = (float)q[5];
        o->pose.position.z = (float)q[6];
        o->pose.orientation.x = (float)q[7];
        o->pose.orientation.y = (float)q[8];
        o->pose.orientation.z = (float)q[9];
        o->pose.orientation.w = (float)q[10];
        o->joints = (int)q[11];
        o->knuckle.x = (float)q[12];
        o->knuckle.y = (float)q[13];
        o->knuckle.z = (float)q[14];
        o->trigger = (float)q[15];
        o->grip = (float)q[16];
        o->stick.x = (float)q[17];
        o->stick.y = (float)q[18];
        o->buttons = (unsigned)q[19];
        o->src = (int)q[20];
        o->awake = (int)q[21];
    }
    const double* out = &v[RAYLOG_FRAME_COLS + 2 * RAYLOG_HAND_COLS];
    r->pointer = (int)out[0];
    r->chosen = (int)out[1];
    r->ray = (int)out[2];
    r->hover = (int)out[3];
    return 1;
}
