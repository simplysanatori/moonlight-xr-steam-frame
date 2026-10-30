/* Network path choice: candidates, scoring, probing. See netpath.h. GPLv3. */
#define _GNU_SOURCE
#include "netpath.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "internal.h"

/* ---------------------------------------------------------------- preference */

const char *mlxr_net_pref_name(mlxr_net_pref p)
{
    return p == MLXR_NET_HOME ? "home" : p == MLXR_NET_ADAPTER ? "adapter" : p == MLXR_NET_USB ? "usb" : "auto";
}

const char *mlxr_net_path_label(mlxr_net_path p)
{
    return p == MLXR_PATH_HOME ? "Wi-Fi" : p == MLXR_PATH_ADAPTER ? "Steam Frame Wireless Adapter"
         : p == MLXR_PATH_USB ? "USB cable" : "none";
}

static const char *path_short(mlxr_net_path p)
{
    return p == MLXR_PATH_HOME ? "home" : p == MLXR_PATH_ADAPTER ? "adapter" : p == MLXR_PATH_USB ? "usb" : "none";
}

bool mlxr_net_pref_parse(const char *s, mlxr_net_pref *out)
{
    if (!s) return false;
    while (*s == ' ' || *s == '\t') s++;
    char b[16];
    size_t n = 0;
    while (s[n] && s[n] != ' ' && s[n] != '\t' && s[n] != '\r' && s[n] != '\n' && n < sizeof b - 1) { b[n] = s[n]; n++; }
    b[n] = 0;
    const char *rest = s + n;
    while (*rest == ' ' || *rest == '\t' || *rest == '\r' || *rest == '\n') rest++;
    if (*rest) return false;
    if (!strcasecmp(b, "auto")) *out = MLXR_NET_AUTO;
    else if (!strcasecmp(b, "home")) *out = MLXR_NET_HOME;
    else if (!strcasecmp(b, "adapter")) *out = MLXR_NET_ADAPTER;
    else if (!strcasecmp(b, "usb")) *out = MLXR_NET_USB;
    else return false;
    return true;
}

mlxr_net_pref mlxr_net_pref_resolve(const char *flag, const char *env, const char *config,
                                    const char **source, const char **warn)
{
    const char *vals[3] = { env, flag, config };
    const char *names[3] = { "env", "flag", "config" };
    if (warn) *warn = NULL;
    if (source) *source = "default";
    for (int i = 0; i < 3; i++) {
        if (!vals[i] || !*vals[i]) continue;
        mlxr_net_pref p;
        if (mlxr_net_pref_parse(vals[i], &p)) {
            if (source) *source = names[i];
            return p;
        }
        if (warn) *warn = i == 1 ? "bad --network value (want auto, home, adapter or usb)"
                          : i == 0 ? "bad MLXR_NETWORK value (want auto, home, adapter or usb)"
                                   : "bad network_path in config.ini (want auto, home, adapter or usb)";
    }
    return MLXR_NET_AUTO;
}

/* ---------------------------------------------------------------- plan */

void mlxr_net_plan_init(mlxr_net_plan *p, mlxr_net_pref pref, const char *source, uint16_t http_port,
                        const char *uuid, const char *uniqueid)
{
    memset(p, 0, sizeof *p);
    p->pref = pref;
    p->source = source ? source : "default";
    p->default_port = http_port ? http_port : 47989;
    p->budget_ms = 1500;
    p->bitrate_kbps = 80000;
    p->retry_ms = 4000;
    p->grace_ms = 700;   /* Native (lane netpath-quality): the slower path needs time for its 15 probes */
    p->chosen = -1;
    if (uuid) mlxr_strlcpy(p->uuid, uuid, sizeof p->uuid);
    if (uniqueid) mlxr_strlcpy(p->uniqueid, uniqueid, sizeof p->uniqueid);
}

int mlxr_net_plan_add(mlxr_net_plan *p, const char *address, uint16_t port)
{
    return mlxr_net_plan_add_src(p, address, port, "");
}

int mlxr_net_plan_add_src(mlxr_net_plan *p, const char *address, uint16_t port, const char *source)
{
    if (!address) return -1;
    char a[128];
    mlxr_strlcpy(a, address, sizeof a);
    mlxr_trim(a);
    if (!a[0]) return -1;
    for (int i = 0; i < p->n; i++)
        if (!strcasecmp(p->c[i].address, a) && (p->c[i].port ? p->c[i].port : p->default_port) == (port ? port : p->default_port))
            return i;
    if (p->n >= MLXR_NET_MAX_CANDS) return -1;
    mlxr_net_cand *c = &p->c[p->n];
    memset(c, 0, sizeof *c);
    mlxr_strlcpy(c->address, a, sizeof c->address);
    mlxr_strlcpy(c->source, source ? source : "", sizeof c->source);
    c->port = port;
    return p->n++;
}

void mlxr_net_plan_add_csv(mlxr_net_plan *p, const char *csv, uint16_t port)
{
    if (!csv) return;
    char buf[512];
    mlxr_strlcpy(buf, csv, sizeof buf);
    char *save = NULL;
    for (char *t = strtok_r(buf, ", \t", &save); t; t = strtok_r(NULL, ", \t", &save)) mlxr_net_plan_add(p, t, port);
}

void mlxr_net_plan_add_record(mlxr_net_plan *p, const char *address, const char *extra_addresses)
{
    mlxr_net_plan_add_src(p, address, 0, "hosts.ini");
    if (extra_addresses) {
        char buf[512];
        mlxr_strlcpy(buf, extra_addresses, sizeof buf);
        char *save = NULL;
        for (char *t = strtok_r(buf, ", \t", &save); t; t = strtok_r(NULL, ", \t", &save))
            mlxr_net_plan_add_src(p, t, 0, "hosts.ini");
    }
}

void mlxr_net_plan_classify(mlxr_net_plan *p, const mlxr_netifs *t)
{
    for (int i = 0; i < p->n; i++) {
        const mlxr_netif *via = NULL;
        p->c[i].path = mlxr_net_classify(t, p->c[i].address, &via);
        p->c[i].onlink = mlxr_net_onlink_if(t, p->c[i].address) != NULL;
        p->c[i].ifname[0] = 0;
        if (via) mlxr_strlcpy(p->c[i].ifname, via->name, sizeof p->c[i].ifname);
    }
}

void mlxr_net_plan_read_links(mlxr_net_plan *p, const mlxr_netifs *t)
{
    for (int i = 0; i < p->n; i++) {
        mlxr_net_cand *c = &p->c[i];
        memset(&c->link, 0, sizeof c->link);
        const mlxr_netif *via = NULL;
        if (c->ifname[0])
            for (int k = 0; k < t->n; k++)
                if (!strcmp(t->v[k].name, c->ifname)) { via = &t->v[k]; break; }
        if (via) mlxr_net_link_read(via, c->address, &c->link);
        c->link_quality = mlxr_net_link_quality(&c->link);
    }
}

/* ---------------------------------------------------------------- the USB cable (Native, lane wave4c-usb) */

