// Native (lane eye-calibration): the eye calibration's pure parts. The grid and its order; one dot's
// fixation (steady, through blinks, with the head moving; refused when it wanders, is lost or looks
// elsewhere); the run's timing, its one retry and its skip; the fit recovering offsets, scales and
// rotations under noise, dropping outlier dots, refusing too few dots, no gain and an extreme
// correction, and taking the second-order term only where it pays on dots left out; and the
// config.ini form both ways. GPLv3.
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "check.h"
#include "xr_eyecal.h"

#define FRAME_NS 11111111LL

static uint32_t g_seed = 12345u;

// Uniform in -1..1
static float noise(void) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return (float)((g_seed >> 8) & 0xFFFFu) / 32767.5f - 1.0f;
}

// Where a dot is as seen from the head: a 48 by 27 degree picture straight ahead
static void dotAngles(int dot, float* yaw, float* pitch) {
    float u, v;
    eyeCalTarget(dot, &u, &v);
    *yaw = (u - 0.5f) * 48.0f;
    *pitch = (0.5f - v) * 27.0f;
}

typedef struct {
    float scaleY, scaleP;   // measured = R (S t) + offset
    float rotDeg;
    float offY, offP;
    float quadY;            // plus quadY * (t_yaw / 10)^2 on the measured yaw
} Distort;

static void distort(const Distort* d, float ty, float tp, float* my, float* mp) {
    float sy = ty * d->scaleY, sp = tp * d->scaleP;
    float r = d->rotDeg * (float)M_PI / 180.0f;
    *my = cosf(r) * sy - sinf(r) * sp + d->offY + d->quadY * (ty / 10.0f) * (ty / 10.0f);
    *mp = sinf(r) * sy + cosf(r) * sp + d->offP;
}

// The twelve dots as a run would give them: the gaze distorted, noise (uniform, in degrees) on top
static void makePoints(const Distort* d, float noiseDeg, EyeCalPoint* p, int* have) {
    for (int i = 0; i < EYECAL_DOTS; i++) {
        dotAngles(i, &p[i].ty, &p[i].tp);
        distort(d, p[i].ty, p[i].tp, &p[i].my, &p[i].mp);
        p[i].my += noise() * noiseDeg;
        p[i].mp += noise() * noiseDeg;
        have[i] = 1;
    }
}

// The fit's error on fresh points between and round the dots, none of them fitted
static float heldOutError(const EyeCal* cal, const Distort* d) {
    float worst = 0.0f;
    for (int iy = -4; iy <= 4; iy++) {
        for (int ip = -2; ip <= 2; ip++) {
            float ty = 5.0f * iy + 1.3f, tp = 5.0f * ip - 0.7f, my, mp, cy, cp;
            distort(d, ty, tp, &my, &mp);
            eyeCalApply(cal, my, mp, &cy, &cp);
            float e = eyeCalAngleDeg(cy, cp, ty, tp);
            if (e > worst) worst = e;
        }
    }
    return worst;
}

static void testGrid(void) {
    int seen[EYECAL_DOTS] = { 0 };
    for (int s = 0; s < EYECAL_DOTS; s++) {
        int dot = eyeCalOrder(s);
        CHECK(dot >= 0 && dot < EYECAL_DOTS);
        if (dot >= 0 && dot < EYECAL_DOTS) seen[dot]++;
        // never a neighbour of the one before
        if (s > 0) {
            int a = eyeCalOrder(s - 1);
            int dc = a % EYECAL_COLS - dot % EYECAL_COLS, dr = a / EYECAL_COLS - dot / EYECAL_COLS;
            CHECK(dc * dc + dr * dr > 1);
        }
    }
    for (int i = 0; i < EYECAL_DOTS; i++) CHECK(seen[i] == 1);
    CHECK(eyeCalOrder(-1) == -1 && eyeCalOrder(EYECAL_DOTS) == -1);
    float u, v;
    eyeCalTarget(0, &u, &v);
    CHECK_NEAR(u, 0.08, 1e-6);
    CHECK_NEAR(v, 0.08, 1e-6);
    eyeCalTarget(EYECAL_DOTS - 1, &u, &v);
    CHECK_NEAR(u, 0.92, 1e-6);
    CHECK_NEAR(v, 0.92, 1e-6);
    eyeCalTarget(5, &u, &v);   // the middle row, second column
    CHECK_NEAR(v, 0.5, 1e-6);
    CHECK_NEAR(u, 0.08 + 0.84 / 3.0, 1e-6);
}

