/* Network diagnostics for the stream: Wi-Fi power save and kernel UDP counters. See netdiag.h. GPLv3. */
#define _GNU_SOURCE
#include "netdiag.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
        if (!token_at(hdr, 1, tok, sizeof tok) || strcmp(tok, "InDatagrams") != 0) continue;   /* a value line */
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
        }
        out->valid = got_rcvbuf;
        return got_rcvbuf;
    }
    return false;
}

bool mlxr_udp_read(mlxr_udp_counters *out)
{
    memset(out, 0, sizeof *out);
    FILE *f = fopen("/proc/net/snmp", "r");
    if (!f) return false;
    char *buf = malloc(16384);
    if (!buf) { fclose(f); return false; }
    size_t n = fread(buf, 1, 16383, f);
    fclose(f);
    buf[n] = 0;
    bool ok = mlxr_udp_parse(buf, out);
    free(buf);
    return ok;
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
