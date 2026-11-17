// Moonlight XR Native (1.3.4): hosts that end the app when their last client leaves (stream/host_app.h):
// what a resume found, what is learnt from it, when the failover holds, which notice each case has, and the
// file it is remembered in. GPLv3.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../stream/host_app.h"

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void outcomes(void) {
    // the same app resumed
    CHECK(mlxr_hostapp_outcome_of(881, 881, 200, NULL) == MLXR_HOSTAPP_RESUMED);
    // Vibepollo/Apollo after "Terminating app ... when all clients are disconnected": currentgame 0
    CHECK(mlxr_hostapp_outcome_of(881, 0, 0, NULL) == MLXR_HOSTAPP_ENDED);
    // or /serverinfo still said the app (it was being ended) and /resume answered 503, no app running
    const char* NOAPP = "host returned status 503: No running app to resume";
    CHECK(mlxr_hostapp_outcome_of(881, 881, 503, NOAPP) == MLXR_HOSTAPP_ENDED);
    CHECK(mlxr_hostapp_outcome_of(881, -1, 503, NOAPP) == MLXR_HOSTAPP_ENDED);
    // a 503 that says something else (the concurrent stream limit, met by a quick reconnect while the host
    // still counts the lost session) or nothing is not an ended app: the reconnect goes on trying
    CHECK(mlxr_hostapp_outcome_of(881, 881, 503, "host returned status 503: The host's concurrent stream limit has been "
                                                 "reached. Stop an existing stream or increase the 'Channels' value in the "
                                                 "Sunshine Web UI.") == MLXR_HOSTAPP_UNSURE);
    CHECK(mlxr_hostapp_outcome_of(881, 881, 503, NULL) == MLXR_HOSTAPP_UNSURE);
    CHECK(mlxr_hostapp_outcome_of(881, 881, 503, "") == MLXR_HOSTAPP_UNSURE);
    // another app: says nothing of the setting
    CHECK(mlxr_hostapp_outcome_of(881, 42, 0, NULL) == MLXR_HOSTAPP_OTHER);
    // no answer, or another refusal
    CHECK(mlxr_hostapp_outcome_of(881, -1, 0, NULL) == MLXR_HOSTAPP_UNSURE);
    CHECK(mlxr_hostapp_outcome_of(881, 881, 0, NULL) == MLXR_HOSTAPP_UNSURE);
    CHECK(mlxr_hostapp_outcome_of(881, 881, 401, NULL) == MLXR_HOSTAPP_UNSURE);

    CHECK(mlxr_hostapp_learn(MLXR_HOSTAPP_UNKNOWN, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_ENDS);
    CHECK(mlxr_hostapp_learn(MLXR_HOSTAPP_ENDS, MLXR_HOSTAPP_RESUMED) == MLXR_HOSTAPP_KEEPS);   // the setting changed
    CHECK(mlxr_hostapp_learn(MLXR_HOSTAPP_KEEPS, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_ENDS);
    CHECK(mlxr_hostapp_learn(MLXR_HOSTAPP_ENDS, MLXR_HOSTAPP_OTHER) == MLXR_HOSTAPP_ENDS);
    CHECK(mlxr_hostapp_learn(MLXR_HOSTAPP_KEEPS, MLXR_HOSTAPP_UNSURE) == MLXR_HOSTAPP_KEEPS);
    CHECK(mlxr_hostapp_learn(MLXR_HOSTAPP_UNKNOWN, MLXR_HOSTAPP_UNSURE) == MLXR_HOSTAPP_UNKNOWN);
}

static void policy(void) {
    // only the failover is the client's own choice, and it holds on a PC and app known to end the app
    CHECK(!mlxr_hostapp_may_leave(MLXR_HOSTAPP_ENDS, MLXR_HOSTAPP_WHY_FAILOVER));
    CHECK(mlxr_hostapp_may_leave(MLXR_HOSTAPP_UNKNOWN, MLXR_HOSTAPP_WHY_FAILOVER));
    CHECK(mlxr_hostapp_may_leave(MLXR_HOSTAPP_KEEPS, MLXR_HOSTAPP_WHY_FAILOVER));
    for (int w = MLXR_HOSTAPP_WHY_DROP; w <= MLXR_HOSTAPP_WHY_IDLE; w++) {
        CHECK(mlxr_hostapp_may_leave(MLXR_HOSTAPP_ENDS, (mlxr_hostapp_why)w));
    }
    // the notices before leaving
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_ENDS, MLXR_HOSTAPP_WHY_FAILOVER) == MLXR_HOSTAPP_NOTICE_HELD);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_UNKNOWN, MLXR_HOSTAPP_WHY_FAILOVER) == MLXR_HOSTAPP_NOTICE_SWITCH_MAY_END);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_KEEPS, MLXR_HOSTAPP_WHY_FAILOVER) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_ENDS, MLXR_HOSTAPP_WHY_DOFF) == MLXR_HOSTAPP_NOTICE_MAY_END);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_ENDS, MLXR_HOSTAPP_WHY_IDLE) == MLXR_HOSTAPP_NOTICE_MAY_END);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_UNKNOWN, MLXR_HOSTAPP_WHY_IDLE) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_ENDS, MLXR_HOSTAPP_WHY_DROP) == MLXR_HOSTAPP_NOTICE_NONE);
    // and after a resume
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_FAILOVER, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_NOTICE_ENDED);
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_DROP, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_NOTICE_ENDED);
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_DROP, MLXR_HOSTAPP_RESUMED) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_DROP, MLXR_HOSTAPP_OTHER) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_DOFF, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(strcmp(mlxr_hostapp_notice_name(MLXR_HOSTAPP_NOTICE_HELD), "held") == 0);
}