int mlxr_net_subnet_peers(uint32_t addr, uint32_t mask, uint32_t *out, int cap)
{
    if (!out || cap <= 0 || !mask) return 0;
    uint32_t inv = ~mask;
    if ((inv & (inv + 1u)) != 0) return 0;      /* ones then zeros, or it is no prefix */
    if (inv == 0 || inv > 0xffu) return 0;      /* a /32, or larger than a /24: a LAN, not a cable */
    int n = 0;
    if (inv == 1u) {                            /* a /31 (RFC 3021): both addresses are hosts */
        out[n++] = addr ^ 1u;
        return n;
    }
    uint32_t net = addr & mask;
    uint32_t first = net + 1u, last = (net | inv) - 1u;     /* the usable range */
    uint32_t count = last - first + 1u;
    bool self_in = addr >= first && addr <= last;
    uint32_t start = self_in ? addr - first : count - 1u;
    uint32_t total = self_in ? count - 1u : count;
    for (uint32_t k = 1; k <= total && n < cap && n < MLXR_NET_USB_MAX_PEERS; k++)
        out[n++] = first + (start + k) % count;
    return n;
}

void mlxr_net_usb_assess(const mlxr_netifs *t, mlxr_usb_info *out)
{
    memset(out, 0, sizeof *out);
    out->checked = true;
    const mlxr_netif *down = NULL;
    for (int k = 0; k < t->n; k++) {
        const mlxr_netif *i = &t->v[k];
        if (!i->usb || i->loopback) continue;
        if (!i->up) {
            if (!down) down = i;
            continue;
        }
        uint32_t peers[MLXR_NET_USB_MAX_PEERS];
        int np = mlxr_net_subnet_peers(i->addr, i->mask, peers, MLXR_NET_USB_MAX_PEERS);
        if (!out->present) {
            out->present = true;
            mlxr_strlcpy(out->ifname, i->name, sizeof out->ifname);
        }
        out->peers += np;
    }
    if (out->present) {
        if (out->peers > 0)
            snprintf(out->reason, sizeof out->reason, "%s up, carrier on, %d peer candidate%s", out->ifname, out->peers,
                     out->peers == 1 ? "" : "s");
        else
            snprintf(out->reason, sizeof out->reason, "%s up, carrier on, its subnet is too large to guess the PC's address",
                     out->ifname);
    } else if (down) {
        mlxr_strlcpy(out->ifname, down->name, sizeof out->ifname);
        snprintf(out->reason, sizeof out->reason, "%s down (no cable, or the PC end is not up)", down->name);
    } else {
        snprintf(out->reason, sizeof out->reason, "no USB network interface found");
    }
}

bool mlxr_net_usb_usable(const mlxr_usb_info *u)
{
    return !u->checked || u->present;
}

mlxr_usb_event mlxr_usb_watch_poll(mlxr_usb_watch *w, const mlxr_netifs *t, mlxr_usb_info *now)
{
    mlxr_usb_info u;
    mlxr_net_usb_assess(t, &u);
    mlxr_usb_event ev = MLXR_USB_EVENT_NONE;
    if (u.present && (!w->known || !w->present)) ev = MLXR_USB_EVENT_UP;
    else if (!u.present && w->known && w->present) {
        ev = MLXR_USB_EVENT_LOST;
        if (!u.ifname[0]) mlxr_strlcpy(u.ifname, w->ifname, sizeof u.ifname);
    }
    w->known = true;
    w->present = u.present;
    if (u.ifname[0]) mlxr_strlcpy(w->ifname, u.ifname, sizeof w->ifname);
    if (now) *now = u;
    return ev;
}

int mlxr_net_plan_add_usb(mlxr_net_plan *p, const mlxr_netifs *t)
{
    mlxr_net_usb_assess(t, &p->usb);
    int added = 0;
    for (int k = 0; k < t->n; k++) {
        const mlxr_netif *i = &t->v[k];
        if (!i->usb || !i->up || i->loopback) continue;
        uint32_t peers[MLXR_NET_USB_MAX_PEERS];
        int np = mlxr_net_subnet_peers(i->addr, i->mask, peers, MLXR_NET_USB_MAX_PEERS);
        for (int m = 0; m < np; m++) {
            char a[INET_ADDRSTRLEN];
            struct in_addr ia = { .s_addr = htonl(peers[m]) };
            if (!inet_ntop(AF_INET, &ia, a, sizeof a)) continue;
            int before = p->n;
            if (mlxr_net_plan_add_src(p, a, 0, "usb") == before && p->n > before) added++;
        }
    }
    return added;
}

int mlxr_net_hist_index(mlxr_net_path path)
{
    return path == MLXR_PATH_HOME ? MLXR_NETHIST_HOME : path == MLXR_PATH_ADAPTER ? MLXR_NETHIST_ADAPTER
         : path == MLXR_PATH_USB ? MLXR_NETHIST_USB : -1;
}

/* ---------------------------------------------------------------- the adapter (Native, lane adapter-detect) */

/* Complete entries of the neighbour table (/proc/net/arp text) on `ifname`; counts them and says
 * whether any has the address `ip` (when given). */
static int arp_scan(const char *arp, const char *ifname, const char *ip, bool *has_ip)
{
    int n = 0;
    if (has_ip) *has_ip = false;
    if (!arp) return 0;
    const char *line = arp;
    bool first = true;
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        char buf[256];
        if (len >= sizeof buf) len = sizeof buf - 1;
        memcpy(buf, line, len);
        buf[len] = 0;
        line = nl ? nl + 1 : line + len;
        if (first) { first = false; if (!strncmp(buf, "IP address", 10)) continue; }
        char a[64], hw[32], flags[16], mac[64], mask[32], dev[32];
        if (sscanf(buf, "%63s %31s %15s %63s %31s %31s", a, hw, flags, mac, mask, dev) != 6) continue;
        if (strcmp(dev, ifname)) continue;
        if (!(strtoul(flags, NULL, 16) & 2) || !strcmp(mac, "00:00:00:00:00:00")) continue;   /* incomplete */
        n++;
        if (ip && has_ip && !strcmp(a, ip)) *has_ip = true;
    }
    return n;
}

/* `iw dev <if> station dump`: one "Station <mac> (on <if>)" per station, "signal:" lines */
static int iw_scan(const char *dump, int *best_signal)
{
    int n = 0;
    *best_signal = 0;
    const char *line = dump;
    while (*line) {
        const char *nl = strchr(line, '\n');
        const char *t = line;
        const char *end = nl ? nl : line + strlen(line);
        while (t < end && (*t == ' ' || *t == '\t')) t++;
        size_t len = (size_t)(end - t);
        if (len >= 8 && !strncmp(t, "Station ", 8)) n++;
        else if (len >= 7 && !strncmp(t, "signal:", 7)) {
            int v = 0;
            if (sscanf(t + 7, " %d", &v) == 1 && v < 0 && (!*best_signal || v > *best_signal)) *best_signal = v;
        }
        if (!nl) break;
        line = nl + 1;
    }
    return n;
}

