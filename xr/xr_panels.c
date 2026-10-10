// Moonlight XR Native: the panels' art. Upstream drew every panel in Java
// (XrPanels.java) on a loader thread and handed the bitmaps down through the
// nativeUpload* entry points; the frame loop showed whichever sheet the state
// asked for. Here lane U's rasterisers draw each panel into its slot of the
// UI atlas, on the frame loop, only when what it shows has changed (a view
// "key" per panel compared with the one it was last drawn from).
//
// What draws where:
//   settings panel     UI_PANEL_COG, with the thumbs, rings, readouts and the
//                      hover drawn in (upstream: separate quads)
//   clock strip        UI_PANEL_CLOCK, over the panel while it is up
//   keyboard           UI_PANEL_KEYBOARD per sheet and lit modifiers; the key
//                      under the ray is upstream's outline ring quad
//   exit prompt        UI_PANEL_EXIT per hovered button (upstream: 3 sheets)
//   picker             UI_PANEL_PICKER once; hover and choice are ring quads
//   bar buttons        UI_PANEL_BUTTON_ATLAS once (every face, BTN_CELL_*)
//   hint, Ko-fi        once, when first wanted
//   report sheet       UI_PANEL_REPORT from the native report form
//   toast              UI_PANEL_TOAST per notice
//   stats              ATLAS_OVERLAY, the session's stats lines
//   launcher lists     UI_PANEL_PC_LIST / UI_PANEL_APP_LIST, with the hover
// GPLv3.
#include "xr_native.h"

#include "text.h"

#define KEY_MAX 2048

typedef struct XrPanels {
    unsigned char key[ATLAS_SLOTS][KEY_MAX];
    int keyLen[ATLAS_SLOTS];
    int drawn[ATLAS_SLOTS];
    UiHits launcherHits;
    int launcherSlot;
    UiView lastView;
    Settings lastSettings;
    UiFont* font;
    // The report form (XrReportForm.java): the note, the address, which takes
    // the keys
    char reportNote[512];
    char reportAddress[128];
    int reportFocus;
    int64_t drawNs;
    int draws;
} XrPanels;

int xrPanelsCreate(XrCtx* ctx) {
    XrPanels* p = calloc(1, sizeof(XrPanels));
    if (p == NULL) {
        return 0;
    }
    p->launcherSlot = -1;
    p->reportFocus = REPORT_ZONE_NOTE;
    ctx->panels = p;
    // Ready as upstream says it once Java's art has arrived; here the art is
    // drawn the first time the panel shows
    ctx->pickerReady = 1;
    ctx->envButtonReady = 1;
    ctx->cogButtonReady = 1;
    ctx->cogThumbReady = 1;
    ctx->kbButtonReady = 1;
    ctx->exitButtonReady = 1;
    ctx->rayButtonReady = 1;
    ctx->aimButtonReady = 1;
    // Native: gamepad mode is M3, so its button stays off the bar, and the 3D
    // button only shows with a depth model (M6), as upstream leaves it out
    // of a session without stereo
    ctx->padButtonReady = 0;
    ctx->stereoButtonReady = ctx->stereoMode != DEPTH_MODE_OFF;
    ctx->kofiReady = 1;
    ctx->hintReady = ctx->handsEnabled;
    ctx->cogClockReady = 1;
    // The keyboard's layout, which upstream's Java handed down with the art
    ctx->kbKeyCount = ui_kb_key_count();
    if (ctx->kbKeyCount > KB_MAX_KEYS) {
        ctx->kbKeyCount = KB_MAX_KEYS;
    }
    for (int k = 0; k < ctx->kbKeyCount; k++) {
        ui_kb_key_rect(k, &ctx->kbKeyRects[k * 4]);
        for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
            ctx->kbCodes[sheet][k] = ui_kb_key_code(sheet, k);
        }
    }
    return 1;
}

void xrPanelsDestroy(XrCtx* ctx) {
    free(ctx->panels);
    ctx->panels = NULL;
}

