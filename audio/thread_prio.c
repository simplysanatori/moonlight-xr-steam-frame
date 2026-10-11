#define _GNU_SOURCE
#include "thread_prio.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NICE_WANTED (-10)
#define RT_WANTED 10

MlxrPrioMode mlxr_prio_mode_parse(const char* s, MlxrPrioMode def) {
    if (s == NULL || *s == '\0') {
        return def;
    }
    if (strcmp(s, "off") == 0 || strcmp(s, "0") == 0) {
        return MLXR_PRIO_OFF;
    }
    if (strcmp(s, "nice") == 0) {
        return MLXR_PRIO_NICE;
    }
    if (strcmp(s, "rt") == 0 || strcmp(s, "realtime") == 0) {
        return MLXR_PRIO_REALTIME;
    }
    return def;
}

int mlxr_thread_id(void) {
    return (int) syscall(SYS_gettid);
}

int mlxr_thread_schedstat(int tid, uint64_t* cpuNs, uint64_t* waitNs) {
    char path[64];
    snprintf(path, sizeof path, "/proc/self/task/%d/schedstat", tid);
    FILE* f = fopen(path, "r");
    if (f == NULL) {
        return -1;
    }
    unsigned long long cpu = 0, wait = 0;
    int n = fscanf(f, "%llu %llu", &cpu, &wait);
    fclose(f);
    if (n != 2) {
        return -1;
    }
    *cpuNs = cpu;
    *waitNs = wait;
    return 0;
}

// ---- sd-bus through dlopen -----------------------------------------------------

typedef struct {
    const char* name;
    const char* message;
    int needFree;
} SdBusError;

typedef int (*SdOpenFn)(void** bus);
typedef int (*SdCallFn)(void* bus, const char* dest, const char* path, const char* iface,
                        const char* member, SdBusError* err, void** reply, const char* types, ...);
typedef int (*SdGetPropFn)(void* bus, const char* dest, const char* path, const char* iface,
                           const char* member, SdBusError* err, char type, void* ret);
typedef void* (*SdUnrefFn)(void* p);
typedef void (*SdErrFreeFn)(SdBusError* e);

typedef struct {
    void* lib;
    void* bus;
    SdCallFn call;
    SdGetPropFn getProp;
    SdUnrefFn msgUnref, busUnref;
    SdErrFreeFn errFree;
} Rtkit;

#define RTKIT_DEST "org.freedesktop.RealtimeKit1"
#define RTKIT_PATH "/org/freedesktop/RealtimeKit1"

static int rtkitOpen(Rtkit* r, char* why, int whyLen) {
    memset(r, 0, sizeof *r);
    r->lib = dlopen("libsystemd.so.0", RTLD_NOW | RTLD_LOCAL);
    if (r->lib == NULL) {
        snprintf(why, (size_t) whyLen, "no libsystemd");
        return -1;
    }
    SdOpenFn open = (SdOpenFn) dlsym(r->lib, "sd_bus_open_system");
    r->call = (SdCallFn) dlsym(r->lib, "sd_bus_call_method");
    r->getProp = (SdGetPropFn) dlsym(r->lib, "sd_bus_get_property_trivial");
    r->msgUnref = (SdUnrefFn) dlsym(r->lib, "sd_bus_message_unref");
    r->busUnref = (SdUnrefFn) dlsym(r->lib, "sd_bus_flush_close_unref");
    r->errFree = (SdErrFreeFn) dlsym(r->lib, "sd_bus_error_free");
    if (!open || !r->call || !r->getProp || !r->msgUnref || !r->busUnref || !r->errFree) {
        snprintf(why, (size_t) whyLen, "libsystemd lacks sd-bus");
        dlclose(r->lib);
        r->lib = NULL;
        return -1;
    }
    int rc = open(&r->bus);
    if (rc < 0 || r->bus == NULL) {
        snprintf(why, (size_t) whyLen, "no system bus (%s)", strerror(-rc));
        dlclose(r->lib);
        r->lib = NULL;
        return -1;
    }
    return 0;
}

static void rtkitClose(Rtkit* r) {
    if (r->bus != NULL) {
        r->busUnref(r->bus);
    }
    if (r->lib != NULL) {
        dlclose(r->lib);
    }
    memset(r, 0, sizeof *r);
}

static int rtkitProp(Rtkit* r, const char* name, char type, void* out) {
    SdBusError e = { 0 };
    int rc = r->getProp(r->bus, RTKIT_DEST, RTKIT_PATH, RTKIT_DEST, name, &e, type, out);
    r->errFree(&e);
    return rc;
}

