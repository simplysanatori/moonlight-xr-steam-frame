// Moonlight XR Native: the context and config.ini. Upstream's renderer got its
// starting state from nativeInit's arguments and a row of JNI setters, and
// handed every change back to Java (IN_SETTING, IN_POSE) to store in the
// Android preferences (XrRenderer.applySetting, saveScreenPose). Here both
// directions go through ui/settings.c's Settings, which is config.ini. No
// OpenXR calls, so the tests reach all of it. GPLv3.
#include "xr_native.h"
#include "xr_pad_bridge.h"
#include "../input/gamepads.h"
#include "xr_keyboard.h"
#include "xr_depth.h"

// ---- what nativeInit set ---------------------------------------------------

void xrCtxDefaults(XrCtx* ctx, int videoWidth, int videoHeight, int fps) {
    ctx->videoWidth = videoWidth > 0 ? videoWidth : 1920;
    ctx->videoHeight = videoHeight > 0 ? videoHeight : 1080;
    ctx->streamFps = fps;
    ctx->perfLevel = PERF_LEVEL_SUSTAINED_HIGH;
    ctx->depthRateSetting = DEPTH_RATE_DEFAULT;
    depthGovernorStart(&ctx->depthGov, DEPTH_RATE_DEFAULT);
    // Flat until config.ini's depth_source says otherwise (xrDepthConfigure,
    // from xrApplySettings)
    ctx->stereoMode = DEPTH_MODE_OFF;
    ctx->stereoLive = 0;
    ctx->drawnEyes = 1;
    ctx->depthModel = -1;
    ctx->depthTexW = 512;
    ctx->depthTexH = 288;
    atomic_init(&ctx->depthStagedIndex, 0);
    atomic_init(&ctx->depthGaveUp, 0);
    // 0.25 measured best on a captured frame: same 5 px edge as tighter
    // values with a tenth of the speckle
    ctx->upsampleSigmaR = 0.25f;
    ctx->upsampleEnabled = 1;
    ctx->occlusionEnabled = 1;
    // The warp's edges: sample held inside the frame, shift faded out over
    // the last few pixels each side, and each eye's rectangle a texel clear
    // of the seam between them. The cubic depth read is off for its cost.
    ctx->srcInsetOn = 1;
    ctx->edgeFadePx = EDGE_FADE_PX;
    ctx->depthCubic = 0;
    // Off until it earns its place in a blind comparison on device
    ctx->depthSharp = 0.0f;
    ctx->separationOverride = -1.0f;
    ctx->separationPref = 0.006f;
    // Native: upstream's debug properties as environment variables
    // (MLXR_SEPARATION in tenths of a percent, MLXR_DEPTH_CUBIC,
    // MLXR_OCCLUSION, MLXR_UPSAMPLE, MLXR_EDGE_FADE in pixels)
    const char* knob = getenv("MLXR_SEPARATION");
    if (knob != NULL && *knob) ctx->separationOverride = (float)atof(knob) / 1000.0f;
    knob = getenv("MLXR_DEPTH_CUBIC");
    if (knob != NULL && *knob) ctx->depthCubic = atoi(knob) != 0;
    knob = getenv("MLXR_OCCLUSION");
    if (knob != NULL && *knob) ctx->occlusionEnabled = atoi(knob) != 0;
    knob = getenv("MLXR_UPSAMPLE");
    if (knob != NULL && *knob) ctx->upsampleEnabled = atoi(knob) != 0;
    knob = getenv("MLXR_EDGE_FADE");
    if (knob != NULL && *knob) {
        int px = atoi(knob);
        ctx->edgeFadePx = px < 0 ? 0 : (px > EDGE_FADE_MAX_PX ? EDGE_FADE_MAX_PX : px);
    }
    ctx->sessionState = XR_SESSION_STATE_UNKNOWN;
    ctx->seamInset = 1;
    ctx->panelCurve = -1.0f;
    ctx->panelSeparation = -1.0f;
    ctx->separationCurrent = 0.005f;
    ctx->defaultSeparation = 0.005f;
    ctx->defaultConvergence = 0.5f;
    ctx->convergence = 0.5f;
    ctx->presetUnits[COG_PRESET_COMFORT] = 2;
    ctx->presetUnits[COG_PRESET_BALANCED] = 5;
    ctx->presetUnits[COG_PRESET_STRONG] = 8;
    ctx->cogDragSlider = -1;
    ctx->cogDragHand = -1;
    ctx->cogDragFace = -1;
    ctx->cogHoverSlider = -1;
    ctx->cogHoverCell = -1;
    ctx->fadeNs = FADE_NS;
    // Native: no splash is drawn (nothing to wait for that takes long), so it
    // starts gone, which is also what lets input and notices through
    ctx->splash.phase = SPLASH_GONE;
    noticeInit(&ctx->notices);
    ctx->toastDrawnKind = -1;
    ctx->kbHoverKey = -1;
    kbfInit(&ctx->kbFull);
    ctx->kbLayoutShown = -1;
    ctx->pickerHover = -1;
    ctx->pickerPick = -1;
    // Native: the pointer's smoothing as M2 shipped it, see xr_renderer.h
    ctx->pointerMinCutoff = nativePointerTunable("MLXR_POINTER_MIN_CUTOFF", NATIVE_POINTER_MIN_CUTOFF);
    ctx->pointerBeta = nativePointerTunable("MLXR_POINTER_BETA", NATIVE_POINTER_BETA_PER_PX) * (float)ctx->videoWidth;
    ctx->clickHoldPx = clickHoldPixelsFromEnv();
    clickHoldReset(&ctx->clickHold);
    // Native (lane pointer-clicks)
    ctx->pressCfg.stabilise = pressStabiliseFromEnv();
    ctx->pressCfg.doublePx = doubleClickPxFromEnv();
    // Native (lane double-click): the double-click time, press to press
    ctx->pressCfg.doubleNs = (int64_t)(doubleClickMsFromEnv() * 1000000.0f);
    ctx->stickRightClick = stickRightClickFromEnv();
    // Native (lane wave2-bind-wire): the variables above are the Normal level
    // with any that is set taken in, until config.ini says otherwise
    ctx->clickSteady = CLICK_STEADY_NORMAL;
    ctx->scrollSpeed = SCROLL_SPEED_DEFAULT;
    pressFixReset(&ctx->pressFix);
    ctx->aimMinCutoff = NATIVE_AIM_MIN_CUTOFF;
    ctx->aimBeta = AIM_BETA;
    ctx->pointerWake = POINTER_WAKE_SEC;
    ctx->pointerSleep = POINTER_SLEEP_SEC;
    ctx->beamWidth = 0.010f;
    ctx->raySetting = 1;
    ctx->rayDrawnSaid = -1;
    ctx->dotOn = 1;
    ctx->headAimSensitivity = HEAD_AIM_SENSITIVITY_DEFAULT;
    ctx->headAimDeadZone = HEAD_AIM_DEADZONE_DEFAULT;
    headAimReset(&ctx->headAim);
    pointerNudgeReset(&ctx->headAimNudge);
    ctx->headAimSaid = -1;
    ctx->padDeadzone = PAD_STICK_DEADZONE_DEFAULT;
    ctx->padShortcut = PAD_SHORTCUT_MENU_GRIP;
    padToggleReset(&ctx->padToggle);
    padChordReset(&ctx->padChord, 0);
    padRest(&ctx->pad);
    ctx->padResting = 1;
    ctx->ambiOverride = -1;
    for (int row = 0; row < PICTURE_VALUES; row++) {
        ctx->pictureUnits[row] = pictureDefault(row);
    }
    ctx->grade = pictureGradeFor(ctx->pictureUnits);
    ctx->gradeOn = 0;
    // The room's own light off the picture, and each room's own rows as its
    // table row starts them, until the preferences are handed down
    roomLevelsFromTable(ctx);
    // Same for the room, which the picker sets and a property can force, and
    // its scale and brightness (native: environment variables stand in for
    // upstream's debug properties)
    const char* room = getenv("MLXR_ROOM");
    const char* roomScale = getenv("MLXR_ROOM_SCALE");
    const char* roomDim = getenv("MLXR_ROOM_DIM");
    ctx->roomOverride = room != NULL && *room ? atoi(room) : -1;
    ctx->roomScaleOverride = roomScale != NULL && *roomScale ? (float)atof(roomScale) : -1.0f;
    ctx->roomDimOverride = roomDim != NULL && *roomDim ? (float)atof(roomDim) : -1.0f;
    // Roughly ten frames to cross a scene cut, which reads as the glow
    // following the picture rather than flickering with it
    ctx->ambiSmooth = 0.08f;
    // The glow's colours lifted to a steady luma, with a dark edge rolled off
    ctx->glowNorm = 1;
    // Letterbox detection on, starting from the whole frame
    ctx->ambiBarDetect = 1;
    ctx->ambiCrop[0] = 0.0f;
    ctx->ambiCrop[1] = 0.0f;
    ctx->ambiCrop[2] = 1.0f;
    ctx->ambiCrop[3] = 1.0f;
    ctx->envResTier = ENV_RES_STANDARD;
    ctx->loggedChoice = -1;
    // Every cell of the picker: passthrough, the void and the three rooms
    ctx->pickerCells = ENV_CELL_COUNT;
    ctx->maxLayerCount = 16;
    ctx->launcherHoverHit = -1;
    ctx->launcherPressHit = -1;
    ctx->lastHand = -1;
}

