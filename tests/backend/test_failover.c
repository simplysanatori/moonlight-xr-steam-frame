/* Path failover: the loss history, the share of frames lost, and the decision table. No network. GPLv3. */
#include <string.h>

#include "check.h"
#include "failover.h"

#define S 1000

/* the table rows change fields of a baseline row, so a field is set twice on purpose */
#if defined(__clang__)
#pragma clang diagnostic ignored "-Winitializer-overrides"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Woverride-init"
#endif

static void test_history(void)
{
    mlxr_failover_history h;
    mlxr_failover_window w;
    mlxr_failover_history_init(&h);
    mlxr_failover_history_window(&h, NULL, 100 * S, 10 * S, &w);
    CHECK(w.count == 0 && w.lossMs == 0 && w.frames == 0);

    /* three bursts inside the 10 s before t=100 s */
    mlxr_failover_history_add(&h, 91 * S, 92 * S, 10);          /* 1000 ms */
    mlxr_failover_history_add(&h, 94 * S, 95 * S + 500, 20);    /* 1500 ms */
    mlxr_failover_history_add(&h, 97 * S, 98 * S, 5);           /* 1000 ms */
    mlxr_failover_history_window(&h, NULL, 100 * S, 10 * S, &w);
    CHECK(w.count == 3 && w.lossMs == 3500 && w.frames == 35);

    /* a burst that began before the window counts only the part inside it */
    mlxr_failover_history_add(&h, 85 * S, 92 * S, 1);           /* 2 s of it inside [90 s, 100 s] */
    mlxr_failover_history_window(&h, NULL, 100 * S, 10 * S, &w);
    CHECK(w.count == 4 && w.lossMs == 5500);

    /* an older one is out; a burst of one frame has no length but is counted */
    mlxr_failover_history_init(&h);
    mlxr_failover_history_add(&h, 10 * S, 12 * S, 3);
    mlxr_failover_history_add(&h, 95 * S, 95 * S, 1);
    mlxr_failover_history_window(&h, NULL, 100 * S, 10 * S, &w);
    CHECK(w.count == 1 && w.lossMs == 0 && w.frames == 1);

    /* the burst still open counts up to its last lost frame, never past now */
    mlxr_failover_burst open = { 96 * S, 99 * S, 40 };
    mlxr_failover_history_window(&h, &open, 100 * S, 10 * S, &w);
    CHECK(w.count == 2 && w.lossMs == 3000 && w.frames == 41);
    mlxr_failover_burst ahead = { 99 * S, 105 * S, 1 };
    mlxr_failover_history_window(&h, &ahead, 100 * S, 10 * S, &w);
    CHECK(w.lossMs == 1000);

    /* the ring keeps the newest MLXR_FAILOVER_RING */
    mlxr_failover_history_init(&h);
    for (int i = 0; i < MLXR_FAILOVER_RING + 10; i++) mlxr_failover_history_add(&h, 1000 + i * 100, 1050 + i * 100, 1);
    CHECK(h.n == MLXR_FAILOVER_RING);
    mlxr_failover_history_window(&h, NULL, 10000, 100000, &w);
    CHECK(w.count == MLXR_FAILOVER_RING);
}

