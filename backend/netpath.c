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
    return p == MLXR_NET_HOME ? "home" : p == MLXR_NET_ADAPTER ? "adapter" : "auto";
}

const char *mlxr_net_path_label(mlxr_net_path p)
{
    return p == MLXR_PATH_HOME ? "Wi-Fi" : p == MLXR_PATH_ADAPTER ? "Steam Frame Wireless Adapter" : "none";
}

static const char *path_short(mlxr_net_path p)
{
    return p == MLXR_PATH_HOME ? "home" : p == MLXR_PATH_ADAPTER ? "adapter" : "none";
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
        if (warn) *warn = i == 1 ? "bad --network value (want auto, home or adapter)"
                          : i == 0 ? "bad MLXR_NETWORK value (want auto, home or adapter)"
                                   : "bad network_path in config.ini (want auto, home or adapter)";
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
    p->grace_ms = 300;
    p->chosen = -1;
    if (uuid) mlxr_strlcpy(p->uuid, uuid, sizeof p->uuid);
    if (uniqueid) mlxr_strlcpy(p->uniqueid, uniqueid, sizeof p->uniqueid);
}

int mlxr_net_plan_add(mlxr_net_plan *p, const char *address, uint16_t port)
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
    mlxr_net_plan_add(p, address, 0);
    mlxr_net_plan_add_csv(p, extra_addresses, 0);
}

