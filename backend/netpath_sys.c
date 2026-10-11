/* Network path choice: the system side. Interfaces (getifaddrs, /proc/net/route), their Wi-Fi mode
 * and station link numbers (nl80211 over generic netlink, read-only). See netpath.h. GPLv3. */
#define _GNU_SOURCE
#include "netpath.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "internal.h"

/* ---------------------------------------------------------------- classification (pure) */

bool mlxr_netif_is_ap(const mlxr_netifs *t, const mlxr_netif *i)
{
    if (i->wifi_type == 2) return true;
    if (i->wifi_type != 0 || !i->wifi || i->default_route) return false;
    /* nl80211 did not say: a Wi-Fi interface without the default route, while another interface has it */
    for (int k = 0; k < t->n; k++)
        if (t->v[k].default_route && !t->v[k].loopback && strcmp(t->v[k].name, i->name) != 0) return true;
    return false;
}

mlxr_net_path mlxr_net_classify(const mlxr_netifs *t, const char *addr, const mlxr_netif **via)
{
    const mlxr_netif *best = NULL;
    struct in_addr ia;
    if (via) *via = NULL;
    if (addr && inet_pton(AF_INET, addr, &ia) == 1) {
        uint32_t a = ntohl(ia.s_addr);
        for (int k = 0; k < t->n; k++) {
            const mlxr_netif *i = &t->v[k];
            if (!i->up || !i->mask) continue;
            if ((a & i->mask) != (i->addr & i->mask)) continue;
            if (i->loopback && (i->addr == a)) { best = i; break; }
            if (!best || i->mask > best->mask) best = i;
        }
    }
    if (!best)
        for (int k = 0; k < t->n; k++)
            if (t->v[k].default_route && t->v[k].up) { best = &t->v[k]; break; }
    if (via) *via = best;
    if (!best) return MLXR_PATH_HOME;
    return mlxr_netif_is_ap(t, best) ? MLXR_PATH_ADAPTER : MLXR_PATH_HOME;
}

double mlxr_net_link_quality(const mlxr_net_link *l)
{
    if (!l->valid) return 30.0;               /* nothing known: neutral */
    if (!l->wifi) return 100.0;               /* wired */
    double q = 0;
    /* signal: -90 dBm or worse 0, -50 or better 60 */
    if (l->signal_dbm) {
        double s = (l->signal_dbm + 90.0) * 1.5;
        q += s < 0 ? 0 : s > 60 ? 60 : s;
    } else {
        q += 30;
    }
    /* tx rate: 0 at nothing, 40 at 1 Gbit/s or more */
    if (l->tx_mbps > 0) {
        double r = l->tx_mbps / 25.0;
        q += r > 40 ? 40 : r;
    } else {
        q += 15;
    }
    return q;
}

/* ---------------------------------------------------------------- interfaces */

static bool is_wireless(const char *name)
{
    char p[96];
    struct stat st;
    snprintf(p, sizeof p, "/sys/class/net/%s/wireless", name);
    if (stat(p, &st) == 0) return true;
    snprintf(p, sizeof p, "/sys/class/net/%s/phy80211", name);
    return stat(p, &st) == 0;
}

static void read_default_routes(mlxr_netifs *t)
{
    FILE *f = fopen("/proc/net/route", "r");
    if (!f) return;
    char line[256];
    if (!fgets(line, sizeof line, f)) { fclose(f); return; }
    while (fgets(line, sizeof line, f)) {
        char ifn[32];
        unsigned dest, gw, flags, mask;
        int refcnt, use, metric;
        if (sscanf(line, "%31s %x %x %x %d %d %d %x", ifn, &dest, &gw, &flags, &refcnt, &use, &metric, &mask) != 8) continue;
        if (dest != 0 || mask != 0 || !(flags & 1)) continue;
        for (int k = 0; k < t->n; k++)
            if (!strcmp(t->v[k].name, ifn)) t->v[k].default_route = true;
    }
    fclose(f);
}

/* ---- generic netlink ---- */

typedef struct {
    int fd;
    uint32_t seq;
} nlsock;

#define NLBUF 32768

