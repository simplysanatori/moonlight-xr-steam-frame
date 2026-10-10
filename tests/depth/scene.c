// See scene.h. GPLv3.
#include "scene.h"

#include <math.h>
#include <stddef.h>

typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 r = { x, y, z }; return r; }
static V3 add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 norm(V3 a) { return mul(a, 1.0f / sqrtf(dot(a, a))); }

// Camera at height 1.6 m looking down +z, slightly down
#define EYE_Y 1.6f
#define HFOV_TAN 0.70f  // ~70 degrees across
#define WALL_Z 12.0f

typedef struct {
    float t;
    int part;
    V3 n;
    V3 p;
} Hit;

static void sphereHit(V3 o, V3 d, V3 c, float r, Hit* h, int part) {
    V3 oc = sub(o, c);
    float b = dot(oc, d), cc = dot(oc, oc) - r * r, disc = b * b - cc;
    if (disc < 0) return;
    float t = -b - sqrtf(disc);
    if (t > 1e-3f && t < h->t) {
        h->t = t;
        h->part = part;
        h->p = add(o, mul(d, t));
        h->n = norm(sub(h->p, c));
    }
}

static void boxHit(V3 o, V3 d, V3 lo, V3 hi, Hit* h, int part) {
    float t0 = -1e30f, t1 = 1e30f;
    int axis = 0;
    float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    float l[3] = { lo.x, lo.y, lo.z }, u[3] = { hi.x, hi.y, hi.z };
    for (int i = 0; i < 3; i++) {
        if (fabsf(dd[i]) < 1e-9f) {
            if (oo[i] < l[i] || oo[i] > u[i]) return;
            continue;
        }
        float a = (l[i] - oo[i]) / dd[i], b = (u[i] - oo[i]) / dd[i];
        if (a > b) { float s = a; a = b; b = s; }
        if (a > t0) { t0 = a; axis = i; }
        if (b < t1) t1 = b;
    }
    if (t0 > t1 || t0 < 1e-3f || t0 >= h->t) return;
    h->t = t0;
    h->part = part;
    h->p = add(o, mul(d, t0));
    float n[3] = { 0, 0, 0 };
    n[axis] = dd[axis] > 0 ? -1.0f : 1.0f;
    h->n = v3(n[0], n[1], n[2]);
}

static Hit trace(SceneId id, V3 o, V3 d) {
    Hit h = { 1e30f, 0, { 0, 0, 0 }, { 0, 0, 0 } };
    if (id == SCENE_ROOM) {
        if (d.y < 0) {
            float t = -o.y / d.y;
            if (t < h.t) { h.t = t; h.part = PART_FLOOR; h.p = add(o, mul(d, t)); h.n = v3(0, 1, 0); }
        }
        sphereHit(o, d, v3(-0.35f, 0.75f, 2.6f), 0.75f, &h, PART_NEAR);
        boxHit(o, d, v3(1.2f, 0.0f, 6.5f), v3(2.4f, 1.3f, 7.7f), &h, PART_BOX);
    }
    if (d.z > 0) {
        float t = (WALL_Z - o.z) / d.z;
        if (t < h.t) { h.t = t; h.part = PART_WALL; h.p = add(o, mul(d, t)); h.n = v3(0, 0, -1); }
    }
    if (id == SCENE_DISC) {
        // A disc facing the camera 2 m out, radius 0.9 m
        float t = (2.0f - o.z) / d.z;
        V3 p = add(o, mul(d, t));
        float dx = p.x - 0.0f, dy = p.y - EYE_Y;
        if (t > 0 && t < h.t && dx * dx + dy * dy < 0.81f) {
            h.t = t; h.part = PART_NEAR; h.p = p; h.n = v3(0, 0, -1);
        }
    }
    return h;
}

static float hash(int x, int y) {
    unsigned n = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (float)((n ^ (n >> 16)) & 0xffff) / 65535.0f;
}