static int rtkitCall(Rtkit* r, const char* member, const char* types, uint64_t tid, int32_t value,
                     char* why, int whyLen) {
    SdBusError e = { 0 };
    void* reply = NULL;
    int rc = r->call(r->bus, RTKIT_DEST, RTKIT_PATH, RTKIT_DEST, member, &e, &reply, types, tid,
                     value);
    if (rc < 0) {
        snprintf(why, (size_t) whyLen, "%s", e.message != NULL ? e.message : strerror(-rc));
    }
    if (reply != NULL) {
        r->msgUnref(reply);
    }
    r->errFree(&e);
    return rc;
}

static int raiseNice(Rtkit* r, int* haveRtkit, int tid, char* said, int saidLen) {
    if (setpriority(PRIO_PROCESS, (id_t) tid, NICE_WANTED) == 0) {
        snprintf(said, (size_t) saidLen, "nice %d", NICE_WANTED);
        return 0;
    }
    char why[160] = "";
    if (!*haveRtkit) {
        if (rtkitOpen(r, why, sizeof why) != 0) {
            snprintf(said, (size_t) saidLen, "left at normal priority (setpriority: %s; rtkit: %s)",
                     strerror(errno), why);
            return -1;
        }
        *haveRtkit = 1;
    }
    int32_t minNice = NICE_WANTED;
    if (rtkitProp(r, "MinNiceLevel", 'i', &minNice) < 0) {
        minNice = NICE_WANTED;
    }
    int32_t nice = minNice > NICE_WANTED ? minNice : NICE_WANTED;
    if (rtkitCall(r, "MakeThreadHighPriority", "ti", (uint64_t) tid, nice, why, sizeof why) < 0) {
        snprintf(said, (size_t) saidLen, "left at normal priority (rtkit: %s)", why);
        return -1;
    }
    snprintf(said, (size_t) saidLen, "nice %d (rtkit)", (int) nice);
    return 0;
}

MlxrPrioMode mlxr_thread_raise_priority(MlxrPrioMode want, char* said, int saidLen) {
    return mlxr_thread_raise_priority_tid(mlxr_thread_id(), want, said, saidLen);
}

// Native (lane perf): any thread of this process (rtkit takes a thread id of
// the caller's process; setpriority one of ours)
MlxrPrioMode mlxr_thread_raise_priority_tid(int tid, MlxrPrioMode want, char* said, int saidLen) {
    if (want == MLXR_PRIO_OFF) {
        snprintf(said, (size_t) saidLen, "left at normal priority (off)");
        return MLXR_PRIO_OFF;
    }
    Rtkit r;
    memset(&r, 0, sizeof r);
    int haveRtkit = 0;
    MlxrPrioMode got = MLXR_PRIO_OFF;
    char rtWhy[160] = "";
    if (want == MLXR_PRIO_REALTIME) {
        if (rtkitOpen(&r, rtWhy, sizeof rtWhy) == 0) {
            haveRtkit = 1;
            int32_t maxPrio = RT_WANTED;
            int64_t rttimeMax = 200000;
            if (rtkitProp(&r, "MaxRealtimePriority", 'i', &maxPrio) < 0) {
                maxPrio = RT_WANTED;
            }
            if (rtkitProp(&r, "RTTimeUSecMax", 'x', &rttimeMax) < 0) {
                rttimeMax = 200000;
            }
            // rtkit only grants realtime to a process whose RLIMIT_RTTIME is
            // bounded: a realtime thread that spins that long without
            // sleeping gets SIGXCPU instead of freezing the machine
            struct rlimit rl;
            if (getrlimit(RLIMIT_RTTIME, &rl) == 0
                && (rl.rlim_max == RLIM_INFINITY || rl.rlim_max > (rlim_t) rttimeMax)) {
                rl.rlim_cur = rl.rlim_max = (rlim_t) rttimeMax;
                setrlimit(RLIMIT_RTTIME, &rl);
            }
            int32_t prio = maxPrio < RT_WANTED ? maxPrio : RT_WANTED;
            if (rtkitCall(&r, "MakeThreadRealtime", "tu", (uint64_t) tid, prio, rtWhy,
                          sizeof rtWhy) >= 0) {
                snprintf(said, (size_t) saidLen, "realtime priority %d (rtkit)", (int) prio);
                got = MLXR_PRIO_REALTIME;
            }
        }
    }
    if (got == MLXR_PRIO_OFF) {
        char niceSaid[200] = "";
        if (raiseNice(&r, &haveRtkit, tid, niceSaid, sizeof niceSaid) == 0) {
            got = MLXR_PRIO_NICE;
        }
        if (want == MLXR_PRIO_REALTIME) {
            snprintf(said, (size_t) saidLen, "%s (realtime refused: %s)", niceSaid, rtWhy);
        }
        else {
            snprintf(said, (size_t) saidLen, "%s", niceSaid);
        }
    }
    rtkitClose(&r);
    return got;
}
