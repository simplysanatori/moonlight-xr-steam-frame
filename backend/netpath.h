/* Moonlight XR Native - network path choice (home Wi-Fi / Steam Frame adapter). GPLv3.
 *
 * The PC can be reached over two links: the Steam Frame's own wireless adapter (the Frame hosts an
 * access point, the PC's Steam Frame USB adapter joins it) and the home network (the Frame's
 * ordinary Wi-Fi or ethernet). The `network_path` setting says which to use: auto | home | adapter.
 *
 * Setting precedence: the MLXR_NETWORK environment variable (a testing override, strongest), then
 * the --network flag (the flat launcher passes its own setting that way), then config.ini's
 * network_path, then auto.
 *
 * Classifying an address: the interface it is on-link with (longest matching subnet among the
 * interfaces' IPv4 addresses, else the interface of the default route) and that interface's mode.
 * An access-point interface (nl80211 type AP) is the adapter path; a managed Wi-Fi or an ethernet
 * interface is the home path. Where nl80211 is unavailable an interface that is Wi-Fi, has no
 * default route while another interface has one, counts as the AP. Nothing is hard-coded.
 *
 * Choosing (auto), all candidates probed in parallel within a time budget (default 1.5 s) by
 * a serverinfo round trip over HTTP (is it the PC? how long does it take) and several TCP
 * connects to the same port, whose handshake times are the network round trips (the PC's web server
 * takes a quarter second to answer serverinfo, which would drown the difference between the links).
 * Per candidate: failed samples, median round trip, jitter and the Wi-Fi link (signal dBm and tx
 * bitrate of the station on that interface, read-only from nl80211). A link is "weak" if its signal
 * is below -70 dBm, its PHY rate is under 4x the session bitrate, or half the probes failed.
 * The policy prefers Wi-Fi whenever it is usable: Wi-Fi is chosen unless it did not answer at all,
 * or it is weak while the adapter is not. Within one path the candidates are ranked by
 * mlxr_net_better() (failures, weak link, round trips tied under 10 ms or within max(25%, 5 ms),
 * link quality, median).
 * The reason is kept in plan->reason ("chose Wi-Fi: usable, preferred over the adapter (...)").
 *
 * Addresses: hosts.ini, the one named by the caller, ARP entries carrying the PC's MAC and mDNS
 * answers on every interface (A records for <hostname>.local). Serverinfo's LocalIP is only the
 * address the request came in on, so it cannot reveal the other path's.
 *
 * home / adapter: the first (in address order) answering candidate on that path. Choosing a path
 * means that path: if nothing answers the probe is repeated for retry_ms (4 s) before the other path
 * is tried, with a toast that names the addresses that failed and how.
 * User-visible names follow Steam's: "Wi-Fi" and "Steam Frame Wireless Adapter".
 *
 * Native (lane adapter-detect): is the adapter there at all? Before probing, the plan looks at the
 * Frame's own access point interface (wlanap) without root: does it exist and is it up, is a station
 * associated (iw, else nl80211, else the neighbour table), is the PC's address in the neighbour table.
 * No station means no adapter: auto never probes or picks the adapter path, and a forced adapter
 * setting falls back to Wi-Fi with the usual toast. When the station list cannot be read at all the
 * adapter is assumed present (the old behaviour). See mlxr_net_adapter_detect().
 */
#ifndef MLXR_NETPATH_H
#define MLXR_NETPATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MLXR_NET_AUTO = 0, MLXR_NET_HOME, MLXR_NET_ADAPTER } mlxr_net_pref;
typedef enum { MLXR_PATH_NONE = 0, MLXR_PATH_HOME, MLXR_PATH_ADAPTER } mlxr_net_path;

const char *mlxr_net_pref_name(mlxr_net_pref p);          /* "auto" "home" "adapter" */
const char *mlxr_net_path_label(mlxr_net_path p);         /* "Wi-Fi" "Steam Frame Wireless Adapter" "none" */
/* Case-insensitive; false (and *out untouched) for anything but auto/home/adapter. */
bool mlxr_net_pref_parse(const char *s, mlxr_net_pref *out);
/* Precedence env > flag > config > auto. Each may be NULL or empty; an invalid one is skipped (and
 * said in *warn, if given, a short message). *source (static string) says which one won:
 * "flag" "env" "config" "default". */
mlxr_net_pref mlxr_net_pref_resolve(const char *flag, const char *env, const char *config,
                                    const char **source, const char **warn);