void mlxr_net_plan_classify(mlxr_net_plan *p, const mlxr_netifs *t)
{
    for (int i = 0; i < p->n; i++) {
        const mlxr_netif *via = NULL;
        p->c[i].path = mlxr_net_classify(t, p->c[i].address, &via);
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

/* ---------------------------------------------------------------- scoring */

static int dcmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

void mlxr_net_cand_stats(mlxr_net_cand *c)
{
    c->median_ms = c->jitter_ms = 0;
    if (c->n_rtt <= 0) return;
    double s[MLXR_NET_MAX_SAMPLES];
    memcpy(s, c->rtt_ms, sizeof(double) * (size_t)c->n_rtt);
    qsort(s, (size_t)c->n_rtt, sizeof(double), dcmp);
    c->median_ms = c->n_rtt % 2 ? s[c->n_rtt / 2] : (s[c->n_rtt / 2 - 1] + s[c->n_rtt / 2]) / 2;
    double dev = 0;
    for (int i = 0; i < c->n_rtt; i++) dev += fabs(c->rtt_ms[i] - c->median_ms);
    c->jitter_ms = dev / c->n_rtt;
}

void mlxr_net_cand_judge(mlxr_net_cand *c, int bitrate_kbps)
{
    if (bitrate_kbps <= 0) bitrate_kbps = 80000;
    c->poor = false;
    if (!c->link.valid || !c->link.wifi) return;
    if (c->link.signal_dbm && c->link.signal_dbm < -70) c->poor = true;
    if (c->link.tx_mbps > 0 && c->link.tx_mbps < 4.0 * bitrate_kbps / 1000.0) c->poor = true;
}

static bool rtt_equal(const mlxr_net_cand *a, const mlxr_net_cand *b)
{
    double sa = a->median_ms + a->jitter_ms, sb = b->median_ms + b->jitter_ms;
    if (sa < 10.0 && sb < 10.0) return true;
    double lo = sa < sb ? sa : sb;
    double tol = 0.25 * lo > 5.0 ? 0.25 * lo : 5.0;
    return fabs(sa - sb) <= tol;
}

bool mlxr_net_better(const mlxr_net_cand *a, const mlxr_net_cand *b)
{
    if (a->failed != b->failed) return a->failed < b->failed;
    if (a->poor != b->poor) return !a->poor;
    if (!rtt_equal(a, b)) return a->median_ms + a->jitter_ms < b->median_ms + b->jitter_ms;
    if (fabs(a->link_quality - b->link_quality) > 5.0) return a->link_quality > b->link_quality;
    if (a->median_ms != b->median_ms) return a->median_ms < b->median_ms;
    return a->path == MLXR_PATH_ADAPTER && b->path != MLXR_PATH_ADAPTER;
}

static void link_text(const mlxr_net_cand *c, char *out, size_t cap)
{
    if (c->link.valid && c->link.wifi && c->link.signal_dbm)
        snprintf(out, cap, "%d dBm, %.0f Mbit/s", c->link.signal_dbm, c->link.tx_mbps);
    else if (c->link.valid && c->link.wifi)
        snprintf(out, cap, "%.0f Mbit/s", c->link.tx_mbps);
    else
        snprintf(out, cap, "no link data");
}

/* Why w beat l, in a few words */
static void why(const mlxr_net_cand *w, const mlxr_net_cand *l, char *out, size_t cap)
{
    char lt[64];
    const char *wn = mlxr_net_path_label(w->path), *ln = l->path == MLXR_PATH_ADAPTER ? "adapter" : "Wi-Fi";
    link_text(l, lt, sizeof lt);
    if (w->failed != l->failed) {
        snprintf(out, cap, "chose %s: %s dropped %d of %d probes", wn, ln, l->failed, l->sent);
    } else if (w->poor != l->poor) {
        snprintf(out, cap, "chose %s: %s link poor (%s)", wn, ln, lt);
    } else if (!rtt_equal(w, l)) {
        snprintf(out, cap, "chose %s: round trip %.1f ms against %.1f ms on %s", wn, w->median_ms, l->median_ms, ln);
    } else if (fabs(w->link_quality - l->link_quality) > 5.0) {
        char wt[64];
        link_text(w, wt, sizeof wt);
        snprintf(out, cap, "chose %s: better link (%s against %s on %s)", wn, wt, lt, ln);
    } else if (w->median_ms != l->median_ms) {
        snprintf(out, cap, "chose %s: lower round trip (%.1f ms against %.1f ms on %s), links alike", wn, w->median_ms,
                 l->median_ms, ln);
    } else {
        snprintf(out, cap, "chose %s: a full tie, the adapter has a link of its own", wn);
    }
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
    } else if (band[0]) {
        snprintf(out, cap, "%s (%s)", name, band);
    } else {
        snprintf(out, cap, "%s", name);
    }
}

int mlxr_net_choose(mlxr_net_plan *p)
{
    p->chosen = -1;
    p->chosen_path = MLXR_PATH_NONE;
    p->fell_back = false;
    p->toast[0] = p->connected[0] = p->chosen_desc[0] = 0;
    p->summary[0] = 0;
    p->reason[0] = 0;
    for (int i = 0; i < p->n; i++) {
        mlxr_net_cand_stats(&p->c[i]);
        mlxr_net_cand_judge(&p->c[i], p->bitrate_kbps);
    }
    if (p->pref == MLXR_NET_AUTO) {
        for (int i = 0; i < p->n; i++) {
            if (!answered(&p->c[i])) continue;
            if (p->chosen < 0 || mlxr_net_better(&p->c[i], &p->c[p->chosen])) p->chosen = i;
        }
        if (p->chosen >= 0) {
            /* the best answering candidate on another path is who it beat */
            int ru = -1;
            for (int i = 0; i < p->n; i++) {
                if (i == p->chosen || !answered(&p->c[i]) || p->c[i].path == p->c[p->chosen].path) continue;
                if (ru < 0 || mlxr_net_better(&p->c[i], &p->c[ru])) ru = i;
            }
            if (ru >= 0) why(&p->c[p->chosen], &p->c[ru], p->reason, sizeof p->reason);
            else snprintf(p->reason, sizeof p->reason, "chose %s: the only path that answered",
                          mlxr_net_path_label(p->c[p->chosen].path));
        }
    } else {
        mlxr_net_path want = p->pref == MLXR_NET_HOME ? MLXR_PATH_HOME : MLXR_PATH_ADAPTER;
        for (int i = 0; i < p->n && p->chosen < 0; i++)
            if (answered(&p->c[i]) && p->c[i].path == want) p->chosen = i;
        if (p->chosen < 0) {
            for (int i = 0; i < p->n && p->chosen < 0; i++)
                if (answered(&p->c[i])) p->chosen = i;
            if (p->chosen >= 0) {
                p->fell_back = true;
                snprintf(p->toast, sizeof p->toast, "%s not reachable. Using %s.", mlxr_net_path_label(want),
                         mlxr_net_path_label(p->c[p->chosen].path));
                snprintf(p->reason, sizeof p->reason, "chose %s: %s not reachable", mlxr_net_path_label(p->c[p->chosen].path),
                         mlxr_net_path_label(want));
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
        int64_t sample_deadline = now + 400000000LL;
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
            struct timespec gap = { 0, 15000000 };
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

void mlxr_net_log_plan(const mlxr_net_plan *p)
{
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
        if (!c->probed)
            mlxr_log(1, "network path: %s %s via %s: not probed; link: %s", mlxr_net_path_label(c->path), c->address, ifn, link);
        else if (c->ok > 0)
            mlxr_log(1, "network path: %s %s via %s: %d/%d ok, rtt median %.1f ms, jitter %.1f ms (serverinfo %.0f ms)%s%s; link: %s",
                     mlxr_net_path_label(c->path), c->address, ifn, c->ok, c->sent, c->median_ms, c->jitter_ms, c->info_ms,
                     c->failed ? ", last failure: " : "", c->failed ? c->error : "", link);
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

int mlxr_net_select(mlxr_net_plan *p, const mlxr_netifs *t)
{
    mlxr_netifs own;
    if (!t) {
        mlxr_netifs_read(&own);
        t = &own;
    }
    mlxr_net_plan_classify(p, t);
    mlxr_net_plan_read_links(p, t);
    if (p->pref == MLXR_NET_AUTO) {
        mlxr_net_probe(p, NULL, 7, false);
    } else {
        mlxr_net_path want = p->pref == MLXR_NET_HOME ? MLXR_PATH_HOME : MLXR_PATH_ADAPTER;
        bool which[MLXR_NET_MAX_CANDS] = { false };
        bool any = false;
        for (int i = 0; i < p->n; i++) any |= which[i] = p->c[i].path == want;
        if (any) mlxr_net_probe(p, which, 2, true);
        mlxr_net_choose(p);
        if (p->chosen < 0 || p->c[p->chosen].path != want) {
            /* nothing answered on the asked-for path: try the others */
            bool rest[MLXR_NET_MAX_CANDS] = { false };
            for (int i = 0; i < p->n; i++) rest[i] = !which[i];
            mlxr_net_probe(p, rest, 2, true);
        }
    }
    int r = mlxr_net_choose(p);
    mlxr_net_log_plan(p);
    return r;
}
