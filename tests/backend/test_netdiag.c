/* Tests for backend/netdiag: the iw power_save, /proc/net/snmp Udp and rmem_max parsers, and the
 * stats-line delta text. Fed with sample text; nothing touches the system. GPLv3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "netdiag.h"

static const char *const SNMP =
    "Ip: Forwarding DefaultTTL InReceives InHdrErrors\n"
    "Ip: 2 64 912345 0\n"
    "Icmp: InMsgs InErrors\n"
    "Icmp: 10 0\n"
    "Tcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens\n"
    "Tcp: 1 200 120000 -1 4321\n"
    "Udp: InDatagrams NoPorts InErrors OutDatagrams RcvbufErrors SndbufErrors InCsumErrors IgnoredMulti MemErrors\n"
    "Udp: 482113 17 40 120554 38 2 0 11 0\n"
    "UdpLite: InDatagrams NoPorts InErrors OutDatagrams RcvbufErrors SndbufErrors InCsumErrors IgnoredMulti MemErrors\n"
    "UdpLite: 0 0 0 0 0 0 0 0 0\n";

static void test_power_save(void)
{
    CHECK_EQ_INT(mlxr_wifi_ps_parse("Power save: on\n"), 1);
    CHECK_EQ_INT(mlxr_wifi_ps_parse("Power save: off\n"), 0);
    CHECK_EQ_INT(mlxr_wifi_ps_parse("Power save: off"), 0);
    CHECK_EQ_INT(mlxr_wifi_ps_parse("power save:   ON\n"), 1);
    CHECK_EQ_INT(mlxr_wifi_ps_parse("\nPower save: on\n\n"), 1);
    CHECK_EQ_INT(mlxr_wifi_ps_parse(""), -1);
    CHECK_EQ_INT(mlxr_wifi_ps_parse("command failed: No such device (-19)\n"), -1);
    CHECK_EQ_INT(mlxr_wifi_ps_parse("Power save: maybe\n"), -1);
    CHECK_EQ_INT(mlxr_wifi_ps_parse("Power save: onward\n"), -1);
    CHECK_EQ_INT(mlxr_wifi_ps_parse(NULL), -1);
}

static void test_udp_parse(void)
{
    mlxr_udp_counters c;
    CHECK(mlxr_udp_parse(SNMP, &c));
    CHECK(c.valid);
    CHECK_EQ_INT((long long)c.in_datagrams, 482113);
    CHECK_EQ_INT((long long)c.in_errors, 40);
    CHECK_EQ_INT((long long)c.rcvbuf_errors, 38);

    /* the column position is read from the header, not assumed */
    CHECK(mlxr_udp_parse("Udp: RcvbufErrors InErrors InDatagrams\nUdp: 7 8 9\n", &c));
    CHECK_EQ_INT((long long)c.rcvbuf_errors, 7);

    /* no Udp section, no RcvbufErrors column, a header without values, empty */
    CHECK(!mlxr_udp_parse("Ip: Forwarding\nIp: 2\n", &c));
    CHECK(!c.valid);
    CHECK(!mlxr_udp_parse("Udp: InDatagrams NoPorts\nUdp: 1 2\n", &c));
    CHECK(!c.valid);
    CHECK(!mlxr_udp_parse("Udp: InDatagrams RcvbufErrors\n", &c));
    CHECK(!mlxr_udp_parse("", &c));
    CHECK(!mlxr_udp_parse(NULL, &c));

    /* UdpLite alone must not be taken for Udp */
    CHECK(!mlxr_udp_parse("UdpLite: InDatagrams RcvbufErrors\nUdpLite: 5 6\n", &c));
}

static void test_delta_text(void)
{
    mlxr_udp_counters a, b;
    char t[200];
    CHECK(mlxr_udp_parse(SNMP, &a));
    b = a;
    b.rcvbuf_errors += 12;
    b.in_errors += 12;
    mlxr_udp_delta_text(&a, &b, t, sizeof t);
    CHECK_EQ_STR(t, "udp rcvbuf errors +12 (system total 50), in errors +12");

    mlxr_udp_delta_text(&a, &a, t, sizeof t);
    CHECK_EQ_STR(t, "udp rcvbuf errors +0 (system total 38), in errors +0");

    /* a counter that went down (reset) counts as the new value */
    b = a;
    b.rcvbuf_errors = 3;
    mlxr_udp_delta_text(&a, &b, t, sizeof t);
    CHECK_EQ_STR(t, "udp rcvbuf errors +3 (system total 3), in errors +0");

    mlxr_udp_counters none = { 0 };
    mlxr_udp_delta_text(&none, &b, t, sizeof t);
    CHECK_EQ_STR(t, "udp counters unavailable");
    mlxr_udp_delta_text(&a, &none, t, sizeof t);
    CHECK_EQ_STR(t, "udp counters unavailable");
}

