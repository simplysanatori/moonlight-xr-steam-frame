// Moonlight XR Native (lane wave0-commonc): log contract with moonlight-common-c. GPLv3.
//
// stream/session.c (clLog) and stream/rfi_watch.c do not get structured events
// from common-c, they match its Limelog() text. A bump of the vendored
// submodule that rewords one of those lines would silently blind the RFI
// watchdog and the loss counters. This test reads the vendored sources and
// asserts that every line the fork parses is still emitted verbatim, and that
// the fragment the fork matches is part of it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../xrcore/check.h"

#ifndef COMMONC_SRC_DIR
#error "COMMONC_SRC_DIR must point at third_party/moonlight-common-c/src"
#endif

typedef struct {
    const char* file;     // under COMMONC_SRC_DIR
    const char* format;   // the Limelog() format string, verbatim, without the trailing \n
    const char* matched;  // what the fork looks for in the formatted line (a fragment of format)
    const char* users;    // who matches it, for the failure message
} Contract;

static const Contract contracts[] = {
    // session.c: the frame-loss counters (and the sscanf "Network dropped %d frame")
    {"VideoDepacketizer.c", "Network dropped 1 frame (frame %d)", "Network dropped ", "session.c"},
    {"VideoDepacketizer.c", "Network dropped 1 frame (frame %d)", " frame", "session.c"},
    {"VideoDepacketizer.c", "Network dropped %d frames (frames %d to %d)", "Network dropped ", "session.c"},
    {"VideoDepacketizer.c", "Network dropped %d frames (frames %d to %d)", " frame", "session.c"},
    {"VideoDepacketizer.c", "Waiting for IDR frame", "Waiting for IDR frame", "session.c, rfi_watch.c"},
    {"VideoDepacketizer.c", "Waiting for RFI frame", "Waiting for RFI frame", "session.c, rfi_watch.c"},
    {"ControlStream.c", "IDR frame request sent", "IDR frame request sent", "session.c"},
    // rfi_watch.c: the RFI wait window (opened by a request, closed by the next frame)
    {"VideoDepacketizer.c", "Next post-invalidation frame is: %d (%s-frame)", "post-invalidation frame", "rfi_watch.c"},
    {"ControlStream.c", "Invalidate reference frame request sent (%d to %d)", "Invalidate reference frame request", "rfi_watch.c"},
    {"VideoDepacketizer.c", "Sending speculative RFI request for predicted loss of frame %d", "RFI request", "rfi_watch.c"},
    {"VideoDepacketizer.c", "Sending RFI request for unrecoverable frame %d", "RFI request", "rfi_watch.c"},
    // log_throttle.c: the lines written at most about once a second (the parsers above still see all)
    {"RtpVideoQueue.c", "Unrecoverable frame %d: %d+%d=%d received < %d needed", "Unrecoverable frame", "log_throttle.c"},
    {"RtpVideoQueue.c", "Unrecoverable frame %d: lost FEC blocks %d to %d", "Unrecoverable frame", "log_throttle.c"},
};

static char* slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    char* buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)n + 1);
            if (buf) {
                size_t got = fread(buf, 1, (size_t)n, f);
                buf[got] = 0;
            }
        }
    }
    fclose(f);
    return buf;
}

int main(void) {
    const char* files[] = {"VideoDepacketizer.c", "ControlStream.c", "RtpVideoQueue.c"};
    enum { NFILES = sizeof(files) / sizeof(files[0]) };
    char* src[NFILES] = {NULL};
    for (int i = 0; i < NFILES; i++) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", COMMONC_SRC_DIR, files[i]);
        src[i] = slurp(path);
        if (!src[i]) fprintf(stderr, "cannot read %s (submodule not checked out?)\n", path);
        CHECK(src[i] != NULL);
    }

    for (size_t c = 0; c < sizeof(contracts) / sizeof(contracts[0]); c++) {
        const Contract* k = &contracts[c];
        const char* text = NULL;
        for (int i = 0; i < NFILES; i++)
            if (strcmp(k->file, files[i]) == 0) text = src[i];
        if (!text) continue;
        // the format is emitted through Limelog("<format>\n"...)
        char needle[512];
        snprintf(needle, sizeof(needle), "Limelog(\"%s\\n\"", k->format);
        int found = strstr(text, needle) != NULL;
        if (!found) fprintf(stderr, "%s no longer emits \"%s\" (parsed by %s)\n", k->file, k->format, k->users);
        CHECK(found);
        // and the fragment the fork matches is still part of that line
        int part = strstr(k->format, k->matched) != NULL;
        if (!part) fprintf(stderr, "contract table error: \"%s\" is not in \"%s\"\n", k->matched, k->format);
        CHECK(part);
    }

    // The exact scan session.c runs on the formatted line must still yield the count
    char line[128];
    snprintf(line, sizeof(line), "Network dropped %d frames (frames %d to %d)", 3, 40, 42);
    int lost = 0;
    const char* d = strstr(line, "Network dropped ");
    CHECK(d && strstr(line, " frame") && sscanf(d, "Network dropped %d frame", &lost) == 1 && lost == 3);

    for (int i = 0; i < NFILES; i++) free(src[i]);
    return checksDone("commonc_log");
}