static void testDirections(void) {
    float d[3], y, p;
    eyeCalDir(0.0f, 0.0f, d);
    CHECK_NEAR(d[2], -1.0, 1e-6);
    eyeCalDir(30.0f, -12.0f, d);
    CHECK(d[0] > 0.0f && d[1] < 0.0f);
    eyeCalAngles(d, &y, &p);
    CHECK_NEAR(y, 30.0, 1e-4);
    CHECK_NEAR(p, -12.0, 1e-4);
    CHECK_NEAR(eyeCalAngleDeg(0.0f, 0.0f, 3.0f, 4.0f), 5.0, 0.02);
    CHECK_NEAR(eyeCalAngleDeg(10.0f, 5.0f, 10.0f, 5.0f), 0.0, 1e-5);
    CHECK_NEAR(eyeCalAngleDeg(0.0f, 0.0f, 0.01f, 0.0f), 0.01, 1e-4);   // small angles stay exact
}

// One dot's samples: the gaze offset (oy, op) from a dot that moves with the head (headDegPerFrame),
// noise in degrees, every blinkEvery'th frame lost to a blink or thrown 8 degrees away
static int fixation(float oy, float op, float noiseDeg, float headDegPerFrame, int blinkEvery, int lost,
                    EyeCalPoint* out, float* spread) {
    float gy[EYECAL_SAMPLES_MAX], gp[EYECAL_SAMPLES_MAX], ty[EYECAL_SAMPLES_MAX], tp[EYECAL_SAMPLES_MAX];
    int frames = 72, n = 0;
    for (int i = 0; i < frames; i++) {
        float t = 10.0f - headDegPerFrame * i, q = -5.0f + 0.5f * headDegPerFrame * i;
        float y = t + oy + noise() * noiseDeg, p = q + op + noise() * noiseDeg;
        if (blinkEvery > 0 && i % blinkEvery == 0) {
            if (lost) continue;
            y += 8.0f;
        }
        gy[n] = y;
        gp[n] = p;
        ty[n] = t;
        tp[n] = q;
        n++;
    }
    return eyeCalFixation(gy, gp, ty, tp, n, frames, out, spread);
}

static void testFixation(void) {
    EyeCalPoint p;
    float spread;
    // a degree of jitter round one spot, as the Frame's eyes give it: steady
    CHECK(fixation(2.0f, -1.0f, 1.0f, 0.0f, 0, 0, &p, &spread));
    CHECK(spread > 0.3f && spread < EYECAL_STEADY_DEG);
    CHECK_NEAR(p.my - p.ty, 2.0, 0.25);
    CHECK_NEAR(p.mp - p.tp, -1.0, 0.25);
    // the head turning through it (the eye holds the dot): the offset is what counts
    CHECK(fixation(2.0f, -1.0f, 0.5f, 0.1f, 0, 0, &p, &spread));
    CHECK_NEAR(p.my - p.ty, 2.0, 0.2);
    CHECK_NEAR(p.ty, 10.0f - 0.1f * 35.5f, 0.05);
    // blinks lost (one frame in five) or thrown wide (one in six): still steady, the wide ones not counted
    CHECK(fixation(0.0f, 0.0f, 0.5f, 0.0f, 5, 1, &p, NULL));
    CHECK(fixation(0.0f, 0.0f, 0.5f, 0.0f, 6, 0, &p, NULL));
    CHECK_NEAR(p.my - p.ty, 0.0, 0.2);
    // half the frames lost: too few kept
    CHECK(!fixation(0.0f, 0.0f, 0.5f, 0.0f, 2, 1, &p, NULL));
    // wandering by three degrees: not a fixation
    CHECK(!fixation(0.0f, 0.0f, 3.0f, 0.0f, 0, 0, &p, &spread));
    CHECK(spread > EYECAL_STEADY_DEG);
    // looking 20 degrees off the dot: somewhere else
    CHECK(!fixation(20.0f, 0.0f, 0.3f, 0.0f, 0, 0, &p, NULL));
    // nothing at all
    CHECK(!eyeCalFixation(NULL, NULL, NULL, NULL, 0, 72, &p, NULL));
}