static void test_rmem(void)
{
    CHECK_EQ_INT(mlxr_rmem_max_parse("212992\n"), 212992);
    CHECK_EQ_INT(mlxr_rmem_max_parse("4194304"), 4194304);
    CHECK_EQ_INT(mlxr_rmem_max_parse("  26214400\n"), 26214400);
    CHECK_EQ_INT(mlxr_rmem_max_parse(""), -1);
    CHECK_EQ_INT(mlxr_rmem_max_parse("\n"), -1);
    CHECK_EQ_INT(mlxr_rmem_max_parse("abc"), -1);
    CHECK_EQ_INT(mlxr_rmem_max_parse("-5"), -1);
    CHECK_EQ_INT(mlxr_rmem_max_parse(NULL), -1);
}

/* ---- Native (lane wave1-burst): the "rx drop misc" readers on synthetic proc text */

static const char *const NETDEV =
    "Inter-|   Receive                                                |  Transmit\n"
    " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n"
    "    lo:  123456     789    0    0    0     0          0         0   123456     789    0    0    0     0       0          0\n"
    " wlan0: 9876543210 7654321  3   41    2     0          0       12 1234567  23456    0    0    0     0       0          0\n"
    "wlan01: 1 2 99 99 99 0 0 0 0 0 0 0 0 0 0 0\n";

/* two of this process's sockets (inodes 4242 and 5151) among others; one in udp6 */
static const char *const UDP4 =
    "   sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n"
    "  211: 00000000:14E9 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 4242 2 0000000000000000 17\n"
    "  518: 00000000:BD62 00000000:0000 07 00000000:00000000 00:00000000 00000000   101        0 9999 2 0000000000000000 900\n";
static const char *const UDP6 =
    "  sl  local_address                         remote_address                        st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n"
    "  100: 00000000000000000000000000000000:BF05 00000000000000000000000000000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 5151 2 0000000000000000 3\n";

static void test_netdev_parse(void)
{
    uint64_t e = 0, d = 0, f = 0;
    CHECK(mlxr_netdev_parse(NETDEV, "wlan0", &e, &d, &f));
    CHECK_EQ_INT((long long)e, 3);
    CHECK_EQ_INT((long long)d, 41);
    CHECK_EQ_INT((long long)f, 2);
    CHECK(mlxr_netdev_parse(NETDEV, "lo", &e, &d, &f) && d == 0);
    CHECK(!mlxr_netdev_parse(NETDEV, "wlan", &e, &d, &f));      /* a prefix is not the interface */
    CHECK(!mlxr_netdev_parse(NETDEV, "usb0", &e, &d, &f));
    CHECK(!mlxr_netdev_parse(NETDEV, "", &e, &d, &f));
    CHECK(!mlxr_netdev_parse(NULL, "wlan0", &e, &d, &f));
    CHECK(!mlxr_netdev_parse(" wlan0: 12 x\n", "wlan0", &e, &d, &f));
}

static void test_sock_drops_parse(void)
{
    unsigned long ino[] = { 4242, 5151, 77 };
    uint64_t drops = 0;
    CHECK_EQ_INT(mlxr_udp_sock_drops_parse(UDP4, ino, 3, &drops), 1);
    CHECK_EQ_INT((long long)drops, 17);                          /* not the other process's 900 */
    CHECK_EQ_INT(mlxr_udp_sock_drops_parse(UDP6, ino, 3, &drops), 1);
    CHECK_EQ_INT((long long)drops, 20);
    CHECK_EQ_INT(mlxr_udp_sock_drops_parse(UDP4, ino, 0, &drops), 0);
    CHECK_EQ_INT(mlxr_udp_sock_drops_parse("garbage\n", ino, 3, &drops), -1);
    CHECK_EQ_INT(mlxr_udp_sock_drops_parse(NULL, ino, 3, &drops), -1);
}