/* ---- interfaces ---- */

#define MLXR_NET_MAX_IFS 24

typedef struct {
    char name[16];
    int ifindex;
    bool up, loopback;
    bool wifi;                  /* a wireless interface */
    int wifi_type;              /* 0 unknown, 1 managed (station), 2 AP, 3 other */
    char ssid[33];              /* nl80211, "" unknown */
    int freq_mhz;               /* the channel's centre frequency, 0 unknown */
    uint32_t addr, mask;        /* IPv4, host byte order */
    bool default_route;         /* the default route leaves through it */
} mlxr_netif;

typedef struct {
    mlxr_netif v[MLXR_NET_MAX_IFS];
    int n;
    bool nl80211;               /* the Wi-Fi types came from nl80211 */
} mlxr_netifs;

/* Read the system's table (getifaddrs, /proc/net/route, nl80211). Returns the count. */
int mlxr_netifs_read(mlxr_netifs *t);
/* The path of an address and the interface entry it goes through (NULL when none matched). */
mlxr_net_path mlxr_net_classify(const mlxr_netifs *t, const char *addr, const mlxr_netif **via);
bool mlxr_netif_is_ap(const mlxr_netifs *t, const mlxr_netif *i);

/* ---- link quality (read-only, nl80211) ---- */

typedef struct {
    bool valid;
    bool wifi;                  /* false: wired or unknown, no radio numbers */
    int signal_dbm;             /* 0 unknown */
    double tx_mbps;             /* 0 unknown */
    char tx_desc[40];           /* "HE-MCS 3" "EHT-MCS 9" "MCS 7" */
    char ssid[33];              /* of the interface ("" unknown); for an AP it is the Frame's own network */
    int band;                   /* 24 (2.4 GHz), 5, 6; 0 unknown */
} mlxr_net_link;

/* The words the user sees for a path (Steam's own terms): "Wi-Fi", "Wi-Fi (Home 5GHz, 5 GHz)",
 * "Steam Frame Wireless Adapter", "Steam Frame Wireless Adapter (6 GHz)". `link` may be NULL. */
void mlxr_net_path_describe(mlxr_net_path p, const mlxr_net_link *link, char *out, size_t cap);

/* The station on interface `i` that is `peer` (matched by the ARP table's MAC), else the only or
 * the most recently active one. */
void mlxr_net_link_read(const mlxr_netif *i, const char *peer, mlxr_net_link *out);
/* 0..100 */
double mlxr_net_link_quality(const mlxr_net_link *l);

/* ---- is there a Steam Frame Wireless Adapter? (Native, lane adapter-detect) ---- */

typedef enum {
    MLXR_ADAPTER_NONE = 0,      /* the AP interface is missing or down */
    MLXR_ADAPTER_LINK_ONLY,     /* the AP is up, no station is associated */
    MLXR_ADAPTER_STATION        /* a station is associated (or the list could not be read) */
} mlxr_adapter_state;

typedef struct {
    bool checked;               /* false: not looked at, nothing is assumed */
    mlxr_adapter_state state;
    char ifname[16];            /* the AP interface, "wlanap" */
    bool exists, up;
    bool stations_known;        /* false: neither iw, nl80211 nor the neighbour table could say; state is STATION then */
    int stations;               /* associated stations (neighbour entries when from "neighbours") */
    int signal_dbm;             /* the strongest station's, 0 unknown */
    char stations_from[12];     /* "iw" "nl80211" "neighbours" "" */
    bool pc_known;              /* an address of the PC on the AP's subnet was given */
    bool pc_in_table;           /* ... and the neighbour table of the AP interface has it */
    char reason[128];           /* "wlanap up, no station" "wlanap down" "wlanap missing" "wlanap up, 1 station (-48 dBm)" */
} mlxr_adapter_info;

/* The raw facts, as text, so the tests can feed their own. mlxr_net_adapter_inputs_read() fills it
 * from the system; mlxr_net_adapter_assess() turns it into the answer (pure). */
typedef struct {
    bool exists;                /* /sys/class/net/<if> is there */
    char operstate[32];         /* its operstate file ("up", "down", "unknown") */
    char carrier[8];            /* its carrier file ("1" "0" ""), */
    bool has_iw;                /* `iw dev <if> station dump` ran and succeeded */
    char iw_dump[8192];
    int nl_stations;            /* nl80211's station count, -1 unavailable */
    int nl_signal;
    bool has_arp;               /* /proc/net/arp was readable */
    char arp[16384];
} mlxr_adapter_inputs;