void mlxr_net_adapter_assess(const mlxr_adapter_inputs *in, const char *ifname, const char *const *pc_addrs, int n_pc,
                             mlxr_adapter_info *out)
{
    memset(out, 0, sizeof *out);
    out->checked = true;
    mlxr_strlcpy(out->ifname, ifname && ifname[0] ? ifname : "wlanap", sizeof out->ifname);
    const char *ifn = out->ifname;
    out->exists = in->exists;
    if (!in->exists) {
        out->state = MLXR_ADAPTER_NONE;
        snprintf(out->reason, sizeof out->reason, "%s missing", ifn);
        return;
    }
    /* up: operstate "up"; "unknown" (some drivers) counts when the carrier is on or not reported */
    if (!strncmp(in->operstate, "up", 2)) out->up = true;
    else if (!strncmp(in->operstate, "unknown", 7)) out->up = in->carrier[0] != '0';
    out->up = out->up && in->carrier[0] != '0';
    if (!out->up) {
        out->state = MLXR_ADAPTER_NONE;
        snprintf(out->reason, sizeof out->reason, "%s down", ifn);
        return;
    }
    int neigh = in->has_arp ? arp_scan(in->arp, ifn, NULL, NULL) : 0;
    if (in->has_iw) {
        out->stations = iw_scan(in->iw_dump, &out->signal_dbm);
        out->stations_known = true;
        mlxr_strlcpy(out->stations_from, "iw", sizeof out->stations_from);
    } else if (in->nl_stations >= 0) {
        out->stations = in->nl_stations;
        out->signal_dbm = in->nl_signal;
        out->stations_known = true;
        mlxr_strlcpy(out->stations_from, "nl80211", sizeof out->stations_from);
    } else if (neigh > 0) {
        out->stations = neigh;
        out->stations_known = true;
        mlxr_strlcpy(out->stations_from, "neighbours", sizeof out->stations_from);
    }
    for (int i = 0; i < n_pc && pc_addrs; i++) {
        if (!pc_addrs[i] || !pc_addrs[i][0]) continue;
        bool has = false;
        out->pc_known = true;
        arp_scan(in->arp, ifn, pc_addrs[i], &has);
        if (has && in->has_arp) out->pc_in_table = true;
    }
    if (!out->stations_known) {
        out->state = MLXR_ADAPTER_STATION;   /* cannot tell: keep the old behaviour */
        snprintf(out->reason, sizeof out->reason, "%s up, station list unavailable (assumed present)", ifn);
        return;
    }
    if (out->stations == 0) {
        out->state = MLXR_ADAPTER_LINK_ONLY;
        snprintf(out->reason, sizeof out->reason, "%s up, no station", ifn);
        return;
    }
    out->state = MLXR_ADAPTER_STATION;
    char sig[24] = "";
    if (out->signal_dbm) snprintf(sig, sizeof sig, " (%d dBm)", out->signal_dbm);
    snprintf(out->reason, sizeof out->reason, "%s up, %d station%s%s%s", ifn, out->stations, out->stations == 1 ? "" : "s", sig,
             !out->pc_known ? "" : out->pc_in_table ? ", PC address in the neighbour table"
                                                   : ", PC address not in the neighbour table");
}

void mlxr_net_adapter_text(const mlxr_adapter_info *a, char *out, size_t cap)
{
    if (!a->checked) snprintf(out, cap, "not checked");
    else if (a->state == MLXR_ADAPTER_STATION) snprintf(out, cap, "present (%s)", a->reason);
    else snprintf(out, cap, "no adapter found (%s)", a->reason);
}

bool mlxr_net_adapter_usable(const mlxr_adapter_info *a)
{
    return !a->checked || a->state == MLXR_ADAPTER_STATION;
}

void mlxr_net_plan_detect_adapter(mlxr_net_plan *p, const mlxr_netifs *t)
{
    const char *ifname = NULL;
    const char *addrs[MLXR_NET_MAX_CANDS];
    int na = 0;
    for (int i = 0; i < p->n; i++) {
        p->c[i].skipped = false;
        if (p->c[i].path != MLXR_PATH_ADAPTER) continue;
        if (!ifname && p->c[i].ifname[0]) ifname = p->c[i].ifname;
        addrs[na++] = p->c[i].address;
    }
    for (int k = 0; !ifname && t && k < t->n; k++)
        if (!t->v[k].loopback && mlxr_netif_is_ap(t, &t->v[k])) ifname = t->v[k].name;
    if (!ifname) ifname = "wlanap";
    if (p->adapter_inputs) mlxr_net_adapter_assess(p->adapter_inputs, ifname, addrs, na, &p->adapter);
    else mlxr_net_adapter_detect(ifname, addrs, na, &p->adapter);
    if (!mlxr_net_adapter_usable(&p->adapter))
        for (int i = 0; i < p->n; i++)
            if (p->c[i].path == MLXR_PATH_ADAPTER) p->c[i].skipped = true;
}

/* ---------------------------------------------------------------- scoring */

static int dcmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

void mlxr_net_cand_stats(mlxr_net_cand *c)
{
    c->median_ms = c->jitter_ms = c->p90_ms = 0;
    if (c->n_rtt <= 0) return;
    double s[MLXR_NET_MAX_SAMPLES];
    memcpy(s, c->rtt_ms, sizeof(double) * (size_t)c->n_rtt);
    qsort(s, (size_t)c->n_rtt, sizeof(double), dcmp);
    c->median_ms = c->n_rtt % 2 ? s[c->n_rtt / 2] : (s[c->n_rtt / 2 - 1] + s[c->n_rtt / 2]) / 2;
    /* Native (lane netpath-quality): nearest-rank 90th percentile */
    int r = (int)ceil(0.9 * c->n_rtt) - 1;
    c->p90_ms = s[r < 0 ? 0 : r >= c->n_rtt ? c->n_rtt - 1 : r];
    if (c->p90_ms < c->median_ms) c->p90_ms = c->median_ms;
    double dev = 0;
    for (int i = 0; i < c->n_rtt; i++) dev += fabs(c->rtt_ms[i] - c->median_ms);
    c->jitter_ms = dev / c->n_rtt;
}

/* ---------------------------------------------------------------- Native (lane netpath-quality): score and history */

double mlxr_net_hist_penalty(double pct)
{
    if (pct <= MLXR_NET_HIST_LOSS_FREE_PCT) return 0;
    double ms = MLXR_NET_HIST_MS_PER_PCT * pct;
    return ms > MLXR_NET_HIST_CAP_MS ? MLXR_NET_HIST_CAP_MS : ms;
}

/* the three parts of the score, ms */
static double latency_part(const mlxr_net_cand *c) { return c->median_ms + c->jitter_ms + (c->p90_ms - c->median_ms); }
static double failure_part(const mlxr_net_cand *c) { return MLXR_NET_FAIL_PENALTY_MS * c->failed; }

double mlxr_net_score(const mlxr_net_cand *c)
{
    return latency_part(c) + failure_part(c) + c->hist_penalty_ms;
}

