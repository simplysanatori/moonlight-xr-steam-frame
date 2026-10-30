/* Network diagnostics for the stream: Wi-Fi power save and kernel UDP counters. See netdiag.h. GPLv3. */
#define _GNU_SOURCE
#include "netdiag.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "internal.h"
#include "netpath.h"

/* ---------------------------------------------------------------- power save */

int mlxr_wifi_ps_parse(const char *iw_output)
{
    if (!iw_output) return -1;
    const char *p = strcasestr(iw_output, "power save:");
    if (!p) return -1;
    p += strlen("power save:");
    while (*p == ' ' || *p == '\t') p++;
    if (!strncasecmp(p, "on", 2) && !isalnum((unsigned char)p[2])) return 1;
    if (!strncasecmp(p, "off", 3) && !isalnum((unsigned char)p[3])) return 0;
    return -1;
}

static const char *ps_word(int s)
{
    return s == 1 ? "on" : s == 0 ? "off" : "unknown";
}

void mlxr_wifi_ps_begin(mlxr_wifi_ps_guard *g, const char *host_addr)
{
    memset(g, 0, sizeof *g);
    mlxr_netifs *t = malloc(sizeof *t);
    if (!t) return;
    mlxr_netifs_read(t);
    const mlxr_netif *via = NULL;
    mlxr_net_classify(t, host_addr, &via);
    if (!via) {
        mlxr_log(1, "wifi power save: no network interface found for %s, not checked", host_addr ? host_addr : "(no address)");
        free(t);
        return;
    }
    char ifn[16];
    mlxr_strlcpy(ifn, via->name, sizeof ifn);
    bool wifi = via->wifi;
    int type = via->wifi_type;
    free(t);
    if (!wifi) {
        mlxr_log(1, "wifi power save: the stream goes over %s, which is not Wi-Fi, nothing to do", ifn);
        return;
    }
    if (type == 2) {
        mlxr_log(1, "wifi power save: the stream goes over %s, the access point side of the Steam Frame Wireless Adapter, "
                    "not a station, nothing to do", ifn);
        return;
    }
    int before = mlxr_wifi_ps_get(ifn);
    if (before < 0) {
        mlxr_log(1, "wifi power save: %s state could not be read (iw missing or failed), left as is", ifn);
        return;
    }
    mlxr_log(1, "wifi power save: %s is %s", ifn, ps_word(before));
    if (before == 0) return;
    if (mlxr_wifi_ps_set(ifn, false) && mlxr_wifi_ps_get(ifn) == 0) {
        mlxr_strlcpy(g->ifname, ifn, sizeof g->ifname);
        g->active = true;
        mlxr_log(1, "wifi power save: %s turned off for the stream, to be restored to on when it ends", ifn);
    } else {
        mlxr_log(1, "wifi power save: %s could not be turned off (needs CAP_NET_ADMIN; unprivileged, left as is; "
                    "as root: iw dev %s set power_save off)", ifn, ifn);
    }
}

void mlxr_wifi_ps_end(mlxr_wifi_ps_guard *g)
{
    if (!g->active) return;
    g->active = false;
    if (mlxr_wifi_ps_set(g->ifname, true))
        mlxr_log(1, "wifi power save: %s restored to on", g->ifname);
    else
        mlxr_log(1, "wifi power save: %s could not be restored to on (it stays off until reconnect or reboot)", g->ifname);
}

/* ---------------------------------------------------------------- /proc/net/snmp */

/* The whitespace separated token n (0-based) of a line; false when there are fewer. */
static bool token_at(const char *line, int n, char *out, size_t cap)
{
    const char *p = line;
    for (int k = 0;; k++) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '\n' || *p == '\r') return false;
        const char *s = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        if (k == n) {
            size_t l = (size_t)(p - s);
            if (l >= cap) l = cap - 1;
            memcpy(out, s, l);
            out[l] = 0;
            return true;
        }
    }
}

static const char *next_line(const char *p)
{
    const char *nl = strchr(p, '\n');
    return nl ? nl + 1 : p + strlen(p);
}

