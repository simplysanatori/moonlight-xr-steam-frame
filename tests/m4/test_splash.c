// Moonlight XR Native, tests/m4: the splash logo (PLAN 6.3). It is on from the
// moment a stream is the picture's source until the first frame is shown, off
// once the 300 ms fade after it is over, on again (worded as a reconnect) when
// the picture goes away while the stream stays, and never on with a picture,
// the launcher's list or no stream. It adds exactly one layer while it shows,
// takes the place of the picture's own layer, and is the first thing to go
// when a frame is at the limit. GPLv3.
#include "check.h"
#include "scene.h"

#define MS 1000000LL

static int hasText(const XrCtx* c, const char* want) {
    return strcmp(c->splashUi.line, want) == 0;
}

// Pixels of the splash's slot that carry anything, and the strongest alpha
static int inkOf(XrCtx* c, int* maxAlpha) {
    UiRectPx r = xrAtlasRect(ATLAS_SPLASH);
    const uint8_t* px = xrAtlasPixels(c);
    int n = 0;
    *maxAlpha = 0;
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            int a = px[((size_t)(r.y + y) * UI_ATLAS_W + (size_t)(r.x + x)) * 4 + 3];
            if (a > 0) n++;
            if (a > *maxAlpha) *maxAlpha = a;
        }
    }
    return n;
}

static int splashQuads(const Scene* s) {
    int n = 0;
    UiRectPx r = xrAtlasRect(ATLAS_SPLASH);
    for (uint32_t i = 0; i < s->out.count; i++) {
        const XrCompositionLayerBaseHeader* b = s->out.order[i];
        if (b->type != XR_TYPE_COMPOSITION_LAYER_QUAD) continue;
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)b;
        if (q->subImage.imageRect.offset.x == r.x && q->subImage.imageRect.offset.y == r.y
            && q->subImage.imageRect.extent.width == r.w && q->space == s->ctx->viewSpace)
            n++;
    }
    return n;
}

// The layers of a frame with the layer pass run at time t
static uint32_t frameAt(Scene* s, int64_t t) {
    XrCtx* c = s->ctx;
    s->live = s->settings;
    xrSettingsFromCtx(c, &s->live);
    s->live.head_locked = s->settings.head_locked;
    xrPanelsUpdate(c, &s->live, &s->launcher, NULL, "10:42", nowNs());
    XrFrameIn in;
    memset(&in, 0, sizeof(in));
    xrDefaultPlacement(&in.distance, &in.quadWidth, &in.curvature);
    in.headLocked = s->settings.head_locked;
    memset(&s->out, 0, sizeof(s->out));
    xrBuildLayers(c, &in, &s->out);
    xrSplashLayers(c, &s->out, t);
    xrPanelsFlush(c);
    return s->out.count;
}

static void stateMachine(void) {
    XrSplashState st;
    memset(&st, 0, sizeof(st));
    int64_t t = 1000 * MS;
    // no stream: nothing, whatever else is true
    CHECK(xrSplashStep(&st, 0, 0, 0, t) == 0.0f);
    // a stream, no picture: up, worded as a first connect
    CHECK(xrSplashStep(&st, 1, 0, 0, t) == 1.0f);
    CHECK(st.up && !st.reconnect);
    // the first frame starts the fade: 1 at once, 0.5 at 150 ms, gone at 300
    CHECK_NEAR(xrSplashStep(&st, 1, 0, 1, t + 1 * MS), 1.0f, 0.01);
    CHECK_NEAR(xrSplashStep(&st, 1, 0, 1, t + 151 * MS), 0.5f, 0.01);
    CHECK(xrSplashStep(&st, 1, 0, 1, t + 301 * MS) == 0.0f);
    CHECK(!st.fading && !st.up);
    CHECK(xrSplashStep(&st, 1, 0, 1, t + 5000 * MS) == 0.0f);
    // the picture goes away with the stream still on: up again, as a reconnect
    CHECK(xrSplashStep(&st, 1, 0, 0, t + 6000 * MS) == 1.0f);
    CHECK(st.reconnect);
    // the stream ends, and the next one is a first connect again
    CHECK(xrSplashStep(&st, 0, 0, 0, t + 7000 * MS) == 0.0f);
    CHECK(xrSplashStep(&st, 1, 0, 0, t + 8000 * MS) == 1.0f);
    CHECK(!st.reconnect);
    // the launcher's list in the picture's place: no splash, and none appears when it goes
    CHECK(xrSplashStep(&st, 1, 1, 0, t + 9000 * MS) == 0.0f);
    CHECK(xrSplashStep(&st, 1, 0, 1, t + 9010 * MS) == 0.0f);
    // a fade cut short by a drop is full again
    memset(&st, 0, sizeof(st));
    xrSplashStep(&st, 1, 0, 0, t);
    CHECK(xrSplashStep(&st, 1, 0, 1, t + 10 * MS) > 0.9f);
    CHECK(xrSplashStep(&st, 1, 0, 1, t + 100 * MS) < 0.8f);
    CHECK(xrSplashStep(&st, 1, 0, 0, t + 120 * MS) == 1.0f);
    CHECK(st.reconnect);
}