void mlxr_net_plan_apply_history(mlxr_net_plan *p)
{
    int64_t now = p->now_s ? p->now_s : (int64_t)time(NULL);
    for (int i = 0; i < p->n; i++) {
        mlxr_net_cand *c = &p->c[i];
        c->hist_loss_pct = -1;
        c->hist_penalty_ms = 0;
        int hi = mlxr_net_hist_index(c->path);
        if (hi < 0) continue;
        const mlxr_nethist_entry *e = &p->hist.e[hi];
        if (!e->valid || e->frames + e->lost < MLXR_NETHIST_MIN_FRAMES) continue;
        if (now - e->time_s > MLXR_NETHIST_MAX_AGE_S) continue;
        c->hist_loss_pct = mlxr_nethist_loss_pct(e);
        c->hist_penalty_ms = mlxr_net_hist_penalty(c->hist_loss_pct);
    }
}

void mlxr_net_cand_judge(mlxr_net_cand *c, int bitrate_kbps)
{
    if (bitrate_kbps <= 0) bitrate_kbps = 80000;
    c->poor = false;
    if (!c->link.valid || !c->link.wifi) return;
    if (c->link.signal_dbm && c->link.signal_dbm < -70) c->poor = true;
    if (c->link.tx_mbps > 0 && c->link.tx_mbps < 4.0 * bitrate_kbps / 1000.0) c->poor = true;
}

/* the last word in a tie: Wi-Fi, then the cable, then the adapter */
static int tie_rank(mlxr_net_path p)
{
    return p == MLXR_PATH_HOME ? 0 : p == MLXR_PATH_USB ? 1 : 2;
}

bool mlxr_net_better(const mlxr_net_cand *a, const mlxr_net_cand *b)
{
    double sa = mlxr_net_score(a), sb = mlxr_net_score(b);
    if (a->path == b->path) return sa < sb;   /* two addresses on one path: the lower score */
    if (fabs(sa - sb) > MLXR_NET_TIE_MS) return sa < sb;
    /* a tie: the link decides if it is clearly different, else Wi-Fi */
    if (fabs(a->link_quality - b->link_quality) > 5.0) return a->link_quality > b->link_quality;
    return tie_rank(a->path) < tie_rank(b->path);
}

static bool answered(const mlxr_net_cand *c) { return c->probed && c->ok > 0 && !c->other_host; }

void mlxr_net_path_describe(mlxr_net_path p, const mlxr_net_link *l, char *out, size_t cap)
{
    const char *name = mlxr_net_path_label(p);
    char band[16] = "";
    if (l && l->band) snprintf(band, sizeof band, "%s GHz", l->band == 24 ? "2.4" : l->band == 5 ? "5" : "6");
    if (p == MLXR_PATH_HOME && l && l->valid && !l->wifi) {
        snprintf(out, cap, "Wired network");
    } else if (p == MLXR_PATH_HOME && l && l->ssid[0]) {
        snprintf(out, cap, "%s (%s%s%s)", name, l->ssid, band[0] ? ", " : "", band);
    } else if (p == MLXR_PATH_USB) {
        snprintf(out, cap, "%s", name);   /* Native (lane wave4c-usb): the cable has no band */
    } else if (band[0]) {
        snprintf(out, cap, "%s (%s)", name, band);
    } else {
        snprintf(out, cap, "%s", name);
    }
}

static int best_on(const mlxr_net_plan *p, mlxr_net_path path)
{
    int best = -1;
    for (int i = 0; i < p->n; i++) {
        if (!answered(&p->c[i]) || p->c[i].path != path) continue;
        if (best < 0 || mlxr_net_better(&p->c[i], &p->c[best])) best = i;
    }
    return best;
}

/* "192.168.1.10: connect timed out; ..." for the candidates on a path that did not answer */
static void failures_on(const mlxr_net_plan *p, mlxr_net_path path, char *out, size_t cap)
{
    out[0] = 0;
    size_t n = 0;
    for (int i = 0; i < p->n; i++) {
        const mlxr_net_cand *c = &p->c[i];
        if (c->path != path || answered(c)) continue;
        int w = snprintf(out + n, cap - n, "%s%s: %s", n ? "; " : "", c->address,
                         !c->probed ? "not tried" : c->error[0] ? c->error : "no answer");
        if (w < 0 || (size_t)w >= cap - n) break;
        n += (size_t)w;
    }
}

/* Native (lane netpath-quality): the words for an automatic choice between the two paths' best candidates.
 * Says both scores and what decided it: the part of the score where the winner is furthest ahead. */
static void pct_text(double pct, char *out, size_t cap)
{
    if (pct < 0) snprintf(out, cap, "no history");
    else snprintf(out, cap, "%.1f%% lost last time", pct);
}

static const char *short_name(mlxr_net_path p)
{
    return p == MLXR_PATH_HOME ? "Wi-Fi" : p == MLXR_PATH_ADAPTER ? "adapter" : "USB cable";
}

/* x and y: the best candidates of two different paths. The text names the lower path (Wi-Fi, adapter,
 * USB cable, in the order of the enum) first. */
static void auto_reason(mlxr_net_plan *p, int x, int y)
{
    int h = p->c[x].path < p->c[y].path ? x : y, a = h == x ? y : x;
    const mlxr_net_cand *H = &p->c[h], *A = &p->c[a];
    double sh = mlxr_net_score(H), sa = mlxr_net_score(A);
    bool second_wins = mlxr_net_better(A, H);
    const mlxr_net_cand *W = second_wins ? A : H, *L = second_wins ? H : A;
    const char *wn = short_name(W->path), *ln = short_name(L->path);
    const char *hn = short_name(H->path), *an = short_name(A->path);
    if (fabs(sh - sa) > MLXR_NET_TIE_MS) {
        double lat = latency_part(L) - latency_part(W), fail = failure_part(L) - failure_part(W);
        double hist = L->hist_penalty_ms - W->hist_penalty_ms;
        char why[160], t1[48], t2[48];
        if (fail >= lat && fail >= hist && fail > 0)
            snprintf(why, sizeof why, "failed probes (%s %d of %d, %s %d of %d)", ln, L->failed, L->sent, wn, W->failed, W->sent);
        else if (hist >= lat && hist > 0) {
            pct_text(L->hist_loss_pct, t1, sizeof t1);
            pct_text(W->hist_loss_pct, t2, sizeof t2);
            snprintf(why, sizeof why, "the last stream's losses (%s: %s; %s: %s)", ln, t1, wn, t2);
        } else
            snprintf(why, sizeof why, "latency (rtt %.1f ms against %.1f ms, jitter %.1f ms against %.1f ms)", W->median_ms,
                     L->median_ms, W->jitter_ms, L->jitter_ms);
        snprintf(p->reason, sizeof p->reason, "chose %s: score %.1f ms against %s %.1f ms (lower is better); decided by %s", wn,
                 mlxr_net_score(W), ln, mlxr_net_score(L), why);
    } else if (fabs(H->link_quality - A->link_quality) > 5.0) {
        snprintf(p->reason, sizeof p->reason,
                 "chose %s: scores within %.0f ms (%s %.1f, %s %.1f), the better link breaks the tie (quality %.0f against %.0f)",
                 wn, MLXR_NET_TIE_MS, hn, sh, an, sa, W->link_quality, L->link_quality);
    } else {
        snprintf(p->reason, sizeof p->reason, "chose %s: scores within %.0f ms (%s %.1f, %s %.1f), %s preferred on a tie",
                 wn, MLXR_NET_TIE_MS, hn, sh, an, sa, wn);
    }
}

