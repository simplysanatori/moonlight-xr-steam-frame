// Where the screen goes on its own: Follow mode (the screen stays in the world
// until the head turns past the leash, then eases after the gaze), the
// angular-size lock (pushing or pulling the held screen keeps its size in the
// view), "bring here", and the layout kept per PC and app. The arithmetic is
// xr_layout.c's; this is the glue to the context, the head and the settings
// that the session saves. GPLv3.
#include <string.h>

#include "xr_native.h"

// A change to the layout has to sit this long before it is written, so a drag
// is one write and not a hundred
#define LAYOUT_DEBOUNCE_NS 750000000LL

_Static_assert(sizeof(AppLayout) == sizeof(((XrCtx*)0)->layoutSeen), "layoutSeen holds an AppLayout");

// ---- the PC and app now streaming ----------------------------------------------------------

static pthread_mutex_t g_identityLock = PTHREAD_MUTEX_INITIALIZER;
static char g_identityHost[UI_STR_MAX];
static char g_identityApp[UI_STR_MAX];
static unsigned g_identityGen;

void xrSetStreamIdentity(const char* host, const char* app) {
    pthread_mutex_lock(&g_identityLock);
    snprintf(g_identityHost, sizeof(g_identityHost), "%s", host ? host : "");
    snprintf(g_identityApp, sizeof(g_identityApp), "%s", app ? app : "");
    g_identityGen++;
    pthread_mutex_unlock(&g_identityLock);
}

void xrStreamIdentityHost(char* out, size_t cap) {
    pthread_mutex_lock(&g_identityLock);
    snprintf(out, cap, "%s", g_identityHost);
    pthread_mutex_unlock(&g_identityLock);
}

// ---- the head ------------------------------------------------------------------------------------

