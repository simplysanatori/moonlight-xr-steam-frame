// Native (lane eye-calibration): see xr_eyecal.h. GPLv3.
#include "xr_eyecal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG_PER_RAD 57.29577951308232
#define RAD_PER_DEG 0.017453292519943295

// ---- the dots ----------------------------------------------------------------------------------

void eyeCalTarget(int dot, float* u, float* v) {
    if (dot < 0 || dot >= EYECAL_DOTS) {
        *u = 0.5f;
        *v = 0.5f;
        return;
    }
    int col = dot % EYECAL_COLS, row = dot / EYECAL_COLS;
    *u = EYECAL_INSET + (float)col * (1.0f - 2.0f * EYECAL_INSET) / (float)(EYECAL_COLS - 1);
    *v = EYECAL_INSET + (float)row * (1.0f - 2.0f * EYECAL_INSET) / (float)(EYECAL_ROWS - 1);
}

int eyeCalOrder(int step) {
    // middle left, then corners and edges in turn, never a neighbour of the dot before
    static const int ORDER[EYECAL_DOTS] = { 5, 0, 3, 11, 8, 6, 1, 10, 4, 2, 9, 7 };
    return step >= 0 && step < EYECAL_DOTS ? ORDER[step] : -1;
}

// ---- directions --------------------------------------------------------------------------------

void eyeCalDir(float yawDeg, float pitchDeg, float d[3]) {
    double y = yawDeg * RAD_PER_DEG, p = pitchDeg * RAD_PER_DEG;
    d[0] = (float)(cos(p) * sin(y));
    d[1] = (float)sin(p);
    d[2] = (float)(-cos(p) * cos(y));
}

void eyeCalAngles(const float d[3], float* yawDeg, float* pitchDeg) {
    double flat = sqrt((double)d[0] * d[0] + (double)d[2] * d[2]);
    *yawDeg = (float)(atan2(d[0], -d[2]) * DEG_PER_RAD);
    *pitchDeg = (float)(atan2(d[1], flat) * DEG_PER_RAD);
}

float eyeCalAngleDeg(float yaw1, float pitch1, float yaw2, float pitch2) {
    float a[3], b[3];
    eyeCalDir(yaw1, pitch1, a);
    eyeCalDir(yaw2, pitch2, b);
    // atan2 of the cross and dot products: exact for small angles, where acos is not
    double cx = (double)a[1] * b[2] - (double)a[2] * b[1];
    double cy = (double)a[2] * b[0] - (double)a[0] * b[2];
    double cz = (double)a[0] * b[1] - (double)a[1] * b[0];
    double dot = (double)a[0] * b[0] + (double)a[1] * b[1] + (double)a[2] * b[2];
    return (float)(atan2(sqrt(cx * cx + cy * cy + cz * cz), dot) * DEG_PER_RAD);
}

// ---- one dot's fixation ------------------------------------------------------------------------