static mlxr_net_path pref_path(mlxr_net_pref pref)
{
    return pref == MLXR_NET_HOME ? MLXR_PATH_HOME : pref == MLXR_NET_USB ? MLXR_PATH_USB : MLXR_PATH_ADAPTER;
}

int mlxr_net_choose(mlxr_net_plan *p)
{
    p->chosen = -1;
    p->chosen_path = MLXR_PATH_NONE;
    p->fell_back = false;
    p->toast[0] = p->connected[0] = p->chosen_desc[0] = p->failure[0] = 0;
    p->summary[0] = 0;
    p->reason[0] = 0;
    p->adapter_absent_fallback = false;
    p->usb_absent_fallback = false;   /* Native (lane wave4c-usb) */
    mlxr_net_plan_apply_history(p);   /* Native (lane netpath-quality) */
    for (int i = 0; i < p->n; i++) {
        mlxr_net_cand_stats(&p->c[i]);
        mlxr_net_cand_judge(&p->c[i], p->bitrate_kbps);
    }
    if (p->pref == MLXR_NET_AUTO) {
        int h = best_on(p, MLXR_PATH_HOME), a = best_on(p, MLXR_PATH_ADAPTER), u = best_on(p, MLXR_PATH_USB);
        char hf[160];
        failures_on(p, MLXR_PATH_HOME, hf, sizeof hf);
        /* Native (lane wave4c-usb): three paths now. The winner among the paths' best candidates, then the
         * runner-up (the best of the others) for the words. */
        const int bests[3] = { h, a, u };
        int answered_paths = 0, win = -1, runner = -1;
        for (int k = 0; k < 3; k++) {
            if (bests[k] < 0) continue;
            answered_paths++;
            if (win < 0 || mlxr_net_better(&p->c[bests[k]], &p->c[win])) win = bests[k];
        }
        for (int k = 0; k < 3; k++)
            if (bests[k] >= 0 && bests[k] != win && (runner < 0 || mlxr_net_better(&p->c[bests[k]], &p->c[runner])))
                runner = bests[k];
        if (answered_paths >= 2) {
            p->chosen = win;
            auto_reason(p, win, runner);
        } else if (u >= 0) {
            p->chosen = u;
            snprintf(p->reason, sizeof p->reason, "chose USB cable: the only path that answered (score %.1f ms)",
                     mlxr_net_score(&p->c[u]));
        } else if (h >= 0) {
            p->chosen = h;
            if (!mlxr_net_adapter_usable(&p->adapter))
                snprintf(p->reason, sizeof p->reason, "chose Wi-Fi: no Steam Frame Wireless Adapter found (%s)", p->adapter.reason);
            else
                snprintf(p->reason, sizeof p->reason, "chose Wi-Fi: the only path that answered (score %.1f ms)",
                         mlxr_net_score(&p->c[h]));
        } else if (a >= 0) {
            p->chosen = a;
            snprintf(p->reason, sizeof p->reason, "chose adapter: Wi-Fi did not answer (%s)", hf[0] ? hf : "no address of the PC known on that network");
        }
    } else {
        mlxr_net_path want = pref_path(p->pref);
        for (int i = 0; i < p->n && p->chosen < 0; i++)
            if (answered(&p->c[i]) && p->c[i].path == want) p->chosen = i;
        if (p->chosen < 0) {
            failures_on(p, want, p->failure, sizeof p->failure);
            /* Native (lane adapter-detect): the adapter was asked for and none is there */
            bool no_adapter = want == MLXR_PATH_ADAPTER && !mlxr_net_adapter_usable(&p->adapter);
            if (no_adapter) snprintf(p->failure, sizeof p->failure, "no adapter found: %s", p->adapter.reason);
            /* Native (lane wave4c-usb): the cable was asked for and no USB link is up */
            bool no_usb = want == MLXR_PATH_USB && !mlxr_net_usb_usable(&p->usb);
            if (no_usb) snprintf(p->failure, sizeof p->failure, "no USB cable found: %s", p->usb.reason);
            if (!p->failure[0]) snprintf(p->failure, sizeof p->failure, "no address of the PC known on this network");
            for (int i = 0; i < p->n && p->chosen < 0; i++)
                if (answered(&p->c[i])) p->chosen = i;
            if (p->chosen >= 0) {
                p->fell_back = true;
                p->adapter_absent_fallback = no_adapter;
                p->usb_absent_fallback = no_usb;
                snprintf(p->toast, sizeof p->toast, "%s not reachable (%s). Using %s.", mlxr_net_path_label(want), p->failure,
                         mlxr_net_path_label(p->c[p->chosen].path));
                snprintf(p->reason, sizeof p->reason, "chose %s: %s not reachable (%s)",
                         mlxr_net_path_label(p->c[p->chosen].path), mlxr_net_path_label(want), p->failure);
            }
        }
    }
    if (p->chosen >= 0) {
        p->chosen_path = p->c[p->chosen].path;
        if (!p->reason[0])
            snprintf(p->reason, sizeof p->reason, "chose %s: the setting is %s", mlxr_net_path_label(p->chosen_path),
                     mlxr_net_pref_name(p->pref));
        mlxr_net_path_describe(p->chosen_path, &p->c[p->chosen].link, p->chosen_desc, sizeof p->chosen_desc);
        snprintf(p->connected, sizeof p->connected, "Connected over %s.", p->chosen_desc);
        snprintf(p->summary, sizeof p->summary, "%s %s (%s%s)", path_short(p->chosen_path), p->c[p->chosen].address,
                 mlxr_net_pref_name(p->pref), p->fell_back ? ", fell back" : "");
    }
    return p->chosen;
}

const char *mlxr_net_chosen_address(const mlxr_net_plan *p, uint16_t *port)
{
    if (p->chosen < 0) return NULL;
    if (port) *port = p->c[p->chosen].port ? p->c[p->chosen].port : p->default_port;
    return p->c[p->chosen].address;
}

/* ---------------------------------------------------------------- probing */

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

typedef struct {
    mlxr_net_plan *plan;
    int idx, samples;
    int64_t deadline_ns;
    int *cancel;
} probe_job;

static void fail_text(char *err, size_t cap, const char *what, int e)
{
    if (e) snprintf(err, cap, "%s: %s", what, strerror(e));
    else snprintf(err, cap, "%s", what);
}