// ---- rows that come later ---------------------------------------------------

int xrRowLater(XrCtx* ctx, int face, int row) {
    (void)ctx;
    if (face != COG_TAB_DISPLAY) {
        return 0;
    }
    // Every Display row is live since M5 (supersampling is the app's own
    // pass, SteamVR having no layer settings); the 3D tab is live with a
    // stereo mode (cogRowLive), since M6
    (void)row;
    return 0;
}

unsigned xrDisplayLaterMask(XrCtx* ctx) {
    unsigned mask = 0;
    for (int row = 0; row <= COG_DISPLAY_SLIDER_ROW; row++) {
        if (xrRowLater(ctx, COG_TAB_DISPLAY, row) != 0) {
            mask |= 1u << row;
        }
    }
    return mask;
}

// ---- the placement ----------------------------------------------------------

static int iround(float v) {
    return (int)lroundf(v);
}

void xrDefaultPlacement(float* distance, float* width, float* curvature) {
    Settings d;
    settings_defaults(&d);
    *distance = (float)d.screen_distance_cm / 100.0f;
    *width = (float)d.screen_width_cm / 100.0f;
    *curvature = (float)d.screen_curve / 100.0f;
}

void xrPoseToSettings(const float* p, float curve, Settings* s) {
    // screenPitch and screenRoll read the pose off a context; one kept for it
    // (the frame loop's thread only)
    static XrCtx* tmp;
    if (tmp == NULL && (tmp = calloc(1, sizeof(XrCtx))) == NULL) {
        return;
    }
    tmp->screenPose.position.x = p[0];
    tmp->screenPose.position.y = p[1];
    tmp->screenPose.position.z = p[2];
    tmp->screenPose.orientation.x = p[3];
    tmp->screenPose.orientation.y = p[4];
    tmp->screenPose.orientation.z = p[5];
    tmp->screenPose.orientation.w = p[6];
    tmp->screenPose.orientation = quatNorm(tmp->screenPose.orientation);
    float d = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
    const float deg = 180.0f / (float)M_PI;
    s->screen_distance_cm = iround(d * 100.0f);
    s->screen_height_cm = iround(p[1] * 100.0f);
    // The direction it sits in, left of straight ahead; straight overhead has none
    s->screen_azimuth_deg = p[0] * p[0] + p[2] * p[2] > 1e-6f ? iround(atan2f(-p[0], -p[2]) * deg) : 0;
    s->screen_tilt_deg = iround(screenPitch(tmp) * deg);
    // Clockwise as the viewer sees it, which lowers the right edge screenRoll
    // counts as positive
    s->screen_rotate_deg = iround(-screenRoll(tmp) * deg);
    s->screen_curve = iround((curve >= 0.0f ? curve : 0.0f) * 100.0f);
    s->screen_width_cm = iround(p[7] * 100.0f);
    settings_clamp(s);
}