const UiHits* xrPanelsLauncherHits(XrCtx* ctx) {
    return ctx->panels != NULL ? &ctx->panels->launcherHits : NULL;
}

void xrPanelsStats(XrCtx* ctx, int* draws, double* ms) {
    XrPanels* p = ctx->panels;
    *draws = p ? p->draws : 0;
    *ms = p && p->draws ? p->drawNs / 1e6 / p->draws : 0.0;
    if (p) {
        p->draws = 0;
        p->drawNs = 0;
    }
}

const UiView* xrPanelsLastView(XrCtx* ctx) {
    return ctx->panels != NULL ? &ctx->panels->lastView : NULL;
}

// Whether a slot's key differs from the one it was last drawn with; keeps it
static int keyChanged(XrPanels* p, int slot, const void* key, int len) {
    if (len > KEY_MAX) {
        len = KEY_MAX;
    }
    if (p->drawn[slot] && p->keyLen[slot] == len && memcmp(p->key[slot], key, (size_t)len) == 0) {
        return 0;
    }
    memcpy(p->key[slot], key, (size_t)len);
    p->keyLen[slot] = len;
    p->drawn[slot] = 1;
    return 1;
}

static void slotCanvas(XrCtx* ctx, int slot, UiCanvas* cv) {
    UiRectPx r = xrAtlasRect(slot);
    ui_cv_sub(cv, xrAtlasPixels(ctx), UI_ATLAS_W, r.x, r.y, r.w, r.h);
    ui_cv_clear(cv);
}

static void drawn(XrCtx* ctx, int slot, int64_t t0) {
    xrAtlasTouch(ctx, slot);
    ctx->panels->draws++;
    ctx->panels->drawNs += nowNs() - t0;
}

// The view a panel is drawn from: the live settings and what the context says
static void buildView(XrCtx* ctx, const Settings* live, UiView* v) {
    ui_view_init(v, live);
    v->session.head_aim = headAimSwitchOn(ctx->headAimSetting, ctx->headAimFlipped);
    v->session.pad_mode = ctx->padMode;
    v->session.ray = raySwitchOn(ctx->raySetting, ctx->rayFlipped);
    v->session.stereo_on = ctx->stereoLive;
    v->cog_tab = ctx->cogTab;
    v->room_env = -1;
    v->curve_ok = ctx->cylinderSupported;
    v->stereo_ok = ctx->stereoMode != DEPTH_MODE_OFF;
    v->bar_show_aim = headAimCanAct(ctx);
    v->bar_show_stereo = ctx->stereoButtonReady;
    v->kb_sheet = ctx->kbState;
    v->kb_mods = ctx->kbMods;
    v->version = "0.4 native (M4)";
    v->commit = NULL;
    v->log_path = "~/Games/MoonlightXRNative/moonlight.log";
    v->display_later = xrDisplayLaterMask(ctx);
    v->stereo_reason = ui_str(UIS_NATIVE_3D_LATER);
    v->env_live_cells = ctx->pickerCells;
    v->env_selected = -1;
    v->exit_question = ctx->launcher != LAUNCHER_NONE ? ui_str(UIS_NATIVE_EXIT_APP) : NULL;
}

// What the ray is on in the settings panel, as lane U's hover
static UiHover cogHover(XrCtx* ctx) {
    UiHover h = { UI_HIT_NONE, 0, 0 };
    int row = ctx->cogHoverSlider;
    if (ctx->cogReportHot) {
        h.kind = UI_HIT_BUTTON;
        h.a = UI_BTN_REPORT;
    }
    else if (ctx->cogKofiHot) {
        h.kind = UI_HIT_BUTTON;
        h.a = UI_BTN_KOFI;
    }
    else if (row >= 0 && ctx->cogHoverCell >= 0) {
        h.kind = UI_HIT_CELL;
        h.a = row;
        h.b = ctx->cogHoverCell;
    }
    else if (row >= 0 && ctx->cogHoverStep != 0) {
        h.kind = ctx->cogHoverStep > 0 ? UI_HIT_TRACK_UP : UI_HIT_TRACK_DOWN;
        h.a = row;
    }
    else if (row >= 0 || ctx->cogDragSlider >= 0) {
        h.kind = UI_HIT_TRACK_RUN;
        h.a = row >= 0 ? row : ctx->cogDragSlider;
    }
    return h;
}

