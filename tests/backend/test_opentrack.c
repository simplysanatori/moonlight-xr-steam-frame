/* Native (lane wave4-headinput, PLAN 4.5): backend/opentrack, the head pose sender. A UDP socket on
 * the loopback stands in for opentrack: the packet arrives byte for byte, at the stream's PC or the
 * host given, a name is resolved off the caller's thread, no target sends nothing, and the send never
 * blocks. GPLv3. */
#include <arpa/inet.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "check.h"
#include "opentrack.h"

static int listener(int *port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = 0 };
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) != 0) return -1;
    socklen_t n = sizeof a;
    getsockname(fd, (struct sockaddr *)&a, &n);
    *port = ntohs(a.sin_port);
    return fd;
}

/* What arrived within ms, its length (0 nothing) */
static int receive(int fd, uint8_t *buf, size_t cap, int ms)
{
    struct pollfd p = { fd, POLLIN, 0 };
    if (poll(&p, 1, ms) <= 0) return 0;
    return (int)recv(fd, buf, cap, 0);
}

static long nowMs(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

int main(void)
{
    int port = 0;
    int fd = listener(&port);
    CHECK(fd >= 0);
    uint8_t pkt[48], got[64];
    for (int i = 0; i < 48; i++) pkt[i] = (uint8_t)(i * 7 + 1);

    /* no stream and no host: nothing to send to */
    mlxr_opentrack_set_stream_host("");
    CHECK_EQ_INT(mlxr_opentrack_send(NULL, port, pkt, sizeof pkt), 0);
    CHECK_EQ_INT(mlxr_opentrack_send("", port, pkt, sizeof pkt), 0);
    CHECK_EQ_INT(receive(fd, got, sizeof got, 50), 0);

    /* the stream's PC, an address: sent at once, byte for byte */
    mlxr_opentrack_set_stream_host("127.0.0.1");
    CHECK_EQ_INT(mlxr_opentrack_send(NULL, port, pkt, sizeof pkt), 1);
    CHECK_EQ_INT(receive(fd, got, sizeof got, 1000), 48);
    CHECK(memcmp(got, pkt, 48) == 0);
    char target[80];
    CHECK(mlxr_opentrack_target(target, sizeof target) == 1);
    char want[32];
    snprintf(want, sizeof want, "127.0.0.1:%d", port);
    CHECK_EQ_STR(target, want);
    /* frame after frame */
    for (int i = 0; i < 5; i++) CHECK_EQ_INT(mlxr_opentrack_send(NULL, port, pkt, sizeof pkt), 1);
    int count = 0;
    while (receive(fd, got, sizeof got, 200) == 48) count++;
    CHECK_EQ_INT(count, 5);

    /* the stream ends: nothing goes out, and the target is forgotten */
    mlxr_opentrack_set_stream_host(NULL);
    CHECK_EQ_INT(mlxr_opentrack_send(NULL, port, pkt, sizeof pkt), 0);
    CHECK(mlxr_opentrack_target(target, sizeof target) == 0);
    CHECK_EQ_INT(receive(fd, got, sizeof got, 50), 0);

    /* a name given in opentrack_host: resolved on a thread, so the first sends return at once and drop */
    long t0 = nowMs();
    int first = mlxr_opentrack_send("localhost", port, pkt, sizeof pkt);
    CHECK(nowMs() - t0 < 100);
    int sent = first == 1;
    while (!sent && nowMs() - t0 < 5000) {
        usleep(10000);
        sent = mlxr_opentrack_send("localhost", port, pkt, sizeof pkt) == 1;
    }
    /* localhost may resolve to ::1 first, where nothing listens; either way the call never blocked */
    if (sent && mlxr_opentrack_target(target, sizeof target) && strncmp(target, "127.", 4) == 0) {
        CHECK_EQ_INT(receive(fd, got, sizeof got, 1000), 48);
    }
    else {
        printf("localhost resolved to %s; the loopback receive is skipped\n", sent ? target : "nothing");
    }
    while (receive(fd, got, sizeof got, 50) > 0) {
    }

    /* the host given wins over the stream's PC, and a port change is a new target */
    mlxr_opentrack_set_stream_host("192.0.2.1");
    CHECK_EQ_INT(mlxr_opentrack_send("127.0.0.1", port, pkt, sizeof pkt), 1);
    CHECK_EQ_INT(receive(fd, got, sizeof got, 1000), 48);
    int port2 = 0;
    int fd2 = listener(&port2);
    CHECK_EQ_INT(mlxr_opentrack_send("127.0.0.1", port2, pkt, sizeof pkt), 1);
    CHECK_EQ_INT(receive(fd2, got, sizeof got, 1000), 48);
    CHECK_EQ_INT(receive(fd, got, sizeof got, 50), 0);

    /* closed: the next send opens again */
    mlxr_opentrack_close();
    CHECK(mlxr_opentrack_target(target, sizeof target) == 0);
    CHECK_EQ_INT(mlxr_opentrack_send("127.0.0.1", port2, pkt, sizeof pkt), 1);
    CHECK_EQ_INT(receive(fd2, got, sizeof got, 1000), 48);
    mlxr_opentrack_close();
    close(fd);
    close(fd2);
    TEST_DONE();
}