static void test_loss_pct(void)
{
    mlxr_failover_samples s;
    mlxr_failover_samples_reset(&s);
    CHECK(mlxr_failover_loss_pct(&s, 50 * S, 10 * S) < 0);
    /* 90 fps, 1 s apart */
    uint64_t rx = 0, lost = 0;
    for (int i = 0; i <= 10; i++) {
        mlxr_failover_samples_add(&s, 40 * S + i * S, rx, lost);
        rx += 80;
        lost += 10;   /* 11 percent of 90 */
    }
    double pct = mlxr_failover_loss_pct(&s, 50 * S, 10 * S);
    CHECK(pct > 11.0 && pct < 11.2);
    /* samples that span less than half the window: unknown */
    CHECK(mlxr_failover_loss_pct(&s, 50 * S, 60 * S) < 0);
    mlxr_failover_samples_reset(&s);
    mlxr_failover_samples_add(&s, 40 * S, 0, 0);
    mlxr_failover_samples_add(&s, 42 * S, 180, 0);
    CHECK(mlxr_failover_loss_pct(&s, 42 * S, 10 * S) < 0);
    /* a clean link */
    mlxr_failover_samples_add(&s, 48 * S, 900, 0);
    CHECK(mlxr_failover_loss_pct(&s, 48 * S, 10 * S) == 0.0);
    /* the video stopped: nothing arrived, nothing was counted lost */
    mlxr_failover_samples_reset(&s);
    mlxr_failover_samples_add(&s, 40 * S, 5000, 10);
    mlxr_failover_samples_add(&s, 46 * S, 5000, 10);
    CHECK(mlxr_failover_loss_pct(&s, 46 * S, 10 * S) == 100.0);
    /* counters that went backwards belong to another stream */
    mlxr_failover_samples_reset(&s);
    mlxr_failover_samples_add(&s, 40 * S, 5000, 10);
    mlxr_failover_samples_add(&s, 46 * S, 20, 0);
    CHECK(mlxr_failover_loss_pct(&s, 46 * S, 10 * S) < 0);
}

static void test_burst_ms(void)
{
    CHECK_EQ_INT(mlxr_failover_burst_ms(0), 3000);
    CHECK_EQ_INT(mlxr_failover_burst_ms(-5), 3000);
    CHECK_EQ_INT(mlxr_failover_burst_ms(100), 500);
    CHECK_EQ_INT(mlxr_failover_burst_ms(5000), 5000);
    CHECK_EQ_INT(mlxr_failover_burst_ms(60000), 9000);
}

/* one row of the table: a baseline that switches, with fields changed */
typedef struct {
    const char *name;
    bool enabled, pref_auto;
    int burst_ms;
    int64_t age_ms, since_switch_ms;           /* stream age; ms since the last switch, < 0 never */
    mlxr_net_path cur;
    double cur_score, cur_loss;
    uint64_t lossMs;
    /* candidates: the adapter, the cable */
    bool adapter_present, usb_present;
    double adapter_score, usb_score;
    int64_t probe_age_ms;
    /* expect */
    bool sw;
    mlxr_net_path to;
    mlxr_failover_reason reason;
} row;

#define NOW (4 * 3600 * 1000LL)

static void run(const row *r)
{
    mlxr_failover_cand c[3] = {
        { MLXR_PATH_HOME, true, r->cur == MLXR_PATH_HOME ? r->cur_score : 6.0, NOW - r->probe_age_ms },
        { MLXR_PATH_ADAPTER, r->adapter_present, r->adapter_score, NOW - r->probe_age_ms },
        { MLXR_PATH_USB, r->usb_present, r->usb_score, NOW - r->probe_age_ms },
    };
    mlxr_failover_in in;
    memset(&in, 0, sizeof in);
    in.enabled = r->enabled;
    in.pref_auto = r->pref_auto;
    in.burst_ms = r->burst_ms;
    in.now_ms = NOW;
    in.stream_start_ms = NOW - r->age_ms;
    in.last_switch_ms = r->since_switch_ms < 0 ? -1 : NOW - r->since_switch_ms;
    in.cur_path = r->cur;
    in.cur_score_ms = r->cur_score;
    in.cur_loss_pct = r->cur_loss;
    in.loss.count = 4;
    in.loss.lossMs = r->lossMs;
    in.cand = c;
    in.n_cand = 3;
    mlxr_failover_out o = mlxr_failover_decide(&in);
    if (o.sw != r->sw || o.reason != r->reason || (r->sw && o.to != r->to)) {
        fprintf(stderr, "row \"%s\": got %s%s (%s), wanted %s%s\n", r->name, o.sw ? "switch to " : "stay", o.sw ? mlxr_net_path_label(o.to) : "",
                mlxr_failover_reason_text(o.reason), r->sw ? "switch to " : "stay", r->sw ? mlxr_net_path_label(r->to) : "");
        g_failures++;
    }
}

