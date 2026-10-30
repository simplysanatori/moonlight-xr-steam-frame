// Moonlight XR Native (lane perf): a watchdog on reference frame
// invalidation (RFI). GPLv3.
//
// Why: with RFI on (e439ea2), moonlight-common-c drops every frame after a
// loss until Sunshine sends the frame that re-references a good one. In the
// owner's 23:46 run (80 Mbps over the Frame's hotspot) 69 recoveries came
// back as such P-frames, all within 89 ms and after at most 3 dropped
// frames; the other 40 came back only as an IDR frame Sunshine fell back to,
// after 40 ms to 12 s (p90 0.6 s; 43 s of the 91 s run spent waiting, 1055
// complete frames thrown away). A recovery that has not come within ~100 ms
// is not coming as a P-frame: asking for an IDR frame then is what common-c
// does without RFI, so the long waits end in about a round trip.
//
// What: fed common-c's log lines (the only place it says this) and a clock,
// it says when to call LiRequestIdrFrame: once a wait is older than its
// wait AND at least minDrops complete frames were dropped in it (frames
// flow again, so the request is not lost in the same outage), then again
// every repeatMs while the wait goes on. Pure logic, no common-c calls.
//
// Native (lane wave1-burst): the wait adapts. Loss on the Frame's Wi-Fi comes
// in bursts (hundreds of ms to seconds), and an IDR frame is large: asked for
// while the burst is still dropping packets it lands in the same congested
// window, is lost or late, and the stall gets longer. So the wait is
//     base = max(timeoutMs, 3 x round trip)   (a recovery P-frame needs a round trip and an encode)
//     due  = max(wait start, last network loss) + base, but no later than wait start + ceilMs
// A clean link asks after `base` (100 ms on a LAN, as before). A burst that is
// still losing frames pushes the ask on until the link was clean for `base`,
// up to the ceiling, after which it asks anyway. mlxr_rfi_watch_wait_ms says
// what the wait is now (the log shows it).
//
// Also here, as pure logic fed the same lines:
//  - MlxrIdrCap: at most one IDR frame request of ours per minGapMs (the
//    [stream] rfi_idr_ms setting). It holds back the watchdog's asks and the
//    decoder's (hw_video's error and damaged-picture paths); common-c's own
//    requests (its IDR wait, DR_NEED_IDR) cannot be held and are only counted.
//  - MlxrBurst: a burst runs from the first lost or dropped frame to the last
//    one, and closes once gapMs pass without another. It then gives one
//    summary (mlxr_burst_text); while it is open, each per-frame line kind is
//    written once and then only counted (mlxr_burst_line's return).
#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct MlxrRfiWatch {
    int timeoutMs, minDrops, repeatMs; // timeoutMs 0: off; else the floor of the wait
    int ceilMs;                        // the longest the wait grows to in a burst
    uint32_t rttMs;                    // the latest round trip estimate (mlxr_rfi_watch_set_rtt)
    int waiting;                       // an RFI wait is open
    uint64_t startMs, lastIdrMs, lastLineMs;
    int anyLoss;                       // lastLossMs is valid
    uint64_t lastLossMs;               // the last network loss line
    int drops;                         // complete frames dropped in this wait
    int heldThisWait;                  // this wait's ask was put off by loss (counted once)
    int lastAskWaitMs;                 // the wait the last ask came after (for the log)
    // counters (the log takes deltas)
    uint64_t waits, closed, recovered, idrAsked;
    uint64_t waitMsSum, waitMsMax;     // closed waits
    uint64_t heldByLoss;               // waits whose ask was put off because the link was still losing
} MlxrRfiWatch;

void mlxr_rfi_watch_init(MlxrRfiWatch* w, int timeoutMs, int minDrops, int repeatMs);
/** The wait's ceiling (1000 ms after init; never below timeoutMs). */
void mlxr_rfi_watch_set_ceiling(MlxrRfiWatch* w, int ceilMs);
/** The current round trip estimate in ms (0: unknown). */
void mlxr_rfi_watch_set_rtt(MlxrRfiWatch* w, uint32_t rttMs);
/** The base wait: max(timeoutMs, 3 x rtt), held to the ceiling. 0 when off. */
int mlxr_rfi_watch_base_ms(const MlxrRfiWatch* w);
/** How long the open wait lasts before an ask, as of nowMs (its due time minus its start); the
 *  base wait when none is open. */
int mlxr_rfi_watch_wait_ms(const MlxrRfiWatch* w, uint64_t nowMs);
/** One common-c log line at nowMs. Returns 1 when an IDR frame is to be requested now. */
int mlxr_rfi_watch_line(MlxrRfiWatch* w, const char* line, uint64_t nowMs);

// ---- the IDR request cap ([stream] rfi_idr_ms)

