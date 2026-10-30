// Moonlight XR Native: the panels' art. Upstream drew every panel in Java
// (XrPanels.java) on a loader thread and handed the bitmaps down through the
// nativeUpload* entry points; the frame loop showed whichever sheet the state
// asked for. Here lane U's rasterisers draw each panel into its slot of the
// UI atlas only when what it shows has changed (a view "key" per panel
// compared with the one it was last drawn from).
//
// Native: the frame loop only decides what changed. With a swapchain atlas
// (xrPanelsSetAsync) the drawing itself goes to one worker thread, as
// upstream's loader thread did: a cog redraw takes 4-7 ms and the keyboard or
// picker ~10 ms on the Frame's CPU, most of an 11.1 ms frame at 90 Hz, and the
// owner saw the head-tracked world judder while the menu was open. The frame
// loop hands the worker a job (a copy of everything the panel is drawn from),
// the worker draws it into a buffer of its own, and the frame loop copies a
// finished buffer into the atlas on a later frame, after that frame's fence.
// Only the newest job of a slot is drawn. A CPU-only atlas (the tests and
// snapshots) draws on the caller's thread, as before.
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
#define _GNU_SOURCE // pthread_setname_np
#include "xr_native.h"
#include "xr_bugzip.h"
#include "xr_keyboard.h"

#include "text.h"

#include <pthread.h>
#ifndef MLXR_VERSION
#define MLXR_VERSION "dev"
#endif

#define KEY_MAX 2048

// ---- a panel drawing, as the worker gets it ------------------------------------------

typedef enum { JOB_PANEL, JOB_CLOCK, JOB_REPORT, JOB_TOAST, JOB_OVERLAY, JOB_PC_LIST, JOB_APP_LIST } JobKind;

// Everything a panel is drawn from, by value: the worker never reads the
// context. view.settings and view.stereo_reason are pointed at this job's own
// copies just before it is drawn (jobFix), as the job moves about.
typedef struct {
    int kind;
    int slot;
    UiView view;
    Settings settings;
    char stereoReason[96];
    char text[1024];  // clock line, toast line, stats lines (native: room for a long stats line)
    int outW, outH;   // native: what the stats window drew (pixels of its slot), for the quad
    char more[256];   // the toast's quieter line
    char note[512];   // report form
    int focus, hover, canSend;
    int toastKind, toastArg;
    int clockBattery, clockCharging, clockSlow;   // native (lane wave3-hud): the clock strip's extras
    UiPcItem* pcs;    // launcher lists, owned by the job
    UiAppItem* apps;
    int count, page, loading, failed;
    UiHover listHover;
    char pcName[64];
    int64_t queuedNs;
} PanelJob;

typedef struct {
    int w, h;          // the slot's rectangle in the atlas
    PanelJob pending;  // the newest drawing wanted, not yet taken
    int hasPending;
    uint8_t* work;     // the worker's canvas (w*h RGBA8)
    uint8_t* done;     // the last finished drawing, waiting for the frame loop
    int hasDone;
    PanelJob doneJob;  // what it was drawn from (lists freed)
    UiHits doneHits;
} SlotWork;

typedef struct XrPanels {
    unsigned char key[ATLAS_SLOTS][KEY_MAX];
    int keyLen[ATLAS_SLOTS];
    int drawn[ATLAS_SLOTS];
    UiHits launcherHits;
    int launcherSlot;
    UiView lastView;
    Settings lastSettings;
    char lastStereoReason[96];
    UiFont* font;     // the drawing thread's only
    // The report form (XrReportForm.java): the note, which takes
    // the keys
    char reportNote[512];
    int reportFocus;
    // Native: the worker. lock guards slots[], busy, quit and the stats.
    pthread_mutex_t lock;
    pthread_cond_t wake;   // a job is waiting, or quit
    pthread_cond_t idle;   // a job finished
    pthread_t thread;
    int threaded;
    int busy;
    int quit;
    SlotWork slots[ATLAS_SLOTS];
    int64_t drawNs;
    int draws;
    int64_t waitNs;   // from asking to the drawing landing in the atlas, worst of the window
} XrPanels;

int xrPanelsCreate(XrCtx* ctx) {
    XrPanels* p = calloc(1, sizeof(XrPanels));
    if (p == NULL) {
        return 0;
    }
    p->launcherSlot = -1;
    p->reportFocus = REPORT_ZONE_NOTE;
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->wake, NULL);
    pthread_cond_init(&p->idle, NULL);
    for (int s = 0; s < ATLAS_SLOTS; s++) {
        UiRectPx r = xrAtlasRect(s);
        p->slots[s].w = r.w > 0 ? r.w : 0;
        p->slots[s].h = r.h > 0 ? r.h : 0;
    }
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
    // The 3D button only shows with a stereo mode (a depth model or a test
    // pattern), as upstream leaves it out of a session without stereo
    ctx->padButtonReady = 1; // M3: the pad reaches the host
    ctx->stereoButtonReady = ctx->stereoMode != DEPTH_MODE_OFF;
    ctx->kofiReady = 1;
    ctx->hintReady = ctx->handsEnabled;
    ctx->cogClockReady = 1;
    // The keyboard's layout, which upstream's Java handed down with the art
    xrKeyboardFillTables(ctx);
    return 1;
}