int main(void)
{
    test_history();
    test_loss_pct();
    test_burst_ms();

    /* The baseline: Wi-Fi (score 6 ms) lost 4 s of video in 10 s, 12 percent of the frames, five minutes into
     * the stream; the adapter measured 22 ms at the start, no cable. Wi-Fi's cost is 6 + 20 x 12 = 246, capped
     * at 6 + 150 = 156, and 22 + 15 is below that. */
#define BASE(n) .name = n, .enabled = true, .pref_auto = true, .burst_ms = 3000, .age_ms = 300 * S, .since_switch_ms = -1, \
        .cur = MLXR_PATH_HOME, .cur_score = 6.0, .cur_loss = 12.0, .lossMs = 4000, .adapter_present = true, \
        .adapter_score = 22.0, .usb_present = false, .usb_score = -1, .probe_age_ms = 300 * S, \
        .sw = true, .to = MLXR_PATH_ADAPTER, .reason = MLXR_FO_SWITCH
    const row table[] = {
        { BASE("switches to the adapter, worse latency but no loss") },
        /* the gates */
        { BASE("off"), .enabled = false, .sw = false, .reason = MLXR_FO_STAY_OFF },
        { BASE("network path set by hand"), .pref_auto = false, .sw = false, .reason = MLXR_FO_STAY_PINNED },
        { BASE("no current path"), .cur = MLXR_PATH_NONE, .sw = false, .reason = MLXR_FO_STAY_NO_BASIS },
        { BASE("current path unmeasured"), .cur_score = -1, .sw = false, .reason = MLXR_FO_STAY_NO_BASIS },
        { BASE("young stream"), .age_ms = 19 * S, .sw = false, .reason = MLXR_FO_STAY_SETTLING },
        { BASE("just old enough"), .age_ms = 20 * S },
        { BASE("switched 4 minutes ago"), .since_switch_ms = 240 * S, .sw = false, .reason = MLXR_FO_STAY_COOLDOWN },
        { BASE("switched 5 minutes ago"), .since_switch_ms = 300 * S },
        { BASE("switched just now"), .since_switch_ms = 0, .sw = false, .reason = MLXR_FO_STAY_COOLDOWN },
        /* the burst threshold */
        { BASE("2999 ms of loss"), .lossMs = 2999, .sw = false, .reason = MLXR_FO_STAY_NO_BURST },
        { BASE("3000 ms of loss"), .lossMs = 3000 },
        { BASE("no loss"), .lossMs = 0, .sw = false, .reason = MLXR_FO_STAY_NO_BURST },
        { BASE("a longer threshold, not reached"), .burst_ms = 5000, .lossMs = 4000, .sw = false, .reason = MLXR_FO_STAY_NO_BURST },
        { BASE("a longer threshold, reached"), .burst_ms = 5000, .lossMs = 5000 },
        { BASE("a threshold of 0 is the default"), .burst_ms = 0, .lossMs = 2999, .sw = false, .reason = MLXR_FO_STAY_NO_BURST },
        { BASE("a silly short threshold is held to 500"), .burst_ms = 1, .lossMs = 499, .sw = false, .reason = MLXR_FO_STAY_NO_BURST },
        /* the other paths */
        { BASE("the adapter was absent"), .adapter_present = false, .sw = false, .reason = MLXR_FO_STAY_NO_ALTERNATIVE },
        { BASE("the adapter was never measured"), .adapter_score = -1, .sw = false, .reason = MLXR_FO_STAY_NO_ALTERNATIVE },
        { BASE("the measurement is two hours and a second old"), .probe_age_ms = 2 * 3600 * S + 1, .sw = false,
          .reason = MLXR_FO_STAY_NO_ALTERNATIVE },
        { BASE("the measurement is exactly two hours old"), .probe_age_ms = 2 * 3600 * S },
        { BASE("the cable is the better one"), .usb_present = true, .usb_score = 4.0, .to = MLXR_PATH_USB },
        { BASE("the cable is absent, the adapter is used"), .usb_present = false, .usb_score = 1.0 },
        { BASE("the cable ties with the adapter: the lower path wins"), .usb_present = true, .usb_score = 22.0 },
        /* what clearly better means */
        { BASE("the adapter needs 15 ms of margin, 141 passes"), .adapter_score = 141.0 },
        { BASE("the adapter at 142 ms is too close"), .adapter_score = 142.0, .sw = false, .reason = MLXR_FO_STAY_NOT_BETTER },
        { BASE("a worse path is never chosen"), .adapter_score = 300.0, .sw = false, .reason = MLXR_FO_STAY_NOT_BETTER },
        { BASE("1 percent loss costs 20 ms: 6 + 20 = 26, the adapter at 22 is not 15 ms better"), .cur_loss = 1.0, .sw = false,
          .reason = MLXR_FO_STAY_NOT_BETTER },
        { BASE("1 percent loss, the adapter at 10 ms is"), .cur_loss = 1.0, .adapter_score = 10.0 },
        { BASE("loss below 0.5 percent costs nothing"), .cur_loss = 0.4, .adapter_score = 22.0, .sw = false, .reason = MLXR_FO_STAY_NOT_BETTER },
        { BASE("loss unknown costs nothing"), .cur_loss = -1, .sw = false, .reason = MLXR_FO_STAY_NOT_BETTER },
        { BASE("loss unknown, a much faster path still wins"), .cur_loss = -1, .adapter_score = 2.0, .cur_score = 20.0 },
        { BASE("the video stopped altogether"), .cur_loss = 100.0, .adapter_score = 100.0 },
        /* away from another path than Wi-Fi */
        { BASE("from the adapter to Wi-Fi"), .cur = MLXR_PATH_ADAPTER, .cur_score = 20.0, .adapter_score = 20.0,
          .to = MLXR_PATH_HOME },
        { BASE("from the cable to Wi-Fi"), .cur = MLXR_PATH_USB, .cur_score = 5.0, .usb_present = true, .usb_score = 5.0,
          .adapter_present = false, .to = MLXR_PATH_HOME },
    };
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++) run(&table[i]);

    /* the current path is never its own alternative, even when the table lists it as better */
    {
        mlxr_failover_cand c[1] = { { MLXR_PATH_HOME, true, 1.0, NOW } };
        mlxr_failover_in in;
        memset(&in, 0, sizeof in);
        in.enabled = in.pref_auto = true;
        in.now_ms = NOW;
        in.stream_start_ms = NOW - 300 * S;
        in.last_switch_ms = -1;
        in.cur_path = MLXR_PATH_HOME;
        in.cur_score_ms = 50.0;
        in.cur_loss_pct = 20;
        in.loss.lossMs = 9000;
        in.cand = c;
        in.n_cand = 1;
        mlxr_failover_out o = mlxr_failover_decide(&in);
        CHECK(!o.sw && o.reason == MLXR_FO_STAY_NO_ALTERNATIVE);
        in.n_cand = 0;
        o = mlxr_failover_decide(&in);
        CHECK(!o.sw && o.reason == MLXR_FO_STAY_NO_ALTERNATIVE);
    }

    /* no flapping: after a switch from Wi-Fi to the adapter, the adapter going bad is a stay for five minutes,
     * then it may go on (to the cable) */
    {
        mlxr_failover_cand c[2] = { { MLXR_PATH_ADAPTER, true, 22.0, NOW - 600 * S }, { MLXR_PATH_USB, true, 5.0, NOW - 600 * S } };
        mlxr_failover_in in;
        memset(&in, 0, sizeof in);
        in.enabled = in.pref_auto = true;
        in.stream_start_ms = NOW - 60 * S;
        in.last_switch_ms = NOW - 60 * S;
        in.cur_path = MLXR_PATH_ADAPTER;
        in.cur_score_ms = 22.0;
        in.cur_loss_pct = 15;
        in.loss.lossMs = 6000;
        in.cand = c;
        in.n_cand = 2;
        in.now_ms = NOW;
        mlxr_failover_out o = mlxr_failover_decide(&in);
        CHECK(!o.sw && o.reason == MLXR_FO_STAY_COOLDOWN);
        in.now_ms = NOW + 241 * S;
        o = mlxr_failover_decide(&in);
        CHECK(o.sw && o.to == MLXR_PATH_USB);
    }

    CHECK(strstr(mlxr_failover_reason_text(MLXR_FO_STAY_OFF), "off"));
    CHECK_EQ_STR(mlxr_failover_reason_text(MLXR_FO_SWITCH), "switch");
    TEST_DONE();
}