/* One TCP connection to the host's HTTP port: the connect time is the network round trip (the
 * kernel answers the handshake, so the PC's busy web server does not count; serverinfo itself takes
 * a quarter of a second on Apollo). With `request`, then a serverinfo request and its answer, which
 * checks that it is the PC and gives total_ms. 0 ok, -1 failed, -2 cancelled. */
static int http_sample(const char *addr, uint16_t port, const char *uniqueid, bool request, int64_t deadline_ns,
                       int *cancel, double *connect_ms, double *total_ms, char *host_uuid, size_t uuid_cap, char *err,
                       size_t errcap)
{
    host_uuid[0] = 0;
    char ps[8];
    snprintf(ps, sizeof ps, "%u", port);
    struct addrinfo hints = { 0 }, *ai = NULL;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    if (getaddrinfo(addr, ps, &hints, &ai) != 0 || !ai) {
        fail_text(err, errcap, "not an IP address", 0);
        return -1;
    }
    int64_t t0 = now_ns();
    int fd = socket(ai->ai_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        fail_text(err, errcap, "socket", errno);
        freeaddrinfo(ai);
        return -1;
    }
    int rc = -1;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0 && errno != EINPROGRESS) {
        fail_text(err, errcap, "connect", errno);
        goto out;
    }
    char req[384];
    int rl = snprintf(req, sizeof req,
                      "GET /serverinfo?uniqueid=%s&uuid=00000000000000000000000000000000 HTTP/1.1\r\n"
                      "Host: %s%s%s:%u\r\nConnection: close\r\nUser-Agent: moonlight-xr-netpath\r\n\r\n",
                      uniqueid && uniqueid[0] ? uniqueid : "0123456789ABCDEF", strchr(addr, ':') ? "[" : "", addr, strchr(addr, ':') ? "]" : "", port);
    enum { CONNECTING, SENDING, READING } st = CONNECTING;
    int sent = 0;
    char buf[32768];
    size_t got = 0;
    for (;;) {
        if (*(volatile int *)cancel) { rc = -2; goto out; }
        int64_t now = now_ns();
        if (now >= deadline_ns) {
            fail_text(err, errcap, st == CONNECTING ? "connect timed out" : "no answer in time", 0);
            goto out;
        }
        int slice = (int)((deadline_ns - now) / 1000000) + 1;
        if (slice > 20) slice = 20;
        struct pollfd pf = { fd, st == READING ? POLLIN : POLLOUT, 0 };
        int pr = poll(&pf, 1, slice);
        if (pr < 0 && errno != EINTR) { fail_text(err, errcap, "poll", errno); goto out; }
        if (pr <= 0) continue;
        if (st == CONNECTING) {
            int soerr = 0;
            socklen_t sl = sizeof soerr;
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
            if (soerr) { fail_text(err, errcap, "connect", soerr); goto out; }
            *connect_ms = (double)(now_ns() - t0) / 1e6;
            if (!request) { rc = 0; goto out; }
            st = SENDING;
        }
        if (st == SENDING) {
            ssize_t w = send(fd, req + sent, (size_t)(rl - sent), MSG_NOSIGNAL);
            if (w < 0 && errno != EAGAIN) { fail_text(err, errcap, "send", errno); goto out; }
            if (w > 0) sent += (int)w;
            if (sent >= rl) st = READING;
            continue;
        }
        ssize_t r = recv(fd, buf + got, sizeof buf - 1 - got, 0);
        if (r < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            fail_text(err, errcap, "recv", errno);
            goto out;
        }
        got += (size_t)(r > 0 ? r : 0);
        buf[got] = 0;
        if (r == 0 || strstr(buf, "</root>") || got >= sizeof buf - 1) break;
    }
    *total_ms = (double)(now_ns() - t0) / 1e6;
    if (!(!strncmp(buf, "HTTP/1.1 200", 12) || !strncmp(buf, "HTTP/1.0 200", 12))) {
        fail_text(err, errcap, "HTTP status is not 200", 0);
        goto out;
    }
    if (!strstr(buf, "<root")) { fail_text(err, errcap, "not a serverinfo answer", 0); goto out; }
    char *u = strstr(buf, "<uniqueid>");
    if (u) {
        u += 10;
        char *e = strstr(u, "</uniqueid>");
        if (e && (size_t)(e - u) < uuid_cap) { memcpy(host_uuid, u, (size_t)(e - u)); host_uuid[e - u] = 0; }
    }
    rc = 0;
out:
    close(fd);
    freeaddrinfo(ai);
    return rc;
}

static void *probe_thread(void *arg)
{
    probe_job *j = arg;
    mlxr_net_plan *p = j->plan;
    mlxr_net_cand *c = &p->c[j->idx];
    uint16_t port = c->port ? c->port : p->default_port;
    for (int s = 0; s < j->samples; s++) {
        int64_t now = now_ns();
        if (*(volatile int *)j->cancel || now >= j->deadline_ns) break;
        /* Native (lane netpath-quality): the serverinfo sample may take 400 ms; a bare handshake that takes
         * over 250 ms is a failure (a lost SYN waits a second to be resent), and it is charged as one */
        int64_t sample_deadline = now + (s == 0 ? 400000000LL : 250000000LL);
        if (sample_deadline > j->deadline_ns) sample_deadline = j->deadline_ns;
        double ms = 0, total = 0;
        char hu[64], err[96] = "";
        /* the first sample asks serverinfo (is it the PC?), the others only time the handshake */
        int r = http_sample(c->address, port, p->uniqueid, s == 0, sample_deadline, j->cancel, &ms, &total, hu,
                            sizeof hu, err, sizeof err);
        if (r == -2) break;
        c->sent++;
        if (r == 0 && p->uuid[0] && hu[0] && strcasecmp(hu, p->uuid) != 0) {
            c->other_host = true;
            snprintf(err, sizeof err, "answers as another PC (%s)", hu);
            r = -1;
            c->failed++;
            mlxr_strlcpy(c->error, err, sizeof c->error);
            break;   /* no point timing a link to the wrong PC */
        }
        if (r == 0) {
            c->ok++;
            if (s == 0) c->info_ms = total;
            if (c->n_rtt < MLXR_NET_MAX_SAMPLES) c->rtt_ms[c->n_rtt++] = ms;
        } else {
            c->failed++;
            mlxr_strlcpy(c->error, err, sizeof c->error);
        }
        if (s + 1 < j->samples) {
            struct timespec gap = { 0, 10000000 };
            nanosleep(&gap, NULL);
        }
    }
    c->probed = true;
    __atomic_store_n(&c->done, true, __ATOMIC_RELEASE);
    return NULL;
}

