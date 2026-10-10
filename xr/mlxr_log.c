// Moonlight XR Native: minimal logging. GPLv3.
#include "mlxr_log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void mlxr_logf(char level, const char* fmt, ...) {
    static struct timespec t0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&g_lock);
    if (!t0.tv_sec && !t0.tv_nsec) t0 = ts;
    double t = (ts.tv_sec - t0.tv_sec) + (ts.tv_nsec - t0.tv_nsec) * 1e-9;
    fprintf(stdout, "[%9.3f] %c ", t, level);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
    pthread_mutex_unlock(&g_lock);
}