// A whole run at 90 Hz with the gaze at the dot plus an offset; jitter(dot, try) is the noise
typedef float (*JitterFn)(int dot, int attempt);

static float steadyJitter(int dot, int attempt) {
    (void)dot; (void)attempt;
    return 0.6f;
}

// the third dot shown jitters on its first try only, the seventh on both
static float badJitter(int dot, int attempt) {
    if (dot == eyeCalOrder(2) && attempt == 0) return 4.0f;
    if (dot == eyeCalOrder(6)) return 4.0f;
    return 0.6f;
}

static int runAll(EyeCalRun* r, JitterFn jitter, int64_t* tookNs, int* nexts) {
    int64_t now = 1000000000LL, start = now;
    eyeCalRunStart(r);
    CHECK(eyeCalRunUp(r) && eyeCalRunDot(r) == eyeCalOrder(0));
    int lastDot = -1, attempt = 0, got = EYECAL_STEP_NONE;
    *nexts = 0;
    for (int frame = 0; frame < 90 * 60 && got != EYECAL_STEP_FINISHED; frame++) {
        int dot = eyeCalRunDot(r);
        if (dot != lastDot) {
            attempt = 0;
            lastDot = dot;
        }
        float ty, tp;
        dotAngles(dot, &ty, &tp);
        float j = jitter(dot, attempt);
        got = eyeCalRunStep(r, now, 1, ty + 1.5f + noise() * j, tp - 0.5f + noise() * j, ty, tp);
        if (got == EYECAL_STEP_NEXT) {
            (*nexts)++;
            if (eyeCalRunDot(r) == dot) attempt++;
        }
        now += FRAME_NS;
    }
    *tookNs = now - start;
    return got;
}

static void testRun(void) {
    EyeCalRun r;
    int64_t took;
    int nexts;
    CHECK(runAll(&r, steadyJitter, &took, &nexts) == EYECAL_STEP_FINISHED);
    CHECK(!eyeCalRunUp(&r) && eyeCalRunDot(&r) == -1);
    CHECK(r.good == EYECAL_DOTS && r.skipped == 0 && r.retries == 0 && nexts == EYECAL_DOTS - 1);
    // 12 dots of 0.4 s settling and 0.8 s reading, give or take a frame each
    CHECK(took > 12 * 1150000000LL && took < 12 * 1250000000LL);
    for (int i = 0; i < EYECAL_DOTS; i++) {
        CHECK(r.have[i]);
        CHECK_NEAR(r.points[i].my - r.points[i].ty, 1.5, 0.2);
        CHECK_NEAR(r.points[i].mp - r.points[i].tp, -0.5, 0.2);
    }
    // one dot steady on its second try, one never: one retry each, one skipped
    CHECK(runAll(&r, badJitter, &took, &nexts) == EYECAL_STEP_FINISHED);
    CHECK(r.good == EYECAL_DOTS - 1 && r.retries == 2 && r.skipped == 1);
    CHECK(r.have[eyeCalOrder(2)] && !r.have[eyeCalOrder(6)]);
    // the dot shrinks while the eye settles, then holds its size
    eyeCalRunStart(&r);
    CHECK_NEAR(eyeCalRunDotDeg(&r, 0), EYECAL_DOT_DEG * EYECAL_DOT_START, 1e-5);
    eyeCalRunStep(&r, 1000000000LL, 1, 0, 0, 0, 0);
    float a = eyeCalRunDotDeg(&r, 1000000000LL + 100000000LL);
    float b = eyeCalRunDotDeg(&r, 1000000000LL + 300000000LL);
    CHECK(a < EYECAL_DOT_DEG * EYECAL_DOT_START && b < a && b > EYECAL_DOT_DEG);
    CHECK_NEAR(eyeCalRunDotDeg(&r, 1000000000LL + EYECAL_SETTLE_NS), EYECAL_DOT_DEG, 1e-4);
    // nothing counts while the gaze is lost: the dot is retried, then skipped
    eyeCalRunStart(&r);
    int64_t now = 1000000000LL;
    int got = EYECAL_STEP_NONE;
    for (int i = 0; i < 400 && r.step == 0; i++, now += FRAME_NS) {
        got = eyeCalRunStep(&r, now, 0, 0, 0, 0, 0);
    }
    CHECK(got == EYECAL_STEP_NEXT && r.step == 1 && r.skipped == 1 && !r.have[eyeCalOrder(0)]);
    eyeCalRunStop(&r);
    CHECK(!eyeCalRunUp(&r) && eyeCalRunStep(&r, now, 1, 0, 0, 0, 0) == EYECAL_STEP_NONE);
}

