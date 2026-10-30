// Moonlight XR Native, tests/m4: the in-headset HUD and Lite stats, the clock strip's battery and slow connection
// badge, the style setting through the context, and Report a problem's Save and zip (lane wave3-hud). GPLv3.
#define _GNU_SOURCE
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "scene.h"
#include "text.h"
#include "xr_bugzip.h"

static char g_dir[256];

static void writeFile(const char* name, const char* text) {
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    FILE* f = fopen(path, "w");
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

// How many files match a shell glob
static int countGlob(const char* glob) {
    char cmd[800];
    snprintf(cmd, sizeof(cmd), "ls %s 2>/dev/null | wc -l", glob);
    FILE* f = popen(cmd, "r");
    if (f == NULL) return -1;
    char line[32] = "";
    if (fgets(line, sizeof(line), f) == NULL) line[0] = '\0';
    pclose(f);
    return atoi(line);
}

// ---- Save and zip

static void testBugzipParse(void) {
    char path[128];
    CHECK(xrBugzipParse("   moonlight.log 1234\nwrote /home/steamos/reports/mlxr-report-20261029-120000.zip\n"
                        "   mlxr-report/moonlight.log 99\nLook inside before you share it.\n", path, sizeof(path)));
    CHECK(strcmp(path, "/home/steamos/reports/mlxr-report-20261029-120000.zip") == 0);
    // the last one counts, and a line ending in CR is fine
    CHECK(xrBugzipParse("wrote /a.zip\nwrote /b.zip\r\n", path, sizeof(path)) && strcmp(path, "/b.zip") == 0);
    CHECK(!xrBugzipParse("masking failed; nothing written\n", path, sizeof(path)));
    CHECK(!xrBugzipParse("wrote \n", path, sizeof(path)));
    CHECK(!xrBugzipParse("not wrote /a.zip\n", path, sizeof(path)));
    CHECK(!xrBugzipParse("", path, sizeof(path)));
    CHECK(!xrBugzipParse(NULL, path, sizeof(path)));
    // a path that does not fit is not a path
    char tiny[8];
    CHECK(!xrBugzipParse("wrote /a/very/long/path.zip\n", tiny, sizeof(tiny)));
}

static void testBugzipRun(void) {
    char script[600], result[400];
    writeFile("ok.sh", "echo 'building'\necho \"wrote $REPORT_DIR/fake.zip\"\necho 'Look inside before you share it.'\n");
    snprintf(script, sizeof(script), "%s/ok.sh", g_dir);
    CHECK(xrBugzipRun(script, "/tmp/some-reports", result, sizeof(result)) == 0);
    CHECK(strcmp(result, "/tmp/some-reports/fake.zip") == 0);   // the report directory reached the script
    writeFile("fail.sh", "echo 'masking failed; nothing written'\nexit 1\n");
    snprintf(script, sizeof(script), "%s/fail.sh", g_dir);
    CHECK(xrBugzipRun(script, NULL, result, sizeof(result)) == -1);
    CHECK(strstr(result, "exit 1") != NULL && strstr(result, "masking failed") != NULL);
    writeFile("mute.sh", "echo 'all done'\n");
    snprintf(script, sizeof(script), "%s/mute.sh", g_dir);
    CHECK(xrBugzipRun(script, NULL, result, sizeof(result)) == -1);
    CHECK(strstr(result, "did not say") != NULL);
    // no such script: bash says so and exits
    snprintf(script, sizeof(script), "%s/nothing.sh", g_dir);
    CHECK(xrBugzipRun(script, NULL, result, sizeof(result)) == -1);
    // a lot of output does not block it
    writeFile("loud.sh", "for i in $(seq 1 20000); do echo \"line $i of the output that goes on\"; done\necho \"wrote /tmp/loud.zip\"\n");
    snprintf(script, sizeof(script), "%s/loud.sh", g_dir);
    CHECK(xrBugzipRun(script, NULL, result, sizeof(result)) == 0 || strstr(result, "did not say") != NULL);
}

static void testBugzipFind(void) {
    char script[600], found[600];
    snprintf(script, sizeof(script), "%s/ok.sh", g_dir);
    setenv("MLXR_BUG_REPORT_SCRIPT", script, 1);
    CHECK(xrBugzipFindScript(found, sizeof(found)) && strcmp(found, script) == 0);
    setenv("MLXR_BUG_REPORT_SCRIPT", "/nonexistent/bug-report.sh", 1);
    // not readable: the other places are tried (none here, or the installed one)
    int got = xrBugzipFindScript(found, sizeof(found));
    CHECK(!got || strcmp(found, "/nonexistent/bug-report.sh") != 0);
    unsetenv("MLXR_BUG_REPORT_SCRIPT");
}

static int waitForZip(int* ok, char* result, size_t cap) {
    for (int i = 0; i < 400; i++) {
        if (xrBugzipPoll(ok, result, cap)) return 1;
        usleep(25000);
    }
    return 0;
}

static void testBugzipThread(void) {
    char script[600], result[400];
    int ok = 0;
    CHECK(!xrBugzipPoll(&ok, result, sizeof(result)));
    snprintf(script, sizeof(script), "%s/ok.sh", g_dir);
    setenv("MLXR_BUG_REPORT_SCRIPT", script, 1);
    CHECK(xrBugzipStart("/tmp/threaded"));
    CHECK(waitForZip(&ok, result, sizeof(result)));
    CHECK(ok && strcmp(result, "/tmp/threaded/fake.zip") == 0);
    CHECK(!xrBugzipPoll(&ok, result, sizeof(result)));   // said once
    snprintf(script, sizeof(script), "%s/fail.sh", g_dir);
    setenv("MLXR_BUG_REPORT_SCRIPT", script, 1);
    CHECK(xrBugzipStart(NULL));
    CHECK(waitForZip(&ok, result, sizeof(result)));
    CHECK(!ok && strstr(result, "masking failed") != NULL);
    unsetenv("MLXR_BUG_REPORT_SCRIPT");
}

// The sheet's third button: the note is saved when there is one, the zip is made off the frame loop, and the
// finished run goes up as a notice
static void testSheetButton(Scene* s) {
    XrCtx* c = s->ctx;
    char script[600];
    snprintf(script, sizeof(script), "%s/ok.sh", g_dir);
    setenv("MLXR_BUG_REPORT_SCRIPT", script, 1);
    char reports[600];
    snprintf(reports, sizeof(reports), "%s/reports", g_dir);
    mkdir(reports, 0755);

    // no note: the zip only
    char noteGlob[700];
    snprintf(noteGlob, sizeof(noteGlob), "%s/report-*.txt", reports);
    xrPanelsReportEvent(c, REPORT_OPENED, reports);
    xrPanelsReportEvent(c, REPORT_ZONE_ZIP, reports);
    int ok = 0;
    char result[400];
    CHECK(waitForZip(&ok, result, sizeof(result)));
    CHECK(ok);
    CHECK(countGlob(noteGlob) == 0);

    // with a note typed: saved as the Save button does, and the zip made
    xrPanelsReportEvent(c, REPORT_OPENED, reports);
    for (const char* p = "it froze"; *p; p++) xrPanelsReportKey(c, *p);
    xrPanelsReportEvent(c, REPORT_ZONE_ZIP, reports);
    // the run's end reaches the toast through the frame loop's poll
    int toasted = 0;
    for (int i = 0; i < 400 && !toasted; i++) {
        xrPanelsReportPoll(c);
        Notice up;
        if (noticeAdvance(&c->notices, nowNs(), 0, &up)) {
            toasted = up.kind == TOAST_TEXT;
        }
        if (!toasted) usleep(25000);
    }
    CHECK(toasted);
    CHECK(countGlob(noteGlob) == 1);

    // a failing script says so, once, and is not a path
    snprintf(script, sizeof(script), "%s/fail.sh", g_dir);
    setenv("MLXR_BUG_REPORT_SCRIPT", script, 1);
    xrPanelsReportEvent(c, REPORT_ZONE_ZIP, reports);
    toasted = 0;
    for (int i = 0; i < 400 && !toasted; i++) {
        xrPanelsReportPoll(c);
        Notice up;
        if (noticeAdvance(&c->notices, nowNs() + 10000000000LL, 0, &up)) {
            toasted = up.kind == TOAST_TEXT;
        }
        if (!toasted) usleep(25000);
    }
    CHECK(toasted);
    unsetenv("MLXR_BUG_REPORT_SCRIPT");
}

// ---- the HUD and Lite lines in the stats window

static int textPixels(XrCtx* c, int x0, int x1, int y0, int y1) {
    UiRectPx r = xrAtlasRect(ATLAS_OVERLAY);
    const uint8_t* px = xrAtlasPixels(c);
    int n = 0;
    for (int y = y0; y < y1 && y < r.h; y++) {
        for (int x = x0; x < x1 && x < r.w; x++) {
            const uint8_t* p = px + ((size_t)(r.y + y) * UI_ATLAS_W + (size_t)(r.x + x)) * 4;
            if (p[0] > 200 && p[1] > 200 && p[2] > 200 && p[3] > 200) n++;
        }
    }
    return n;
}

static MlxrHudFigures typical(void) {
    MlxrHudFigures f;
    memset(&f, 0, sizeof(f));
    f.valid = 1;
    f.mbps = 54.2;
    f.hostMs = 4.2;
    f.recvMs = 1.1;
    f.queueMs = 0.4;
    f.decodeMs = 3.1;
    f.rttMs = 4.0;
    f.latencyMs = 10.8;
    f.lossPct = 0.2;
    f.fps = 90.0;
    return f;
}

static void testHudWindow(Scene* s) {
    XrCtx* c = s->ctx;
    UiRectPx r = xrAtlasRect(ATLAS_OVERLAY);
    UiFont* font = ui_font_default();
    CHECK(font != NULL);
    scene_reset(s);
    c->overlayVisible = 1;
    char text[512];
    MlxrHudFigures f = typical();

    // Lite: one line, which fits the window without wrapping, at its worst
    mlxr_hud_lite(&f, 0, text, sizeof(text));
    scene_frame(s, text);
    CHECK(c->overlayHasContent && c->overlayW == r.w);
    CHECK(c->overlayH < 50);
    CHECK(textPixels(c, 0, r.w, 0, r.h) > 100);
    MlxrHudFigures worst = typical();
    worst.mbps = 512.4;
    worst.latencyMs = 188.0;
    worst.decodeMs = 12.6;
    worst.lossPct = 12.5;
    worst.fps = 144.0;
    mlxr_hud_lite(&worst, 1, text, sizeof(text));
    // wider than the window at the usual size: the type steps down and it stays one line
    CHECK(ui_text_width(font, UI_WEIGHT_REGULAR, 17.0f, text, -1) > (float)r.w - 24.0f - 40.0f);
    scene_frame(s, text);
    CHECK(c->overlayH < 50);
    CHECK(textPixels(c, 0, r.w, 0, r.h) > 100);
    CHECK(textPixels(c, r.w - 11, r.w, 0, r.h) == 0);

    // the HUD: a few lines, each of which fits
    MlxrCompositorMetrics m;
    memset(&m, 0, sizeof(m));
    mlxr_metric_set(&m, "compositor/cpu_frametime", 1.4f);
    mlxr_metric_set(&m, "compositor/gpu_frametime", 3.2f);
    mlxr_metric_set(&m, "compositor/dropped_frame_count", 2.0f);
    mlxr_hud_panel(&f, 1, &m, 90.0, text, sizeof(text));
    int lines = 1;
    for (const char* p = text; *p; p++) lines += *p == '\n';
    CHECK(lines == 5);
    char one[160];
    const char* p = text;
    while (*p) {
        const char* e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        snprintf(one, sizeof(one), "%.*s", (int)n, p);
        CHECK(ui_text_width(font, UI_WEIGHT_REGULAR, 17.0f, one, -1) < (float)r.w - 24.0f);
        if (!e) break;
        p = e + 1;
    }
    scene_frame(s, text);
    CHECK(c->overlayH > 100 && c->overlayH < 150);
    CHECK(textPixels(c, 0, r.w, 0, r.h) > 400);
    CHECK(textPixels(c, r.w - 11, r.w, 0, r.h) == 0);
    c->overlayVisible = 0;
}

// ---- the clock strip over the cog

static int stripDiff(const uint8_t* a, const uint8_t* b, UiRectPx r, int x0, int x1) {
    int n = 0;
    for (int y = 0; y < r.h; y++) {
        for (int x = x0; x < x1 && x < r.w; x++) {
            size_t o = ((size_t)(r.y + y) * UI_ATLAS_W + (size_t)(r.x + x)) * 4;
            n += memcmp(a + o, b + o, 4) != 0;
        }
    }
    return n;
}

static uint8_t* copyStrip(XrCtx* c, UiRectPx r) {
    size_t bytes = (size_t)UI_ATLAS_W * (size_t)(r.y + r.h) * 4;
    uint8_t* copy = malloc(bytes);
    memcpy(copy, xrAtlasPixels(c), bytes);
    return copy;
}

static void testStrip(Scene* s) {
    XrCtx* c = s->ctx;
    UiRectPx r = xrAtlasRect(UI_PANEL_CLOCK);
    scene_reset(s);
    c->cogOpen = 1;
    c->cogTab = COG_TAB_DISPLAY;
    c->cogPose = cogPanelPose(c, &c->cogW, &c->cogH);
    c->batteryPresent = 0;
    c->slowConn = 0;
    scene_frame(s, NULL);
    uint8_t* plain = copyStrip(c, r);

    // the headset's cell on the strip, and a bolt once it charges
    c->batteryPresent = 1;
    c->batteryPercent = 87;
    c->batteryCharging = 0;
    scene_frame(s, NULL);
    uint8_t* bat = copyStrip(c, r);
    CHECK(stripDiff(plain, bat, r, 0, 150) > 200);
    CHECK(stripDiff(plain, bat, r, 150, r.w) == 0);
    c->batteryCharging = 1;
    scene_frame(s, NULL);
    uint8_t* chg = copyStrip(c, r);
    CHECK(stripDiff(bat, chg, r, 0, 150) > 20);

    // the percentage moving redraws it
    c->batteryPercent = 86;
    scene_frame(s, NULL);
    uint8_t* lower = copyStrip(c, r);
    CHECK(stripDiff(chg, lower, r, 0, 150) > 0);

    // the badge at the right
    c->slowConn = 1;
    scene_frame(s, NULL);
    uint8_t* slow = copyStrip(c, r);
    CHECK(stripDiff(lower, slow, r, 235, r.w) > 1000);
    CHECK(stripDiff(lower, slow, r, 0, 235) == 0);

    // no battery (the desktop): back to the bare strip, badge or not
    c->batteryPresent = 0;
    c->slowConn = 0;
    scene_frame(s, NULL);
    uint8_t* back = copyStrip(c, r);
    CHECK(stripDiff(plain, back, r, 0, r.w) == 0);

    free(plain);
    free(bat);
    free(chg);
    free(lower);
    free(slow);
    free(back);
    scene_reset(s);
}

// ---- the style through the context and config.ini

static void testStyle(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    Settings st;
    settings_defaults(&st);
    xrApplySettings(c, &st);
    CHECK(c->statsStyle == STATS_STYLE_FULL);
    st.stats = 1;
    st.stats_style = STATS_STYLE_HUD;
    xrApplySettings(c, &st);
    CHECK(c->overlayVisible == 1 && c->statsStyle == STATS_STYLE_HUD);
    Settings back;
    settings_defaults(&back);
    xrSettingsFromCtx(c, &back);
    CHECK(back.stats == 1 && back.stats_style == STATS_STYLE_HUD);
    // the cog's Stats row turns it off and on and leaves the style alone
    CHECK(xrStoreSetting(c, &st, SETTING_STATS, 0));
    CHECK(st.stats == 0 && st.stats_style == STATS_STYLE_HUD);
    CHECK(xrStoreSetting(c, &st, SETTING_STATS, 1));
    CHECK(st.stats == 1 && st.stats_style == STATS_STYLE_HUD);
    // a style out of range is the full lines
    st.stats_style = 7;
    xrApplySettings(c, &st);
    CHECK(c->statsStyle == STATS_STYLE_FULL);
    st.stats_style = STATS_STYLE_LITE;
    xrApplySettings(c, &st);
    CHECK(c->statsStyle == STATS_STYLE_LITE);
    scene_free(s);
}

int main(void) {
    snprintf(g_dir, sizeof(g_dir), "/tmp/mlxr-hud-test-XXXXXX");
    if (mkdtemp(g_dir) == NULL) {
        fprintf(stderr, "no temporary directory\n");
        return 1;
    }
    testBugzipParse();
    testBugzipRun();
    testBugzipFind();
    testBugzipThread();
    Scene* s = scene_new();
    testSheetButton(s);
    testHudWindow(s);
    testStrip(s);
    scene_free(s);
    testStyle();
    char cmd[320];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_dir);
    if (system(cmd) != 0) {
        fprintf(stderr, "could not remove %s\n", g_dir);
    }
    return checksDone("m4_hud");
}