static void jobFree(PanelJob* j) {
    free(j->pcs);
    free(j->apps);
    j->pcs = NULL;
    j->apps = NULL;
}

void xrPanelsDestroy(XrCtx* ctx) {
    XrPanels* p = ctx->panels;
    if (p == NULL) {
        return;
    }
    if (p->threaded) {
        pthread_mutex_lock(&p->lock);
        p->quit = 1;
        pthread_cond_broadcast(&p->wake);
        pthread_mutex_unlock(&p->lock);
        pthread_join(p->thread, NULL);
    }
    for (int s = 0; s < ATLAS_SLOTS; s++) {
        if (p->slots[s].hasPending) {
            jobFree(&p->slots[s].pending);
        }
        free(p->slots[s].work);
        free(p->slots[s].done);
    }
    pthread_cond_destroy(&p->idle);
    pthread_cond_destroy(&p->wake);
    pthread_mutex_destroy(&p->lock);
    free(p);
    ctx->panels = NULL;
}

const UiHits* xrPanelsLauncherHits(XrCtx* ctx) {
    return ctx->panels != NULL ? &ctx->panels->launcherHits : NULL;
}

void xrPanelsStats(XrCtx* ctx, int* draws, double* ms) {
    XrPanels* p = ctx->panels;
    *draws = 0;
    *ms = 0.0;
    if (p == NULL) {
        return;
    }
    pthread_mutex_lock(&p->lock);
    *draws = p->draws;
    *ms = p->draws ? p->drawNs / 1e6 / p->draws : 0.0;
    p->draws = 0;
    p->drawNs = 0;
    pthread_mutex_unlock(&p->lock);
}