bool mlxr_udp_parse(const char *snmp, mlxr_udp_counters *out)
{
    memset(out, 0, sizeof *out);
    if (!snmp) return false;
    const char *p = snmp;
    while (*p) {
        const char *hdr = p;
        p = next_line(p);
        if (strncmp(hdr, "Udp: ", 5) != 0) continue;                 /* not "UdpLite:" */
        char tok[40];
        if (!token_at(hdr, 1, tok, sizeof tok) || !isalpha((unsigned char)tok[0])) continue;   /* a value line */
        const char *val = p;
        if (strncmp(val, "Udp: ", 5) != 0) return false;
        bool got_rcvbuf = false;
        for (int k = 1;; k++) {
            char name[40], num[40];
            if (!token_at(hdr, k, name, sizeof name)) break;
            if (!token_at(val, k, num, sizeof num)) break;
            char *end = NULL;
            unsigned long long v = strtoull(num, &end, 10);
            if (end == num) continue;
            if (!strcmp(name, "InDatagrams")) out->in_datagrams = v;
            else if (!strcmp(name, "InErrors")) out->in_errors = v;
            else if (!strcmp(name, "RcvbufErrors")) { out->rcvbuf_errors = v; got_rcvbuf = true; }
            else if (!strcmp(name, "MemErrors")) out->mem_errors = v;
            else if (!strcmp(name, "InCsumErrors")) out->csum_errors = v;
        }
        out->valid = got_rcvbuf;
        return got_rcvbuf;
    }
    return false;
}

/* A proc file's text (they report size 0, so read until the end), NULL when unreadable. Caller frees. */
static char *slurp_proc(const char *path, size_t max)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *buf = malloc(max + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = 0, got;
    while (n < max && (got = fread(buf + n, 1, max - n, f)) > 0) n += got;
    fclose(f);
    buf[n] = 0;
    return buf;
}

static bool udp_read_at(const char *path, mlxr_udp_counters *out)
{
    memset(out, 0, sizeof *out);
    char *buf = slurp_proc(path, 16383);
    if (!buf) return false;
    bool ok = mlxr_udp_parse(buf, out);
    free(buf);
    return ok;
}

bool mlxr_udp_read(mlxr_udp_counters *out)
{
    return udp_read_at("/proc/net/snmp", out);
}

static unsigned long long counter_delta(uint64_t prev, uint64_t cur)
{
    return cur >= prev ? cur - prev : cur;
}

void mlxr_udp_delta_text(const mlxr_udp_counters *prev, const mlxr_udp_counters *cur, char *out, size_t cap)
{
    if (!prev->valid || !cur->valid) {
        snprintf(out, cap, "udp counters unavailable");
        return;
    }
    snprintf(out, cap, "udp rcvbuf errors +%llu (system total %llu), in errors +%llu",
             counter_delta(prev->rcvbuf_errors, cur->rcvbuf_errors), (unsigned long long)cur->rcvbuf_errors,
             counter_delta(prev->in_errors, cur->in_errors));
}

/* ---------------------------------------------------------------- rmem_max */

long long mlxr_rmem_max_parse(const char *text)
{
    if (!text) return -1;
    while (*text == ' ' || *text == '\t') text++;
    if (!isdigit((unsigned char)*text)) return -1;
    char *end = NULL;
    long long v = strtoll(text, &end, 10);
    return end == text ? -1 : v;
}

long long mlxr_rmem_max_read(void)
{
    FILE *f = fopen("/proc/sys/net/core/rmem_max", "r");
    if (!f) return -1;
    char b[64];
    size_t n = fread(b, 1, sizeof b - 1, f);
    fclose(f);
    b[n] = 0;
    return mlxr_rmem_max_parse(b);
}

/* ---------------------------------------------------------------- rx drop misc (lane wave1-burst) */

bool mlxr_netdev_parse(const char *text, const char *ifname, uint64_t *errs, uint64_t *drop, uint64_t *fifo)
{
    if (!text || !ifname || !*ifname) return false;
    size_t want = strlen(ifname);
    for (const char *p = text; *p; p = next_line(p)) {
        const char *colon = strchr(p, ':');
        const char *nl = strchr(p, '\n');
        if (!colon || (nl && colon > nl)) continue;              /* the two header lines */
        const char *s = p;
        while (*s == ' ' || *s == '\t') s++;
        if ((size_t)(colon - s) != want || strncmp(s, ifname, want) != 0) continue;
        /* bytes packets errs drop fifo frame compressed multicast | transmit ... */
        unsigned long long v[5];
        const char *q = colon + 1;
        for (int k = 0; k < 5; k++) {
            char *end = NULL;
            v[k] = strtoull(q, &end, 10);
            if (end == q) return false;
            q = end;
        }
        *errs = v[2];
        *drop = v[3];
        *fifo = v[4];
        return true;
    }
    return false;
}

int mlxr_udp_sock_drops_parse(const char *text, const unsigned long *inodes, int n, uint64_t *drops)
{
    if (!text || strstr(text, "local_address") == NULL) return -1;
    int matched = 0;
    for (const char *p = text; *p; p = next_line(p)) {
        char tok[72];
        /* "  sl  local_address ..." is the header; entries start "  123:" */
        if (!token_at(p, 0, tok, sizeof tok) || tok[0] == 0 || tok[strlen(tok) - 1] != ':') continue;
        char ino[32], dr[32];
        /* sl local rem st tx:rx tr:tm retrnsmt uid timeout inode ref pointer drops */
        if (!token_at(p, 9, ino, sizeof ino) || !token_at(p, 12, dr, sizeof dr)) continue;
        unsigned long i = strtoul(ino, NULL, 10);
        for (int k = 0; k < n; k++) {
            if (inodes[k] != i || i == 0) continue;
            *drops += strtoull(dr, NULL, 10);
            matched++;
            break;
        }
    }
    return matched;
}