typedef struct {
    Settings s;
    SessionToggles t;
    int tab, curveOk, stereoOk, showAim;
    UiHover hover;
    unsigned later;
} CogKey;

static void updateCog(XrCtx* ctx, XrPanels* p, const UiView* base) {
    if (!ctx->cogOpen && ctx->panelFades[FADE_COG].level <= 0.0f) {
        return;
    }
    UiView v = *base;
    v.hover = cogHover(ctx);
    CogKey k;
    memset(&k, 0, sizeof(k));
    k.s = *v.settings;
    k.t = v.session;
    k.tab = v.cog_tab;
    k.curveOk = v.curve_ok;
    k.stereoOk = v.stereo_ok;
    k.showAim = v.bar_show_aim;
    k.hover = v.hover;
    k.later = v.display_later;
    if (!keyChanged(p, UI_PANEL_COG, &k, sizeof(k))) {
        return;
    }
    int64_t t0 = nowNs();
    UiCanvas cv;
    slotCanvas(ctx, UI_PANEL_COG, &cv);
    ui_draw_panel(UI_PANEL_COG, &cv, &v, NULL);
    p->lastSettings = *v.settings;
    p->lastView = v;
    p->lastView.settings = &p->lastSettings;
    drawn(ctx, UI_PANEL_COG, t0);
}

static void updateClock(XrCtx* ctx, XrPanels* p, const char* clockText) {
    if (!ctx->cogOpen || clockText == NULL) {
        return;
    }
    if (!keyChanged(p, UI_PANEL_CLOCK, clockText, (int)strlen(clockText) + 1)) {
        return;
    }
    int64_t t0 = nowNs();
    UiCanvas cv;
    slotCanvas(ctx, UI_PANEL_CLOCK, &cv);
    ui_draw_clock(&cv, clockText);
    drawn(ctx, UI_PANEL_CLOCK, t0);
}

static void updateKeyboard(XrCtx* ctx, XrPanels* p, const UiView* base) {
    if (!ctx->kbOpen) {
        return;
    }
    int k[2] = { ctx->kbState, ctx->kbMods };
    if (!keyChanged(p, UI_PANEL_KEYBOARD, k, sizeof(k))) {
        return;
    }
    int64_t t0 = nowNs();
    UiView v = *base;
    v.hover.kind = UI_HIT_NONE;
    UiCanvas cv;
    slotCanvas(ctx, UI_PANEL_KEYBOARD, &cv);
    ui_draw_panel(UI_PANEL_KEYBOARD, &cv, &v, NULL);
    drawn(ctx, UI_PANEL_KEYBOARD, t0);
}

static void updateExit(XrCtx* ctx, XrPanels* p, const UiView* base) {
    if (!ctx->exitConfirmOpen && ctx->panelFades[FADE_EXIT].level <= 0.0f) {
        return;
    }
    int zone[2] = { ctx->exitHoverZone, ctx->launcher != LAUNCHER_NONE };
    if (!keyChanged(p, UI_PANEL_EXIT, zone, sizeof(zone))) {
        return;
    }
    int64_t t0 = nowNs();
    UiView v = *base;
    v.hover.kind = zone[0] == EXIT_ZONE_NONE ? UI_HIT_NONE : UI_HIT_BUTTON;
    v.hover.a = zone[0] == EXIT_ZONE_EXIT ? UI_BTN_EXIT_CONFIRM : UI_BTN_EXIT_CANCEL;
    UiCanvas cv;
    slotCanvas(ctx, UI_PANEL_EXIT, &cv);
    ui_draw_panel(UI_PANEL_EXIT, &cv, &v, NULL);
    drawn(ctx, UI_PANEL_EXIT, t0);
}