void xrPoseFromSettings(const Settings* s, float* p) {
    const float rad = (float)M_PI / 180.0f;
    float d = (float)s->screen_distance_cm / 100.0f;
    float y = (float)s->screen_height_cm / 100.0f;
    if (y > d) y = d;
    if (y < -d) y = -d;
    float h = sqrtf(d * d - y * y);
    float az = (float)s->screen_azimuth_deg * rad;
    XrQuaternionf q = screenOrient(az, (float)s->screen_tilt_deg * rad, -(float)s->screen_rotate_deg * rad);
    float curve = (float)s->screen_curve / 100.0f;
    p[0] = -h * sinf(az);
    p[1] = y;
    p[2] = -h * cosf(az);
    p[3] = q.x;
    p[4] = q.y;
    p[5] = q.z;
    p[6] = q.w;
    p[7] = (float)s->screen_width_cm / 100.0f;
    // updatePlacement's radius for this curve: 4x the distance gently curved,
    // down to the distance itself wrapped round the viewer
    p[8] = (d > 0.01f ? d : 0.01f) * (1.0f + 3.0f * (1.0f - curve));
    p[9] = curve;
}

// ---- config.ini -> context ------------------------------------------------------

// The picker's cell for a stored environment id and back (settings.h ENV_ID_*,
// the environment key's 1 passthrough, 2 void, 3..5 the rooms)
int xrCellForEnvironment(int env) {
    switch (env) {
    case ENV_ID_PASSTHROUGH: return ENV_CELL_PASSTHROUGH;
    case ENV_ID_HOME_THEATER: return ENV_CELL_HOME_THEATER;
    case ENV_ID_GRAND_CINEMA: return ENV_CELL_GRAND_CINEMA;
    case ENV_ID_SYNTHWAVE: return ENV_CELL_SYNTHWAVE;
    default: return ENV_CELL_VOID;
    }
}

