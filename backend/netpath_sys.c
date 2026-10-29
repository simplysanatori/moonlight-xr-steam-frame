/* Network path choice: the system side. Interfaces (getifaddrs, /proc/net/route), their Wi-Fi mode
 * and station link numbers (nl80211 over generic netlink, read-only). See netpath.h. GPLv3. */
#define _GNU_SOURCE
#include "netpath.h"
#include "netdiag.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <spawn.h>
#include <sys/wait.h>
#include <fcntl.h>
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

const mlxr_netif *mlxr_net_onlink_if(const mlxr_netifs *t, const char *addr)
{
    struct in_addr ia;
    const mlxr_netif *best = NULL;
    if (!addr || inet_pton(AF_INET, addr, &ia) != 1) return NULL;
    uint32_t a = ntohl(ia.s_addr);
    for (int k = 0; k < t->n; k++) {
        const mlxr_netif *i = &t->v[k];
        if (!i->up || !i->mask || (a & i->mask) != (i->addr & i->mask)) continue;
        if (!best || i->mask > best->mask) best = i;
    }
    return best;
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
    /* Native (lane wave4c-usb): an address on a USB interface's own subnet is on the cable. Reached only by
     * the subnet match: a default route that happens to leave through a USB tether is not a cable to the PC. */
    if (best->usb && best->mask && best->up && addr && inet_pton(AF_INET, addr, &ia) == 1 &&
        (ntohl(ia.s_addr) & best->mask) == (best->addr & best->mask))
        return MLXR_PATH_USB;
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

/* Native (lane wave4c-usb): is this the USB gadget network (the Frame's usb0, an NCM function of its
 * own USB device controller), or a USB network device in host mode? Three independent facts, any one is
 * enough: the name (the kernel names both usb%d), the device type in uevent (DEVTYPE=gadget, set by the
 * gadget ethernet code), and the sysfs path, which runs through the gadget device
 * (.../a600000.usb/gadget.0/net/usb0 on the Frame, see the probe in docs/dev/reports/usb-cable.md). */
static bool is_usb_net(const char *name)
{
    if (!strncmp(name, "usb", 3)) return true;
    char p[128], buf[512];
    snprintf(p, sizeof p, "/sys/class/net/%s/uevent", name);
    FILE *f = fopen(p, "r");
    if (f) {
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[n] = 0;
        if (strstr(buf, "DEVTYPE=gadget")) return true;
    }
    char real[512];
    snprintf(p, sizeof p, "/sys/class/net/%s", name);
    if (realpath(p, real) && strstr(real, "/gadget")) return true;
    return false;
}

/* The carrier file says 1 only with a link partner; reading it on an interface that is down fails. */
static bool no_carrier(const char *name)
{
    char p[128], c[8] = "";
    snprintf(p, sizeof p, "/sys/class/net/%s/carrier", name);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    if (!fgets(c, sizeof c, f)) c[0] = 0;
    fclose(f);
    return c[0] == '0';
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
        /* Native (lane wave4c-usb): the cable counts as up only with carrier */
        i->usb = !i->loopback && !i->wifi && is_usb_net(i->name);
        if (i->usb && i->up && no_carrier(i->name)) i->up = false;
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

/* ---------------------------------------------------------------- the adapter (Native, lane adapter-detect) */

static void read_small(const char *path, char *out, size_t cap)
{
    out[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    size_t n = fread(out, 1, cap - 1, f);
    fclose(f);
    out[n] = 0;
    while (n && (out[n - 1] == '\n' || out[n - 1] == ' ')) out[--n] = 0;
}

/* Runs argv[0] (found in the usual bin directories, as the user, no shell) and captures stdout.
 * False when it is not there, fails, or does not finish in timeout_ms. */
static bool run_capture(const char *name, char *const argv[], char *out, size_t cap, int timeout_ms)
{
    static const char *const dirs[] = { "/usr/bin", "/usr/sbin", "/bin", "/sbin" };
    char path[96] = "";
    for (size_t k = 0; k < sizeof dirs / sizeof dirs[0]; k++) {
        snprintf(path, sizeof path, "%s/%s", dirs[k], name);
        if (access(path, X_OK) == 0) break;
        path[0] = 0;
    }
    if (!path[0]) return false;
    int pfd[2];
    if (pipe2(pfd, O_CLOEXEC) != 0) return false;
    /* the Steam runtime's libraries do not belong to a system tool: pass the environment without them */
    extern char **environ;
    size_t ne = 0;
    while (environ && environ[ne]) ne++;
    char **env = calloc(ne + 1, sizeof *env);
    if (!env) { close(pfd[0]); close(pfd[1]); return false; }
    size_t k2 = 0;
    for (size_t k = 0; k < ne; k++)
        if (strncmp(environ[k], "LD_LIBRARY_PATH=", 16) && strncmp(environ[k], "LD_PRELOAD=", 11)) env[k2++] = environ[k];
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int sr = posix_spawn(&pid, path, &fa, NULL, argv, env);
    posix_spawn_file_actions_destroy(&fa);
    free(env);
    if (sr != 0) { close(pfd[0]); close(pfd[1]); return false; }
    close(pfd[1]);
    size_t got = 0;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool ok = true;
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long el = (now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
        if (el >= timeout_ms) { ok = false; break; }
        struct pollfd pf = { pfd[0], POLLIN, 0 };
        int pr = poll(&pf, 1, timeout_ms - (int)el);
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) { ok = false; break; }
        ssize_t r = read(pfd[0], out + got, cap - 1 - got);
        if (r <= 0) break;
        got += (size_t)r;
        if (got >= cap - 1) break;   /* longer than we keep: the count stays right for a few stations */
    }
    out[got] = 0;
    close(pfd[0]);
    int st = 0;
    if (!ok) kill(pid, SIGKILL);
    waitpid(pid, &st, 0);
    return ok && WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

/* Wi-Fi power save through iw (netdiag.h): a read-only get, and a set that needs CAP_NET_ADMIN */
int mlxr_wifi_ps_get(const char *ifname)
{
    if (!ifname || !ifname[0]) return -1;
    char out[256];
    char *argv[] = { "iw", "dev", (char *)ifname, "get", "power_save", NULL };
    if (!run_capture("iw", argv, out, sizeof out, 1500)) return -1;
    return mlxr_wifi_ps_parse(out);
}

bool mlxr_wifi_ps_set(const char *ifname, bool on)
{
    if (!ifname || !ifname[0]) return false;
    char out[256];
    char *argv[] = { "iw", "dev", (char *)ifname, "set", "power_save", (char *)(on ? "on" : "off"), NULL };
    return run_capture("iw", argv, out, sizeof out, 1500);
}

/* nl80211's station list of the interface: the count and the strongest signal, false if unavailable */
static bool nl_station_count(int ifindex, int *count, int *signal)
{
    nlsock s;
    if (nl_open(&s) != 0) return false;
    int fam = nl80211_family(&s);
    stations *r = calloc(1, sizeof *r);
    bool ok = false;
    if (fam > 0 && r && nl_call(&s, (uint16_t)fam, NL80211_CMD_GET_STATION, 1, NL80211_ATTR_IFINDEX, (uint32_t)ifindex, NULL, station_cb, r) == 0) {
        ok = true;
        *count = r->n;
        *signal = 0;
        for (int k = 0; k < r->n; k++)
            if (r->v[k].signal && (!*signal || r->v[k].signal > *signal)) *signal = r->v[k].signal;
    }
    free(r);
    close(s.fd);
    return ok;
}

void mlxr_net_adapter_inputs_read(const char *ifname, mlxr_adapter_inputs *in)
{
    memset(in, 0, sizeof *in);
    in->nl_stations = -1;
    if (!ifname || !ifname[0]) ifname = "wlanap";
    char path[96];
    struct stat st;
    snprintf(path, sizeof path, "/sys/class/net/%s", ifname);
    in->exists = stat(path, &st) == 0;
    if (!in->exists) return;
    snprintf(path, sizeof path, "/sys/class/net/%s/operstate", ifname);
    read_small(path, in->operstate, sizeof in->operstate);
    snprintf(path, sizeof path, "/sys/class/net/%s/carrier", ifname);
    read_small(path, in->carrier, sizeof in->carrier);   /* reading it fails while the interface is down: "" */
    char *argv[] = { "iw", "dev", (char *)ifname, "station", "dump", NULL };
    in->has_iw = run_capture("iw", argv, in->iw_dump, sizeof in->iw_dump, 1500);
    if (!in->has_iw) {
        in->iw_dump[0] = 0;
        int ifx = (int)if_nametoindex(ifname), cnt = 0, sig = 0;
        if (ifx > 0 && nl_station_count(ifx, &cnt, &sig)) { in->nl_stations = cnt; in->nl_signal = sig; }
    }
    FILE *f = fopen("/proc/net/arp", "r");
    if (f) {
        size_t n = fread(in->arp, 1, sizeof in->arp - 1, f);
        in->arp[n] = 0;
        in->has_arp = true;
        fclose(f);
    }
}

void mlxr_net_adapter_detect(const char *ifname, const char *const *pc_addrs, int n_pc, mlxr_adapter_info *out)
{
    mlxr_adapter_inputs *in = malloc(sizeof *in);
    if (!in) { memset(out, 0, sizeof *out); return; }   /* not checked: the old behaviour */
    mlxr_net_adapter_inputs_read(ifname, in);
    mlxr_net_adapter_assess(in, ifname, pc_addrs, n_pc, out);
    free(in);
}

/* ---------------------------------------------------------------- finding the PC's other addresses */

static int add_found(char out[][128], const char **src, int n, int cap, const char *addr, const char *how)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(out[i], addr)) return n;
    if (n >= cap) return n;
    mlxr_strlcpy(out[n], addr, 128);
    src[n] = how;
    return n + 1;
}

/* A DNS name at *pos (with compression) as lower case text; advances *pos past it in the record */
static int dns_name(const uint8_t *m, size_t len, size_t *pos, char *out, size_t cap)
{
    size_t p = *pos, o = 0;
    bool jumped = false;
    int hops = 0;
    for (;;) {
        if (p >= len || hops > 20) return -1;
        uint8_t l = m[p];
        if (l == 0) { p++; break; }
        if ((l & 0xC0) == 0xC0) {
            if (p + 1 >= len) return -1;
            size_t off = (size_t)((l & 0x3F) << 8) | m[p + 1];
            if (!jumped) *pos = p + 2;
            jumped = true;
            p = off;
            hops++;
            continue;
        }
        if (p + 1 + l > len || o + l + 2 >= cap) return -1;
        if (o) out[o++] = '.';
        for (int i = 0; i < l; i++) out[o++] = (char)((m[p + 1 + i] >= 'A' && m[p + 1 + i] <= 'Z') ? m[p + 1 + i] + 32 : m[p + 1 + i]);
        p += 1 + l;
    }
    out[o] = 0;
    if (!jumped) *pos = p;
    return 0;
}

static size_t dns_put_name(uint8_t *b, const char *name)
{
    size_t o = 0;
    const char *s = name;
    while (*s) {
        const char *dot = strchr(s, '.');
        size_t l = dot ? (size_t)(dot - s) : strlen(s);
        b[o++] = (uint8_t)l;
        memcpy(b + o, s, l);
        o += l;
        s += l + (dot ? 1 : 0);
    }
    b[o++] = 0;
    return o;
}

/* Addresses in A records named `want` (lower case, "" any) in one mDNS message */
int mlxr_net_mdns_parse(const uint8_t *m, size_t len, const char *want, char out[][128], const char **src, int n, int cap)
{
    if (len < 12) return n;
    int qd = m[4] << 8 | m[5], rr = (m[6] << 8 | m[7]) + (m[8] << 8 | m[9]) + (m[10] << 8 | m[11]);
    size_t pos = 12;
    char name[256];
    for (int i = 0; i < qd; i++) {
        if (dns_name(m, len, &pos, name, sizeof name) < 0 || pos + 4 > len) return n;
        pos += 4;
    }
    for (int i = 0; i < rr; i++) {
        if (dns_name(m, len, &pos, name, sizeof name) < 0 || pos + 10 > len) return n;
        int type = m[pos] << 8 | m[pos + 1], rdlen = m[pos + 8] << 8 | m[pos + 9];
        pos += 10;
        if (pos + (size_t)rdlen > len) return n;
        if (type == 1 && rdlen == 4 && (!want[0] || !strcmp(name, want))) {
            char a[32];
            snprintf(a, sizeof a, "%u.%u.%u.%u", m[pos], m[pos + 1], m[pos + 2], m[pos + 3]);
            if (m[pos] != 0 && m[pos] != 127 && !(m[pos] == 169 && m[pos + 1] == 254)) n = add_found(out, src, n, cap, a, "mdns");
        }
        pos += (size_t)rdlen;
    }
    return n;
}

int mlxr_net_discover(const mlxr_netifs *t, const char *hostname, const uint8_t *mac, int budget_ms,
                      char out[][128], const char **out_src, int cap)
{
    int n = 0;
    if (mac) {
        FILE *f = fopen("/proc/net/arp", "r");
        if (f) {
            char line[256];
            if (fgets(line, sizeof line, f))
                while (fgets(line, sizeof line, f)) {
                    char a[64], hw[32], flags[16], m[64], mask[32], dev[32];
                    unsigned b[6];
                    if (sscanf(line, "%63s %31s %15s %63s %31s %31s", a, hw, flags, m, mask, dev) != 6) continue;
                    if (strtoul(flags, NULL, 16) == 0) continue;     /* incomplete */
                    if (sscanf(m, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) continue;
                    bool same = true;
                    for (int k = 0; k < 6; k++) same &= b[k] == mac[k];
                    if (same) n = add_found(out, out_src, n, cap, a, "arp");
                }
            fclose(f);
        }
    }
    if (!hostname || !hostname[0] || budget_ms <= 0) return n;

    /* mDNS: one query per up interface, legacy unicast replies to our own port */
    char want[160];
    snprintf(want, sizeof want, "%s.local", hostname);
    for (char *c = want; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
    uint8_t q[320];
    memset(q, 0, 12);
    q[5] = 2;                                           /* two questions */
    size_t o = 12;
    o += dns_put_name(q + o, want);
    q[o++] = 0; q[o++] = 1; q[o++] = 0x80; q[o++] = 1;  /* A IN, unicast reply welcome */
    o += dns_put_name(q + o, "_nvstream._tcp.local");
    q[o++] = 0; q[o++] = 12; q[o++] = 0x80; q[o++] = 1; /* PTR IN */
    /* One socket on the mDNS port (shared with the system's responder), joined to the group on every
     * up interface; the PC answers to the group, from each of its own addresses */
    struct sockaddr_in dst = { .sin_family = AF_INET, .sin_port = htons(5353) };
    inet_pton(AF_INET, "224.0.0.251", &dst.sin_addr);
    int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return n;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
    struct sockaddr_in me = { .sin_family = AF_INET, .sin_port = htons(5353), .sin_addr.s_addr = htonl(INADDR_ANY) };
    unsigned char ttl = 255, loop = 0;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);
    if (bind(fd, (struct sockaddr *)&me, sizeof me) < 0) { close(fd); return n; }
    struct in_addr ifs[MLXR_NET_MAX_IFS];
    int ni = 0;
    for (int k = 0; k < t->n && ni < MLXR_NET_MAX_IFS; k++) {
        const mlxr_netif *f = &t->v[k];
        if (!f->up || f->loopback) continue;
        struct ip_mreq mr = { .imr_multiaddr = dst.sin_addr, .imr_interface = { .s_addr = htonl(f->addr) } };
        setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mr, sizeof mr);
        ifs[ni++].s_addr = htonl(f->addr);
    }
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int sends = 0;
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long el = (now.tv_sec - t0.tv_sec) * 1000 + (now.tv_nsec - t0.tv_nsec) / 1000000;
        if (el >= budget_ms || ni == 0) break;
        if ((sends == 0) || (sends == 1 && el >= budget_ms / 2)) {
            sends++;
            for (int i = 0; i < ni; i++) {
                setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifs[i], sizeof ifs[i]);
                sendto(fd, q, o, 0, (struct sockaddr *)&dst, sizeof dst);
            }
        }
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 20) <= 0) continue;
        for (int r = 0; r < 8; r++) {
            uint8_t buf[2048];
            ssize_t got = recv(fd, buf, sizeof buf, 0);
            if (got <= 0) break;
            if (got > 2 && (buf[2] & 0x80)) n = mlxr_net_mdns_parse(buf, (size_t)got, want, out, out_src, n, cap);
        }
    }
    close(fd);
    return n;
}
