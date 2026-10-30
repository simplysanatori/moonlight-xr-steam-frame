/* Per-path stream history for the automatic network choice. See nethistory.h. GPLv3. */
#define _GNU_SOURCE
#include "nethistory.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "netpath.h"

#define HIST_FILE "netpath-history.ini"
#define HIST_MAX_TEXT 2048

const char *mlxr_nethist_key(int path)
{
    return path == MLXR_NETHIST_ADAPTER ? "adapter" : path == MLXR_NETHIST_USB ? "usb" : "home";
}

const char *mlxr_nethist_filename(void) { return HIST_FILE; }

double mlxr_nethist_loss_pct(const mlxr_nethist_entry *e)
{
    if (!e || !e->valid) return 0;
    double all = (double)e->frames + (double)e->lost;
    return all > 0 ? 100.0 * (double)e->lost / all : 0;
}

/* an unsigned integer, the whole value, with a sane upper bound */
static bool num_u64(const char *s, uint64_t *out)
{
    if (!*s || *s == '-') return false;
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || *end || v > 1000000000000ULL) return false;
    *out = v;
    return true;
}

static bool num_dbl(const char *s, double *out, double max)
{
    if (!*s) return false;
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (errno || *end || !isfinite(v) || v < 0 || v > max) return false;
    *out = v;
    return true;
}

int mlxr_nethist_parse(const char *text, mlxr_nethist *h)
{
    memset(h, 0, sizeof *h);
    if (!text) return 0;
    /* what was seen for each path: bit 0 frames, 1 lost, 2 jitter, 3 rtt, 4 time */
    unsigned seen[MLXR_NETHIST_PATHS] = { 0 };
    mlxr_nethist_entry tmp[MLXR_NETHIST_PATHS];
    memset(tmp, 0, sizeof tmp);
    int cur = -1;       /* -1: outside any known section */
    const char *line = text;
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        char b[128];
        if (len >= sizeof b) len = sizeof b - 1;
        memcpy(b, line, len);
        b[len] = 0;
        line = nl ? nl + 1 : line + len;
        char *s = b;
        while (*s == ' ' || *s == '\t') s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
        if (!*s || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            cur = -1;
            for (int p = 0; p < MLXR_NETHIST_PATHS; p++) {
                char want[24];
                snprintf(want, sizeof want, "[%s]", mlxr_nethist_key(p));
                if (!strcmp(s, want)) cur = p;
            }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq || cur < 0) continue;
        *eq = 0;
        char *k = s, *v = eq + 1;
        char *ke = k + strlen(k);
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = 0;
        while (*v == ' ' || *v == '\t') v++;
        uint64_t u;
        double d;
        if (!strcmp(k, "frames") && num_u64(v, &u)) { tmp[cur].frames = u; seen[cur] |= 1; }
        else if (!strcmp(k, "lost") && num_u64(v, &u)) { tmp[cur].lost = u; seen[cur] |= 2; }
        else if (!strcmp(k, "jitter_ms") && num_dbl(v, &d, 100000)) { tmp[cur].jitter_ms = d; seen[cur] |= 4; }
        else if (!strcmp(k, "rtt_ms") && num_dbl(v, &d, 100000)) { tmp[cur].rtt_ms = d; seen[cur] |= 8; }
        else if (!strcmp(k, "time") && num_u64(v, &u) && u > 0 && u < 100000000000ULL) { tmp[cur].time_s = (int64_t)u; seen[cur] |= 16; }
    }
    int n = 0;
    for (int p = 0; p < MLXR_NETHIST_PATHS; p++)
        if (seen[p] == 31 && tmp[p].frames + tmp[p].lost > 0) {
            h->e[p] = tmp[p];
            h->e[p].valid = true;
            n++;
        }
    return n;
}

int mlxr_nethist_format(const mlxr_nethist *h, char *out, int cap)
{
    int n = snprintf(out, (size_t)cap, "# Moonlight XR: how the last stream went on each network path. Numbers only.\n");
    for (int p = 0; p < MLXR_NETHIST_PATHS && n >= 0 && n < cap; p++) {
        const mlxr_nethist_entry *e = &h->e[p];
        if (!e->valid) continue;
        n += snprintf(out + n, (size_t)(cap - n),
                      "[%s]\nframes=%llu\nlost=%llu\njitter_ms=%.2f\nrtt_ms=%.2f\ntime=%lld\n", mlxr_nethist_key(p),
                      (unsigned long long)e->frames, (unsigned long long)e->lost, e->jitter_ms, e->rtt_ms,
                      (long long)e->time_s);
    }
    return n;
}

static void file_path(const char *dir, const char *suffix, char *out, size_t cap)
{
    snprintf(out, cap, "%s/%s%s", dir, HIST_FILE, suffix);
}

int mlxr_nethist_load(const char *dir, mlxr_nethist *h)
{
    memset(h, 0, sizeof *h);
    if (!dir || !*dir) return 0;
    char path[1100], text[HIST_MAX_TEXT];
    file_path(dir, "", path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[n] = 0;
    return mlxr_nethist_parse(text, h);
}

bool mlxr_nethist_save(const char *dir, const mlxr_nethist *h)
{
    if (!dir || !*dir) return false;
    char text[HIST_MAX_TEXT], path[1100], tmp[1110];
    int n = mlxr_nethist_format(h, text, sizeof text);
    if (n <= 0 || n >= (int)sizeof text) return false;
    file_path(dir, "", path, sizeof path);
    file_path(dir, ".tmp", tmp, sizeof tmp);
    FILE *f = fopen(tmp, "w");
    if (!f) return false;
    chmod(tmp, 0600);
    bool ok = fwrite(text, 1, (size_t)n, f) == (size_t)n;
    ok = fclose(f) == 0 && ok;
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) unlink(tmp);
    return ok;
}

bool mlxr_nethist_record(const char *dir, int path, const mlxr_nethist_entry *e)
{
    if (path < 0 || path >= MLXR_NETHIST_PATHS) return false;
    mlxr_nethist h;
    mlxr_nethist_load(dir, &h);
    h.e[path] = *e;
    h.e[path].valid = true;
    return mlxr_nethist_save(dir, &h);
}

bool mlxr_nethist_record_stream(const char *dir, const char *address, uint64_t frames, uint64_t lost, double jitter_ms,
                                double rtt_ms, int64_t now_s)
{
    if (!dir || !address || frames + lost < MLXR_NETHIST_MIN_FRAMES) return false;
    mlxr_netifs t;
    mlxr_netifs_read(&t);
    const mlxr_netif *via = NULL;
    mlxr_net_path p = mlxr_net_classify(&t, address, &via);
    if (p == MLXR_PATH_NONE) return false;
    /* Native (lane vpn-fallback): a stream over a VPN address says nothing about the home link it would be filed
     * under; it is left out */
    char rif[16] = "";
    if (mlxr_net_addr_is_vpn(address, via ? via->name : NULL, NULL) ||
        (mlxr_net_route_ifname(&t, address, rif, sizeof rif) && mlxr_net_ifname_is_vpn(rif)))
        return false;
    mlxr_nethist_entry e = { true, frames, lost, jitter_ms < 0 ? 0 : jitter_ms, rtt_ms < 0 ? 0 : rtt_ms,
                             now_s ? now_s : (int64_t)time(NULL) };
    return mlxr_nethist_record(dir, mlxr_net_hist_index(p), &e);
}