void mlxr_net_probe(mlxr_net_plan *p, const bool *which, int samples, bool first_wins)
{
    if (samples < 1) samples = 1;
    if (samples > MLXR_NET_MAX_SAMPLES) samples = MLXR_NET_MAX_SAMPLES;
    int64_t deadline = now_ns() + (int64_t)p->budget_ms * 1000000LL;
    pthread_t th[MLXR_NET_MAX_CANDS];
    bool started[MLXR_NET_MAX_CANDS] = { false };
    probe_job job[MLXR_NET_MAX_CANDS];
    int cancel = 0;
    for (int i = 0; i < p->n; i++) {
        mlxr_net_cand *c = &p->c[i];
        if (which && !which[i]) continue;
        c->probed = c->done = false;
        c->sent = c->ok = c->failed = c->n_rtt = 0;
        c->error[0] = 0;
        c->other_host = false;
        job[i] = (probe_job){ p, i, samples, deadline, &cancel };
        started[i] = pthread_create(&th[i], NULL, probe_thread, &job[i]) == 0;
        if (!started[i]) { c->probed = true; snprintf(c->error, sizeof c->error, "no thread"); c->failed = 1; }
    }
    int64_t first_answer_ns = 0;
    for (;;) {
        bool all = true, win = false;
        for (int i = 0; i < p->n; i++) {
            if (!started[i]) continue;
            if (!__atomic_load_n(&p->c[i].done, __ATOMIC_ACQUIRE)) { all = false; if (first_wins) break; continue; }
            if (first_wins && p->c[i].ok > 0 && !p->c[i].other_host) { win = true; break; }
        }
        if (all || win) break;
        if (!first_wins) {
            /* auto: one path answered, the other is given a moment, not the whole budget */
            bool got = false;
            for (int i = 0; i < p->n; i++)
                if (started[i] && __atomic_load_n(&p->c[i].done, __ATOMIC_ACQUIRE) && p->c[i].ok > 0) got = true;
            if (got && !first_answer_ns) first_answer_ns = now_ns();
            if (first_answer_ns && now_ns() - first_answer_ns > (int64_t)p->grace_ms * 1000000LL) break;
        }
        struct timespec nap = { 0, 3000000 };
        nanosleep(&nap, NULL);
    }
    *(volatile int *)&cancel = 1;
    for (int i = 0; i < p->n; i++)
        if (started[i]) pthread_join(th[i], NULL);
}

/* ---------------------------------------------------------------- orchestration */

/* Native (lane netpath-quality): "history: Wi-Fi lost 0.0% of 17510 frames last time (jitter 1.2 ms, rtt 3 ms, 2 days ago); ..." */
static void log_history(const mlxr_net_plan *p)
{
    char line[400];
    size_t n = 0;
    line[0] = 0;
    int64_t now = p->now_s ? p->now_s : (int64_t)time(NULL);
    for (int k = 0; k < MLXR_NETHIST_PATHS; k++) {
        const mlxr_nethist_entry *e = &p->hist.e[k];
        if (!e->valid) continue;
        double pct = mlxr_nethist_loss_pct(e);
        double age_h = (double)(now - e->time_s) / 3600.0;
        char ago[32], note[48] = "";
        if (age_h < 0) age_h = 0;
        if (age_h < 48) snprintf(ago, sizeof ago, "%.0f h ago", age_h);
        else snprintf(ago, sizeof ago, "%.0f days ago", age_h / 24.0);
        if (now - e->time_s > MLXR_NETHIST_MAX_AGE_S) snprintf(note, sizeof note, ", too old to count");
        else if (e->frames + e->lost < MLXR_NETHIST_MIN_FRAMES) snprintf(note, sizeof note, ", too short to count");
        else if (mlxr_net_hist_penalty(pct) > 0) snprintf(note, sizeof note, ", penalty %.0f ms", mlxr_net_hist_penalty(pct));
        int w = snprintf(line + n, sizeof line - n, "%s%s lost %.1f%% of %llu frames last time (jitter %.1f ms, rtt %.0f ms, %s%s)",
                         n ? "; " : "", k == MLXR_NETHIST_ADAPTER ? "adapter" : k == MLXR_NETHIST_USB ? "USB cable" : "Wi-Fi", pct,
                         (unsigned long long)(e->frames + e->lost), e->jitter_ms, e->rtt_ms, ago, note);
        if (w < 0 || (size_t)w >= sizeof line - n) break;
        n += (size_t)w;
    }
    if (n) mlxr_log(1, "network path: history: %s", line);
}

void mlxr_net_log_plan(const mlxr_net_plan *p)
{
    {
        char list[512];
        size_t n = 0;
        list[0] = 0;
        for (int i = 0; i < p->n && n < sizeof list - 1; i++) {
            const mlxr_net_cand *c = &p->c[i];
            int w = snprintf(list + n, sizeof list - n, "%s%s (%s, %s%s via %s)", n ? ", " : "", c->address,
                             c->source[0] ? c->source : "?", c->onlink ? "" : "off-link, ", mlxr_net_path_label(c->path),
                             c->ifname[0] ? c->ifname : "?");
            if (w < 0 || (size_t)w >= sizeof list - n) break;
            n += (size_t)w;
        }
        mlxr_log(1, "network path: setting %s from %s; %d candidate address%s: %s", mlxr_net_pref_name(p->pref), p->source,
                 p->n, p->n == 1 ? "" : "es", list);
    }
    if (p->adapter.checked) {
        char at[192];
        mlxr_net_adapter_text(&p->adapter, at, sizeof at);
        mlxr_log(1, "network path: %s: %s", mlxr_net_path_label(MLXR_PATH_ADAPTER), at);
    }
    /* Native (lane wave4c-usb): the cable, when there is one or it was asked for */
    if (p->usb.checked && (p->usb.present || p->usb.ifname[0] || p->pref == MLXR_NET_USB))
        mlxr_log(1, "network path: %s: %s", mlxr_net_path_label(MLXR_PATH_USB), p->usb.reason);
    log_history(p);
    for (int i = 0; i < p->n; i++) {
        const mlxr_net_cand *c = &p->c[i];
        char link[112] = "";
        if (c->link.valid && !c->link.wifi) {
            snprintf(link, sizeof link, "wired");
        } else if (c->link.valid) {
            char sig[24] = "signal ?";
            if (c->link.signal_dbm) snprintf(sig, sizeof sig, "%d dBm", c->link.signal_dbm);
            snprintf(link, sizeof link, "%s, tx %.0f Mbit/s%s%s, quality %.0f", sig, c->link.tx_mbps,
                     c->link.tx_desc[0] ? " " : "", c->link.tx_desc, c->link_quality);
        } else {
            snprintf(link, sizeof link, "no link data");
        }
        const char *ifn = c->ifname[0] ? c->ifname : "?";
        if (c->skipped)
            mlxr_log(1, "network path: %s %s via %s: not probed, no adapter found; link: %s", mlxr_net_path_label(c->path), c->address, ifn, link);
        else if (!c->probed)
            mlxr_log(1, "network path: %s %s via %s: not probed; link: %s", mlxr_net_path_label(c->path), c->address, ifn, link);
        else if (c->ok > 0)
            mlxr_log(1, "network path: %s %s via %s: %d/%d ok, rtt median %.1f ms, p90 %.1f ms, jitter %.1f ms (serverinfo %.0f ms)%s%s; score %.1f ms; link: %s",
                     mlxr_net_path_label(c->path), c->address, ifn, c->ok, c->sent, c->median_ms, c->p90_ms, c->jitter_ms, c->info_ms,
                     c->failed ? ", last failure: " : "", c->failed ? c->error : "", mlxr_net_score(c), link);
        else
            mlxr_log(1, "network path: %s %s via %s: no answer (%d/%d failed: %s); link: %s", path_short(c->path),
                     c->address, ifn, c->failed, c->sent, c->error[0] ? c->error : "not tried", link);
    }
    if (p->chosen >= 0)
        mlxr_log(1, "network path: %s", p->reason);
    if (p->chosen >= 0)
        mlxr_log(1, "network path: using %s %s via %s (setting %s from %s)%s%s", p->chosen_desc,
                 p->c[p->chosen].address, p->c[p->chosen].ifname[0] ? p->c[p->chosen].ifname : "?", mlxr_net_pref_name(p->pref), p->source, p->fell_back ? ": " : "", p->toast);
    else
        mlxr_log(0, "network path: no candidate answered (setting %s from %s, %d addresses)",
                 mlxr_net_pref_name(p->pref), p->source, p->n);
}

