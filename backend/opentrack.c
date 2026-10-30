/* Native (lane wave4-headinput, PLAN 4.5): see opentrack.h. GPLv3. */
#define _GNU_SOURCE
#include "opentrack.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "internal.h"

#define OT_HOST_MAX 256

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_streamHost[OT_HOST_MAX];
/* The target asked for (host and port as handed in), what it resolved to, and the socket */
static char g_wantHost[OT_HOST_MAX];
static int g_wantPort = -1;
static struct sockaddr_storage g_addr;
static socklen_t g_addrLen;
static int g_resolved;      /* 1 resolved, 0 not yet, -1 failed */
static unsigned g_generation;
static int g_fd = -1;
static int g_saidError;

void mlxr_opentrack_set_stream_host(const char *addr)
{
    pthread_mutex_lock(&g_lock);
    snprintf(g_streamHost, sizeof g_streamHost, "%s", addr ? addr : "");
    pthread_mutex_unlock(&g_lock);
}

static int targetText(char *out, size_t cap);

static void closeSocket(void)
{
    if (g_fd >= 0) close(g_fd);
    g_fd = -1;
}

/* Under the lock: the result of resolving the target for this generation */
static void takeResult(unsigned generation, const struct addrinfo *ai, int ok)
{
    if (generation != g_generation) return; /* the target changed meanwhile */
    if (!ok || !ai || ai->ai_addrlen > sizeof g_addr) {
        g_resolved = -1;
        mlxr_log(0, "opentrack: %s could not be resolved; no head pose is sent", g_wantHost);
        return;
    }
    memcpy(&g_addr, ai->ai_addr, ai->ai_addrlen);
    g_addrLen = (socklen_t)ai->ai_addrlen;
    g_resolved = 1;
    closeSocket(); /* the family may have changed */
    char text[80];
    targetText(text, sizeof text);
    mlxr_log(1, "opentrack: sending the head pose to %s", text);
}

typedef struct {
    char host[OT_HOST_MAX];
    char port[8];
    unsigned generation;
} Resolve;

static void *resolveThread(void *arg)
{
    Resolve *r = arg;
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_DGRAM, .ai_flags = AI_NUMERICSERV };
    struct addrinfo *ai = NULL;
    int rc = getaddrinfo(r->host, r->port, &hints, &ai);
    pthread_mutex_lock(&g_lock);
    takeResult(r->generation, ai, rc == 0);
    pthread_mutex_unlock(&g_lock);
    if (ai) freeaddrinfo(ai);
    free(r);
    return NULL;
}

/* Under the lock: a new target. An address is taken at once; a name goes to a thread. */
static void startResolve(const char *host, int port)
{
    snprintf(g_wantHost, sizeof g_wantHost, "%s", host);
    g_wantPort = port;
    g_resolved = 0;
    g_saidError = 0;
    g_generation++;
    closeSocket();
    char portText[8];
    snprintf(portText, sizeof portText, "%d", port);
    struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_DGRAM,
                              .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV };
    struct addrinfo *ai = NULL;
    if (getaddrinfo(host, portText, &hints, &ai) == 0) {
        takeResult(g_generation, ai, 1);
        freeaddrinfo(ai);
        return;
    }
    Resolve *r = calloc(1, sizeof *r);
    pthread_t th;
    pthread_attr_t attr;
    if (!r || pthread_attr_init(&attr) != 0) {
        free(r);
        g_resolved = -1;
        return;
    }
    snprintf(r->host, sizeof r->host, "%s", host);
    snprintf(r->port, sizeof r->port, "%s", portText);
    r->generation = g_generation;
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &attr, resolveThread, r) != 0) {
        free(r);
        g_resolved = -1;
    }
    pthread_attr_destroy(&attr);
}

int mlxr_opentrack_send(const char *host, int port, const uint8_t *pkt, size_t len)
{
    /* Never wait on the lock: a resolver finishing this instant costs one packet */
    if (pthread_mutex_trylock(&g_lock) != 0) return 0;
    int sent = 0;
    const char *want = host && *host ? host : g_streamHost;
    if (!*want) {
        /* no PC to send to (no stream, or its address not known): forget any old target */
        if (g_wantHost[0]) {
            g_wantHost[0] = 0;
            g_wantPort = -1;
            g_resolved = 0;
            g_generation++;
            closeSocket();
        }
        pthread_mutex_unlock(&g_lock);
        return 0;
    }
    if (port != g_wantPort || strcmp(want, g_wantHost) != 0) startResolve(want, port);
    if (g_resolved < 0) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    if (g_resolved == 1) {
        if (g_fd < 0) g_fd = socket(g_addr.ss_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (g_fd >= 0) {
            ssize_t n = sendto(g_fd, pkt, len, MSG_DONTWAIT | MSG_NOSIGNAL, (struct sockaddr *)&g_addr, g_addrLen);
            if (n == (ssize_t)len) {
                sent = 1;
                g_saidError = 0;
            }
            else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && !g_saidError) {
                /* an unreachable network or a refused port (ICMP from an earlier packet) is said once, and
                 * the sender keeps trying: opentrack may be started later */
                g_saidError = 1;
                mlxr_log(1, "opentrack: send to %s failed (%s), still trying", g_wantHost, strerror(errno));
            }
        }
        else if (!g_saidError) {
            g_saidError = 1;
            mlxr_log(0, "opentrack: no UDP socket (%s)", strerror(errno));
        }
    }
    pthread_mutex_unlock(&g_lock);
    return sent;
}

void mlxr_opentrack_close(void)
{
    pthread_mutex_lock(&g_lock);
    closeSocket();
    g_wantHost[0] = 0;
    g_wantPort = -1;
    g_resolved = 0;
    g_generation++;
    pthread_mutex_unlock(&g_lock);
}

int mlxr_opentrack_target(char *out, size_t cap)
{
    pthread_mutex_lock(&g_lock);
    int r = targetText(out, cap);
    pthread_mutex_unlock(&g_lock);
    return r;
}

/* Under the lock */
static int targetText(char *out, size_t cap)
{
    if (cap) out[0] = 0;
    if (g_resolved != 1) return 0;
    char a[INET6_ADDRSTRLEN] = "";
    int port = 0;
    if (g_addr.ss_family == AF_INET) {
        const struct sockaddr_in *s = (const struct sockaddr_in *)&g_addr;
        inet_ntop(AF_INET, &s->sin_addr, a, sizeof a);
        port = ntohs(s->sin_port);
        snprintf(out, cap, "%s:%d", a, port);
    }
    else if (g_addr.ss_family == AF_INET6) {
        const struct sockaddr_in6 *s = (const struct sockaddr_in6 *)&g_addr;
        inet_ntop(AF_INET6, &s->sin6_addr, a, sizeof a);
        port = ntohs(s->sin6_port);
        snprintf(out, cap, "[%s]:%d", a, port);
    }
    return 1;
}