// Drawn once (and again only if what it depends on changes)
static void updateOnce(XrCtx* ctx, XrPanels* p, const UiView* base, int slot, int wanted) {
    if (!wanted) {
        return;
    }
    int k[3] = { base->env_live_cells, base->bar_show_aim, base->bar_show_stereo };
    if (!keyChanged(p, slot, k, sizeof(k))) {
        return;
    }
    int64_t t0 = nowNs();
    UiView v = *base;
    v.hover.kind = UI_HIT_NONE;
    UiCanvas cv;
    slotCanvas(ctx, slot, &cv);
    ui_draw_panel((UiPanelId)slot, &cv, &v, NULL);
    drawn(ctx, slot, t0);
}

static void updateReport(XrCtx* ctx, XrPanels* p) {
    if (!ctx->reportOpen) {
        return;
    }
    struct {
        char note[512];
        char address[128];
        int focus, hover, canSend;
    } k;
    memset(&k, 0, sizeof(k));
    snprintf(k.note, sizeof(k.note), "%s", p->reportNote);
    snprintf(k.address, sizeof(k.address), "%s", p->reportAddress);
    k.focus = p->reportFocus;
    k.hover = ctx->reportHoverZone;
    k.canSend = p->reportNote[0] != '\0';
    ctx->reportSendReady = k.canSend;
    if (!keyChanged(p, UI_PANEL_REPORT, &k, sizeof(k))) {
        return;
    }
    int64_t t0 = nowNs();
    UiReport r;
    memset(&r, 0, sizeof(r));
    r.note = p->reportNote;
    r.address = p->reportAddress;
    r.focus = p->reportFocus;
    r.hover = ctx->reportHoverZone;
    r.can_send = k.canSend;
    UiCanvas cv;
    slotCanvas(ctx, UI_PANEL_REPORT, &cv);
    ui_draw_report(&cv, &r, NULL);
    ctx->reportReady = 1;
    drawn(ctx, UI_PANEL_REPORT, t0);
}

// The report form (XrReportForm.java, cut down): a key typed while the sheet
// is up goes into the field that has the keys, never to the host
void xrPanelsReportKey(XrCtx* ctx, int code) {
    XrPanels* p = ctx->panels;
    if (p == NULL) {
        return;
    }
    char* field = p->reportFocus == REPORT_ZONE_EMAIL ? p->reportAddress : p->reportNote;
    size_t cap = p->reportFocus == REPORT_ZONE_EMAIL ? sizeof(p->reportAddress) : sizeof(p->reportNote);
    size_t len = strlen(field);
    if (code == 8) {
        // Back over one whole UTF-8 character
        while (len > 0) {
            len--;
            if (((unsigned char)field[len] & 0xC0) != 0x80) {
                break;
            }
        }
        field[len] = '\0';
    }
    else if (code == 13 && p->reportFocus == REPORT_ZONE_NOTE) {
        if (len + 1 < cap) {
            field[len] = '\n';
            field[len + 1] = '\0';
        }
    }
    else if (code >= 32 && code < KB_CODE_VK) {
        char utf8[4];
        int n = ui_utf8_encode((uint32_t)code, utf8);
        if (n > 0 && len + (size_t)n < cap) {
            memcpy(field + len, utf8, (size_t)n);
            field[len + n] = '\0';
        }
    }
}