// Where the head is in the local space, which is what the screen's pose is in.
// A session that is not running, or a runtime that cannot say, leaves the last
// answer marked stale.
static void locateHead(XrCtx* ctx) {
    if (!ctx->sessionRunning || ctx->viewSpace == XR_NULL_HANDLE || ctx->localSpace == XR_NULL_HANDLE) {
        return;
    }
    XrSpaceLocation head = { XR_TYPE_SPACE_LOCATION };
    ctx->headPoseValid = 0;
    if (XR_SUCCEEDED(xrLocateSpace(ctx->viewSpace, ctx->localSpace, ctx->predictedDisplayTime, &head))
            && (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0
            && (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
        ctx->headPose = head.pose;
        ctx->headPoseValid = 1;
    }
}

// ---- bring here ------------------------------------------------------------------------------------

int xrBringScreenHere(XrCtx* ctx) {
    if (ctx == NULL || !ctx->placementValid || !ctx->headPoseValid) {
        return 0;
    }
    // A screen locked to the head is already where you look, and a room hangs
    // its picture on a wall
    if (ctx->headLockedPref || roomEffective(ctx) > 0) {
        return 0;
    }
    ctx->screenPose = layoutBringHere(ctx->screenPose, ctx->headPose);
    ctx->grabMode = GRAB_NONE;
    followReset(&ctx->follow);
    ctx->poseDirty = 1;
    Vec3 d = { ctx->screenPose.position.x - ctx->headPose.position.x,
               ctx->screenPose.position.y - ctx->headPose.position.y,
               ctx->screenPose.position.z - ctx->headPose.position.z };
    LOGEV("screen brought here: %.2f m away, %.2f m wide", sqrtf(vecDot(d, d)), ctx->screenWidth);
    return 1;
}

// ---- Follow -------------------------------------------------------------------------------------------

static void followFrame(XrCtx* ctx, int64_t now) {
    float dt = ctx->followNs > 0 ? (float)(now - ctx->followNs) * 1e-9f : 0.0f;
    ctx->followNs = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    // Only a screen in the world, with nothing in the way: not in a room, not
    // under a panel, not while the screen is held, and not before there is a
    // head to go by
    int ok = ctx->followOn && !ctx->headLockedPref && ctx->placementValid && ctx->headPoseValid
            && roomEffective(ctx) <= 0 && ctx->launcher == LAUNCHER_NONE && !panelUp(ctx)
            && ctx->grabMode == GRAB_NONE && ctx->sessionState == XR_SESSION_STATE_FOCUSED;
    if (!ok) {
        followReset(&ctx->follow);
        return;
    }
    Vec3 hp = { ctx->headPose.position.x, ctx->headPose.position.y, ctx->headPose.position.z };
    Vec3 d = { ctx->screenPose.position.x - hp.x, ctx->screenPose.position.y - hp.y,
               ctx->screenPose.position.z - hp.z };
    float dist = sqrtf(vecDot(d, d));
    if (dist < 0.3f) {
        followReset(&ctx->follow);
        return;
    }
    Vec3 screenDir = { d.x / dist, d.y / dist, d.z / dist };
    Vec3 gaze = layoutGazeDir(ctx->headPose.orientation);
    float leash = (float)(ctx->followLeashDeg > 0 ? ctx->followLeashDeg : FOLLOW_LEASH_DEFAULT_DEG)
            * (float)M_PI / 180.0f;
    int was = ctx->follow.easing;
    Vec3 dir = followStep(&ctx->follow, screenDir, gaze, dt, leash);
    if (was || ctx->follow.easing) {
        ctx->screenPose = layoutScreenAlong(ctx->screenPose, hp, dir);
    }
    if (!was && ctx->follow.easing) {
        LOGEV("follow: the head is past the leash, the screen comes after it");
    }
    if (was && !ctx->follow.easing) {
        // Landed: kept where it is, like a screen that was moved
        ctx->poseDirty = 1;
    }
}

// ---- the angular-size lock -------------------------------------------------------------------------

// While the held screen is pushed or pulled along the line from the head, its
// width follows its distance, so it keeps the size it has in the view. The
// input pass has moved it by grabPush this frame; the width it had before the
// push began is the one to scale.
static void angularLockFrame(XrCtx* ctx) {
    if (!ctx->angularLockOn || ctx->grabMode != GRAB_MOVE || ctx->grabPush == 0.0f
            || !ctx->placementValid) {
        ctx->lockActive = 0;
        return;
    }
    Vec3 d = { ctx->screenPose.position.x - ctx->headPos.x, ctx->screenPose.position.y - ctx->headPos.y,
               ctx->screenPose.position.z - ctx->headPos.z };
    float len = sqrtf(vecDot(d, d));
    float before = len - ctx->grabPush;
    if (before < 0.05f) {
        return;
    }
    if (!ctx->lockActive) {
        ctx->lockActive = 1;
        ctx->lockBaseWidth = ctx->screenWidth;
        ctx->lockBaseRadius = ctx->screenRadius;
    }
    float w = layoutAngularWidth(ctx->lockBaseWidth, before, len, SCREEN_MIN_WIDTH, SCREEN_MAX_WIDTH);
    if (ctx->lockBaseWidth > 0.01f) {
        // The arc keeps its shape as the width changes, the rule every resize follows
        ctx->screenRadius = ctx->lockBaseRadius * w / ctx->lockBaseWidth;
    }
    ctx->screenWidth = w;
}

// ---- the layout per PC and app -------------------------------------------------------------------

static void seenSet(XrCtx* ctx, const AppLayout* l) {
    memcpy(ctx->layoutSeen, l, sizeof(*l));
    ctx->layoutSeenValid = 1;
}

static void seenGet(const XrCtx* ctx, AppLayout* l) {
    memcpy(l, ctx->layoutSeen, sizeof(*l));
}

// A remembered layout put in force: the settings first, then the context from
// them the way a session start does
static void layoutIntoContext(XrCtx* ctx, Settings* s) {
    float pose[POSE_VALUES];
    xrPoseFromSettings(s, pose);
    if (ctx->roomHoldingScreen) {
        // A room has the screen; this is the placement waiting behind it
        if (pose[7] >= SCREEN_MIN_WIDTH && pose[7] <= SCREEN_MAX_WIDTH && pose[8] > 0.0f) {
            ctx->savedScreenPose.position.x = pose[0];
            ctx->savedScreenPose.position.y = pose[1];
            ctx->savedScreenPose.position.z = pose[2];
            ctx->savedScreenPose.orientation.x = pose[3];
            ctx->savedScreenPose.orientation.y = pose[4];
            ctx->savedScreenPose.orientation.z = pose[5];
            ctx->savedScreenPose.orientation.w = pose[6];
            ctx->savedScreenPose.orientation = quatNorm(ctx->savedScreenPose.orientation);
            ctx->savedScreenWidth = pose[7];
            ctx->savedScreenRadius = pose[8];
            ctx->panelCurve = (pose[9] >= 0.0f && pose[9] <= 1.0f) ? pose[9] : -1.0f;
        }
    }
    else {
        xrSetScreenPose(ctx, pose);
    }
    ctx->grabMode = GRAB_NONE;
    followReset(&ctx->follow);
    ctx->followOn = s->head_follow && !s->head_locked;
    // The part of the picture shown (PLAN 4b.6)
    xrZoomFromSettings(ctx, s);
    // The 3D state, where there is a 3D to set
    if (ctx->stereoMode != DEPTH_MODE_OFF) {
        setStereoLive(ctx, s->stereo, "the layout kept for this app");
        ctx->panelSeparation = separationOf(settings_separation_units(s));
        ctx->separationCurrent = ctx->panelSeparation;
        ctx->convergence = (float)settings_convergence_percent(s) / 100.0f;
    }
}

// A new stream's PC and app: what was kept for the last one is written first,
// then the layout kept for this one, if there is one, is put in force. An app
// with none starts from what is in force.
static int identityFrame(XrCtx* ctx, Settings* s) {
    pthread_mutex_lock(&g_identityLock);
    unsigned gen = g_identityGen;
    char host[UI_STR_MAX], app[UI_STR_MAX];
    snprintf(host, sizeof(host), "%s", g_identityHost);
    snprintf(app, sizeof(app), "%s", g_identityApp);
    pthread_mutex_unlock(&g_identityLock);
    if (gen == ctx->identityGen) {
        return 0;
    }
    ctx->identityGen = gen;
    int changed = 0;
    if (ctx->layoutHash && ctx->layoutPending && s->per_app) {
        AppLayout was;
        seenGet(ctx, &was);
        changed |= settings_layout_store(s, ctx->layoutHash, &was);
    }
    ctx->layoutPending = 0;
    ctx->layoutHash = settings_app_hash(host, app);
    const AppLayout* kept = s->per_app ? settings_layout_find(s, ctx->layoutHash) : NULL;
    if (kept != NULL) {
        AppLayout copy = *kept;
        settings_layout_apply(s, &copy);
        layoutIntoContext(ctx, s);
        changed = 1;
        LOGEV("layout kept for this app put back: %d cm away, %d cm wide, head lock %d, follow %d",
              s->screen_distance_cm, s->screen_width_cm, s->head_locked, s->head_follow);
    }
    AppLayout now;
    settings_layout_snapshot(s, &now);
    seenSet(ctx, &now);
    return changed;
}

// The layout in force against the one last seen: a change starts the debounce,
// and when it has sat still long enough it is kept under the app's hash
static int syncFrame(XrCtx* ctx, Settings* s, int64_t now) {
    if (!s->per_app || !ctx->layoutHash) {
        ctx->layoutPending = 0;
        ctx->layoutSeenValid = 0;
        return 0;
    }
    AppLayout cur;
    settings_layout_snapshot(s, &cur);
    if (!ctx->layoutSeenValid) {
        seenSet(ctx, &cur);
        return 0;
    }
    AppLayout seen;
    seenGet(ctx, &seen);
    if (!settings_layout_same(&cur, &seen)) {
        seenSet(ctx, &cur);
        ctx->layoutPending = 1;
        ctx->layoutDirtyNs = now;
    }
    if (ctx->layoutPending && now - ctx->layoutDirtyNs >= LAYOUT_DEBOUNCE_NS) {
        ctx->layoutPending = 0;
        seenGet(ctx, &seen);
        if (settings_layout_store(s, ctx->layoutHash, &seen)) {
            LOGEV("layout kept for this app: %d cm away, %d cm wide, zoom %d%%", seen.distance_cm, seen.width_cm,
                  seen.zoom);
            return 1;
        }
    }
    return 0;
}

int xrLayoutFrame(XrCtx* ctx, Settings* s, int64_t now) {
    ctx->followOn = s->head_follow && !s->head_locked;
    ctx->followLeashDeg = s->follow_leash_deg;
    ctx->angularLockOn = s->angular_lock;
    ctx->perAppOn = s->per_app;
    locateHead(ctx);
    int save = identityFrame(ctx, s);
    // Native (PLAN 4b.6): a zoom step the bindings took, into the settings before the layout is compared
    save |= xrZoomSync(ctx, s);
    angularLockFrame(ctx);
    followFrame(ctx, now);
    save |= syncFrame(ctx, s, now);
    return save;
}