int xrEnvironmentSetting(int cell) {
    switch (cell) {
    case ENV_CELL_PASSTHROUGH: return 1;
    case ENV_CELL_HOME_THEATER: return 3;
    case ENV_CELL_GRAND_CINEMA: return 4;
    case ENV_CELL_SYNTHWAVE: return 5;
    default: return 2;
    }
}

void xrApplySettings(XrCtx* ctx, const Settings* s) {
    ctx->overlayVisible = s->stats;
    ctx->statsStyle = s->stats_style >= STATS_STYLE_FULL && s->stats_style <= STATS_STYLE_LITE ? s->stats_style : STATS_STYLE_FULL;
    ctx->sharpenMode = s->sharpening;
    ctx->supersampleMode = s->supersampling;
    ctx->ambilightOn = s->glow;
    ctx->ambiIntensity = (float)s->glow_level / 100.0f;
    ctx->roomLightOn = s->screen_light;
    for (int style = ROOM_STYLE_FIRST; style <= ROOM_STYLE_LAST; style++) {
        const RoomSettings* r = &s->room[style - ROOM_STYLE_FIRST];
        ctx->roomBrightness[style] = r->brightness;
        ctx->roomGlow[style] = r->glow;
        ctx->roomLightLevel[style] = r->light;
        ctx->roomScreen[style] = r->screen;
    }
    ctx->headLockedPref = s->head_locked;
    // Native (lane layout): Follow, its leash, the angular-size lock and the per app switch
    ctx->followOn = s->head_follow && !s->head_locked;
    ctx->followLeashDeg = s->follow_leash_deg;
    ctx->angularLockOn = s->angular_lock;
    ctx->perAppOn = s->per_app;
    ctx->pointerSleepOn = s->pointer_sleep;
    ctx->modelOn = s->controller_model;
    xrSetClickSound(ctx, s->click_sound);
    noticeSetLevel(&ctx->notices, s->notices);
    ctx->cogTab = s->cog_tab >= 0 && s->cog_tab < COG_TAB_COUNT ? s->cog_tab : COG_TAB_SCREEN;
    xrSetShowRay(ctx, s->show_ray);
    ctx->dotOn = s->pointer_dot;
    ctx->kbLayout = s->keyboard_layout == KB_LAYOUT_FULL ? KB_LAYOUT_FULL : KB_LAYOUT_COMPACT;
    ctx->kbMap = s->keyboard_keymap;
    kbfSetHoldMs(&ctx->kbFull, s->keyboard_hold_ms);
    xrSetHeadAim(ctx, s->head_aim, s->head_aim_sensitivity, s->head_aim_deadzone);
    // The stick dead zone is the launcher slider's when it was moved, else Moonlight's own
    // default of 7 percent, the preference upstream hands down
    xrSetGamepad(ctx, s->gamepad_toggle, padDeadzonePercentFromLauncher(gamepadsGetDeadzone()));
    xrSetPadGrip(ctx, s->pad_grip);
    xrSetClickSteadiness(ctx, s->click_steadiness);
    xrSetScrollSpeed(ctx, s->scroll_speed);
    xrPadBridgeSetFlip(s->flip_face_buttons);
    // Native (lane wave2b-padmouse): the same switch for the physical pads, live (the stream start
    // sets it too), and the pad-as-mouse and pad arbitration settings
    gamepadsSetFlipFaceButtons(s->flip_face_buttons);
    gamepadsSetPadMouse(s->pad_mouse_chord, s->pad_mouse_scroll);
    gamepadsSetArbitration(s->pad_arbitration);
    xrPadBridgeSetRumble(s->rumble_mix, s->rumble_intensity);
    int picture[PICTURE_VALUES];
    for (int row = 0; row < PICTURE_VALUES; row++) {
        picture[row] = settings_picture_value(s, row);
    }
    xrSetPicture(ctx, picture);
    int presets[COG_PRESET_CELLS];
    ui_presets_for(s, presets);
    xrSetDepthDefaults(ctx, settings_model_default_separation(s), 50, presets);
    ctx->separationPref = separationOf(settings_separation_units(s));
    ctx->separationCurrent = ctx->separationPref;
    ctx->convergence = (float)settings_convergence_percent(s) / 100.0f;
    xrSetDepthRate(ctx, s->depth_rate);
    // M6: the depth model or test pattern, and the 3D switch as last left
    xrDepthConfigure(ctx, s);
    // The placement saved last time, which the first frame keeps
    float pose[POSE_VALUES];
    xrPoseFromSettings(s, pose);
    xrSetScreenPose(ctx, pose);
    float distance, width, curvature;
    xrDefaultPlacement(&distance, &width, &curvature);
    ctx->prefCurvature = curvature;
    // Where the picture starts: passthrough, the void or a room
    int cell = xrCellForEnvironment(settings_environment_id(s));
    ctx->envResTier = s->env_res;
    xrSetEnvironment(ctx, cell);
    ctx->passthrough = cell == ENV_CELL_PASSTHROUGH;
    ctx->handsEnabled = s->hand_tracking;
    xrSetHandHint(ctx, s->hand_tracking && !s->hand_hint_seen);
}