static void testFitRecovers(void) {
    static const Distort CASES[] = {
        { 1.0f, 1.0f, 0.0f, 2.5f, -1.5f, 0.0f },     // an offset
        { 1.12f, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f },     // scales
        { 1.0f, 1.0f, 5.0f, 0.0f, 0.0f, 0.0f },      // a rotation
        { 0.92f, 1.1f, -3.0f, -1.8f, 2.2f, 0.0f },   // all three
    };
    for (size_t c = 0; c < sizeof(CASES) / sizeof(CASES[0]); c++) {
        for (int trial = 0; trial < 3; trial++) {
            EyeCalPoint p[EYECAL_DOTS];
            int have[EYECAL_DOTS];
            makePoints(&CASES[c], 0.3f, p, have);
            EyeCal cal;
            int got = eyeCalFit(p, have, &cal);
            CHECK(got == EYECAL_OK);
            CHECK(cal.model == EYECAL_MODEL_AFFINE);
            CHECK(cal.used == EYECAL_DOTS && cal.dropped == 0);
            CHECK(cal.meanBefore > 1.0f && cal.meanAfter < 0.35f && cal.maxAfter < 0.7f);
            CHECK(cal.maxBefore >= cal.meanBefore && cal.maxAfter >= cal.meanAfter);
            CHECK(heldOutError(&cal, &CASES[c]) < 0.6f);
            if (got != EYECAL_OK) {
                printf("  case %zu: %d, %.2f -> %.2f\n", c, got, cal.meanBefore, cal.meanAfter);
            }
        }
    }
    // no correction, no change
    EyeCal none;
    memset(&none, 0, sizeof(none));
    float y, q;
    eyeCalApply(&none, 3.0f, -2.0f, &y, &q);
    CHECK(y == 3.0f && q == -2.0f);
    eyeCalApply(NULL, 3.0f, -2.0f, &y, &q);
    CHECK(y == 3.0f && q == -2.0f);
}

static void testOutliers(void) {
    Distort d = { 1.05f, 0.95f, 2.0f, 1.5f, -1.0f, 0.0f };
    EyeCalPoint p[EYECAL_DOTS];
    int have[EYECAL_DOTS];
    makePoints(&d, 0.2f, p, have);
    // two dots the eye was not quite on, 6 degrees out
    p[1].my += 6.0f;
    p[10].mp -= 6.0f;
    EyeCal cal;
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_OK);
    CHECK(cal.dropped == 2 && cal.used == EYECAL_DOTS - 2);
    CHECK(cal.meanAfter < 0.3f);
    CHECK(heldOutError(&cal, &d) < 0.6f);
    // five bad dots: no more than three go, and the fit shows it
    makePoints(&d, 0.2f, p, have);
    for (int i = 0; i < 5; i++) p[i * 2].my += (i % 2 ? -7.0f : 7.0f);
    CHECK(eyeCalFit(p, have, &cal) != EYECAL_OK || cal.dropped <= EYECAL_DROP_MAX);
    CHECK(cal.dropped <= EYECAL_DROP_MAX);
    // none dropped below the minimum: eight dots, one bad, all kept
    makePoints(&d, 0.2f, p, have);
    for (int i = 8; i < EYECAL_DOTS; i++) have[i] = 0;
    p[2].my += 6.0f;
    eyeCalFit(p, have, &cal);
    CHECK(cal.dropped == 0 && cal.used == EYECAL_MIN_DOTS);
}

