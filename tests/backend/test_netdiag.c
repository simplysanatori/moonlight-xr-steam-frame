/* Tests for backend/netdiag: the iw power_save, /proc/net/snmp Udp and rmem_max parsers, and the
 * stats-line delta text. Fed with sample text; nothing touches the system. GPLv3. */
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

int main(void)
{
    test_power_save();
    test_udp_parse();
    test_delta_text();
    test_rmem();
    TEST_DONE();
}