static V3 albedo(SceneId id, const Hit* h) {
    switch (h->part) {
    case PART_FLOOR: {
        int cx = (int)floorf(h->p.x * 1.5f), cz = (int)floorf(h->p.z * 1.5f);
        return ((cx + cz) & 1) ? v3(0.75f, 0.72f, 0.66f) : v3(0.30f, 0.26f, 0.22f);
    }
    case PART_WALL: {
        // Bricks 0.5 x 0.25 m, mortar lines, a little noise
        float y = h->p.y, x = h->p.x + (((int)floorf(y / 0.25f)) & 1) * 0.25f;
        float fx = x / 0.5f - floorf(x / 0.5f), fy = y / 0.25f - floorf(y / 0.25f);
        if (fx < 0.06f || fy < 0.12f) return v3(0.78f, 0.76f, 0.70f);
        float n = hash((int)floorf(x / 0.5f), (int)floorf(y / 0.25f)) * 0.15f;
        if (id == SCENE_DISC) return v3(0.35f + n, 0.45f + n, 0.55f + n);
        return v3(0.55f + n, 0.25f + n * 0.5f, 0.18f);
    }
    case PART_NEAR: {
        if (id == SCENE_DISC) {
            float a = atan2f(h->p.y - EYE_Y, h->p.x);
            return (((int)floorf(a * 4.0f / 3.14159f)) & 1) ? v3(0.9f, 0.8f, 0.1f) : v3(0.2f, 0.5f, 0.9f);
        }
        return v3(0.15f, 0.35f, 0.85f);
    }
    case PART_BOX:
        return v3(0.2f, 0.65f, 0.25f);
    default:
        return v3(0.55f, 0.70f, 0.95f);
    }
}

static void cameraRay(int w, int h, int x, int y, float shift, V3* o, V3* d) {
    float u = ((x + 0.5f) / w * 2.0f - 1.0f) * HFOV_TAN;
    float v = (1.0f - (y + 0.5f) / h * 2.0f) * HFOV_TAN * 9.0f / 16.0f;
    *o = v3(shift, EYE_Y, 0.0f);
    // Pitched down ~8 degrees
    float c = 0.990f, s = -0.139f;
    V3 dir = v3(u, v, 1.0f);
    *d = norm(v3(dir.x, dir.y * c + dir.z * s, -dir.y * s + dir.z * c));
}

void sceneRender(SceneId id, int w, int h, float shift, uint8_t* rgba, float* zOut) {
    const V3 light = norm(v3(-0.5f, 1.0f, -0.6f));
    // Forward axis of the camera, for z along it
    V3 fwd = norm(v3(0.0f, -0.139f, 0.990f));
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            V3 o, d;
            cameraRay(w, h, x, y, shift, &o, &d);
            Hit hit = trace(id, o, d);
            V3 col;
            float z = 1e4f;
            if (hit.part == 0) {
                col = v3(0.55f, 0.70f, 0.95f);
            } else {
                V3 a = albedo(id, &hit);
                float lam = dot(hit.n, light);
                if (lam < 0) lam = 0;
                // Hard shadow toward the light
                Hit sh = trace(id, add(hit.p, mul(hit.n, 1e-3f)), light);
                float vis = (sh.part != 0 && sh.part != PART_WALL) ? 0.25f : 1.0f;
                float shade = 0.30f + 0.70f * lam * vis;
                // Distance haze, the cue a monocular model leans on as well
                float fog = 1.0f - expf(-hit.t * 0.03f);
                col = add(mul(a, shade * (1.0f - fog)), mul(v3(0.7f, 0.75f, 0.8f), fog));
                z = dot(sub(hit.p, o), fwd);
            }
            uint8_t* p = rgba + ((size_t)y * w + x) * 4;
            float c3[3] = { col.x, col.y, col.z };
            for (int i = 0; i < 3; i++) {
                float c = powf(c3[i] < 0 ? 0 : (c3[i] > 1 ? 1 : c3[i]), 1.0f / 2.2f);
                p[i] = (uint8_t)(c * 255.0f + 0.5f);
            }
            p[3] = 255;
            if (zOut) zOut[(size_t)y * w + x] = z;
        }
    }
}

void scenePart(SceneId id, int w, int h, float shift, uint8_t* partOut) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            V3 o, d;
            cameraRay(w, h, x, y, shift, &o, &d);
            partOut[(size_t)y * w + x] = (uint8_t)trace(id, o, d).part;
        }
    }
}
