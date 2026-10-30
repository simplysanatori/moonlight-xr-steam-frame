/* Network diagnostics for the stream (Wave 1, item 1.13). GPLv3.
 *
 * Two things the one lossy run needs to be able to tell apart from plain network loss:
 *  - Wi-Fi power save. The radio of a managed Wi-Fi interface that sleeps between packets adds
 *    latency and bursts. Upstream Moonlight XR holds Android's low-latency Wi-Fi lock for this.
 *    At stream start the state is read (`iw dev <if> get power_save`) and logged; if it is on, the
 *    stream asks for it off (`iw dev <if> set power_save off`). That needs CAP_NET_ADMIN, which the
 *    user-level client does not have, so it usually fails: that is logged and the state is left as
 *    it was (no sudo, no privilege escalation). If the change did succeed it is undone when the
 *    stream ends.
 *  - UDP receive buffer overflow in the kernel. moonlight-common-c asks for a large SO_RCVBUF; Linux
 *    clamps it to net.core.rmem_max (about 208 KB by default). /proc/net/snmp "Udp: RcvbufErrors"
 *    counts datagrams dropped for a full socket buffer (system-wide); its change per 5 s window goes
 *    into the stats line, and rmem_max is logged once at stream start.
 *
 *  - Native (lane wave1-burst): receive drops neither of the above shows, for the 5 s line's
 *    "rx drop misc" tag: the stream interface's rx dropped / errors / fifo in /proc/net/dev (the
 *    driver or the stack threw the packet away before any socket), Udp MemErrors and InCsumErrors
 *    in /proc/net/snmp, and the drops column of /proc/net/udp(6) for this process's own UDP sockets
 *    (found through /proc/self/fd), which says whether the system-wide RcvbufErrors are ours.
 *
 * The parsers are pure (text in, numbers out) so the tests feed them samples; the readers take a
 * proc root, so the tests point them at a directory of synthetic files. */
#ifndef MLXR_NETDIAG_H
#define MLXR_NETDIAG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- Wi-Fi power save ---- */

/* `iw dev <if> get power_save` output ("Power save: on" / "Power save: off"): 1 on, 0 off, -1 not understood. */
int mlxr_wifi_ps_parse(const char *iw_output);
/* Runs iw as the user (no shell, no escalation). -1 when iw is missing or fails, else 1 on / 0 off. */
int mlxr_wifi_ps_get(const char *ifname);
/* `iw dev <if> set power_save on|off`. False when iw is missing or refuses (usually: not permitted). */
bool mlxr_wifi_ps_set(const char *ifname, bool on);

typedef struct {
    bool active;                /* power save was on and was turned off: end() turns it back on */
    char ifname[16];
} mlxr_wifi_ps_guard;

/* At stream start. `host_addr` is the PC's address; the interface it is reached through is the one
 * looked at. Logs what it finds and does (lines start "wifi power save:"). */
void mlxr_wifi_ps_begin(mlxr_wifi_ps_guard *g, const char *host_addr);
/* At stream end: undoes what begin() changed. Safe to call twice or without begin(). */
void mlxr_wifi_ps_end(mlxr_wifi_ps_guard *g);

/* ---- kernel UDP counters ---- */

typedef struct {
    bool valid;
    uint64_t in_datagrams;
    uint64_t in_errors;
    uint64_t rcvbuf_errors;     /* datagrams dropped because a socket's receive buffer was full */
    uint64_t mem_errors;        /* Native (lane wave1-burst): MemErrors, 0 when the kernel has no column */
    uint64_t csum_errors;       /* InCsumErrors, likewise */
} mlxr_udp_counters;

/* The "Udp:" header and value lines of /proc/net/snmp text. False when RcvbufErrors is not there. */
bool mlxr_udp_parse(const char *snmp, mlxr_udp_counters *out);
bool mlxr_udp_read(mlxr_udp_counters *out);                 /* from /proc/net/snmp */
/* "udp rcvbuf errors +3 (system total 120), in errors +3" ; "udp counters unavailable" when either is not valid.
 * A counter that went down (a reset) counts as the new value. */
void mlxr_udp_delta_text(const mlxr_udp_counters *prev, const mlxr_udp_counters *cur, char *out, size_t cap);

/* net.core.rmem_max file text ("212992\n"): the number, -1 when not one. */
long long mlxr_rmem_max_parse(const char *text);
long long mlxr_rmem_max_read(void);                         /* from /proc/sys/net/core/rmem_max, -1 unreadable */

/* ---- receive drops not shown elsewhere (Native, lane wave1-burst) ---- */

typedef struct {
    bool if_valid;
    char ifname[16];
    uint64_t if_errs, if_drop, if_fifo;   /* /proc/net/dev receive columns of ifname */
    bool udp_valid;
    uint64_t mem_errors, csum_errors;     /* /proc/net/snmp Udp */
    bool sock_valid;
    int sockets;                          /* this process's UDP sockets found */
    uint64_t sock_drops;                  /* their drops column, summed */
} mlxr_rx_misc;

/* /proc/net/dev text: the receive errs, drop and fifo of ifname. False when the interface is not there. */
bool mlxr_netdev_parse(const char *text, const char *ifname, uint64_t *errs, uint64_t *drop, uint64_t *fifo);
/* /proc/net/udp or udp6 text: adds the drops of the sockets whose inode is in inodes[0..n) to *drops.
 * Returns how many matched, -1 when the text is not such a table. */
int mlxr_udp_sock_drops_parse(const char *text, const unsigned long *inodes, int n, uint64_t *drops);
/* The socket inodes of the links in an fd directory (/proc/self/fd: "socket:[12345]"). Count, -1 unreadable. */
int mlxr_fd_socket_inodes(const char *fd_dir, unsigned long *out, int cap);
/* All of it under a proc root ("/proc" in the app): net/dev, net/snmp, net/udp, net/udp6, self/fd. */
void mlxr_rx_misc_read(const char *proc_root, const char *ifname, mlxr_rx_misc *out);
/* "rx drop misc +5 (wlan0 drop +5 err +0 fifo +0, udp mem +0 csum +0, own sockets +0 of 3)". The total
 * is the interface's and the Udp counters' changes (the own sockets' drops are already in the rcvbuf and
 * in errors, so they only say whose those were). A part not read twice says "?". */
void mlxr_rx_misc_delta_text(const mlxr_rx_misc *prev, const mlxr_rx_misc *cur, char *out, size_t cap);
/* The interface the stream reaches addr through, into out ("" and false when none). */
bool mlxr_net_ifname_for(const char *addr, char *out, size_t cap);

#endif