static int cmpFloat(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static float median(const float* v, int n) {
    float tmp[EYECAL_SAMPLES_MAX];
    if (n <= 0) {
        return 0.0f;
    }
    if (n > EYECAL_SAMPLES_MAX) {
        n = EYECAL_SAMPLES_MAX;
    }
    memcpy(tmp, v, sizeof(float) * (size_t)n);
    qsort(tmp, (size_t)n, sizeof(float), cmpFloat);
    return n % 2 ? tmp[n / 2] : 0.5f * (tmp[n / 2 - 1] + tmp[n / 2]);
}

int eyeCalFixation(const float* gy, const float* gp, const float* ty, const float* tp, int n, int frames,
                   EyeCalPoint* out, float* spreadDeg) {
    if (spreadDeg != NULL) {
        *spreadDeg = -1.0f;
    }
    if (n > EYECAL_SAMPLES_MAX) {
        n = EYECAL_SAMPLES_MAX;
    }
    if (n < EYECAL_SAMPLES_MIN) {
        return 0;
    }
    // the gaze's offset from the dot, sample by sample: the head may move, the offset is what holds
    float oy[EYECAL_SAMPLES_MAX], op[EYECAL_SAMPLES_MAX];
    for (int i = 0; i < n; i++) {
        oy[i] = gy[i] - ty[i];
        op[i] = gp[i] - tp[i];
    }
    float my = median(oy, n), mp = median(op, n);
    double sy = 0, sp = 0, sgy = 0, sgp = 0, sty = 0, stp = 0;
    int kept = 0;
    for (int i = 0; i < n; i++) {
        if (hypotf(oy[i] - my, op[i] - mp) <= EYECAL_GATE_DEG) {
            sy += oy[i];
            sp += op[i];
            sgy += gy[i];
            sgp += gp[i];
            sty += ty[i];
            stp += tp[i];
            kept++;
        }
    }
    if (kept < EYECAL_SAMPLES_MIN || (float)kept < EYECAL_KEEP_FRAC * (float)frames) {
        return 0;
    }
    double ay = sy / kept, ap = sp / kept, ss = 0;
    for (int i = 0; i < n; i++) {
        if (hypotf(oy[i] - my, op[i] - mp) <= EYECAL_GATE_DEG) {
            ss += (oy[i] - ay) * (oy[i] - ay) + (op[i] - ap) * (op[i] - ap);
        }
    }
    float spread = (float)sqrt(ss / kept);
    if (spreadDeg != NULL) {
        *spreadDeg = spread;
    }
    if (spread > EYECAL_STEADY_DEG || hypot(ay, ap) > EYECAL_FAR_DEG) {
        return 0;
    }
    if (out != NULL) {
        out->my = (float)(sgy / kept);
        out->mp = (float)(sgp / kept);
        out->ty = (float)(sty / kept);
        out->tp = (float)(stp / kept);
    }
    return 1;
}

// ---- the run -----------------------------------------------------------------------------------

void eyeCalRunStart(EyeCalRun* r) {
    memset(r, 0, sizeof(*r));
    r->phase = EYECAL_PHASE_SETTLE;
}

void eyeCalRunStop(EyeCalRun* r) {
    r->phase = EYECAL_PHASE_IDLE;
}

int eyeCalRunUp(const EyeCalRun* r) {
    return r != NULL && r->phase != EYECAL_PHASE_IDLE;
}

int eyeCalRunDot(const EyeCalRun* r) {
    return eyeCalRunUp(r) ? eyeCalOrder(r->step) : -1;
}

float eyeCalRunDotDeg(const EyeCalRun* r, int64_t nowNs) {
    if (!eyeCalRunUp(r)) {
        return 0.0f;
    }
    if (r->phase == EYECAL_PHASE_COLLECT) {
        return EYECAL_DOT_DEG;
    }
    if (r->phaseNs == 0) {
        return EYECAL_DOT_DEG * EYECAL_DOT_START;
    }
    float t = (float)(nowNs - r->phaseNs) / (float)EYECAL_SETTLE_NS;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    // eased, quick at first
    float k = 1.0f - (1.0f - t) * (1.0f - t);
    return EYECAL_DOT_DEG * (EYECAL_DOT_START + (1.0f - EYECAL_DOT_START) * k);
}

// The dot is done, kept or skipped: on to the next, or the end
static int nextDot(EyeCalRun* r, int64_t nowNs) {
    r->step++;
    r->retried = 0;
    if (r->step >= EYECAL_DOTS) {
        r->phase = EYECAL_PHASE_IDLE;
        return EYECAL_STEP_FINISHED;
    }
    r->phase = EYECAL_PHASE_SETTLE;
    r->phaseNs = nowNs;
    return EYECAL_STEP_NEXT;
}

int eyeCalRunStep(EyeCalRun* r, int64_t nowNs, int gazeOk, float gy, float gp, float ty, float tp) {
    if (!eyeCalRunUp(r)) {
        return EYECAL_STEP_NONE;
    }
    if (r->phaseNs == 0) {
        r->phaseNs = nowNs;   // the first dot appears this frame
        return EYECAL_STEP_NONE;
    }
    if (r->phase == EYECAL_PHASE_SETTLE) {
        if (nowNs - r->phaseNs >= EYECAL_SETTLE_NS) {
            r->phase = EYECAL_PHASE_COLLECT;
            r->phaseNs = nowNs;
            r->frames = 0;
            r->n = 0;
        }
        else {
            return EYECAL_STEP_NONE;
        }
    }
    r->frames++;
    if (gazeOk && r->n < EYECAL_SAMPLES_MAX && isfinite(gy) && isfinite(gp) && isfinite(ty) && isfinite(tp)) {
        r->gy[r->n] = gy;
        r->gp[r->n] = gp;
        r->ty[r->n] = ty;
        r->tp[r->n] = tp;
        r->n++;
    }
    if (nowNs - r->phaseNs < EYECAL_COLLECT_NS) {
        return EYECAL_STEP_NONE;
    }
    int dot = eyeCalOrder(r->step);
    EyeCalPoint p;
    if (eyeCalFixation(r->gy, r->gp, r->ty, r->tp, r->n, r->frames, &p, NULL)) {
        r->points[dot] = p;
        r->have[dot] = 1;
        r->good++;
        return nextDot(r, nowNs);
    }
    if (!r->retried) {
        // once more, from the settling
        r->retried = 1;
        r->retries++;
        r->phase = EYECAL_PHASE_SETTLE;
        r->phaseNs = nowNs;
        return EYECAL_STEP_NEXT;
    }
    r->skipped++;
    return nextDot(r, nowNs);
}

// ---- the fit -----------------------------------------------------------------------------------

static int modelTerms(int model) {
    return model == EYECAL_MODEL_QUAD ? 6 : 3;
}

static void basis(float yawDeg, float pitchDeg, double f[EYECAL_TERMS]) {
    double u = yawDeg / EYECAL_UNIT_DEG, v = pitchDeg / EYECAL_UNIT_DEG;
    f[0] = 1.0;
    f[1] = u;
    f[2] = v;
    f[3] = u * u;
    f[4] = u * v;
    f[5] = v * v;
}

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : x > hi ? hi : x;
}