static void file(void) {
    char dir[] = "/tmp/mlxr-hostapp-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    const char* A = "6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B";
    const char* B = "11111111-2222-3333-4444-555555555555";
    CHECK(mlxr_hostapp_load(dir, A, 881) == MLXR_HOSTAPP_UNKNOWN);   // no file
    CHECK(mlxr_hostapp_save(dir, A, 881, MLXR_HOSTAPP_ENDS) == 0);
    CHECK(mlxr_hostapp_save(dir, B, 881, MLXR_HOSTAPP_KEEPS) == 0);
    CHECK(mlxr_hostapp_save(dir, A, 7, MLXR_HOSTAPP_KEEPS) == 0);
    CHECK(mlxr_hostapp_load(dir, A, 881) == MLXR_HOSTAPP_ENDS);
    CHECK(mlxr_hostapp_load(dir, B, 881) == MLXR_HOSTAPP_KEEPS);
    CHECK(mlxr_hostapp_load(dir, A, 7) == MLXR_HOSTAPP_KEEPS);
    CHECK(mlxr_hostapp_load(dir, B, 7) == MLXR_HOSTAPP_UNKNOWN);
    // learnt again: replaced, the others kept
    CHECK(mlxr_hostapp_save(dir, A, 881, MLXR_HOSTAPP_KEEPS) == 0);
    CHECK(mlxr_hostapp_load(dir, A, 881) == MLXR_HOSTAPP_KEEPS && mlxr_hostapp_load(dir, B, 881) == MLXR_HOSTAPP_KEEPS);
    // unknown takes the line away
    CHECK(mlxr_hostapp_save(dir, B, 881, MLXR_HOSTAPP_UNKNOWN) == 0 && mlxr_hostapp_load(dir, B, 881) == MLXR_HOSTAPP_UNKNOWN);
    // junk lines and bad arguments
    char path[600];
    snprintf(path, sizeof path, "%s/host-apps.ini", dir);
    FILE* f = fopen(path, "a");
    CHECK(f != NULL);
    if (f) {
        fputs("nonsense\nC 0 ends\nD 5 maybe\n\n", f);
        fclose(f);
    }
    CHECK(mlxr_hostapp_load(dir, A, 881) == MLXR_HOSTAPP_KEEPS && mlxr_hostapp_load(dir, "D", 5) == MLXR_HOSTAPP_UNKNOWN);
    CHECK(mlxr_hostapp_save(dir, "has space", 1, MLXR_HOSTAPP_ENDS) == -1);
    CHECK(mlxr_hostapp_save(NULL, A, 1, MLXR_HOSTAPP_ENDS) == -1);
    CHECK(mlxr_hostapp_load(dir, A, 0) == MLXR_HOSTAPP_UNKNOWN);
    // at most MLXR_HOSTAPP_MAX lines, the oldest dropped
    for (int i = 1; i <= MLXR_HOSTAPP_MAX + 3; i++) CHECK(mlxr_hostapp_save(dir, B, 1000 + i, MLXR_HOSTAPP_ENDS) == 0);
    CHECK(mlxr_hostapp_load(dir, B, 1000 + MLXR_HOSTAPP_MAX + 3) == MLXR_HOSTAPP_ENDS);
    CHECK(mlxr_hostapp_load(dir, A, 881) == MLXR_HOSTAPP_UNKNOWN);   // the oldest went
    unlink(path);
    rmdir(dir);
}

int main(void) {
    outcomes();
    policy();
    file();
    printf("stream_host_app: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