// ---- context -> config.ini ---------------------------------------------------------

void xrSettingsFromCtx(XrCtx* ctx, Settings* s) {
    s->stats = ctx->overlayVisible;
    s->stats_style = ctx->statsStyle;
    s->sharpening = ctx->sharpenMode;
    s->supersampling = ctx->supersampleMode;
    s->glow = ctx->ambilightOn;
    s->glow_level = iround(ctx->ambiIntensity * 100.0f);
    s->screen_light = ctx->roomLightOn;
    for (int style = ROOM_STYLE_FIRST; style <= ROOM_STYLE_LAST; style++) {
        RoomSettings* r = &s->room[style - ROOM_STYLE_FIRST];
        r->brightness = ctx->roomBrightness[style];
        r->glow = ctx->roomGlow[style];
        r->light = ctx->roomLightLevel[style];
        r->screen = ctx->roomScreen[style];
    }
    s->pointer_sleep = ctx->pointerSleepOn;
    s->controller_model = ctx->modelOn;
    s->click_sound = ctx->clickSoundOn;
    s->notices = ctx->notices.level;
    s->click_steadiness = ctx->clickSteady;
    s->scroll_speed = ctx->scrollSpeed;
    s->cog_tab = ctx->cogTab;
    s->pointer_dot = ctx->dotOn;
    s->keyboard_layout = ctx->kbLayout;
    s->keyboard_keymap = ctx->kbMap;
    s->head_aim_sensitivity = ctx->headAimSensitivity;
    s->head_aim_deadzone = ctx->headAimDeadZone;
    for (int row = 0; row < PICTURE_VALUES; row++) {
        int* v = settings_picture_ptr(s, row);
        if (v != NULL) {
            *v = ctx->pictureUnits[row];
        }
    }
    // Native (owner's request: the 3D sliders did not move until let go): the
    // 3D tab's two tracks as they stand while being dragged. The panel is
    // drawn from this, and config.ini only has the value once the drag ends,
    // so without it the thumb, readout and preset mark sat on the old value
    // for the whole drag. Left alone while they agree, so a value the file
    // leaves to the depth model's own (-1) stays that.
    if (ctx->panelSeparation >= 0.0f
            && separationUnits(ctx->panelSeparation) != settings_separation_units(s)) {
        s->separation = separationUnits(ctx->panelSeparation);
    }
    if (iround(ctx->convergence * 100.0f) != settings_convergence_percent(s)) {
        s->convergence = iround(ctx->convergence * 100.0f);
    }
    if (ctx->placementValid) {
        float pose[POSE_VALUES];
        pose[0] = ctx->screenPose.position.x;
        pose[1] = ctx->screenPose.position.y;
        pose[2] = ctx->screenPose.position.z;
        pose[3] = ctx->screenPose.orientation.x;
        pose[4] = ctx->screenPose.orientation.y;
        pose[5] = ctx->screenPose.orientation.z;
        pose[6] = ctx->screenPose.orientation.w;
        pose[7] = ctx->screenWidth;
        pose[8] = ctx->screenRadius;
        pose[9] = ctx->panelCurve;
        xrPoseToSettings(pose, xrCurveSetting(ctx), s);
    }
    settings_clamp(s);
}