double xrPanelsWorstWaitMs(XrCtx* ctx) {
    XrPanels* p = ctx->panels;
    if (p == NULL) {
        return 0.0;
    }
    pthread_mutex_lock(&p->lock);
    double ms = p->waitNs / 1e6;
    p->waitNs = 0;
    pthread_mutex_unlock(&p->lock);
    return ms;
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

// ---- drawing a job (either thread) and landing it (frame loop) ------------------------

static void jobFix(PanelJob* j) {
    j->view.settings = &j->settings;
    j->view.stereo_reason = j->stereoReason[0] ? j->stereoReason : NULL;
}

// A job for one of the panels drawn from the view alone
static void jobPanel(PanelJob* j, int slot, const UiView* v) {
    memset(j, 0, sizeof(*j));
    j->kind = JOB_PANEL;
    j->slot = slot;
    j->view = *v;
    if (v->settings != NULL) {
        j->settings = *v->settings;
    }
    if (v->stereo_reason != NULL) {
        snprintf(j->stereoReason, sizeof(j->stereoReason), "%s", v->stereo_reason);
    }
}

// Native (owner: "it cuts off text on the right side"): the stats lines laid
// out to fit the slot. The lines were drawn at a fixed 17 px from the left
// and the app's lines ("hw decode ... errors", "audio ... (stream totals)")
// are wider than the 384 px slot, so their right ends were cut. Every line
// now wraps at the slot's width (at a space, or mid-word if one word is
// wider than the slot), and the type steps down from 17 px only if the
// wrapped lines would run past the slot's height. The window is as tall as
// its lines, so the quad (xr_layers.c) shows just that much of the slot.
#define OVERLAY_PAD 12.0f
#define OVERLAY_MAX_LINES 96
#define OVERLAY_MIN_SIZE 10.0f

typedef struct {
    const char* s;
    int len;
    int indent;   // a continuation of the line above
} OverlayLine;

static int overlayWrap(UiFont* font, float size, float maxW, const char* text, OverlayLine* out) {
    int n = 0;
    float indentPx = size * 0.8f;
    const char* s = text;
    while (*s != '\0' && n < OVERLAY_MAX_LINES) {
        const char* e = strchr(s, '\n');
        int len = e != NULL ? (int)(e - s) : (int)strlen(s);
        int indent = 0;
        while (len > 0 && n < OVERLAY_MAX_LINES) {
            float room = maxW - (indent ? indentPx : 0.0f);
            if (ui_text_width(font, UI_WEIGHT_REGULAR, size, s, len) <= room) {
                out[n].s = s;
                out[n].len = len;
                out[n].indent = indent;
                n++;
                break;
            }
            // The longest prefix that fits, then back to the last space
            int fit = 1;
            for (int k = 2; k <= len; k++) {
                if (ui_text_width(font, UI_WEIGHT_REGULAR, size, s, k) > room) {
                    break;
                }
                fit = k;
            }
            int cut = fit;
            for (int k = fit; k > 0; k--) {
                if (s[k] == ' ' || s[k - 1] == ' ') {
                    cut = s[k] == ' ' ? k : k - 1;
                    break;
                }
            }
            if (cut <= 0) {
                cut = fit;
            }
            out[n].s = s;
            out[n].len = cut;
            out[n].indent = indent;
            n++;
            s += cut;
            len -= cut;
            while (len > 0 && *s == ' ') {
                s++;
                len--;
            }
            indent = 1;
        }
        s = e != NULL ? e + 1 : s + len;
        if (e == NULL) {
            break;
        }
    }
    return n;
}

// Lays the text out and draws it; says the extent it used in j->outW/outH
static void overlayDraw(UiCanvas* cv, UiFont* font, PanelJob* j) {
    static OverlayLine lines[OVERLAY_MAX_LINES];
    float maxW = (float)cv->w - 2.0f * OVERLAY_PAD;
    float size = 17.0f;
    int n = 0;
    // Native (lane wave3-hud): a text of one line (the Lite stats) keeps to one line: the type steps down to fit
    // it, to 12 px, instead of the line wrapping
    if (strchr(j->text, '\n') == NULL) {
        while (size > 12.0f && ui_text_width(font, UI_WEIGHT_REGULAR, size, j->text, (int)strlen(j->text)) > maxW) {
            size -= 1.0f;
        }
    }
    for (; size >= OVERLAY_MIN_SIZE; size -= 1.0f) {
        n = overlayWrap(font, size, maxW, j->text, lines);
        if ((float)n * size * 1.35f + 2.0f * OVERLAY_PAD - size * 0.35f <= (float)cv->h) {
            break;
        }
    }
    if (size < OVERLAY_MIN_SIZE) {
        size = OVERLAY_MIN_SIZE;
        n = overlayWrap(font, size, maxW, j->text, lines);
    }
    float lh = size * 1.35f;
    int h = (int)ceilf((float)n * lh + 2.0f * OVERLAY_PAD - size * 0.35f);
    if (h > cv->h) {
        h = cv->h;
    }
    if (h < 24) {
        h = 24;
    }
    ui_fill_rrect(cv, 0.0f, 0.0f, (float)cv->w, (float)h, 14.0f, 0xB0000000u);
    float y = OVERLAY_PAD + size;
    for (int i = 0; i < n && y < (float)h; i++) {
        ui_text_draw(cv, font, UI_WEIGHT_REGULAR, size, OVERLAY_PAD + (lines[i].indent ? size * 0.8f : 0.0f), y,
                     UI_ALIGN_LEFT, 0xFFFFFFFFu, lines[i].s, lines[i].len);
        y += lh;
    }
    j->outW = cv->w;
    j->outH = h;
}

// Draws the job onto a cleared canvas. 0: nothing drawn (no font)
static int jobDraw(XrPanels* p, PanelJob* j, UiCanvas* cv, UiHits* hits) {
    jobFix(j);
    ui_hits_clear(hits);
    switch (j->kind) {
    case JOB_PANEL: ui_draw_panel((UiPanelId)j->slot, cv, &j->view, NULL); break;
    case JOB_CLOCK: {
        UiClock c = { j->text, j->clockBattery, j->clockCharging, j->clockSlow };
        ui_draw_clock_strip(cv, &c);
        break;
    }
    case JOB_REPORT: {
        UiReport r;
        memset(&r, 0, sizeof(r));
        r.note = j->note;
        r.focus = j->focus;
        r.hover = j->hover;
        r.can_send = j->canSend;
        ui_draw_report(cv, &r, NULL);
        break;
    }
    case JOB_TOAST: ui_draw_toast(cv, j->text, j->more, j->more[0] == '/' || j->more[0] == '~'); break;
    case JOB_OVERLAY: {
        if (p->font == NULL) {
            p->font = ui_font_default();
            if (p->font == NULL) {
                return 0;
            }
        }
        overlayDraw(cv, p->font, j);
        break;
    }
    case JOB_PC_LIST: {
        UiPcList l;
        memset(&l, 0, sizeof(l));
        l.items = j->pcs;
        l.count = j->count;
        l.page = j->page;
        l.selected = -1;
        l.hover = j->listHover;
        ui_draw_pc_list(cv, &l, hits);
        break;
    }
    case JOB_APP_LIST: {
        UiAppList l;
        memset(&l, 0, sizeof(l));
        l.pc_name = j->pcName;
        l.items = j->apps;
        l.count = j->count;
        l.page = j->page;
        l.selected = -1;
        l.loading = j->loading;
        l.failed = j->failed;
        l.hover = j->listHover;
        ui_draw_app_list(cv, &l, hits);
        break;
    }
    default: return 0;
    }
    return 1;
}

// What a drawing changes besides its pixels, once it is in the atlas
static void jobLand(XrCtx* ctx, XrPanels* p, const PanelJob* j, const UiHits* hits) {
    switch (j->kind) {
    case JOB_PANEL:
        if (j->slot == UI_PANEL_FIRSTRUN) {
            ctx->firstRunReady = 1;
        }
        else if (j->slot == UI_PANEL_PADCARD) {
            ctx->padCardReady = 1;
        }
        if (j->slot == UI_PANEL_COG) {
            p->lastSettings = j->settings;
            snprintf(p->lastStereoReason, sizeof(p->lastStereoReason), "%s", j->stereoReason);
            p->lastView = j->view;
            p->lastView.settings = &p->lastSettings;
            p->lastView.stereo_reason = p->lastStereoReason[0] ? p->lastStereoReason : NULL;
        }
        break;
    case JOB_REPORT: ctx->reportReady = 1; break;
    case JOB_TOAST:
        ctx->toastDrawnKind = j->toastKind;
        ctx->toastDrawnArg = j->toastArg;
        ctx->toastArtReady = 1;
        break;
    case JOB_OVERLAY:
        ctx->overlayW = j->outW;
        ctx->overlayH = j->outH;
        ctx->overlayHasContent = 1;
        break;
    case JOB_PC_LIST:
    case JOB_APP_LIST:
        if (p->launcherSlot == j->slot) {
            p->launcherHits = *hits;
        }
        break;
    default: break;
    }
}

// The worker: newest job of each slot, the panels the ray is on first
static const int JOB_ORDER[] = { UI_PANEL_COG, UI_PANEL_KEYBOARD, UI_PANEL_EXIT, UI_PANEL_TOAST,
                                 UI_PANEL_PC_LIST, UI_PANEL_APP_LIST, UI_PANEL_REPORT, UI_PANEL_PICKER,
                                 UI_PANEL_CLOCK };

static int nextJob(XrPanels* p) {
    for (size_t i = 0; i < sizeof(JOB_ORDER) / sizeof(JOB_ORDER[0]); i++) {
        if (p->slots[JOB_ORDER[i]].hasPending) {
            return JOB_ORDER[i];
        }
    }
    for (int s = 0; s < ATLAS_SLOTS; s++) {
        if (p->slots[s].hasPending) {
            return s;
        }
    }
    return -1;
}

static void* panelWorker(void* arg) {
    XrPanels* p = arg;
    pthread_setname_np(pthread_self(), "mlxr-panels");
    pthread_mutex_lock(&p->lock);
    for (;;) {
        int s;
        while (!p->quit && (s = nextJob(p)) < 0) {
            pthread_cond_wait(&p->wake, &p->lock);
        }
        if (p->quit) {
            break;
        }
        SlotWork* sw = &p->slots[s];
        PanelJob job = sw->pending;
        sw->hasPending = 0;
        p->busy = 1;
        if (sw->work == NULL) {
            sw->work = malloc((size_t)sw->w * (size_t)sw->h * 4);
        }
        uint8_t* work = sw->work;
        pthread_mutex_unlock(&p->lock);

        static UiHits hits; // the worker's only
        int64_t t0 = nowNs();
        int ok = 0;
        if (work != NULL) {
            UiCanvas cv;
            ui_cv_init(&cv, work, sw->w, sw->h);
            ui_cv_clear(&cv);
            ok = jobDraw(p, &job, &cv, &hits);
        }
        int64_t t1 = nowNs();
        jobFree(&job);

        pthread_mutex_lock(&p->lock);
        if (ok) {
            sw->work = sw->done;
            sw->done = work;
            sw->doneJob = job;
            sw->doneHits = hits;
            sw->hasDone = 1;
            p->draws++;
            p->drawNs += t1 - t0;
        }
        p->busy = 0;
        pthread_cond_broadcast(&p->idle);
    }
    pthread_mutex_unlock(&p->lock);
    return NULL;
}

// Finished drawings into the atlas: frame loop only, after the frame's fence
// (xrAtlasTouch copies into the staging buffer the GPU reads)
static void collect(XrCtx* ctx, XrPanels* p) {
    if (!p->threaded) {
        return;
    }
    uint8_t* atlas = xrAtlasPixels(ctx);
    int64_t now = nowNs();
    pthread_mutex_lock(&p->lock);
    for (int s = 0; s < ATLAS_SLOTS; s++) {
        SlotWork* sw = &p->slots[s];
        if (!sw->hasDone) {
            continue;
        }
        sw->hasDone = 0;
        UiRectPx r = xrAtlasRect(s);
        for (int y = 0; y < sw->h; y++) {
            memcpy(atlas + ((size_t)(r.y + y) * UI_ATLAS_W + (size_t)r.x) * 4, sw->done + (size_t)y * sw->w * 4,
                   (size_t)sw->w * 4);
        }
        jobLand(ctx, p, &sw->doneJob, &sw->doneHits);
        xrAtlasTouch(ctx, s);
        if (now - sw->doneJob.queuedNs > p->waitNs) {
            p->waitNs = now - sw->doneJob.queuedNs;
        }
    }
    pthread_mutex_unlock(&p->lock);
}

// A drawing wanted: drawn here and now with a CPU-only atlas, else handed to
// the worker (replacing an older one of the slot not yet taken)
static void submit(XrCtx* ctx, XrPanels* p, PanelJob* j) {
    int slot = j->slot;
    j->queuedNs = nowNs();
    if (!p->threaded) {
        static UiHits hits;
        UiRectPx r = xrAtlasRect(slot);
        UiCanvas cv;
        ui_cv_sub(&cv, xrAtlasPixels(ctx), UI_ATLAS_W, r.x, r.y, r.w, r.h);
        ui_cv_clear(&cv);
        int64_t t0 = nowNs();
        int ok = jobDraw(p, j, &cv, &hits);
        int64_t t1 = nowNs();
        if (ok) {
            jobLand(ctx, p, j, &hits);
            xrAtlasTouch(ctx, slot);
            pthread_mutex_lock(&p->lock);
            p->draws++;
            p->drawNs += t1 - t0;
            pthread_mutex_unlock(&p->lock);
        }
        jobFree(j);
        return;
    }
    pthread_mutex_lock(&p->lock);
    SlotWork* sw = &p->slots[slot];
    if (sw->hasPending) {
        jobFree(&sw->pending);
    }
    sw->pending = *j;
    sw->hasPending = 1;
    pthread_cond_signal(&p->wake);
    pthread_mutex_unlock(&p->lock);
}

// A slot's drawings not yet landed are dropped (the report sheet reopened)
static void dropSlot(XrPanels* p, int slot) {
    pthread_mutex_lock(&p->lock);
    SlotWork* sw = &p->slots[slot];
    if (sw->hasPending) {
        jobFree(&sw->pending);
        sw->hasPending = 0;
    }
    sw->hasDone = 0;
    pthread_mutex_unlock(&p->lock);
}

int xrPanelsSetAsync(XrCtx* ctx) {
    XrPanels* p = ctx->panels;
    if (p == NULL || p->threaded) {
        return p != NULL;
    }
    if (pthread_create(&p->thread, NULL, panelWorker, p) != 0) {
        LOGW("panels: no worker thread, drawing on the frame loop");
        return 0;
    }
    p->threaded = 1;
    LOGI("panels: drawn on a worker thread");
    return 1;
}

void xrPanelsFlush(XrCtx* ctx) {
    XrPanels* p = ctx->panels;
    if (p == NULL || !p->threaded) {
        return;
    }
    pthread_mutex_lock(&p->lock);
    while (p->busy || nextJob(p) >= 0) {
        pthread_cond_wait(&p->idle, &p->lock);
    }
    pthread_mutex_unlock(&p->lock);
    collect(ctx, p);
}

// The view a panel is drawn from: the live settings and what the context says
static void buildView(XrCtx* ctx, const Settings* live, UiView* v) {
    ui_view_init(v, live);
    v->session.head_aim = headAimSwitchOn(ctx->headAimSetting, ctx->headAimFlipped);
    v->session.pad_mode = ctx->padMode;
    v->session.ray = raySwitchOn(ctx->raySetting, ctx->rayFlipped);
    v->session.stereo_on = ctx->stereoLive;
    v->cog_tab = ctx->cogTab;
    // The room up, as the environment id the Room tab's rows are stored under
    int style = roomEffective(ctx);
    v->room_env = style >= ROOM_STYLE_FIRST && style <= ROOM_STYLE_LAST ? ENV_ID_HOME_THEATER + style - 1 : -1;
    v->curve_ok = ctx->cylinderSupported;
    v->stereo_ok = ctx->stereoMode != DEPTH_MODE_OFF;
    v->stereo_source = ctx->stereoSource;
    v->bar_show_aim = headAimCanAct(ctx);
    v->bar_show_stereo = ctx->stereoButtonReady;
    v->kb_sheet = ctx->kbState;
    v->kb_mods = ctx->kbMods | (ctx->hostChord.active ? ctx->hostChord.mods : 0);
    v->kb_layout = xrKeyboardLayout(ctx);
    v->kb_map = xrKeyboardMap(ctx);
    v->kb_oneshot = ctx->kbFull.oneshot | (ctx->hostChord.active ? ctx->hostChord.mods : 0);
    // the Compact keyboard's locked modifiers (KB_STICKY_SHIFT is KBF_LSHIFT's bit)
    v->kb_locked = v->kb_layout == KB_LAYOUT_FULL ? ctx->kbFull.locked : ctx->kbSticky.locked;
    v->kb_chord = ctx->hostChord.active;
    v->kb_chord_mods = ctx->hostChord.mods;
    v->kb_caps = ctx->kbFull.caps;
    v->kb_chip = !ctx->reportOpen;
    v->version = MLXR_VERSION;   // the top-level VERSION file, through CMake
    v->commit = NULL;
    v->log_path = "~/Games/MoonlightXRNative/moonlight.log";
    v->display_later = xrDisplayLaterMask(ctx);
    // M6: why the 3D is off (off in settings, no model, the model failed)
    v->stereo_reason = ctx->depthReason[0] ? ctx->depthReason : ui_str(UIS_THREE_D_OFF_IN_SETTINGS);
    v->env_live_cells = ctx->pickerCells;
    v->env_selected = -1;
    v->exit_question = ctx->launcher != LAUNCHER_NONE ? ui_str(UIS_NATIVE_EXIT_APP) : NULL;
    v->exit_disconnect = exitPromptHasDisconnect(ctx);
    v->pad = ctx->padLive;   // Native (lane wave3-cards): the pad card, and the first-run card's wording
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
    int tab, curveOk, stereoOk, showAim, roomEnv;
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
    k.roomEnv = v.room_env;
    k.hover = v.hover;
    k.later = v.display_later;
    if (!keyChanged(p, UI_PANEL_COG, &k, sizeof(k))) {
        return;
    }
    PanelJob j;
    jobPanel(&j, UI_PANEL_COG, &v);
    submit(ctx, p, &j);
}

static void updateClock(XrCtx* ctx, XrPanels* p, const char* clockText) {
    if (!ctx->cogOpen || clockText == NULL) {
        return;
    }
    // Native (lane wave3-hud): the time, and beside it the headset's battery and the slow connection badge
    struct {
        char time[32];
        int battery, charging, slow;
    } k;
    memset(&k, 0, sizeof(k));
    snprintf(k.time, sizeof(k.time), "%s", clockText);
    k.battery = ctx->batteryPresent ? ctx->batteryPercent : -1;
    k.charging = ctx->batteryPresent && ctx->batteryCharging;
    k.slow = ctx->slowConn != 0;
    if (!keyChanged(p, UI_PANEL_CLOCK, &k, sizeof(k))) {
        return;
    }
    PanelJob j;
    memset(&j, 0, sizeof(j));
    j.kind = JOB_CLOCK;
    j.slot = UI_PANEL_CLOCK;
    snprintf(j.text, sizeof(j.text), "%s", clockText);
    j.clockBattery = k.battery;
    j.clockCharging = k.charging;
    j.clockSlow = k.slow;
    submit(ctx, p, &j);
}

static void updateKeyboard(XrCtx* ctx, XrPanels* p, const UiView* base) {
    if (!ctx->kbOpen) {
        return;
    }
    int k[10] = { ctx->kbState, ctx->kbMods, ctx->kbLayoutShown, ctx->kbFull.oneshot,
                  ctx->kbFull.locked, ctx->kbFull.caps, ctx->reportOpen, ctx->kbSticky.locked,
                  ctx->hostChord.active, ctx->hostChord.mods };
    if (!keyChanged(p, UI_PANEL_KEYBOARD, k, sizeof(k))) {
        return;
    }
    UiView v = *base;
    v.hover.kind = UI_HIT_NONE;
    PanelJob j;
    jobPanel(&j, UI_PANEL_KEYBOARD, &v);
    submit(ctx, p, &j);
}

static void updateExit(XrCtx* ctx, XrPanels* p, const UiView* base) {
    if (!ctx->exitConfirmOpen && ctx->panelFades[FADE_EXIT].level <= 0.0f) {
        return;
    }
    int zone[2] = { ctx->exitHoverZone, ctx->launcher != LAUNCHER_NONE };
    if (!keyChanged(p, UI_PANEL_EXIT, zone, sizeof(zone))) {
        return;
    }
    UiView v = *base;
    v.hover.kind = zone[0] == EXIT_ZONE_NONE ? UI_HIT_NONE : UI_HIT_BUTTON;
    v.hover.a = zone[0] == EXIT_ZONE_EXIT ? UI_BTN_EXIT_CONFIRM
              : zone[0] == EXIT_ZONE_DISCONNECT ? UI_BTN_EXIT_DISCONNECT : UI_BTN_EXIT_CANCEL;
    PanelJob j;
    jobPanel(&j, UI_PANEL_EXIT, &v);
    submit(ctx, p, &j);
}

// Native: the Host sheet, per hovered zone, confirmation and macro list
static void updateHost(XrCtx* ctx, XrPanels* p, const UiView* base) {
    if (!ctx->hostOpen && ctx->panelFades[FADE_HOST].level <= 0.0f) {
        return;
    }
    struct {
        int zone, confirm, count;
        char names[XR_HOST_MACRO_SLOTS][XR_MACRO_NAME];
        char label[XR_MACRO_NAME];
    } k;
    memset(&k, 0, sizeof(k));
    k.zone = ctx->hostHoverZone;
    k.confirm = ctx->hostConfirm;
    k.count = ctx->macros.count < XR_HOST_MACRO_SLOTS ? ctx->macros.count : XR_HOST_MACRO_SLOTS;
    for (int i = 0; i < k.count; i++) {
        snprintf(k.names[i], sizeof(k.names[i]), "%s", ctx->macros.macro[i].name);
    }
    if (k.confirm >= 0) {
        XrHostSeq seq;
        int ask;
        xrHostCellSeq(k.confirm, &ctx->macros, &seq, &ask, k.label, sizeof(k.label));
    }
    if (!keyChanged(p, UI_PANEL_HOST, &k, sizeof(k))) {
        return;
    }
    UiView v = *base;
    v.hover.kind = UI_HIT_NONE;
    if (k.zone >= 0 && k.zone < XR_HOST_CELLS) {
        v.hover.kind = UI_HIT_HOST_CELL;
        v.hover.a = k.zone;
    }
    else if (k.zone == XR_HOST_ZONE_CLOSE || k.zone == XR_HOST_ZONE_CONFIRM_SEND
             || k.zone == XR_HOST_ZONE_CONFIRM_CANCEL || k.zone == XR_HOST_ZONE_PCVR) {
        v.hover.kind = UI_HIT_BUTTON;
        v.hover.a = k.zone == XR_HOST_ZONE_CLOSE ? UI_BTN_HOST_CLOSE
                  : k.zone == XR_HOST_ZONE_PCVR ? UI_BTN_HOST_PCVR
                  : k.zone == XR_HOST_ZONE_CONFIRM_SEND ? UI_BTN_HOST_CONFIRM : UI_BTN_HOST_CANCEL;
    }
    v.host_macros = k.count;
    for (int i = 0; i < k.count; i++) {
        snprintf(v.host_macro[i], sizeof(v.host_macro[i]), "%s", k.names[i]);
    }
    v.host_confirm = k.confirm;
    snprintf(v.host_confirm_label, sizeof(v.host_confirm_label), "%s", k.label);
    PanelJob j;
    jobPanel(&j, UI_PANEL_HOST, &v);
    submit(ctx, p, &j);
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
    UiView v = *base;
    v.hover.kind = UI_HIT_NONE;
    PanelJob j;
    jobPanel(&j, slot, &v);
    submit(ctx, p, &j);
}

// Native (lane wave3-cards): the first-run card, drawn when it comes up (and again if the
// gamepad shortcut, the gamepad-as-mouse chord or the pad's style it words changes), and the
// pad card, drawn again when what you could see of the pad changes
static void updateCards(XrCtx* ctx, XrPanels* p, const UiView* base) {
    if (ctx->firstRunUp) {
        struct { int toggle, chord, style; } k;
        memset(&k, 0, sizeof(k));
        k.toggle = base->settings->gamepad_toggle;
        k.chord = base->settings->pad_mouse_chord;
        k.style = base->pad.style;
        if (keyChanged(p, UI_PANEL_FIRSTRUN, &k, sizeof(k))) {
            UiView v = *base;
            v.hover.kind = UI_HIT_NONE;
            PanelJob j;
            jobPanel(&j, UI_PANEL_FIRSTRUN, &v);
            submit(ctx, p, &j);
        }
    }
    if (ctx->padCardUp) {
        PadLive q;
        padLiveQuantise(&base->pad, &q);
        struct { int style, buttons, lt, rt, lx, ly, rx, ry, touching, gyro; float tx, ty; } k;
        memset(&k, 0, sizeof(k));
        k.style = q.style;
        k.buttons = q.buttons;
        k.lt = q.lt;
        k.rt = q.rt;
        k.lx = q.lx;
        k.ly = q.ly;
        k.rx = q.rx;
        k.ry = q.ry;
        k.touching = q.touching;
        k.gyro = q.gyro;
        k.tx = q.touchX;
        k.ty = q.touchY;
        if (keyChanged(p, UI_PANEL_PADCARD, &k, sizeof(k))) {
            UiView v = *base;
            v.hover.kind = UI_HIT_NONE;
            v.pad = q;
            PanelJob j;
            jobPanel(&j, UI_PANEL_PADCARD, &v);
            submit(ctx, p, &j);
        }
    }
}

static void updateReport(XrCtx* ctx, XrPanels* p) {
    if (!ctx->reportOpen) {
        return;
    }
    struct {
        char note[512];
        int focus, hover, canSend;
    } k;
    memset(&k, 0, sizeof(k));
    snprintf(k.note, sizeof(k.note), "%s", p->reportNote);
    k.focus = p->reportFocus;
    k.hover = ctx->reportHoverZone;
    k.canSend = p->reportNote[0] != '\0';
    ctx->reportSendReady = k.canSend;
    if (!keyChanged(p, UI_PANEL_REPORT, &k, sizeof(k))) {
        return;
    }
    PanelJob j;
    memset(&j, 0, sizeof(j));
    j.kind = JOB_REPORT;
    j.slot = UI_PANEL_REPORT;
    memcpy(j.note, k.note, sizeof(j.note));
    j.focus = k.focus;
    j.hover = k.hover;
    j.canSend = k.canSend;
    submit(ctx, p, &j);
}

// The report form (XrReportForm.java, cut down): a key typed while the sheet
// is up goes into the field that has the keys, never to the host
void xrPanelsReportKey(XrCtx* ctx, int code) {
    XrPanels* p = ctx->panels;
    if (p == NULL) {
        return;
    }
    char* field = p->reportNote;
    size_t cap = sizeof(p->reportNote);
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
    else if (code == 13) {
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
        p->reportFocus = REPORT_ZONE_NOTE;
        ctx->reportReady = 0;
        p->drawn[UI_PANEL_REPORT] = 0; // drawn afresh for this opening
        dropSlot(p, UI_PANEL_REPORT);  // Native: nor a drawing of the last one landing
    }
    else if (event == REPORT_ZONE_NOTE) {
        p->reportFocus = event;
    }
    else if (event == REPORT_ZONE_SEND || event == REPORT_ZONE_ZIP) {
        // Native: there is no report server here; the note goes in the log and
        // in a file beside it, for the owner to pass on
        if (event == REPORT_ZONE_ZIP && p->reportNote[0] == '\0') {
            LOGEV("problem report from the headset: no note, the zip only");
        }
        else {
            LOGEV("problem report from the headset: \"%s\"", p->reportNote);
        }
        if (reportDir != NULL && (event == REPORT_ZONE_SEND || p->reportNote[0] != '\0')) {
            char path[512];
            time_t t = time(NULL);
            struct tm tm;
            localtime_r(&t, &tm);
            snprintf(path, sizeof(path), "%s/report-%04d%02d%02d-%02d%02d%02d.txt", reportDir,
                     tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
            FILE* f = fopen(path, "w");
            if (f != NULL) {
                fprintf(f, "%s\n", p->reportNote);
                fclose(f);
                LOGEV("problem report saved to %s", path);
            }
        }
        // Native (lane wave3-hud): Save and zip also makes the masked bug report, off this thread; xrPanelsReportPoll
        // says where it landed
        if (event == REPORT_ZONE_ZIP) {
            if (xrBugzipStart(reportDir)) {
                LOGEV("problem report: making the masked zip (bug-report.sh)");
            }
            else {
                xrPanelsNotice(ctx, ui_str(UIS_NATIVE_REPORT_ZIP_BUSY));
            }
        }
    }
}

// The zip run that ended since the last frame, on the toast (with the path on its second line) and in the log
void xrPanelsReportPoll(XrCtx* ctx) {
    int ok = 0;
    char result[512];
    if (!xrBugzipPoll(&ok, result, sizeof(result))) {
        return;
    }
    if (ok) {
        LOGEV("problem report zip: %s", result);
        const char* home = getenv("HOME");
        char shown[512];
        size_t hl = home != NULL ? strlen(home) : 0;
        if (hl > 1 && strncmp(result, home, hl) == 0 && result[hl] == '/') {
            snprintf(shown, sizeof(shown), "~%s", result + hl);
        }
        else {
            snprintf(shown, sizeof(shown), "%s", result);
        }
        xrPanelsNoticeMore(ctx, ui_str(UIS_NATIVE_REPORT_ZIP_DONE), shown);
    }
    else {
        LOGW("problem report zip failed: %s", result);
        xrPanelsNotice(ctx, ui_str(UIS_NATIVE_REPORT_ZIP_FAIL));
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
static char noticeMores[TOAST_TEXT_SLOTS][256];   // native (lane wave3-hud): the quieter line under it
static int noticeNext;

void xrPanelsNoticeMore(XrCtx* ctx, const char* text, const char* more) {
    int slot = noticeNext++ % TOAST_TEXT_SLOTS;
    snprintf(noticeTexts[slot], sizeof(noticeTexts[slot]), "%s", text);
    snprintf(noticeMores[slot], sizeof(noticeMores[slot]), "%s", more != NULL ? more : "");
    noticePush(&ctx->notices, TOAST_TEXT, slot);
    LOGI("toast: %s%s%s", text, more != NULL && more[0] ? " / " : "", more != NULL ? more : "");
}

void xrPanelsNotice(XrCtx* ctx, const char* text) {
    xrPanelsNoticeMore(ctx, text, NULL);
}

// A notice went up this frame (IN_TOAST): its words into the toast's slot
void xrPanelsToast(XrCtx* ctx, int kind, int arg) {
    XrPanels* p = ctx->panels;
    if (p == NULL || xrAtlasPixels(ctx) == NULL) {
        return;
    }
    PanelJob j;
    memset(&j, 0, sizeof(j));
    j.kind = JOB_TOAST;
    j.slot = UI_PANEL_TOAST;
    j.toastKind = kind;
    j.toastArg = arg;
    const char* more = "";
    if (kind == TOAST_TEXT) {
        int slot = (arg % TOAST_TEXT_SLOTS + TOAST_TEXT_SLOTS) % TOAST_TEXT_SLOTS;
        snprintf(j.text, sizeof(j.text), "%s", noticeTexts[slot]);
        more = noticeMores[slot];
    }
    else {
        toastText(ctx, kind, arg, j.text, sizeof(j.text), &more);
    }
    snprintf(j.more, sizeof(j.more), "%s", more);
    // (xr_layers shows the toast once the art drawn is this notice's kind
    // and argument, so the last toast's art never stands in while the worker
    // draws this one)
    submit(ctx, p, &j);
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
    PanelJob j;
    memset(&j, 0, sizeof(j));
    j.kind = JOB_OVERLAY;
    j.slot = ATLAS_OVERLAY;
    snprintf(j.text, sizeof(j.text), "%s", statsText);
    submit(ctx, p, &j);
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
        // Native: nothing to press until the new list's drawing lands
        ui_hits_clear(&p->launcherHits);
    }
    PanelJob j;
    memset(&j, 0, sizeof(j));
    j.kind = lv->mode == LAUNCHER_PCS ? JOB_PC_LIST : JOB_APP_LIST;
    j.slot = slot;
    j.page = lv->page;
    j.loading = lv->loading;
    j.failed = lv->failed;
    j.listHover = k.hover;
    j.count = k.count > 0 ? k.count : 0;
    if (lv->mode == LAUNCHER_PCS) {
        if (j.count > 0 && lv->pcs != NULL) {
            j.pcs = malloc(sizeof(UiPcItem) * (size_t)j.count);
            if (j.pcs == NULL) return;
            memcpy(j.pcs, lv->pcs, sizeof(UiPcItem) * (size_t)j.count);
        }
        else {
            j.count = 0;
        }
    }
    else {
        snprintf(j.pcName, sizeof(j.pcName), "%s", k.pcName);
        if (j.count > 0 && lv->apps != NULL) {
            j.apps = malloc(sizeof(UiAppItem) * (size_t)j.count);
            if (j.apps == NULL) return;
            memcpy(j.apps, lv->apps, sizeof(UiAppItem) * (size_t)j.count);
        }
        else {
            j.count = 0;
        }
    }
    submit(ctx, p, &j);
}

void xrPanelsUpdate(XrCtx* ctx, const Settings* live, const XrLauncherView* launcher,
                    const char* statsText, const char* clockText, int64_t now) {
    XrPanels* p = ctx->panels;
    if (p == NULL || xrAtlasPixels(ctx) == NULL) {
        return;
    }
    (void)now;
    // What the worker finished since the last frame goes up first
    collect(ctx, p);
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
    updateHost(ctx, p, &base);
    updateCards(ctx, p, &base);
    updateReport(ctx, p);
    xrPanelsReportPoll(ctx);
    updateOverlay(ctx, p, statsText);
    updateLauncher(ctx, p, launcher);
}
