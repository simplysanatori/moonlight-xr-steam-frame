/* Small helpers: errors, hex, random, files. GPLv3. */
#define _GNU_SOURCE
#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/rand.h>

static mlxr_log_fn g_log;
static void *g_log_ud;

void mlxr_set_log(mlxr_log_fn fn, void *ud)
{
    g_log = fn;
    g_log_ud = ud;
}

void mlxr_log(int level, const char *fmt, ...)
{
    if (!g_log) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_log(level, buf, g_log_ud);
}

const char *mlxr_status_name(mlxr_status s)
{
    static const char *const names[] = {
        "ok", "bad-argument", "out-of-memory", "io-error", "crypto-error", "network-error", "timeout",
        "tls-error", "http-status", "bad-response", "pin-wrong", "pairing-busy", "pairing-failed",
        "mitm-detected", "not-paired", "session-owned-by-other",
    };
    return (unsigned)s < sizeof names / sizeof *names ? names[s] : "unknown";
}

mlxr_status mlxr_fail(mlxr_error *err, mlxr_status code, int status, const char *fmt, ...)
{
    if (err) {
        err->code = code;
        err->status = status;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err->msg, sizeof err->msg, fmt, ap);
        va_end(ap);
    }
    return code;
}

void mlxr_hex_encode(const uint8_t *in, size_t n, char *out)
{
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 15];
    }
    out[2 * n] = 0;
}

char *mlxr_hex_encode_alloc(const uint8_t *in, size_t n)
{
    char *s = malloc(2 * n + 1);
    if (s) mlxr_hex_encode(in, n, s);
    return s;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint8_t *mlxr_hex_decode_alloc(const char *hex, size_t *outlen)
{
    if (!hex) return NULL;
    size_t n = strlen(hex);
    if (n == 0 || (n & 1)) return NULL;
    uint8_t *out = malloc(n / 2);
    if (!out) return NULL;
    for (size_t i = 0; i < n / 2; i++) {
        int a = hexval(hex[2 * i]), b = hexval(hex[2 * i + 1]);
        if (a < 0 || b < 0) {
            free(out);
            return NULL;
        }
        out[i] = (uint8_t)(a << 4 | b);
    }
    if (outlen) *outlen = n / 2;
    return out;
}

bool mlxr_random(void *buf, size_t n)
{
    return RAND_bytes(buf, (int)n) == 1;
}

char *mlxr_strdup(const char *s)
{
    return s ? strdup(s) : NULL;
}

size_t mlxr_strlcpy(char *dst, const char *src, size_t cap)
{
    size_t n = strlen(src);
    if (cap) {
        size_t c = n >= cap ? cap - 1 : n;
        memcpy(dst, src, c);
        dst[c] = 0;
    }
    return n;
}

void mlxr_trim(char *s)
{
    char *p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    size_t n = strlen(p);
    while (n && (p[n - 1] == ' ' || p[n - 1] == '\t' || p[n - 1] == '\r' || p[n - 1] == '\n')) n--;
    memmove(s, p, n);
    s[n] = 0;
}

mlxr_status mlxr_mkdir_p(const char *path, mode_t mode, mlxr_error *err)
{
    char tmp[1024];
    if (strlen(path) >= sizeof tmp) return mlxr_fail(err, MLXR_E_ARG, 0, "path too long");
    strcpy(tmp, path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0;
        if (mkdir(tmp, mode) != 0 && errno != EEXIST)
            return mlxr_fail(err, MLXR_E_IO, errno, "mkdir %s: %s", tmp, strerror(errno));
        *p = '/';
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST)
        return mlxr_fail(err, MLXR_E_IO, errno, "mkdir %s: %s", tmp, strerror(errno));
    return MLXR_OK;
}

bool mlxr_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

mlxr_status mlxr_read_file(const char *path, char **data, size_t *len, mlxr_error *err)
{
    FILE *f = fopen(path, "rb");
    if (!f) return mlxr_fail(err, MLXR_E_IO, errno, "open %s: %s", path, strerror(errno));
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap);
    while (buf) {
        size_t r = fread(buf + n, 1, cap - n - 1, f);
        n += r;
        if (r == 0) break;
        if (n + 1 >= cap) {
            if (cap > (64u << 20)) { free(buf); buf = NULL; break; }
            char *nb = realloc(buf, cap *= 2);
            if (!nb) { free(buf); buf = NULL; break; }
            buf = nb;
        }
    }
    bool bad = ferror(f);
    fclose(f);
    if (!buf || bad) {
        free(buf);
        return mlxr_fail(err, MLXR_E_IO, 0, "read %s failed", path);
    }
    buf[n] = 0;
    *data = buf;
    if (len) *len = n;
    return MLXR_OK;
}

mlxr_status mlxr_write_file(const char *path, const void *data, size_t len, mode_t mode, mlxr_error *err)
{
    char tmp[1100];
    if (snprintf(tmp, sizeof tmp, "%s.tmp.%d", path, (int)getpid()) >= (int)sizeof tmp)
        return mlxr_fail(err, MLXR_E_ARG, 0, "path too long");
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return mlxr_fail(err, MLXR_E_IO, errno, "create %s: %s", tmp, strerror(errno));
    fchmod(fd, mode);
    const char *p = data;
    size_t left = len;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            int e = errno;
            close(fd);
            unlink(tmp);
            return mlxr_fail(err, MLXR_E_IO, e, "write %s: %s", tmp, strerror(e));
        }
        p += w;
        left -= (size_t)w;
    }
    fsync(fd);
    close(fd);
    if (rename(tmp, path) != 0) {
        int e = errno;
        unlink(tmp);
        return mlxr_fail(err, MLXR_E_IO, e, "rename to %s: %s", path, strerror(e));
    }
    return MLXR_OK;
}
