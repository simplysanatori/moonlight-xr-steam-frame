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

    // seen without a resume (the look after the timers' and the user's disconnects)
    CHECK(mlxr_hostapp_outcome_seen(881, 881) == MLXR_HOSTAPP_RESUMED);
    CHECK(mlxr_hostapp_outcome_seen(881, 0) == MLXR_HOSTAPP_ENDED);
    CHECK(mlxr_hostapp_outcome_seen(881, 7) == MLXR_HOSTAPP_OTHER);
    CHECK(mlxr_hostapp_outcome_seen(881, -1) == MLXR_HOSTAPP_UNSURE);
}

static void counting(void) {
    // only after a disconnect the client caused, within the window
    for (int w = MLXR_HOSTAPP_WHY_FAILOVER; w <= MLXR_HOSTAPP_WHY_DISCONNECT; w++) {
        CHECK(mlxr_hostapp_counted((mlxr_hostapp_why)w, 3000, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_ENDED);
        CHECK(mlxr_hostapp_counted((mlxr_hostapp_why)w, MLXR_HOSTAPP_WINDOW_MS, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_ENDED);
        CHECK(mlxr_hostapp_counted((mlxr_hostapp_why)w, MLXR_HOSTAPP_WINDOW_MS + 1, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_UNSURE);
        CHECK(mlxr_hostapp_counted((mlxr_hostapp_why)w, -1, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_UNSURE);
        // the app still running counts only once the host has had time to end it
        CHECK(mlxr_hostapp_counted((mlxr_hostapp_why)w, 3000, MLXR_HOSTAPP_RESUMED) == MLXR_HOSTAPP_UNSURE);
        CHECK(mlxr_hostapp_counted((mlxr_hostapp_why)w, MLXR_HOSTAPP_KEPT_AFTER_MS, MLXR_HOSTAPP_RESUMED) == MLXR_HOSTAPP_RESUMED);
        CHECK(mlxr_hostapp_counted((mlxr_hostapp_why)w, 20000, MLXR_HOSTAPP_OTHER) == MLXR_HOSTAPP_OTHER);
    }
    // an early sighting of the app running is looked at again (the stream still up at KEPT_AFTER_MS), except
    // after a dropped link, a quit or a stream the host ended
    for (int w = MLXR_HOSTAPP_WHY_FAILOVER; w <= MLXR_HOSTAPP_WHY_DISCONNECT; w++) {
        int again = w != MLXR_HOSTAPP_WHY_DROP;
        CHECK(mlxr_hostapp_confirm_later((mlxr_hostapp_why)w, 2000, MLXR_HOSTAPP_RESUMED) == again);
        CHECK(mlxr_hostapp_confirm_later((mlxr_hostapp_why)w, 0, MLXR_HOSTAPP_RESUMED) == again);
        CHECK(!mlxr_hostapp_confirm_later((mlxr_hostapp_why)w, MLXR_HOSTAPP_KEPT_AFTER_MS, MLXR_HOSTAPP_RESUMED));
        CHECK(!mlxr_hostapp_confirm_later((mlxr_hostapp_why)w, -1, MLXR_HOSTAPP_RESUMED));
        CHECK(!mlxr_hostapp_confirm_later((mlxr_hostapp_why)w, 2000, MLXR_HOSTAPP_ENDED));
        CHECK(!mlxr_hostapp_confirm_later((mlxr_hostapp_why)w, 2000, MLXR_HOSTAPP_OTHER));
        CHECK(!mlxr_hostapp_confirm_later((mlxr_hostapp_why)w, 2000, MLXR_HOSTAPP_UNSURE));
        // and counts then as the app kept: one clears the count
        CHECK(mlxr_hostapp_learn(2, mlxr_hostapp_counted((mlxr_hostapp_why)w, MLXR_HOSTAPP_KEPT_AFTER_MS,
                                                         MLXR_HOSTAPP_RESUMED)) == MLXR_HOSTAPP_KEEPS);
    }
    CHECK(!mlxr_hostapp_confirm_later(MLXR_HOSTAPP_WHY_NONE, 2000, MLXR_HOSTAPP_RESUMED));

    // a quit, a stream the host ended (the app quit or crashed): never
    CHECK(mlxr_hostapp_counted(MLXR_HOSTAPP_WHY_NONE, 3000, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_UNSURE);
    CHECK(mlxr_hostapp_counted(MLXR_HOSTAPP_WHY_NONE, 30000, MLXR_HOSTAPP_RESUMED) == MLXR_HOSTAPP_UNSURE);

    // each sighting of the app ended adds one, one of it running clears them
    int k = MLXR_HOSTAPP_UNKNOWN;
    k = mlxr_hostapp_learn(k, MLXR_HOSTAPP_ENDED);
    CHECK(k == 1 && mlxr_hostapp_may_leave(k, MLXR_HOSTAPP_WHY_FAILOVER));
    k = mlxr_hostapp_learn(k, MLXR_HOSTAPP_UNSURE);
    k = mlxr_hostapp_learn(k, MLXR_HOSTAPP_OTHER);
    CHECK(k == 1);
    k = mlxr_hostapp_learn(k, MLXR_HOSTAPP_ENDED);
    CHECK(k == 2 && !mlxr_hostapp_may_leave(k, MLXR_HOSTAPP_WHY_FAILOVER));
    k = mlxr_hostapp_learn(k, MLXR_HOSTAPP_RESUMED);
    CHECK(k == MLXR_HOSTAPP_KEEPS && mlxr_hostapp_may_leave(k, MLXR_HOSTAPP_WHY_FAILOVER));
    k = mlxr_hostapp_learn(k, MLXR_HOSTAPP_ENDED);
    CHECK(k == 1);   // the setting changed: counting again
    for (int i = 0; i < 20; i++) k = mlxr_hostapp_learn(k, MLXR_HOSTAPP_ENDED);
    CHECK(k == MLXR_HOSTAPP_ENDS_MAX);
    // a crash after a dropped link, once, cannot hold the failover on its own
    CHECK(mlxr_hostapp_may_leave(mlxr_hostapp_learn(MLXR_HOSTAPP_UNKNOWN, MLXR_HOSTAPP_ENDED), MLXR_HOSTAPP_WHY_FAILOVER));
    char name[32];
    CHECK(strcmp(mlxr_hostapp_known_name(2, name, sizeof name), "ends 2") == 0);
    CHECK(strcmp(mlxr_hostapp_known_name(MLXR_HOSTAPP_KEEPS, name, sizeof name), "keeps") == 0);
}

static void policy(void) {
    // only the failover is held, and only from two sightings
    for (int w = MLXR_HOSTAPP_WHY_NONE; w <= MLXR_HOSTAPP_WHY_DISCONNECT; w++) {
        CHECK(mlxr_hostapp_may_leave(MLXR_HOSTAPP_ENDS_MAX, (mlxr_hostapp_why)w) == (w != MLXR_HOSTAPP_WHY_FAILOVER));
    }
    CHECK(mlxr_hostapp_failover_answer(MLXR_HOSTAPP_KEEPS) == 1);
    CHECK(mlxr_hostapp_failover_answer(MLXR_HOSTAPP_UNKNOWN) == 2);
    CHECK(mlxr_hostapp_failover_answer(1) == 2);
    CHECK(mlxr_hostapp_failover_answer(2) == 0);
    // the notices before leaving
    CHECK(mlxr_hostapp_notice_before(2, MLXR_HOSTAPP_WHY_FAILOVER) == MLXR_HOSTAPP_NOTICE_HELD);
    CHECK(mlxr_hostapp_notice_before(1, MLXR_HOSTAPP_WHY_FAILOVER) == MLXR_HOSTAPP_NOTICE_SWITCH_MAY_END);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_UNKNOWN, MLXR_HOSTAPP_WHY_FAILOVER) == MLXR_HOSTAPP_NOTICE_SWITCH_MAY_END);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_KEEPS, MLXR_HOSTAPP_WHY_FAILOVER) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_before(1, MLXR_HOSTAPP_WHY_DOFF) == MLXR_HOSTAPP_NOTICE_MAY_END);
    CHECK(mlxr_hostapp_notice_before(1, MLXR_HOSTAPP_WHY_IDLE) == MLXR_HOSTAPP_NOTICE_MAY_END);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_UNKNOWN, MLXR_HOSTAPP_WHY_IDLE) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_before(MLXR_HOSTAPP_KEEPS, MLXR_HOSTAPP_WHY_DOFF) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_before(2, MLXR_HOSTAPP_WHY_DROP) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_before(2, MLXR_HOSTAPP_WHY_DISCONNECT) == MLXR_HOSTAPP_NOTICE_NONE);
    // and after a resume
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_FAILOVER, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_NOTICE_ENDED);
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_DROP, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_NOTICE_ENDED);
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_DROP, MLXR_HOSTAPP_RESUMED) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_DROP, MLXR_HOSTAPP_OTHER) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(mlxr_hostapp_notice_after(MLXR_HOSTAPP_WHY_DOFF, MLXR_HOSTAPP_ENDED) == MLXR_HOSTAPP_NOTICE_NONE);
    CHECK(strcmp(mlxr_hostapp_notice_name(MLXR_HOSTAPP_NOTICE_HELD), "held") == 0);
}