typedef struct MlxrIdrCap {
    int minGapMs;          // 0: no cap
    int any;               // an ask went through (lastMs is valid)
    uint64_t lastMs;
    uint64_t passed, held; // asks let through / held back
} MlxrIdrCap;

void mlxr_idr_cap_init(MlxrIdrCap* c, int minGapMs);
/** 1: ask for the IDR frame now (counted as passed); 0: one was asked less than minGapMs ago (held). */
int mlxr_idr_cap_take(MlxrIdrCap* c, uint64_t nowMs);

// ---- bursts

/** What one common-c line is to the burst tracker. */
typedef enum {
    MLXR_BL_NONE = 0,
    MLXR_BL_NET_DROPPED,   // "Network dropped N frame(s)": frames lost on the network (loss)
    MLXR_BL_UNRECOVERABLE, // "Unrecoverable frame": FEC could not rebuild a frame (loss)
    MLXR_BL_RFI_SEND,      // "Sending (speculative) RFI request for ...": a recovery asked for (loss)
    MLXR_BL_RFI_SENT,      // "Invalidate reference frame request sent": the control stream sent it
    MLXR_BL_WAIT_RFI,      // "Waiting for RFI frame": a complete frame dropped waiting for the recovery
    MLXR_BL_WAIT_IDR,      // "Waiting for IDR frame": a complete frame dropped waiting for an IDR frame
    MLXR_BL_IDR_SENT,      // "IDR frame request sent": the control stream sent one (anyone's)
    MLXR_BL_IDR_FOR_DR,    // "Requesting IDR frame on behalf of DR": our decoder refused a frame
    MLXR_BL_REC_P,         // "Next post-invalidation frame is: n (P-frame)": recovered by RFI
    MLXR_BL_REC_I,         // "... (I-frame)": the host fell back to an IDR frame
    MLXR_BL_KINDS
} MlxrBurstLine;

/** The kind of a common-c line; *count (may be NULL) gets the frames a "Network dropped" line names. */
MlxrBurstLine mlxr_burst_classify(const char* line, int* count);

typedef enum { MLXR_REC_NONE = 0, MLXR_REC_RFI, MLXR_REC_IDR } MlxrRecovery;

typedef struct MlxrBurstSummary {
    uint64_t startMs, durMs;
    uint64_t netLost, unrecoverable; // frames lost on the network, FEC failures
    uint64_t rfiDrops, idrDrops;     // complete frames dropped waiting for RFI / for IDR
    uint64_t rfiRequests;            // common-c's RFI requests (loss reports to the host)
    uint64_t idrSent;                // IDR frame requests sent (anyone's)
    uint64_t idrOurs, idrHeld;       // asks of ours let through / held back by the cap
    uint64_t idrForDecoder;          // common-c's own on behalf of our decoder (not cappable)
    uint64_t folded;                 // per-frame lines not written
    MlxrRecovery recovery;           // what restored the picture after the last loss
    int stillBroken;                 // the picture was still waiting for a recovery at the close
} MlxrBurstSummary;

typedef struct MlxrBurst {
    int gapMs;               // a burst closes after this long without a lost or dropped frame
    int active;
    int broken;              // the picture is waiting for a recovery
    uint64_t lastEventMs;    // the last lost or dropped frame
    MlxrBurstSummary cur;
    unsigned char seen[MLXR_BL_KINDS]; // kinds already written once in this burst
    // totals (the log takes deltas)
    uint64_t bursts, burstMsSum, burstMsMax;
} MlxrBurst;

void mlxr_burst_init(MlxrBurst* b, int gapMs);
/** One common-c line at nowMs. Returns 1 when the line is to be written to the log, 0 when the open
 *  burst folds it (a per-frame kind already written once in this burst). A burst whose gap had
 *  passed is closed first: *closed is filled and 1 stored in *didClose (else 0). */
int mlxr_burst_line(MlxrBurst* b, const char* line, uint64_t nowMs, MlxrBurstSummary* closed, int* didClose);
/** An IDR frame reached the decoder (drSubmit): the picture is whole again. */
void mlxr_burst_idr_frame(MlxrBurst* b);
/** An IDR ask of ours: passed (1) or held back by the cap (0). */
void mlxr_burst_idr_ask(MlxrBurst* b, int passed);
/** The clock moved on (each frame, each stats tick): 1 and *closed filled when the burst closed. */
int mlxr_burst_tick(MlxrBurst* b, uint64_t nowMs, MlxrBurstSummary* closed);
/** The summary as one log line; startText is the burst's start as the caller formats wall time,
 *  capMs the rfi_idr_ms in force (named in the text). */
void mlxr_burst_text(const MlxrBurstSummary* s, const char* startText, int capMs, char* out, size_t cap);