void eyeCalApply(const EyeCal* cal, float yaw, float pitch, float* outYaw, float* outPitch) {
    if (cal == NULL || cal->model == EYECAL_MODEL_NONE) {
        *outYaw = yaw;
        *outPitch = pitch;
        return;
    }
    double f[EYECAL_TERMS];
    basis(clampf(yaw, -EYECAL_CLAMP_YAW_DEG, EYECAL_CLAMP_YAW_DEG),
          clampf(pitch, -EYECAL_CLAMP_PITCH_DEG, EYECAL_CLAMP_PITCH_DEG), f);
    int k = modelTerms(cal->model);
    double dy = 0, dp = 0;
    for (int i = 0; i < k; i++) {
        dy += cal->c[0][i] * f[i];
        dp += cal->c[1][i] * f[i];
    }
    *outYaw = yaw + (float)dy;
    *outPitch = pitch + (float)dp;
}

// Solves a k x k system in place (Gaussian elimination, partial pivoting). 0 when it is singular.
static int solve(double a[EYECAL_TERMS][EYECAL_TERMS], double b[2][EYECAL_TERMS], int k) {
    for (int col = 0; col < k; col++) {
        int pivot = col;
        for (int r = col + 1; r < k; r++) {
            if (fabs(a[r][col]) > fabs(a[pivot][col])) {
                pivot = r;
            }
        }
        if (fabs(a[pivot][col]) < 1e-9) {
            return 0;
        }
        if (pivot != col) {
            for (int c = 0; c < k; c++) {
                double t = a[col][c];
                a[col][c] = a[pivot][c];
                a[pivot][c] = t;
            }
            for (int axis = 0; axis < 2; axis++) {
                double t = b[axis][col];
                b[axis][col] = b[axis][pivot];
                b[axis][pivot] = t;
            }
        }
        for (int r = col + 1; r < k; r++) {
            double m = a[r][col] / a[col][col];
            for (int c = col; c < k; c++) {
                a[r][c] -= m * a[col][c];
            }
            for (int axis = 0; axis < 2; axis++) {
                b[axis][r] -= m * b[axis][col];
            }
        }
    }
    for (int r = k - 1; r >= 0; r--) {
        for (int axis = 0; axis < 2; axis++) {
            double s = b[axis][r];
            for (int c = r + 1; c < k; c++) {
                s -= a[r][c] * b[axis][c];
            }
            b[axis][r] = s / a[r][r];
        }
    }
    return 1;
}