// XrRenderer.applySetting: one value the panel changed, into the file
int xrStoreSetting(XrCtx* ctx, Settings* s, int setting, int value) {
    Settings before = *s;
    switch (setting) {
    case SETTING_KEYBOARD_LAYOUT: s->keyboard_layout = value == KB_LAYOUT_FULL; break;
    case SETTING_SHARPEN: s->sharpening = value; break;
    case SETTING_SUPERSAMPLE: s->supersampling = value; break;
    case SETTING_STATS: s->stats = value != 0; break;
    case SETTING_AMBILIGHT: s->glow = value != 0; break;
    case SETTING_ROOM_LIGHT: s->screen_light = value != 0; break;
    case SETTING_HEAD_LOCK:
        // The Head locked row's cell: Off 0, On 1, Follow 2
        s->head_locked = value == 1;
        s->head_follow = value == 2;
        break;
    case SETTING_PER_APP: s->per_app = value != 0; break;
    case SETTING_ANGULAR_LOCK: s->angular_lock = value != 0; break;
    case SETTING_FORGET_LAYOUT:
        // The layout kept for the app streaming now, and nothing kept again
        // until something about it changes
        if (ctx != NULL && settings_layout_forget(s, ctx->layoutHash)) {
            AppLayout now;
            settings_layout_snapshot(s, &now);
            _Static_assert(sizeof(AppLayout) == sizeof(ctx->layoutSeen), "layoutSeen holds an AppLayout");
            memcpy(ctx->layoutSeen, &now, sizeof(now));
            ctx->layoutSeenValid = 1;
            ctx->layoutPending = 0;
        }
        break;
    case SETTING_POINTER_SLEEP: s->pointer_sleep = value != 0; break;
    case SETTING_CLICK_SOUND: s->click_sound = value != 0; break;
    case SETTING_NOTICES: s->notices = value; break;
    case SETTING_CLICK_STEADY: s->click_steadiness = value; break;
    case SETTING_COG_TAB: s->cog_tab = value; break;
    case SETTING_CONTROLLER_MODEL: s->controller_model = value != 0; break;
    case SETTING_HEAD_AIM_SENSITIVITY: s->head_aim_sensitivity = value; break;
    case SETTING_HEAD_AIM_DEADZONE: s->head_aim_deadzone = value; break;
    case SETTING_AMBI_LEVEL: s->glow_level = value; break;
    case SETTING_SEPARATION: s->separation = value; break;
    case SETTING_CONVERGENCE: s->convergence = value; break;
    case SETTING_RESET_3D: settings_reset_3d(s); break;
    case SETTING_PICTURE_BRIGHTNESS: s->picture_brightness = value; break;
    case SETTING_PICTURE_CONTRAST: s->picture_contrast = value; break;
    case SETTING_PICTURE_GAMMA: s->picture_gamma = value; break;
    case SETTING_PICTURE_SATURATION: s->picture_saturation = value; break;
    case SETTING_RESET_PICTURE: settings_reset_picture(s); break;
    case SETTING_POINTER_DOT:
        // The Display tab's Ray row: both halves are saved. The bar's ray
        // button only flips the session (ctx->rayFlipped), which is not stored.
        s->pointer_dot = ctx != NULL ? ctx->dotOn : value >= 1;
        if (ctx != NULL) s->show_ray = ctx->raySetting != 0;
        break;
    case SETTING_ROOM_BRIGHTNESS:
    case SETTING_ROOM_GLOW:
    case SETTING_ROOM_LIGHT_LEVEL:
    case SETTING_ROOM_SCREEN: {
        // A room's own value, stored under the room showing (upstream
        // IN_SETTING_ROOM, which the input pass fills from the same)
        int style = ctx != NULL ? roomEffective(ctx) : 0;
        if (style < ROOM_STYLE_FIRST || style > ROOM_STYLE_LAST) {
            return 0;
        }
        RoomSettings* r = &s->room[style - ROOM_STYLE_FIRST];
        if (setting == SETTING_ROOM_BRIGHTNESS) r->brightness = value;
        else if (setting == SETTING_ROOM_GLOW) r->glow = value != 0;
        else if (setting == SETTING_ROOM_LIGHT_LEVEL) r->light = value;
        else r->screen = value;
        break;
    }
    default:
        return 0;
    }
    settings_clamp(s);
    return !settings_equal(&before, s);
}