/* Reads the inputs from the system (sysfs, /proc/net/arp, iw if the user can run it, nl80211). No root. */
void mlxr_net_adapter_inputs_read(const char *ifname, mlxr_adapter_inputs *in);
/* Pure. `pc_addrs` are the PC's candidate addresses on the AP's subnet (may be NULL). */
void mlxr_net_adapter_assess(const mlxr_adapter_inputs *in, const char *ifname, const char *const *pc_addrs, int n_pc,
                             mlxr_adapter_info *out);
/* Reads and assesses in one go: what the flat launcher calls to grey the Steam Frame Wireless
 * Adapter option (grey it when out->state != MLXR_ADAPTER_STATION). `ifname` NULL means "wlanap". */
void mlxr_net_adapter_detect(const char *ifname, const char *const *pc_addrs, int n_pc, mlxr_adapter_info *out);
/* The words for a log line: "no adapter found (wlanap up, no station)", "present (wlanap up, ...)". */
void mlxr_net_adapter_text(const mlxr_adapter_info *a, char *out, size_t cap);
/* True when the adapter path may be probed and chosen: not checked, or a station present. */
bool mlxr_net_adapter_usable(const mlxr_adapter_info *a);

/* ---- candidates and the choice ---- */

#define MLXR_NET_MAX_CANDS 16
#define MLXR_NET_MAX_SAMPLES 8

typedef struct {
    char address[128];
    uint16_t port;              /* 0: the plan's default */
    mlxr_net_path path;
    char ifname[16];
    mlxr_net_link link;
    double link_quality;
    bool poor;                  /* signal < -70 dBm, or PHY rate < 4x the session bitrate */
    bool onlink;                /* an interface's subnet contains it (else it goes via the default route) */
    char source[16];            /* "hosts.ini" "launcher" "arp" "mdns" "serverinfo" */
    /* probe */
    bool probed, done;
    int sent, ok, failed;
    double rtt_ms[MLXR_NET_MAX_SAMPLES];    /* TCP connect times: the network round trip */
    double info_ms;             /* the serverinfo round trip as a whole (host processing included) */
    int n_rtt;
    double median_ms, jitter_ms;
    bool other_host;            /* answered, but as another PC (uuid differs) */
    char error[96];             /* the last failure */
    bool skipped;               /* Native (lane adapter-detect): not probed, there is no adapter */
} mlxr_net_cand;

typedef struct {
    mlxr_net_pref pref;
    const char *source;
    uint16_t default_port;      /* HTTP port, 47989 */
    char uuid[64];              /* the PC's uuid ("" accepts any) */
    char uniqueid[17];          /* ours, sent on the probe ("" a placeholder) */
    int budget_ms;              /* 1500 */
    int bitrate_kbps;           /* the session's, for judging the link (80000 if 0) */
    int retry_ms;               /* home/adapter: how long the asked-for path is retried (4000) */
    char hostname[64];          /* the PC's name, for mDNS ("" skips the name lookup) */
    uint8_t mac[6];
    bool has_mac;               /* the PC's MAC, for the ARP table */
    mlxr_net_cand c[MLXR_NET_MAX_CANDS];
    int n;
    /* the outcome */
    int chosen;                 /* index into c, -1 none answered */
    mlxr_net_path chosen_path;
    bool fell_back;             /* the asked-for path (home/adapter) had no answer */
    char toast[200];            /* the in-VR toast for a fallback: "Wi-Fi not reachable (192.168.1.10: connect timed out). Using Steam Frame Wireless Adapter." */
    char failure[160];          /* the asked-for path's failed addresses and how, "" none */
    char connected[128];        /* "Connected over Wi-Fi (5 GHz)." for the chosen path ("" none) */
    char reason[200];           /* "chose Wi-Fi: adapter link poor (-80 dBm, 576 Mbit/s)" */
    char chosen_desc[96];       /* "Wi-Fi (Home 5GHz, 5 GHz)" for the flat launcher to show */
    int grace_ms;               /* auto: how long the others get once a candidate answered (300) */
    char summary[160];          /* "adapter 10.35.50.27 " for the stats line */
    /* Native (lane adapter-detect) */
    mlxr_adapter_info adapter;  /* filled by mlxr_net_select (or mlxr_net_plan_detect_adapter) */
    const mlxr_adapter_inputs *adapter_inputs;   /* tests: facts to use instead of reading the system */
    bool adapter_absent_fallback;   /* the adapter setting was asked, no adapter is present, Wi-Fi used */
} mlxr_net_plan;

