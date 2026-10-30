// Moonlight XR Native (lane deploy-res-fix): rate limit for common-c's "Unrecoverable frame"
// log lines. GPLv3.
//
// Why: on a lossy link common-c writes one "Unrecoverable frame N: a+b=c received < d needed"
// line per lost frame, hundreds a second. In the owner's 21:41 run they rotated the 5 MB
// moonlight.log in 3.5 minutes and took the first half of the run's evidence with them.
//
// What: a pure filter between clLog's parsers and the log file. The parsers (session.c's loss
// counters, rfi_watch.c) see every line first; this only decides whether a line is WRITTEN.
// Lines that are not "Unrecoverable frame" lines always pass unchanged. Of those, the first
// line of a burst is written verbatim, further ones within windowMs are dropped and counted,
// and the next one written after the window carries " ... (+N more)". The running total is
// kept for the stats line, and a burst's tail that no later line reports is flushed by the
// stats tick (mlxr_log_throttle_flush).
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct MlxrLogThrottle {
    uint64_t windowMs;
    uint64_t lastWriteMs;
    int wrote;            // a line has been written (lastWriteMs is valid)
    uint64_t suppressed;  // dropped since the last written line
    uint64_t seen;        // every "Unrecoverable frame" line, written or not
    uint64_t seenPrev;    // seen at the last mlxr_log_throttle_window()
} MlxrLogThrottle;

void mlxr_log_throttle_init(MlxrLogThrottle* t, uint64_t windowMs);
/** 1 when the line is a rate-limited kind (an "Unrecoverable frame" line). */
int mlxr_log_throttle_matches(const char* line);
/** One common-c log line at nowMs. Returns 1 to write it (out holds the text, with the
 *  suppressed-count suffix when lines were dropped before it), 0 to drop it. */
int mlxr_log_throttle_line(MlxrLogThrottle* t, const char* line, uint64_t nowMs, char* out, size_t outSize);
/** Lines dropped and not yet reported once the window since the last write has passed
 *  (resets the count), else 0. The caller logs "... (+N more)" for it. */
uint64_t mlxr_log_throttle_flush(MlxrLogThrottle* t, uint64_t nowMs);
/** "Unrecoverable frame" lines seen since the previous call (written or not). */
uint64_t mlxr_log_throttle_window(MlxrLogThrottle* t);