static void testRejects(void) {
    EyeCalPoint p[EYECAL_DOTS];
    int have[EYECAL_DOTS];
    EyeCal cal;
    // seven good dots are too few
    Distort off = { 1.0f, 1.0f, 0.0f, 3.0f, 0.0f, 0.0f };
    makePoints(&off, 0.2f, p, have);
    for (int i = 7; i < EYECAL_DOTS; i++) have[i] = 0;
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_FAIL_FEW && cal.used == 7 && cal.model == EYECAL_MODEL_NONE);
    have[7] = 1;
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_OK);
    // an exact gaze: nothing to gain
    Distort exact = { 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    makePoints(&exact, 0.3f, p, have);
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_FAIL_GAIN);
    CHECK(cal.meanBefore < 0.5f);
    // a scale past the bounds, a mirror and an offset of 12 degrees: too large
    Distort squash = { 1.0f / 0.6f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    makePoints(&squash, 0.2f, p, have);
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_FAIL_EXTREME);
    Distort mirror = { -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    makePoints(&mirror, 0.2f, p, have);
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_FAIL_EXTREME);
    Distort far = { 1.0f, 1.0f, 0.0f, -12.0f, 0.0f, 0.0f };
    makePoints(&far, 0.2f, p, have);
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_FAIL_EXTREME);
    // inside the bounds: a scale of 1.25 and an offset of 6 degrees are kept
    Distort big = { 1.0f / 1.25f, 1.0f, 0.0f, 6.0f, 0.0f, 0.0f };
    makePoints(&big, 0.2f, p, have);
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_OK);
    // the bounds on their own
    EyeCal c;
    memset(&c, 0, sizeof(c));
    CHECK(!eyeCalSane(&c));
    c.model = EYECAL_MODEL_AFFINE;
    CHECK(eyeCalSane(&c));
    c.c[0][0] = 9.9f;
    CHECK(eyeCalSane(&c));
    c.c[0][0] = 7.5f;
    c.c[1][0] = 7.5f;   // 10.6 degrees off the middle
    CHECK(!eyeCalSane(&c));
    c.c[1][0] = 0.0f;
    c.c[0][1] = 4.5f;   // a scale of 1.45 on the yaw
    CHECK(!eyeCalSane(&c));
    c.c[0][1] = -3.5f;  // 0.65
    CHECK(!eyeCalSane(&c));
    c.c[0][1] = 0.0f;
    c.c[1][1] = NAN;
    CHECK(!eyeCalSane(&c));
}

static void testQuad(void) {
    EyeCalPoint p[EYECAL_DOTS];
    int have[EYECAL_DOTS];
    EyeCal cal;
    // the yaw bends away toward the sides: the second-order term earns its place on the dots left out
    Distort bend = { 1.0f, 1.0f, 0.0f, 0.5f, 0.0f, 0.3f };
    makePoints(&bend, 0.1f, p, have);
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_OK);
    CHECK(cal.model == EYECAL_MODEL_QUAD);
    CHECK(cal.meanAfter < 0.2f);
    // plain affine with noise: affine stays, whatever the noise does to the fit
    int quads = 0;
    Distort plain = { 1.08f, 0.94f, 2.0f, 1.0f, -1.0f, 0.0f };
    for (int trial = 0; trial < 20; trial++) {
        makePoints(&plain, 0.4f, p, have);
        if (eyeCalFit(p, have, &cal) == EYECAL_OK && cal.model == EYECAL_MODEL_QUAD) quads++;
    }
    CHECK(quads == 0);
    // nine dots: not tried
    makePoints(&bend, 0.1f, p, have);
    have[0] = have[5] = have[11] = 0;
    eyeCalFit(p, have, &cal);
    CHECK(cal.model == EYECAL_MODEL_AFFINE);
}