void mlxr_net_plan_init(mlxr_net_plan *p, mlxr_net_pref pref, const char *source, uint16_t http_port,
                        const char *uuid, const char *uniqueid);
/* Adds an address (deduplicated, case-insensitive); returns its index, or -1 for empty, loopback-less junk or a full table. */
int mlxr_net_plan_add(mlxr_net_plan *p, const char *address, uint16_t port);
/* The same, saying where the address came from (kept in the candidate, logged). */
int mlxr_net_plan_add_src(mlxr_net_plan *p, const char *address, uint16_t port, const char *source);
/* "a,b,c" */
void mlxr_net_plan_add_csv(mlxr_net_plan *p, const char *csv, uint16_t port);
/* Interface whose subnet contains the address (NULL if none or not an IPv4 literal). */
const mlxr_netif *mlxr_net_onlink_if(const mlxr_netifs *t, const char *addr);
/* Addresses of the PC the interfaces can see: ARP entries with its MAC (if has_mac) and mDNS
 * answers for <hostname>.local, asked on every up interface for up to budget_ms. Returns the count
 * written to out (cap entries of 128 bytes), with out_src[i] "arp" or "mdns". Nothing is verified:
 * the probe checks the uuid. */
int mlxr_net_discover(const mlxr_netifs *t, const char *hostname, const uint8_t *mac, int budget_ms,
                      char out[][128], const char **out_src, int cap);
/* Addresses in the A records named `want` ("<hostname>.local" lower case, "" for any) of one mDNS
 * message, appended to out[n..] (source "mdns"); returns the new count. Exposed for the tests. */
int mlxr_net_mdns_parse(const uint8_t *msg, size_t len, const char *want, char out[][128], const char **src, int n,
                        int cap);
/* The record's address and extra_addresses (hosts.ini), in that order. */
void mlxr_net_plan_add_record(mlxr_net_plan *p, const char *address, const char *extra_addresses);
/* Sets path, ifname of each candidate from the table. */
void mlxr_net_plan_classify(mlxr_net_plan *p, const mlxr_netifs *t);
/* Native (lane adapter-detect): looks for the adapter (plan->adapter_inputs if set, else the system),
 * stores the answer in plan->adapter and marks the adapter-path candidates `skipped` when there is no
 * station. Call after mlxr_net_plan_classify. mlxr_net_select does it. */
void mlxr_net_plan_detect_adapter(mlxr_net_plan *p, const mlxr_netifs *t);
/* Fills link and link_quality of each candidate (reads nl80211 through the table's interfaces). */
void mlxr_net_plan_read_links(mlxr_net_plan *p, const mlxr_netifs *t);

/* Sets c->poor from its link and the session bitrate. */
void mlxr_net_cand_judge(mlxr_net_cand *c, int bitrate_kbps);
/* True when a is the better choice. Both must have answered, and been judged. */
bool mlxr_net_better(const mlxr_net_cand *a, const mlxr_net_cand *b);
/* Fills the median and jitter from the samples. */
void mlxr_net_cand_stats(mlxr_net_cand *c);
/* The choice over the probed candidates (pure): sets chosen, chosen_path, fell_back, toast,
 * summary. Returns chosen. For home/adapter with no answer on that path it returns the first
 * answering one of the other path. */
int mlxr_net_choose(mlxr_net_plan *p);

/* Probes the candidates with which[i] set (all if which is NULL) in parallel, `samples` probes each (the
 * first with serverinfo), within budget_ms. With first_wins it stops as soon as one candidate answered
 * and every earlier one has finished (for home/adapter). Fills the probe fields. */
void mlxr_net_probe(mlxr_net_plan *p, const bool *which, int samples, bool first_wins);

/* Everything: classify, read the links, probe, choose, log each candidate and the choice.
 * `t` may be NULL (the system's table is read). Returns the chosen index or -1. */
int mlxr_net_select(mlxr_net_plan *p, const mlxr_netifs *t);
/* One log line per candidate (to the backend's log hook) and the choice; also used by the CLI. */
void mlxr_net_log_plan(const mlxr_net_plan *p);
/* Address and port the stream should use: the chosen candidate's. */
const char *mlxr_net_chosen_address(const mlxr_net_plan *p, uint16_t *port);

#ifdef __cplusplus
}
#endif
#endif