static void texts(void) {
    // the approved words, filled as the app fills them
    char t[200];
    CHECK(strcmp(MLXR_HOSTAPP_TEXT_HELD, "Video keeps breaking up. Not switching networks: this PC would end the app") == 0);
    snprintf(t, sizeof t, MLXR_HOSTAPP_TEXT_SWITCH, "Steam Frame Wireless Adapter");
    CHECK(strcmp(t, "Switching to Steam Frame Wireless Adapter. A PC set to end the app on disconnect will close it") == 0);
    CHECK(strcmp(MLXR_HOSTAPP_TEXT_ENDED, "The PC ended the app when the connection dropped. Start it again from the app list") == 0);
    snprintf(t, sizeof t, MLXR_HOSTAPP_TEXT_IDLE, 30);
    CHECK(strcmp(t, "No input for 30 min: disconnecting. This PC may end the app") == 0);
    snprintf(t, sizeof t, MLXR_HOSTAPP_TEXT_DOFF, 60);
    CHECK(strcmp(t, "Headset off for 60 s: disconnecting. This PC may end the app") == 0);
}

static void file(void) {
    char dir[] = "/tmp/mlxr-hostapp-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    const char* A = "6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B";
    const char* B = "11111111-2222-3333-4444-555555555555";
    CHECK(mlxr_hostapp_load(dir, A, 881) == MLXR_HOSTAPP_UNKNOWN);   // no file
    CHECK(mlxr_hostapp_save(dir, A, 881, 2) == 0);
    CHECK(mlxr_hostapp_save(dir, B, 881, MLXR_HOSTAPP_KEEPS) == 0);
    CHECK(mlxr_hostapp_save(dir, A, 7, MLXR_HOSTAPP_KEEPS) == 0);
    CHECK(mlxr_hostapp_load(dir, A, 881) == 2);
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
        // and a count left out (one), a count out of range (kept in it)
        fputs("nonsense\nC 0 ends\nD 5 maybe\nE 6 ends\nF 7 ends 400\nG 8 ends 3\n\n", f);
        fclose(f);
    }
    CHECK(mlxr_hostapp_load(dir, A, 881) == MLXR_HOSTAPP_KEEPS && mlxr_hostapp_load(dir, "D", 5) == MLXR_HOSTAPP_UNKNOWN);
    CHECK(mlxr_hostapp_load(dir, "E", 6) == 1 && mlxr_hostapp_load(dir, "F", 7) == MLXR_HOSTAPP_ENDS_MAX);
    CHECK(mlxr_hostapp_load(dir, "G", 8) == 3);
    // the count is written and read back
    CHECK(mlxr_hostapp_save(dir, "G", 8, 4) == 0 && mlxr_hostapp_load(dir, "G", 8) == 4);
    CHECK(mlxr_hostapp_save(dir, "has space", 1, 2) == -1);
    CHECK(mlxr_hostapp_save(NULL, A, 1, 2) == -1);
    CHECK(mlxr_hostapp_load(dir, A, 0) == MLXR_HOSTAPP_UNKNOWN);
    // at most MLXR_HOSTAPP_MAX lines, the oldest dropped
    for (int i = 1; i <= MLXR_HOSTAPP_MAX + 3; i++) CHECK(mlxr_hostapp_save(dir, B, 1000 + i, 2) == 0);
    CHECK(mlxr_hostapp_load(dir, B, 1000 + MLXR_HOSTAPP_MAX + 3) == 2);
    CHECK(mlxr_hostapp_load(dir, A, 881) == MLXR_HOSTAPP_UNKNOWN);   // the oldest went
    unlink(path);
    rmdir(dir);
}

int main(void) {
    outcomes();
    counting();
    policy();
    texts();
    file();
    printf("stream_host_app: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