// The least squares fit over the dots in use (and not skip), into cal's coefficients
static int fitModel(const EyeCalPoint* p, const int* use, int skip, int model, EyeCal* cal) {
    int k = modelTerms(model), n = 0;
    double ata[EYECAL_TERMS][EYECAL_TERMS], atb[2][EYECAL_TERMS];
    memset(ata, 0, sizeof(ata));
    memset(atb, 0, sizeof(atb));
    for (int i = 0; i < EYECAL_DOTS; i++) {
        if (!use[i] || i == skip) {
            continue;
        }
        double f[EYECAL_TERMS];
        basis(p[i].my, p[i].mp, f);
        double ry = (double)p[i].ty - p[i].my, rp = (double)p[i].tp - p[i].mp;
        for (int r = 0; r < k; r++) {
            for (int c = 0; c < k; c++) {
                ata[r][c] += f[r] * f[c];
            }
            atb[0][r] += f[r] * ry;
            atb[1][r] += f[r] * rp;
        }
        n++;
    }
    if (n < k + 1 || !solve(ata, atb, k)) {
        return 0;
    }
    memset(cal->c, 0, sizeof(cal->c));
    for (int i = 0; i < k; i++) {
        cal->c[0][i] = (float)atb[0][i];
        cal->c[1][i] = (float)atb[1][i];
    }
    cal->model = model;
    return 1;
}

static float pointError(const EyeCal* cal, const EyeCalPoint* p) {
    float y, q;
    eyeCalApply(cal, p->my, p->mp, &y, &q);
    return eyeCalAngleDeg(y, q, p->ty, p->tp);
}

// The mean error on each dot in use when it is left out of the fit; -1 when a fit fails
static float leaveOneOut(const EyeCalPoint* p, const int* use, int model) {
    double sum = 0;
    int n = 0;
    for (int i = 0; i < EYECAL_DOTS; i++) {
        if (!use[i]) {
            continue;
        }
        EyeCal cal;
        memset(&cal, 0, sizeof(cal));
        if (!fitModel(p, use, i, model, &cal)) {
            return -1.0f;
        }
        sum += pointError(&cal, &p[i]);
        n++;
    }
    return n > 0 ? (float)(sum / n) : -1.0f;
}

// The singular values of 2 x 2 [a b; c d]
static void singular2(double a, double b, double c, double d, double* lo, double* hi) {
    double s = a * a + b * b + c * c + d * d, det = a * d - b * c;
    double root = sqrt(fmax(0.0, s * s - 4.0 * det * det));
    *hi = sqrt(fmax(0.0, (s + root) * 0.5));
    *lo = sqrt(fmax(0.0, (s - root) * 0.5));
}

// Whether the correction's scale at a measured yaw and pitch is within the bounds and keeps the
// picture the right way round, and its shift there within limit degrees
static int saneAt(const EyeCal* cal, float yaw, float pitch, float limit) {
    double u = yaw / EYECAL_UNIT_DEG, v = pitch / EYECAL_UNIT_DEG;
    const float* cy = cal->c[0];
    const float* cp = cal->c[1];
    int quad = cal->model == EYECAL_MODEL_QUAD;
    double yu = cy[1] + (quad ? 2.0 * cy[3] * u + cy[4] * v : 0.0);
    double yv = cy[2] + (quad ? cy[4] * u + 2.0 * cy[5] * v : 0.0);
    double pu = cp[1] + (quad ? 2.0 * cp[3] * u + cp[4] * v : 0.0);
    double pv = cp[2] + (quad ? cp[4] * u + 2.0 * cp[5] * v : 0.0);
    double a = 1.0 + yu / EYECAL_UNIT_DEG, b = yv / EYECAL_UNIT_DEG;
    double c = pu / EYECAL_UNIT_DEG, d = 1.0 + pv / EYECAL_UNIT_DEG;
    double lo, hi;
    singular2(a, b, c, d, &lo, &hi);
    if (a * d - b * c <= 0.0 || lo < EYECAL_SCALE_MIN || hi > EYECAL_SCALE_MAX) {
        return 0;
    }
    float y, p;
    eyeCalApply(cal, yaw, pitch, &y, &p);
    return hypotf(y - yaw, p - pitch) <= limit;
}

