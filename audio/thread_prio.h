// Raising the calling thread's scheduling priority without privileges, and
// reading how long a thread waited for a CPU.
//
// SteamOS runs rtkit (org.freedesktop.RealtimeKit1 on the system bus), the
// same service SDL asks for its audio thread (which stock moonlight-qt plays
// through). We talk to it through libsystemd's sd-bus, opened with dlopen so
// the binary gains no link dependency and simply skips this when it is absent.
#ifndef MLXR_THREAD_PRIO_H
#define MLXR_THREAD_PRIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MLXR_PRIO_OFF = 0,   // leave the thread alone
    MLXR_PRIO_NICE,      // nice -10 (setpriority if allowed, else rtkit MakeThreadHighPriority)
    MLXR_PRIO_REALTIME,  // SCHED_RR through rtkit MakeThreadRealtime, nice as the fallback
} MlxrPrioMode;

/** "off" / "nice" / "rt" (NULL or unknown: def). */
MlxrPrioMode mlxr_prio_mode_parse(const char* s, MlxrPrioMode def);

/**
 * Raises the calling thread. Returns the mode actually reached (OFF when
 * nothing worked) and says what happened in `said` (for the log). Blocks for a
 * D-Bus round trip or two (a few ms) when rtkit is needed: call it once.
 */
MlxrPrioMode mlxr_thread_raise_priority(MlxrPrioMode want, char* said, int saidLen);

/** Native (lane perf): the same for another thread of this process (kernel id). */
MlxrPrioMode mlxr_thread_raise_priority_tid(int tid, MlxrPrioMode want, char* said, int saidLen);

/** The calling thread's kernel id. */
int mlxr_thread_id(void);

/**
 * Cumulative CPU time and run-queue wait (time runnable but not running) of a
 * thread of this process, from /proc/self/task/<tid>/schedstat. 0 on success.
 */
int mlxr_thread_schedstat(int tid, uint64_t* cpuNs, uint64_t* waitNs);

#ifdef __cplusplus
}
#endif
#endif