static void put(const char *dir, const char *name, const char *text)
{
    char p[600];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "w");
    CHECK(f != NULL);
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void test_rx_misc_read(void)
{
    char root[] = "/tmp/mlxr-netdiag-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char p[600];
    snprintf(p, sizeof p, "%s/net", root);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/self", root);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/self/fd", root);
    mkdir(p, 0755);
    /* the fd links as /proc/self/fd has them: two sockets, a pipe, an eventfd */
    const char *links[][2] = { { "3", "socket:[4242]" }, { "4", "pipe:[100]" }, { "7", "socket:[5151]" },
                               { "9", "anon_inode:[eventfd]" } };
    for (int i = 0; i < 4; i++) {
        snprintf(p, sizeof p, "%s/self/fd/%s", root, links[i][0]);
        CHECK(symlink(links[i][1], p) == 0);
    }
    unsigned long ino[8];
    snprintf(p, sizeof p, "%s/self/fd", root);
    CHECK_EQ_INT(mlxr_fd_socket_inodes(p, ino, 8), 2);
    CHECK_EQ_INT(mlxr_fd_socket_inodes("/nonexistent-mlxr-dir", ino, 8), -1);

    put(root, "net/dev", NETDEV);
    put(root, "net/snmp", SNMP);
    put(root, "net/udp", UDP4);
    put(root, "net/udp6", UDP6);
    mlxr_rx_misc a, b;
    mlxr_rx_misc_read(root, "wlan0", &a);
    CHECK(a.if_valid && a.udp_valid && a.sock_valid);
    CHECK_EQ_INT((long long)a.if_drop, 41);
    CHECK_EQ_INT(a.sockets, 2);
    CHECK_EQ_INT((long long)a.sock_drops, 20);
    CHECK_EQ_INT((long long)a.mem_errors, 0);

    /* five seconds later: the driver dropped 9 more, the stack 2 for memory, our sockets 6 */
    put(root, "net/dev",
        " wlan0: 9876999999 7660000  3   50    2     0          0       12 1234567  23456    0    0    0     0       0          0\n");
    put(root, "net/snmp",
        "Udp: InDatagrams NoPorts InErrors OutDatagrams RcvbufErrors SndbufErrors InCsumErrors IgnoredMulti MemErrors\n"
        "Udp: 490000 17 46 120554 44 2 1 11 2\n");
    put(root, "net/udp",
        "   sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode ref pointer drops\n"
        "  211: 00000000:14E9 00000000:0000 07 00000000:00000000 00:00000000 00000000  1000        0 4242 2 0000000000000000 23\n");
    mlxr_rx_misc_read(root, "wlan0", &b);
    char t[200];
    mlxr_rx_misc_delta_text(&a, &b, t, sizeof t);
    CHECK_EQ_STR(t, "rx drop misc +12 (wlan0 drop +9 err +0 fifo +0, udp mem +2 csum +1, own sockets +6 of 2)");

    /* nothing read before (stream start), an interface not there, no proc at all */
    mlxr_rx_misc none;
    memset(&none, 0, sizeof none);
    mlxr_rx_misc_delta_text(&none, &b, t, sizeof t);
    CHECK_EQ_STR(t, "rx drop misc +0 (wlan0 ?, udp mem ? csum ?, own sockets ?)");
    mlxr_rx_misc_read(root, "usb0", &b);
    CHECK(!b.if_valid && b.udp_valid);
    mlxr_rx_misc_delta_text(&a, &b, t, sizeof t);
    CHECK_EQ_STR(t, "rx drop misc +3 (usb0 ?, udp mem +2 csum +1, own sockets +6 of 2)");
    mlxr_rx_misc_read("/nonexistent-mlxr-proc", "", &b);
    CHECK(!b.if_valid && !b.udp_valid && !b.sock_valid);
    mlxr_rx_misc_delta_text(&b, &b, t, sizeof t);
    CHECK_EQ_STR(t, "rx drop misc +0 (interface ?, udp mem ? csum ?, own sockets ?)");

    /* the Udp parser also takes MemErrors and InCsumErrors where the kernel has them */
    mlxr_udp_counters c;
    CHECK(mlxr_udp_parse("Udp: InDatagrams InErrors RcvbufErrors\nUdp: 1 2 3\n", &c) && c.mem_errors == 0 && c.csum_errors == 0);

    const char *files[] = { "net/dev", "net/snmp", "net/udp", "net/udp6", "self/fd/3", "self/fd/4", "self/fd/7",
                            "self/fd/9", "self/fd", "self", "net" };
    for (int i = 0; i < (int)(sizeof files / sizeof files[0]); i++) {
        snprintf(p, sizeof p, "%s/%s", root, files[i]);
        remove(p);
    }
    rmdir(root);
}

int main(void)
{
    test_power_save();
    test_udp_parse();
    test_delta_text();
    test_rmem();
    test_netdev_parse();
    test_sock_drops_parse();
    test_rx_misc_read();
    TEST_DONE();
}
