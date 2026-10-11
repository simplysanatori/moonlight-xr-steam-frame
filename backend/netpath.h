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
 * takes a quarter second to answer serverinfo, which would drown the difference between the links). Per candidate: failed samples, median round trip, jitter (mean absolute deviation from the
 * median) and the Wi-Fi link quality (signal dBm and tx bitrate of the station on that interface,
 * read-only from nl80211). A candidate that never answered is out. Idle round trips cannot show
 * the loss a 50-80 Mbit/s stream meets on a weak radio link, so the link counts. A link is "poor"
 * if its signal is below -70 dBm or its PHY rate is under 4x the session bitrate. Between the
 * answering candidates mlxr_net_better() decides, in this order:
 *   1. fewer failed samples win;
 *   2. a good link beats a poor one, whatever the round trips;
 *   3. else a clearly lower (median + jitter) wins; round trips are equal when both are under
 *      10 ms or within max(25%, 5 ms) of each other;
 *   4. else the better link quality (0..100), unless within 5 points;
 *   5. else the lower median, then (a full tie) the adapter, a link of its own.
 * The reason is kept in plan->reason ("chose Wi-Fi: adapter link poor (-80 dBm, 576 Mbit/s)").
 * home / adapter: the first (in address order) answering candidate on that path; if there is none,
 * the other path is tried, with a toast and a log line.
 * User-visible names follow Steam's: "Wi-Fi" and "Steam Frame Wireless Adapter".
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
    /* probe */
    bool probed, done;
    int sent, ok, failed;
    double rtt_ms[MLXR_NET_MAX_SAMPLES];    /* TCP connect times: the network round trip */
    double info_ms;             /* the serverinfo round trip as a whole (host processing included) */
    int n_rtt;
    double median_ms, jitter_ms;
    bool other_host;            /* answered, but as another PC (uuid differs) */
    char error[96];             /* the last failure */
} mlxr_net_cand;

typedef struct {
    mlxr_net_pref pref;
    const char *source;
    uint16_t default_port;      /* HTTP port, 47989 */
    char uuid[64];              /* the PC's uuid ("" accepts any) */
    char uniqueid[17];          /* ours, sent on the probe ("" a placeholder) */
    int budget_ms;              /* 1500 */
    int bitrate_kbps;           /* the session's, for judging the link (80000 if 0) */
    mlxr_net_cand c[MLXR_NET_MAX_CANDS];
    int n;
    /* the outcome */
    int chosen;                 /* index into c, -1 none answered */
    mlxr_net_path chosen_path;
    bool fell_back;             /* the asked-for path (home/adapter) had no answer */
    char toast[128];            /* the in-VR toast for a fallback: "Steam Frame Wireless Adapter not reachable. Using Wi-Fi." */
    char connected[128];        /* "Connected over Wi-Fi (5 GHz)." for the chosen path ("" none) */
    char reason[200];           /* "chose Wi-Fi: adapter link poor (-80 dBm, 576 Mbit/s)" */
    char chosen_desc[96];       /* "Wi-Fi (Home 5GHz, 5 GHz)" for the flat launcher to show */
    int grace_ms;               /* auto: how long the others get once a candidate answered (300) */
    char summary[160];          /* "adapter 198.51.100.27 " for the stats line */
} mlxr_net_plan;

void mlxr_net_plan_init(mlxr_net_plan *p, mlxr_net_pref pref, const char *source, uint16_t http_port,
                        const char *uuid, const char *uniqueid);
/* Adds an address (deduplicated, case-insensitive); returns its index, or -1 for empty, loopback-less junk or a full table. */
int mlxr_net_plan_add(mlxr_net_plan *p, const char *address, uint16_t port);
/* "a,b,c" */
void mlxr_net_plan_add_csv(mlxr_net_plan *p, const char *csv, uint16_t port);
/* The record's address and extra_addresses (hosts.ini), in that order. */
void mlxr_net_plan_add_record(mlxr_net_plan *p, const char *address, const char *extra_addresses);
/* Sets path, ifname of each candidate from the table. */
void mlxr_net_plan_classify(mlxr_net_plan *p, const mlxr_netifs *t);
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