int mlxr_fd_socket_inodes(const char *fd_dir, unsigned long *out, int cap)
{
    DIR *d = opendir(fd_dir);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < cap) {
        if (e->d_name[0] == '.') continue;
        char path[512], link[64];
        snprintf(path, sizeof path, "%s/%s", fd_dir, e->d_name);
        ssize_t l = readlink(path, link, sizeof link - 1);
        if (l <= 0) continue;
        link[l] = 0;
        unsigned long ino = 0;
        if (sscanf(link, "socket:[%lu]", &ino) == 1 && ino) out[n++] = ino;
    }
    closedir(d);
    return n;
}

void mlxr_rx_misc_read(const char *proc_root, const char *ifname, mlxr_rx_misc *out)
{
    memset(out, 0, sizeof *out);
    char path[512];
    if (ifname && *ifname) {
        mlxr_strlcpy(out->ifname, ifname, sizeof out->ifname);
        snprintf(path, sizeof path, "%s/net/dev", proc_root);
        char *t = slurp_proc(path, 65535);
        if (t) {
            out->if_valid = mlxr_netdev_parse(t, ifname, &out->if_errs, &out->if_drop, &out->if_fifo);
            free(t);
        }
    }
    snprintf(path, sizeof path, "%s/net/snmp", proc_root);
    mlxr_udp_counters u;
    if (udp_read_at(path, &u)) {
        out->udp_valid = true;
        out->mem_errors = u.mem_errors;
        out->csum_errors = u.csum_errors;
    }
    unsigned long ino[256];
    snprintf(path, sizeof path, "%s/self/fd", proc_root);
    int n = mlxr_fd_socket_inodes(path, ino, (int)(sizeof ino / sizeof ino[0]));
    if (n < 0) return;
    const char *tables[] = { "net/udp", "net/udp6" };
    for (int k = 0; k < 2; k++) {
        snprintf(path, sizeof path, "%s/%s", proc_root, tables[k]);
        char *t = slurp_proc(path, 1 << 20);
        if (!t) continue;
        int m = mlxr_udp_sock_drops_parse(t, ino, n, &out->sock_drops);
        free(t);
        if (m < 0) continue;
        out->sock_valid = true;
        out->sockets += m;
    }
}

void mlxr_rx_misc_delta_text(const mlxr_rx_misc *prev, const mlxr_rx_misc *cur, char *out, size_t cap)
{
    unsigned long long total = 0;
    char ifp[96], udp[64], sock[64];
    if (prev->if_valid && cur->if_valid && !strcmp(prev->ifname, cur->ifname)) {
        unsigned long long dr = counter_delta(prev->if_drop, cur->if_drop), er = counter_delta(prev->if_errs, cur->if_errs),
                           fi = counter_delta(prev->if_fifo, cur->if_fifo);
        total += dr + er + fi;
        snprintf(ifp, sizeof ifp, "%s drop +%llu err +%llu fifo +%llu", cur->ifname, dr, er, fi);
    } else {
        snprintf(ifp, sizeof ifp, "%s ?", cur->ifname[0] ? cur->ifname : "interface");
    }
    if (prev->udp_valid && cur->udp_valid) {
        unsigned long long me = counter_delta(prev->mem_errors, cur->mem_errors), cs = counter_delta(prev->csum_errors, cur->csum_errors);
        total += me + cs;
        snprintf(udp, sizeof udp, "udp mem +%llu csum +%llu", me, cs);
    } else {
        snprintf(udp, sizeof udp, "udp mem ? csum ?");
    }
    if (prev->sock_valid && cur->sock_valid)
        snprintf(sock, sizeof sock, "own sockets +%llu of %d", counter_delta(prev->sock_drops, cur->sock_drops), cur->sockets);
    else
        snprintf(sock, sizeof sock, "own sockets ?");
    snprintf(out, cap, "rx drop misc +%llu (%s, %s, %s)", total, ifp, udp, sock);
}

bool mlxr_net_ifname_for(const char *addr, char *out, size_t cap)
{
    if (cap) out[0] = 0;
    if (!addr || !*addr) return false;
    mlxr_netifs *t = malloc(sizeof *t);
    if (!t) return false;
    mlxr_netifs_read(t);
    const mlxr_netif *via = NULL;
    mlxr_net_classify(t, addr, &via);
    bool ok = via != NULL;
    if (ok) mlxr_strlcpy(out, via->name, cap);
    free(t);
    return ok;
}