static void words(void) {
    char b[160];
    xrSplashLine(0, "Desk PC", b, sizeof(b));
    CHECK(strstr(b, "Connecting to Desk PC") == b);
    xrSplashLine(0, "", b, sizeof(b));
    CHECK(strstr(b, "Connecting") == b && strstr(b, "to ") == NULL);
    xrSplashLine(1, "Desk PC", b, sizeof(b));
    CHECK(strstr(b, "Reconnecting") == b && strstr(b, "Desk") == NULL);
}

static void inTheFrame(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    xrSetStreamIdentity("Desk PC", "Desktop");
    int64_t t = 50000 * MS;

    // A picture, no splash: the frame is what it always was (and with no stream there is nothing to wait for)
    c->streaming = 0;
    uint32_t withPicture = frameAt(s, t);
    c->streaming = 1;
    CHECK(!xrSplashShowing(c) && splashQuads(s) == 0);

    // Before the first frame: the splash, one layer, in the picture's place
    c->videoShown = 0;
    uint32_t before = frameAt(s, t + 1 * MS);
    CHECK(xrSplashShowing(c) && splashQuads(s) == 1);
    CHECK(hasText(c, "Connecting to Desk PC\xE2\x80\xA6"));
    CHECK(before == withPicture);   // the splash replaces the picture's one layer
    int alpha;
    CHECK(inkOf(c, &alpha) > 20000 && alpha == 255);
    // head locked: its space is the eyes' and it is in front of them
    for (uint32_t i = 0; i < s->out.count; i++) {
        const XrCompositionLayerBaseHeader* b = s->out.order[i];
        if (b->type == XR_TYPE_COMPOSITION_LAYER_QUAD && b->space == c->viewSpace) {
            const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)b;
            if (q->subImage.imageRect.offset.x == xrAtlasRect(ATLAS_SPLASH).x) {
                CHECK(q->pose.position.z < -1.0f && q->pose.position.z > -3.0f);
                CHECK(q->size.width > 0.5f && q->size.width < 1.5f);
            }
        }
    }

    // The first frame lands: the splash is still there, fading, for 300 ms
    c->videoShown = 1;
    uint32_t fading = frameAt(s, t + 100 * MS);
    CHECK(xrSplashShowing(c) && splashQuads(s) == 1);
    CHECK(fading == withPicture + 1);
    frameAt(s, t + 200 * MS);
    CHECK(splashQuads(s) == 1);
    int dim;
    inkOf(c, &dim);
    CHECK(dim < 255 && dim > 0);   // drawn paler as it fades
    // ... and gone after it
    uint32_t after = frameAt(s, t + 400 * MS);
    CHECK(!xrSplashShowing(c) && splashQuads(s) == 0);
    CHECK(after == withPicture);
    CHECK(frameAt(s, t + 9000 * MS) == withPicture);

    // The link drops, the picture is let go, the stream stays: back, as a reconnect
    c->videoShown = 0;
    uint32_t again = frameAt(s, t + 10000 * MS);
    CHECK(xrSplashShowing(c) && splashQuads(s) == 1 && again == withPicture);
    CHECK(hasText(c, "Reconnecting\xE2\x80\xA6"));
    CHECK(inkOf(c, &alpha) > 5000 && alpha == 255);   // full again after the fade left it pale

    // At the layer limit it is the one that goes, not another layer
    int saved = c->maxLayerCount;
    c->maxLayerCount = (int)withPicture - 1;
    frameAt(s, t + 10010 * MS);
    CHECK(splashQuads(s) == 0);
    c->maxLayerCount = saved;

    // The list in the picture's place, or no stream: not at all
    s->launcher.mode = LAUNCHER_PCS;
    frameAt(s, t + 10020 * MS);
    CHECK(c->launcher == LAUNCHER_PCS && !xrSplashShowing(c) && splashQuads(s) == 0);
    s->launcher.mode = LAUNCHER_NONE;
    c->streaming = 0;
    frameAt(s, t + 10030 * MS);
    CHECK(!xrSplashShowing(c) && splashQuads(s) == 0);

    // A new stream is a first connect again
    c->streaming = 1;
    frameAt(s, t + 10040 * MS);
    CHECK(xrSplashShowing(c) && hasText(c, "Connecting to Desk PC\xE2\x80\xA6"));

    // A session that is not rendering gets no layer
    c->shouldRender = 0;
    frameAt(s, t + 10050 * MS);
    CHECK(splashQuads(s) == 0);
    c->shouldRender = 1;

    // The slot overlaps no other (tests/m4 test_budget checks them all; this one is the new one)
    UiRectPx r = xrAtlasRect(ATLAS_SPLASH);
    CHECK(r.w == ATLAS_SPLASH_W && r.h == ATLAS_SPLASH_H);
    scene_free(s);
}

int main(void) {
    stateMachine();
    words();
    inTheFrame();
    return checksDone("m4_splash");
}