int eyeCalSane(const EyeCal* cal) {
    if (cal == NULL || (cal->model != EYECAL_MODEL_AFFINE && cal->model != EYECAL_MODEL_QUAD)) {
        return 0;
    }
    for (int axis = 0; axis < 2; axis++) {
        for (int i = 0; i < EYECAL_TERMS; i++) {
            if (!isfinite(cal->c[axis][i])) {
                return 0;
            }
        }
    }
    if (!saneAt(cal, 0.0f, 0.0f, EYECAL_OFFSET_MAX_DEG)) {
        return 0;
    }
    if (cal->model == EYECAL_MODEL_QUAD) {
        // the scale varies across the view: checked over the region a grid on a picture covers
        for (int iy = -2; iy <= 2; iy++) {
            for (int ip = -2; ip <= 2; ip++) {
                if (!saneAt(cal, 15.0f * iy, 10.0f * ip, 1.5f * EYECAL_OFFSET_MAX_DEG)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static void errors(const EyeCalPoint* p, const int* use, const EyeCal* cal, float* mean, float* max) {
    double sum = 0;
    float worst = 0.0f;
    int n = 0;
    for (int i = 0; i < EYECAL_DOTS; i++) {
        if (!use[i]) {
            continue;
        }
        float e = cal != NULL ? pointError(cal, &p[i]) : eyeCalAngleDeg(p[i].my, p[i].mp, p[i].ty, p[i].tp);
        sum += e;
        if (e > worst) {
            worst = e;
        }
        n++;
    }
    *mean = n > 0 ? (float)(sum / n) : 0.0f;
    *max = worst;
}

int eyeCalFit(const EyeCalPoint* points, const int* have, EyeCal* out) {
    memset(out, 0, sizeof(*out));
    int use[EYECAL_DOTS];
    int n = 0;
    for (int i = 0; i < EYECAL_DOTS; i++) {
        use[i] = have[i] && isfinite(points[i].my) && isfinite(points[i].mp) && isfinite(points[i].ty)
                && isfinite(points[i].tp);
        n += use[i];
    }
    out->used = n;
    if (n < EYECAL_MIN_DOTS) {
        return EYECAL_FAIL_FEW;
    }
    EyeCal cal = *out;
    if (!fitModel(points, use, -1, EYECAL_MODEL_AFFINE, &cal)) {
        return EYECAL_FAIL_FEW;
    }
    // the worst dots out, one at a time, while one stands clear of the rest
    for (int drop = 0; drop < EYECAL_DROP_MAX && n > EYECAL_MIN_DOTS; drop++) {
        float err[EYECAL_DOTS], sorted[EYECAL_DOTS];
        int worst = -1, m = 0;
        for (int i = 0; i < EYECAL_DOTS; i++) {
            if (!use[i]) {
                continue;
            }
            err[i] = pointError(&cal, &points[i]);
            sorted[m++] = err[i];
            if (worst < 0 || err[i] > err[worst]) {
                worst = i;
            }
        }
        float med = median(sorted, m);
        float bar = EYECAL_DROP_RATIO * med > EYECAL_DROP_FLOOR_DEG ? EYECAL_DROP_RATIO * med : EYECAL_DROP_FLOOR_DEG;
        if (worst < 0 || err[worst] <= bar) {
            break;
        }
        use[worst] = 0;
        n--;
        cal.dropped++;
        if (!fitModel(points, use, -1, EYECAL_MODEL_AFFINE, &cal)) {
            return EYECAL_FAIL_FEW;
        }
    }
    // the second-order term only where it clearly does better on the dots it did not see
    if (n >= EYECAL_QUAD_MIN_DOTS) {
        float looA = leaveOneOut(points, use, EYECAL_MODEL_AFFINE);
        float looQ = leaveOneOut(points, use, EYECAL_MODEL_QUAD);
        if (looA > 0.0f && looQ >= 0.0f && looQ < EYECAL_QUAD_RATIO * looA && looA - looQ >= EYECAL_QUAD_GAIN_DEG) {
            EyeCal quad = cal;
            if (fitModel(points, use, -1, EYECAL_MODEL_QUAD, &quad) && eyeCalSane(&quad)) {
                cal = quad;
            }
        }
    }
    cal.used = n;
    errors(points, use, NULL, &cal.meanBefore, &cal.maxBefore);
    errors(points, use, &cal, &cal.meanAfter, &cal.maxAfter);
    *out = cal;
    if (!eyeCalSane(&cal)) {
        return EYECAL_FAIL_EXTREME;
    }
    if (cal.meanAfter > EYECAL_GAIN_RATIO * cal.meanBefore || cal.meanBefore - cal.meanAfter < EYECAL_GAIN_MIN_DEG) {
        return EYECAL_FAIL_GAIN;
    }
    return EYECAL_OK;
}

// ---- config.ini --------------------------------------------------------------------------------

int eyeCalFormat(const EyeCal* cal, char* out, size_t cap) {
    if (cap == 0) {
        return 0;
    }
    out[0] = 0;
    if (cal == NULL || cal->model == EYECAL_MODEL_NONE) {
        return 1;
    }
    int k = modelTerms(cal->model);
    size_t len = 0;
    int w = snprintf(out, cap, "%d %c", EYECAL_VERSION, cal->model == EYECAL_MODEL_QUAD ? 'q' : 'a');
    if (w < 0 || (size_t)w >= cap) {
        out[0] = 0;
        return 0;
    }
    len = (size_t)w;
    for (int axis = 0; axis < 2; axis++) {
        for (int i = 0; i < k; i++) {
            float c = cal->c[axis][i];
            if (fabsf(c) < 0.0005f) {
                c = 0.0f;   // never "-0.000"
            }
            w = snprintf(out + len, cap - len, " %.3f", c);
            if (w < 0 || (size_t)w >= cap - len) {
                out[0] = 0;
                return 0;
            }
            len += (size_t)w;
        }
    }
    w = snprintf(out + len, cap - len, " %.2f %.2f %.2f %.2f", cal->meanAfter, cal->meanBefore, cal->maxAfter,
                 cal->maxBefore);
    if (w < 0 || (size_t)w >= cap - len) {
        out[0] = 0;
        return 0;
    }
    return 1;
}

int eyeCalParse(const char* text, EyeCal* out) {
    memset(out, 0, sizeof(*out));
    if (text == NULL) {
        return 0;
    }
    const char* s = text;
    char* end;
    long version = strtol(s, &end, 10);
    if (end == s || version != EYECAL_VERSION) {
        return 0;
    }
    s = end;
    while (*s == ' ' || *s == '\t') s++;
    int model = *s == 'a' ? EYECAL_MODEL_AFFINE : *s == 'q' ? EYECAL_MODEL_QUAD : EYECAL_MODEL_NONE;
    if (model == EYECAL_MODEL_NONE || (s[1] != ' ' && s[1] != '\t')) {
        return 0;
    }
    s++;
    int k = modelTerms(model);
    float v[2 * EYECAL_TERMS + 4];
    for (int i = 0; i < 2 * k + 4; i++) {
        v[i] = strtof(s, &end);
        if (end == s || !isfinite(v[i])) {
            return 0;
        }
        s = end;
    }
    while (*s == ' ' || *s == '\t') s++;
    if (*s != 0) {
        return 0;
    }
    EyeCal cal;
    memset(&cal, 0, sizeof(cal));
    cal.model = model;
    for (int axis = 0; axis < 2; axis++) {
        for (int i = 0; i < k; i++) {
            cal.c[axis][i] = v[axis * k + i];
        }
    }
    cal.meanAfter = v[2 * k];
    cal.meanBefore = v[2 * k + 1];
    cal.maxAfter = v[2 * k + 2];
    cal.maxBefore = v[2 * k + 3];
    if (!eyeCalSane(&cal) || cal.meanAfter < 0.0f || cal.meanBefore < 0.0f) {
        return 0;
    }
    *out = cal;
    return 1;
}