static int nl_open(nlsock *s)
{
    s->fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
    s->seq = (uint32_t)getpid();
    if (s->fd < 0) return -1;
    struct sockaddr_nl a = { .nl_family = AF_NETLINK };
    struct timeval tv = { 0, 300000 };
    setsockopt(s->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (bind(s->fd, (struct sockaddr *)&a, sizeof a) < 0) { close(s->fd); s->fd = -1; return -1; }
    return 0;
}

static void parse_attrs(const struct nlattr *a, int len, const struct nlattr **tb, int max)
{
    memset(tb, 0, sizeof(*tb) * (size_t)(max + 1));
    while (len >= (int)sizeof(*a) && a->nla_len >= sizeof(*a) && (int)a->nla_len <= len) {
        int type = a->nla_type & NLA_TYPE_MASK;
        if (type <= max) tb[type] = a;
        int l = NLA_ALIGN(a->nla_len);
        a = (const struct nlattr *)((const char *)a + l);
        len -= l;
    }
}
static const void *nla_data(const struct nlattr *a) { return (const char *)a + NLA_HDRLEN; }
static int nla_len(const struct nlattr *a) { return (int)a->nla_len - NLA_HDRLEN; }

typedef void (*nl_cb)(const struct genlmsghdr *g, const struct nlattr *attrs, int alen, void *ud);

/* Send one request (optionally with one u32 attribute) and feed every reply message to cb. */
static int nl_call(nlsock *s, uint16_t type, uint8_t cmd, int dump, int a_type, uint32_t a_u32, const char *a_str, nl_cb cb, void *ud)
{
    char out[128];
    memset(out, 0, sizeof out);
    struct nlmsghdr *h = (struct nlmsghdr *)out;
    struct genlmsghdr *g = (struct genlmsghdr *)(out + NLMSG_HDRLEN);
    g->cmd = cmd;
    g->version = 1;
    size_t len = NLMSG_HDRLEN + GENL_HDRLEN;
    if (a_type) {
        struct nlattr *a = (struct nlattr *)(out + len);
        if (a_str) {
            a->nla_len = (uint16_t)(NLA_HDRLEN + strlen(a_str) + 1);
            memcpy((char *)a + NLA_HDRLEN, a_str, strlen(a_str) + 1);
        } else {
            a->nla_len = NLA_HDRLEN + 4;
            memcpy((char *)a + NLA_HDRLEN, &a_u32, 4);
        }
        a->nla_type = (uint16_t)a_type;
        len += NLA_ALIGN(a->nla_len);
    }
    h->nlmsg_len = (uint32_t)len;
    h->nlmsg_type = type;
    h->nlmsg_flags = (uint16_t)(NLM_F_REQUEST | (dump ? NLM_F_DUMP : 0));
    h->nlmsg_seq = ++s->seq;
    if (send(s->fd, out, len, 0) < 0) return -1;
    char *buf = malloc(NLBUF);
    if (!buf) return -1;
    int rc = -1;
    for (;;) {
        ssize_t n = recv(s->fd, buf, NLBUF, 0);
        if (n <= 0) break;
        int done = 0;
        for (struct nlmsghdr *m = (struct nlmsghdr *)buf; NLMSG_OK(m, (unsigned)n); m = NLMSG_NEXT(m, n)) {
            if (m->nlmsg_seq != s->seq) continue;
            if (m->nlmsg_type == NLMSG_DONE) { rc = 0; done = 1; break; }
            if (m->nlmsg_type == NLMSG_ERROR) {
                const struct nlmsgerr *e = NLMSG_DATA(m);
                rc = e->error ? -1 : 0;
                done = 1;
                break;
            }
            const struct genlmsghdr *gh = NLMSG_DATA(m);
            int alen = (int)m->nlmsg_len - NLMSG_HDRLEN - GENL_HDRLEN;
            if (alen >= 0) cb(gh, (const struct nlattr *)((const char *)gh + GENL_HDRLEN), alen, ud);
            if (!dump) { rc = 0; done = 1; break; }
        }
        if (done) break;
    }
    free(buf);
    return rc;
}

static void fam_cb(const struct genlmsghdr *g, const struct nlattr *attrs, int alen, void *ud)
{
    (void)g;
    const struct nlattr *tb[CTRL_ATTR_MAX + 1];
    parse_attrs(attrs, alen, tb, CTRL_ATTR_MAX);
    if (tb[CTRL_ATTR_FAMILY_ID]) *(int *)ud = *(const uint16_t *)nla_data(tb[CTRL_ATTR_FAMILY_ID]);
}

static int nl80211_family(nlsock *s)
{
    int id = -1;
    if (nl_call(s, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 0, CTRL_ATTR_FAMILY_NAME, 0, "nl80211", fam_cb, &id) != 0) return -1;
    return id;
}

typedef struct {
    int idx[MLXR_NET_MAX_IFS * 2];
    int type[MLXR_NET_MAX_IFS * 2];
    int freq[MLXR_NET_MAX_IFS * 2];
    char ssid[MLXR_NET_MAX_IFS * 2][33];
    int n;
} iftypes;

static void iface_cb(const struct genlmsghdr *g, const struct nlattr *attrs, int alen, void *ud)
{
    (void)g;
    iftypes *r = ud;
    const struct nlattr *tb[NL80211_ATTR_MAX + 1];
    parse_attrs(attrs, alen, tb, NL80211_ATTR_MAX);
    if (!tb[NL80211_ATTR_IFINDEX] || !tb[NL80211_ATTR_IFTYPE] || r->n >= (int)(sizeof r->idx / sizeof r->idx[0])) return;
    uint32_t t = *(const uint32_t *)nla_data(tb[NL80211_ATTR_IFTYPE]);
    r->idx[r->n] = (int)*(const uint32_t *)nla_data(tb[NL80211_ATTR_IFINDEX]);
    r->freq[r->n] = tb[NL80211_ATTR_WIPHY_FREQ] ? (int)*(const uint32_t *)nla_data(tb[NL80211_ATTR_WIPHY_FREQ]) : 0;
    r->ssid[r->n][0] = 0;
    if (tb[NL80211_ATTR_SSID]) {
        int l = nla_len(tb[NL80211_ATTR_SSID]);
        if (l > 32) l = 32;
        memcpy(r->ssid[r->n], nla_data(tb[NL80211_ATTR_SSID]), (size_t)l);
        r->ssid[r->n][l] = 0;
    }
    r->type[r->n++] = t == NL80211_IFTYPE_STATION || t == NL80211_IFTYPE_P2P_CLIENT ? 1
                    : t == NL80211_IFTYPE_AP || t == NL80211_IFTYPE_P2P_GO ? 2 : 3;
}

int mlxr_netifs_read(mlxr_netifs *t)
{
    memset(t, 0, sizeof *t);
    struct ifaddrs *ifa = NULL;
    if (getifaddrs(&ifa) != 0) return 0;
    for (struct ifaddrs *a = ifa; a && t->n < MLXR_NET_MAX_IFS; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_netmask) continue;
        mlxr_netif *i = &t->v[t->n++];
        memset(i, 0, sizeof *i);
        mlxr_strlcpy(i->name, a->ifa_name, sizeof i->name);
        i->ifindex = (int)if_nametoindex(a->ifa_name);
        i->up = (a->ifa_flags & IFF_UP) && (a->ifa_flags & IFF_RUNNING);
        i->loopback = (a->ifa_flags & IFF_LOOPBACK) != 0;
        i->addr = ntohl(((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
        i->mask = ntohl(((struct sockaddr_in *)a->ifa_netmask)->sin_addr.s_addr);
        i->wifi = is_wireless(i->name);
    }
    freeifaddrs(ifa);
    read_default_routes(t);
    nlsock s;
    if (nl_open(&s) == 0) {
        int fam = nl80211_family(&s);
        if (fam > 0) {
            iftypes r = { .n = 0 };
            if (nl_call(&s, (uint16_t)fam, NL80211_CMD_GET_INTERFACE, 1, 0, 0, NULL, iface_cb, &r) == 0) {
                t->nl80211 = true;
                for (int k = 0; k < t->n; k++)
                    for (int m = 0; m < r.n; m++)
                        if (r.idx[m] == t->v[k].ifindex) {
                            t->v[k].wifi = true;
                            t->v[k].wifi_type = r.type[m];
                            t->v[k].freq_mhz = r.freq[m];
                            memcpy(t->v[k].ssid, r.ssid[m], sizeof t->v[k].ssid);
                        }
            }
        }
        close(s.fd);
    }
    return t->n;
}

/* ---------------------------------------------------------------- station link */

typedef struct {
    uint8_t mac[6];
    int signal;                 /* dBm, 0 unknown */
    double mbps;
    char desc[40];
    uint32_t inactive_ms;
} station;

typedef struct {
    station v[16];
    int n;
} stations;

static void rate_cb(const struct nlattr *rate, station *st)
{
    const struct nlattr *rt[NL80211_RATE_INFO_MAX + 1];
    parse_attrs((const struct nlattr *)nla_data(rate), nla_len(rate), rt, NL80211_RATE_INFO_MAX);
    if (rt[NL80211_RATE_INFO_BITRATE32]) st->mbps = *(const uint32_t *)nla_data(rt[NL80211_RATE_INFO_BITRATE32]) / 10.0;
    else if (rt[NL80211_RATE_INFO_BITRATE]) st->mbps = *(const uint16_t *)nla_data(rt[NL80211_RATE_INFO_BITRATE]) / 10.0;
    if (rt[NL80211_RATE_INFO_EHT_MCS]) snprintf(st->desc, sizeof st->desc, "EHT-MCS %u", *(const uint8_t *)nla_data(rt[NL80211_RATE_INFO_EHT_MCS]));
    else if (rt[NL80211_RATE_INFO_HE_MCS]) snprintf(st->desc, sizeof st->desc, "HE-MCS %u", *(const uint8_t *)nla_data(rt[NL80211_RATE_INFO_HE_MCS]));
    else if (rt[NL80211_RATE_INFO_VHT_MCS]) snprintf(st->desc, sizeof st->desc, "VHT-MCS %u", *(const uint8_t *)nla_data(rt[NL80211_RATE_INFO_VHT_MCS]));
    else if (rt[NL80211_RATE_INFO_MCS]) snprintf(st->desc, sizeof st->desc, "MCS %u", *(const uint8_t *)nla_data(rt[NL80211_RATE_INFO_MCS]));
}

static void station_cb(const struct genlmsghdr *g, const struct nlattr *attrs, int alen, void *ud)
{
    (void)g;
    stations *r = ud;
    if (r->n >= 16) return;
    const struct nlattr *tb[NL80211_ATTR_MAX + 1];
    parse_attrs(attrs, alen, tb, NL80211_ATTR_MAX);
    if (!tb[NL80211_ATTR_STA_INFO]) return;
    station *st = &r->v[r->n];
    memset(st, 0, sizeof *st);
    if (tb[NL80211_ATTR_MAC] && nla_len(tb[NL80211_ATTR_MAC]) >= 6) memcpy(st->mac, nla_data(tb[NL80211_ATTR_MAC]), 6);
    const struct nlattr *si[NL80211_STA_INFO_MAX + 1];
    parse_attrs((const struct nlattr *)nla_data(tb[NL80211_ATTR_STA_INFO]), nla_len(tb[NL80211_ATTR_STA_INFO]), si, NL80211_STA_INFO_MAX);
    /* averages first (steadier); the instantaneous value is 0 when the radio does not report it */
    static const int order[] = { NL80211_STA_INFO_SIGNAL_AVG, NL80211_STA_INFO_SIGNAL, NL80211_STA_INFO_ACK_SIGNAL_AVG, NL80211_STA_INFO_ACK_SIGNAL };
    for (size_t k = 0; k < sizeof order / sizeof order[0] && !st->signal; k++)
        if (si[order[k]]) st->signal = *(const int8_t *)nla_data(si[order[k]]);
    if (si[NL80211_STA_INFO_TX_BITRATE]) rate_cb(si[NL80211_STA_INFO_TX_BITRATE], st);
    st->inactive_ms = si[NL80211_STA_INFO_INACTIVE_TIME] ? *(const uint32_t *)nla_data(si[NL80211_STA_INFO_INACTIVE_TIME]) : 0xffffffffu;
    r->n++;
}

/* The MAC the ARP table has for `ip` on `ifname`, or false. */
static bool arp_mac(const char *ifname, const char *ip, uint8_t mac[6])
{
    FILE *f = fopen("/proc/net/arp", "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    if (!fgets(line, sizeof line, f)) { fclose(f); return false; }
    while (!found && fgets(line, sizeof line, f)) {
        char a[64], hw[32], flags[16], m[64], mask[32], dev[32];
        if (sscanf(line, "%63s %31s %15s %63s %31s %31s", a, hw, flags, m, mask, dev) != 6) continue;
        if (strcmp(a, ip) || strcmp(dev, ifname)) continue;
        unsigned b[6];
        if (sscanf(m, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            for (int k = 0; k < 6; k++) mac[k] = (uint8_t)b[k];
            found = true;
        }
    }
    fclose(f);
    return found;
}

void mlxr_net_link_read(const mlxr_netif *i, const char *peer, mlxr_net_link *out)
{
    memset(out, 0, sizeof *out);
    out->valid = true;
    out->wifi = i->wifi;
    if (!i->wifi) return;
    memcpy(out->ssid, i->ssid, sizeof out->ssid);
    out->band = i->freq_mhz <= 0 ? 0 : i->freq_mhz < 3000 ? 24 : i->freq_mhz < 5925 ? 5 : 6;
    nlsock s;
    if (nl_open(&s) != 0) return;
    int fam = nl80211_family(&s);
    stations r = { .n = 0 };
    if (fam > 0) nl_call(&s, (uint16_t)fam, NL80211_CMD_GET_STATION, 1, NL80211_ATTR_IFINDEX, (uint32_t)i->ifindex, NULL, station_cb, &r);
    close(s.fd);
    if (r.n == 0) return;
    int pick = 0;
    uint8_t mac[6];
    bool have_mac = peer && arp_mac(i->name, peer, mac);
    int match = -1;
    if (have_mac)
        for (int k = 0; k < r.n; k++)
            if (!memcmp(r.v[k].mac, mac, 6)) match = k;
    if (match >= 0) pick = match;
    else
        for (int k = 1; k < r.n; k++)
            if (r.v[k].inactive_ms < r.v[pick].inactive_ms) pick = k;
    out->signal_dbm = r.v[pick].signal;
    out->tx_mbps = r.v[pick].mbps;
    memcpy(out->tx_desc, r.v[pick].desc, sizeof out->tx_desc);
}