// A press on the report sheet that the app acts on (IN_REPORT)
void xrPanelsReportEvent(XrCtx* ctx, int event, const char* reportDir) {
    XrPanels* p = ctx->panels;
    if (p == NULL) {
        return;
    }
    if (event == REPORT_OPENED) {
        p->reportNote[0] = '\0';
        p->reportAddress[0] = '\0';
        p->reportFocus = REPORT_ZONE_NOTE;
        ctx->reportReady = 0;
        p->drawn[UI_PANEL_REPORT] = 0; // drawn afresh for this opening
    }
    else if (event == REPORT_ZONE_NOTE || event == REPORT_ZONE_EMAIL) {
        p->reportFocus = event;
    }
    else if (event == REPORT_ZONE_SEND) {
        // Native: there is no report server here; the note goes in the log and
        // in a file beside it, for the owner to pass on
        LOGEV("problem report from the headset: \"%s\" (address \"%s\")", p->reportNote, p->reportAddress);
        if (reportDir != NULL) {
            char path[512];
            time_t t = time(NULL);
            struct tm tm;
            localtime_r(&t, &tm);
            snprintf(path, sizeof(path), "%s/report-%04d%02d%02d-%02d%02d%02d.txt", reportDir,
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            FILE* f = fopen(path, "w");
            if (f != NULL) {
                fprintf(f, "%s\n\naddress: %s\n", p->reportNote, p->reportAddress);
                fclose(f);
                LOGEV("problem report saved to %s", path);
            }
        }
    }
}

// The toast's words for a notice (XrRenderer.updateToast)
static void toastText(XrCtx* ctx, int kind, int arg, char* text, size_t cap, const char** more) {
    *more = "";
    switch (kind) {
    case TOAST_RATE: snprintf(text, cap, ui_str(UIS_TOAST_RATE), arg); break;
    case TOAST_HANDS_LOCKED: snprintf(text, cap, "%s", ui_str(UIS_TOAST_HANDS_LOCKED)); break;
    case TOAST_HANDS_UNLOCKED: snprintf(text, cap, "%s", ui_str(UIS_TOAST_HANDS_UNLOCKED)); break;
    case TOAST_3D_OFF: snprintf(text, cap, "%s", ui_str(UIS_TOAST_3D_OFF)); break;
    case TOAST_3D_ON: snprintf(text, cap, "%s", ui_str(UIS_TOAST_3D_ON)); break;
    case TOAST_HEAD_AIM_ON:
        snprintf(text, cap, "%s", ui_str(UIS_TOAST_HEAD_AIM_ON));
        *more = ui_str(UIS_TOAST_HEAD_AIM_ON_MORE);
        break;
    case TOAST_HEAD_AIM_OFF: snprintf(text, cap, "%s", ui_str(UIS_TOAST_HEAD_AIM_OFF)); break;
    case TOAST_GAMEPAD_MODE:
        snprintf(text, cap, "%s", ui_str(UIS_TOAST_GAMEPAD_MODE));
        *more = ui_str(ctx->padShortcut == PAD_SHORTCUT_STICKS ? UIS_TOAST_BACK_STICKS
                       : ctx->padShortcut == PAD_SHORTCUT_TRIGGERS_GRIPS ? UIS_TOAST_BACK_TRIGGERS_GRIPS
                                                                         : UIS_TOAST_BACK_MENU_GRIP);
        break;
    case TOAST_POINTER_MODE: snprintf(text, cap, "%s", ui_str(UIS_TOAST_POINTER_MODE)); break;
    default: text[0] = '\0'; break;
    }
}

// Native text notices (TOAST_TEXT + slot upstream): what the app wants said
static char noticeTexts[TOAST_TEXT_SLOTS][160];
static int noticeNext;

void xrPanelsNotice(XrCtx* ctx, const char* text) {
    int slot = noticeNext++ % TOAST_TEXT_SLOTS;
    snprintf(noticeTexts[slot], sizeof(noticeTexts[slot]), "%s", text);
    noticePush(&ctx->notices, TOAST_TEXT, slot);
    LOGI("toast: %s", text);
}

// A notice went up this frame (IN_TOAST): its words into the toast's slot
void xrPanelsToast(XrCtx* ctx, int kind, int arg) {
    char text[200];
    const char* more = "";
    if (kind == TOAST_TEXT) {
        snprintf(text, sizeof(text), "%s", noticeTexts[(arg % TOAST_TEXT_SLOTS + TOAST_TEXT_SLOTS) % TOAST_TEXT_SLOTS]);
    }
    else {
        toastText(ctx, kind, arg, text, sizeof(text), &more);
    }
    UiCanvas cv;
    slotCanvas(ctx, UI_PANEL_TOAST, &cv);
    ui_draw_toast(&cv, text, more, 0);
    xrAtlasTouch(ctx, UI_PANEL_TOAST);
    ctx->toastDrawnKind = kind;
    ctx->toastDrawnArg = arg;
    ctx->toastArtReady = 1;
}

// The stats strip: a dark sheet with the session's lines (upstream's overlay
// was Java's PerfOverlay text drawn into a bitmap)
static void updateOverlay(XrCtx* ctx, XrPanels* p, const char* statsText) {
    if (!ctx->overlayVisible || statsText == NULL) {
        return;
    }
    if (!keyChanged(p, ATLAS_OVERLAY, statsText, (int)strlen(statsText) + 1)) {
        return;
    }
    if (p->font == NULL) {
        p->font = ui_font_default();
        if (p->font == NULL) {
            return;
        }
    }
    int64_t t0 = nowNs();
    UiCanvas cv;
    slotCanvas(ctx, ATLAS_OVERLAY, &cv);
    ui_fill_rrect(&cv, 0.0f, 0.0f, (float)cv.w, (float)cv.h, 14.0f, 0xB0000000u);
    float size = 17.0f;
    float y = 12.0f + size;
    const char* s = statsText;
    while (*s != '\0' && y < cv.h - 6.0f) {
        const char* e = strchr(s, '\n');
        int len = e != NULL ? (int)(e - s) : (int)strlen(s);
        ui_text_draw(&cv, p->font, UI_WEIGHT_REGULAR, size, 12.0f, y, UI_ALIGN_LEFT, 0xFFFFFFFFu, s, len);
        y += size * 1.35f;
        s += len;
        if (*s == '\n') {
            s++;
        }
    }
    ctx->overlayHasContent = 1;
    drawn(ctx, ATLAS_OVERLAY, t0);
}

// ---- the launcher's lists ----------------------------------------------------------

int launcherListUv(XrCtx* ctx, float u, float v, float* lu, float* lv) {
    // The list as large as fits the picture, centred: its width over the
    // picture's, and its height over the picture's
    float pictureAspect = (float)ctx->videoWidth / (float)ctx->videoHeight;
    float listAspect = ctx->launcherAspect > 0.0f ? ctx->launcherAspect : 1.6f;
    float sw = 1.0f, sh = 1.0f;
    if (listAspect < pictureAspect) {
        sw = listAspect / pictureAspect;
    }
    else {
        sh = pictureAspect / listAspect;
    }
    *lu = (u - 0.5f) / sw + 0.5f;
    *lv = (v - 0.5f) / sh + 0.5f;
    return *lu >= 0.0f && *lu <= 1.0f && *lv >= 0.0f && *lv <= 1.0f;
}

int xrPanelsLauncherHit(XrCtx* ctx, float lu, float lv) {
    XrPanels* p = ctx->panels;
    if (p == NULL) {
        return -1;
    }
    int i = ui_hits_at(&p->launcherHits, lu, lv);
    return i >= 0 && p->launcherHits.hit[i].enabled ? i : -1;
}

typedef struct {
    int mode, count, page, loading, failed;
    UiHover hover;
    unsigned hash;
    char pcName[64];
} ListKey;

static unsigned hashBytes(unsigned h, const void* data, size_t n) {
    const unsigned char* b = data;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

static void updateLauncher(XrCtx* ctx, XrPanels* p, const XrLauncherView* lv) {
    if (lv == NULL || lv->mode == LAUNCHER_NONE) {
        ctx->launcher = LAUNCHER_NONE;
        return;
    }
    ctx->launcher = lv->mode;
    int slot = lv->mode == LAUNCHER_PCS ? UI_PANEL_PC_LIST : UI_PANEL_APP_LIST;
    int w, h;
    ui_panel_native_size((UiPanelId)slot, &w, &h);
    ctx->launcherAspect = (float)w / (float)h;
    ListKey k;
    memset(&k, 0, sizeof(k));
    k.mode = lv->mode;
    k.page = lv->page;
    k.loading = lv->loading;
    k.failed = lv->failed;
    k.hash = 2166136261u;
    if (lv->mode == LAUNCHER_PCS) {
        k.count = lv->pcCount;
        k.hash = hashBytes(k.hash, lv->pcs, sizeof(UiPcItem) * (size_t)(lv->pcCount > 0 ? lv->pcCount : 0));
    }
    else {
        k.count = lv->appCount;
        k.hash = hashBytes(k.hash, lv->apps, sizeof(UiAppItem) * (size_t)(lv->appCount > 0 ? lv->appCount : 0));
        snprintf(k.pcName, sizeof(k.pcName), "%s", lv->pcName != NULL ? lv->pcName : "");
    }
    // The hover is the hit the ray is on in the last drawing
    int hi = ctx->launcherHoverHit;
    if (p->launcherSlot == slot && hi >= 0 && hi < p->launcherHits.count) {
        k.hover.kind = p->launcherHits.hit[hi].kind;
        k.hover.a = p->launcherHits.hit[hi].a;
        k.hover.b = p->launcherHits.hit[hi].b;
    }
    if (p->launcherSlot == slot && !keyChanged(p, slot, &k, sizeof(k))) {
        return;
    }
    if (p->launcherSlot != slot) {
        keyChanged(p, slot, &k, sizeof(k));
        p->launcherSlot = slot;
        ctx->launcherHoverHit = -1;
        k.hover.kind = UI_HIT_NONE;
    }
    int64_t t0 = nowNs();
    UiCanvas cv;
    slotCanvas(ctx, slot, &cv);
    ui_hits_clear(&p->launcherHits);
    if (lv->mode == LAUNCHER_PCS) {
        UiPcList l;
        memset(&l, 0, sizeof(l));
        l.items = lv->pcs;
        l.count = lv->pcCount;
        l.page = lv->page;
        l.selected = -1;
        l.hover = k.hover;
        ui_draw_pc_list(&cv, &l, &p->launcherHits);
    }
    else {
        UiAppList l;
        memset(&l, 0, sizeof(l));
        l.pc_name = lv->pcName;
        l.items = lv->apps;
        l.count = lv->appCount;
        l.page = lv->page;
        l.selected = -1;
        l.loading = lv->loading;
        l.failed = lv->failed;
        l.hover = k.hover;
        ui_draw_app_list(&cv, &l, &p->launcherHits);
    }
    drawn(ctx, slot, t0);
}

void xrPanelsUpdate(XrCtx* ctx, const Settings* live, const XrLauncherView* launcher,
                    const char* statsText, const char* clockText, int64_t now) {
    XrPanels* p = ctx->panels;
    if (p == NULL || xrAtlasPixels(ctx) == NULL) {
        return;
    }
    (void)now;
    UiView base;
    buildView(ctx, live, &base);
    updateOnce(ctx, p, &base, UI_PANEL_BUTTON_ATLAS, 1);
    updateOnce(ctx, p, &base, UI_PANEL_PICKER, ctx->pickerOpen || ctx->panelFades[FADE_PICKER].level > 0.0f);
    updateOnce(ctx, p, &base, UI_PANEL_KOFI, ctx->kofiOpen || ctx->panelFades[FADE_KOFI].level > 0.0f);
    updateOnce(ctx, p, &base, UI_PANEL_HINT, ctx->hintOpen || ctx->panelFades[FADE_HINT].level > 0.0f);
    updateCog(ctx, p, &base);
    updateClock(ctx, p, clockText);
    updateKeyboard(ctx, p, &base);
    updateExit(ctx, p, &base);
    updateReport(ctx, p);
    updateOverlay(ctx, p, statsText);
    updateLauncher(ctx, p, launcher);
}