static void testText(void) {
    EyeCalPoint p[EYECAL_DOTS];
    int have[EYECAL_DOTS];
    EyeCal cal, back;
    char text[128];
    Distort d = { 0.92f, 1.1f, -3.0f, -1.8f, 2.2f, 0.0f };
    makePoints(&d, 0.3f, p, have);
    CHECK(eyeCalFit(p, have, &cal) == EYECAL_OK);
    CHECK(eyeCalFormat(&cal, text, sizeof text));
    CHECK(text[0] == '1' && text[1] == ' ' && text[2] == 'a');
    CHECK(eyeCalParse(text, &back) == 1 && back.model == EYECAL_MODEL_AFFINE);
    for (int axis = 0; axis < 2; axis++) {
        for (int i = 0; i < 3; i++) CHECK_NEAR(back.c[axis][i], cal.c[axis][i], 0.0006);
    }
    CHECK_NEAR(back.meanAfter, cal.meanAfter, 0.006);
    CHECK_NEAR(back.meanBefore, cal.meanBefore, 0.006);
    float y1, p1, y2, p2;
    eyeCalApply(&cal, 20.0f, -10.0f, &y1, &p1);
    eyeCalApply(&back, 20.0f, -10.0f, &y2, &p2);
    CHECK(eyeCalAngleDeg(y1, p1, y2, p2) < 0.01f);
    // the second-order form, at its longest, still fits the key's 128 bytes
    EyeCal q;
    memset(&q, 0, sizeof(q));
    q.model = EYECAL_MODEL_QUAD;
    for (int axis = 0; axis < 2; axis++) {
        q.c[axis][0] = -6.999f;
        for (int i = 1; i < EYECAL_TERMS; i++) q.c[axis][i] = -0.123f;
    }
    q.meanAfter = 12.34f;
    q.meanBefore = 23.45f;
    q.maxAfter = 34.56f;
    q.maxBefore = 45.67f;
    CHECK(eyeCalFormat(&q, text, sizeof text));
    CHECK(strlen(text) < 128 && text[2] == 'q');
    CHECK(eyeCalParse(text, &back) == 1 && back.model == EYECAL_MODEL_QUAD);
    CHECK_NEAR(back.c[1][5], -0.123, 1e-6);
    // too small a buffer says so
    CHECK(!eyeCalFormat(&q, text, 40) && text[0] == 0);
    // none is ""
    memset(&q, 0, sizeof(q));
    CHECK(eyeCalFormat(&q, text, sizeof text) && text[0] == 0);
    CHECK(eyeCalParse("", &back) == 0 && back.model == EYECAL_MODEL_NONE);
    CHECK(eyeCalParse(NULL, &back) == 0);
    // what is not one: another version, the wrong count, words, a correction out of bounds
    CHECK(eyeCalParse("2 a 0 0 0 0 0 0 1 2 1 2", &back) == 0);
    CHECK(eyeCalParse("1 a 0 0 0 0 0 1 2 1 2", &back) == 0);
    CHECK(eyeCalParse("1 a 0 0 0 0 0 0 1 2 1 2 9", &back) == 0);
    CHECK(eyeCalParse("1 x 0 0 0 0 0 0 1 2 1 2", &back) == 0);
    CHECK(eyeCalParse("1 a 0 0 zero 0 0 0 1 2 1 2", &back) == 0);
    CHECK(eyeCalParse("1 a 11 0 0 0 0 0 1 2 1 2", &back) == 0);
    CHECK(eyeCalParse("1 a 0 0 0 0 0 0 1 2 1 2", &back) == 1 && back.model == EYECAL_MODEL_AFFINE);
    CHECK(eyeCalParse("1 a 1.5 0.2 -0.1 -0.8 0 0.3 0.9 2.4 1.6 4.1  ", &back) == 1);
    CHECK_NEAR(back.c[0][0], 1.5, 1e-6);
    CHECK_NEAR(back.meanAfter, 0.9, 1e-6);
    CHECK_NEAR(back.maxBefore, 4.1, 1e-6);
}

int main(void) {
    testGrid();
    testDirections();
    testFixation();
    testRun();
    testFitRecovers();
    testOutliers();
    testRejects();
    testQuad();
    testText();
    return checksDone("xrcore eyecal");
}