static int64_t mono_ms(void) { return now_ns() / 1000000; }

/* Wi-Fi may not be ready yet, or an AP asleep: probe the asked-for path again for retry_ms */
static bool retry_path(mlxr_net_plan *p, const bool *which)
{
    int64_t end = mono_ms() + p->retry_ms;
    int saved = p->budget_ms;
    bool got = false;
    while (!got && mono_ms() < end) {
        int64_t left = end - mono_ms();
        p->budget_ms = left < 1500 ? (int)left + 50 : 1500;
        mlxr_net_probe(p, which, 2, true);
        for (int i = 0; i < p->n; i++)
            if (which[i] && answered(&p->c[i])) got = true;
        if (!got) {
            struct timespec nap = { 0, 300000000 };
            nanosleep(&nap, NULL);
        }
    }
    p->budget_ms = saved;
    return got;
}

/* Other addresses of the PC: the ARP table and mDNS. mDNS only where an up interface has no
 * candidate on its subnet (the PC may live there under an address nobody told us). */
static void discover(mlxr_net_plan *p, const mlxr_netifs *t)
{
    if (!p->has_mac && !p->hostname[0]) return;
    bool gap = false;
    for (int k = 0; k < t->n; k++) {
        const mlxr_netif *f = &t->v[k];
        if (!f->up || f->loopback) continue;
        bool have = false;
        for (int i = 0; i < p->n; i++) {
            const mlxr_netif *o = mlxr_net_onlink_if(t, p->c[i].address);
            if (o && !strcmp(o->name, f->name)) have = true;
        }
        if (!have) gap = true;
    }
    char found[8][128];
    const char *src[8];
    int n = mlxr_net_discover(t, gap ? p->hostname : "", p->has_mac ? p->mac : NULL, gap ? 400 : 0, found, src, 8);
    for (int i = 0; i < n; i++) mlxr_net_plan_add_src(p, found[i], 0, src[i]);
}

/* Native (lane wave4b-abr): the kind of the path each chosen address went over, so the stream start can find
 * the bitrate for it without the plan (which is gone by then). The last few choices, newest wins. */
#define KIND_SLOTS 8
static pthread_mutex_t g_kind_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { char addr[128]; int kind; } g_kinds[KIND_SLOTS];
static int g_kind_next;

static void kind_note(const char *addr, int kind)
{
    pthread_mutex_lock(&g_kind_lock);
    int slot = -1;
    for (int i = 0; i < KIND_SLOTS && slot < 0; i++)
        if (!strcasecmp(g_kinds[i].addr, addr)) slot = i;
    if (slot < 0) {
        slot = g_kind_next;
        g_kind_next = (g_kind_next + 1) % KIND_SLOTS;
    }
    snprintf(g_kinds[slot].addr, sizeof g_kinds[slot].addr, "%s", addr);
    g_kinds[slot].kind = kind;
    pthread_mutex_unlock(&g_kind_lock);
}

int mlxr_net_bitrate_kind(mlxr_net_path p, const mlxr_net_link *l)
{
    switch (p) {
    case MLXR_PATH_HOME: return l && l->valid && !l->wifi ? 2 : 0;
    case MLXR_PATH_ADAPTER: return 1;
    case MLXR_PATH_USB: return 3;
    default: return -1;
    }
}

int mlxr_net_kind_for_address(const char *addr)
{
    int kind = -1;
    if (!addr || !*addr) return kind;
    pthread_mutex_lock(&g_kind_lock);
    for (int i = 0; i < KIND_SLOTS; i++)
        if (g_kinds[i].addr[0] && !strcasecmp(g_kinds[i].addr, addr)) kind = g_kinds[i].kind;
    pthread_mutex_unlock(&g_kind_lock);
    return kind;
}

int mlxr_net_select(mlxr_net_plan *p, const mlxr_netifs *t)
{
    mlxr_netifs own;
    if (!t) {
        mlxr_netifs_read(&own);
        t = &own;
    }
    /* Native (lane netpath-quality): how the last streams went on each path */
    if (p->history_dir[0]) mlxr_nethist_load(p->history_dir, &p->hist);
    mlxr_net_plan_add_usb(p, t);   /* Native (lane wave4c-usb): the PC's side of an up USB cable, before discovery */
    discover(p, t);
    mlxr_net_plan_classify(p, t);
    mlxr_net_plan_read_links(p, t);
    mlxr_net_plan_detect_adapter(p, t);   /* Native (lane adapter-detect): no station, no adapter probe */
    if (p->pref == MLXR_NET_AUTO) {
        bool live[MLXR_NET_MAX_CANDS] = { false };
        for (int i = 0; i < p->n; i++) live[i] = !p->c[i].skipped;
        mlxr_net_probe(p, live, MLXR_NET_AUTO_SAMPLES, false);
    } else {
        mlxr_net_path want = pref_path(p->pref);
        bool which[MLXR_NET_MAX_CANDS] = { false };
        bool any = false;
        for (int i = 0; i < p->n; i++) any |= which[i] = p->c[i].path == want && !p->c[i].skipped;
        bool got = false;
        if (any) {
            mlxr_net_probe(p, which, 2, true);
            for (int i = 0; i < p->n; i++)
                if (which[i] && answered(&p->c[i])) got = true;
            if (!got) {
                mlxr_log(1, "network path: nothing answered on %s yet, retrying for %d ms", mlxr_net_path_label(want),
                         p->retry_ms);
                got = retry_path(p, which);
            }
        }
        if (!got) {
            /* only now the other path */
            bool rest[MLXR_NET_MAX_CANDS] = { false };
            for (int i = 0; i < p->n; i++) rest[i] = !which[i] && !p->c[i].skipped;
            mlxr_net_probe(p, rest, 2, true);
        }
    }
    int r = mlxr_net_choose(p);
    mlxr_net_log_plan(p);
    if (r >= 0) kind_note(p->c[r].address, mlxr_net_bitrate_kind(p->chosen_path, &p->c[r].link));
    return r;
}
