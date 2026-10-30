// Moonlight XR Native: the headset keyboard's two layouts. See xr_keyboard.h.
// GPLv3.
#include "xr_keyboard.h"

#include "mlxr_log.h"
#include "panels.h"
#include "xr_host.h"

int xrKeyboardLayout(XrCtx* ctx) {
    return ctx->reportOpen ? KB_LAYOUT_COMPACT : ctx->kbLayout;
}

int xrKeyboardMap(XrCtx* ctx) {
    return ctx->reportOpen || ctx->kbMap < 0 || ctx->kbMap >= KB_MAP_COUNT ? KB_MAP_QWERTY : ctx->kbMap;
}

void xrKeyboardFillTables(XrCtx* ctx) {
    memset(ctx->kbKeyRects, 0, sizeof(ctx->kbKeyRects));
    memset(ctx->kbCodes, 0, sizeof(ctx->kbCodes));
    int layout = xrKeyboardLayout(ctx);
    int map = xrKeyboardMap(ctx);
    if (layout == KB_LAYOUT_FULL) {
        int keys = kbfKeyCountFor(map);
        ctx->kbKeyCount = keys;
        for (int k = 0; k < keys; k++) {
            kbfKeyRectFor(map, k, &ctx->kbKeyRects[k * 4]);
            for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
                ctx->kbCodes[sheet][k] = kbfKeyCode(k);
            }
        }
    }
    else {
        ctx->kbKeyCount = ui_kb_key_count();
        if (ctx->kbKeyCount > KB_KEY_LAYOUT) {
            ctx->kbKeyCount = KB_KEY_LAYOUT;
        }
        for (int k = 0; k < ctx->kbKeyCount; k++) {
            ui_kb_key_rect(k, &ctx->kbKeyRects[k * 4]);
            for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
                ctx->kbCodes[sheet][k] = ui_kb_map_key_code(map, sheet, k);
            }
        }
        // The chip beside the home row is hit-tested past the keys (kbKeyAt);
        // the report sheet has no use for it
        if (!ctx->reportOpen) {
            ui_kb_layout_chip_rect(&ctx->kbKeyRects[KB_KEY_LAYOUT * 4]);
            for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
                ctx->kbCodes[sheet][KB_KEY_LAYOUT] = KB_CODE_LAYOUT;
            }
        }
    }
    ctx->kbLayoutShown = layout | (ctx->reportOpen ? 2 : 0) | (map << 2);
}

// A key event into the key slot, if it is free. Returns 1 if it went.
static int emit(float* out, const KbfEvent* ev) {
    if (ev->kind != KBF_EV_DOWN && ev->kind != KBF_EV_UP) {
        return 1;
    }
    if (out == NULL || out[IN_KEY] >= 0.0f) {
        return 0;
    }
    out[IN_KEY] = (float)(KB_CODE_VK + ev->vk);
    out[IN_KEY_MODS] = (float)(ev->mods | (ev->kind == KBF_EV_DOWN ? KBF_FLAG_DOWN : KBF_FLAG_UP));
    return 1;
}

static void letGoOfAll(XrCtx* ctx, float* out) {
    KbFull* k = &ctx->kbFull;
    KbfEvent ev;
    if (k->held >= 0 || k->oneshot || k->locked) {
        if (k->held >= 0 && out != NULL && out[IN_KEY] >= 0.0f) {
            return;   // the slot is taken this frame; the next one lets go
        }
        kbfReleaseAll(k, &ev);
        emit(out, &ev);
        LOGI("full keyboard let go of everything");
    }
}

void xrKeyboardSync(XrCtx* ctx, float* out) {
    int layout = xrKeyboardLayout(ctx) | (ctx->reportOpen ? 2 : 0) | (xrKeyboardMap(ctx) << 2);
    if (layout == ctx->kbLayoutShown) {
        return;
    }
    if ((ctx->kbLayoutShown & 1) == KB_LAYOUT_FULL) {
        letGoOfAll(ctx, out);
        ctx->kbMods = 0;
    }
    ctx->kbState = KB_STATE_LOWER;
    ctx->kbHoverKey = -1;
    ctx->kbKeyDown = 0;
    xrKeyboardFillTables(ctx);
    if (ctx->kbOpen) {
        kbPlace(ctx);
    }
}

void xrKeyboardSetLayout(XrCtx* ctx, float* out, int layout) {
    layout = layout == KB_LAYOUT_FULL ? KB_LAYOUT_FULL : KB_LAYOUT_COMPACT;
    if (ctx->kbLayout != layout) {
        LOGI("keyboard layout %s", layout == KB_LAYOUT_FULL ? "full" : "compact");
        ctx->kbLayout = layout;
        if (out != NULL && out[IN_SETTING] < 0.0f) {
            out[IN_SETTING] = (float)SETTING_KEYBOARD_LAYOUT;
            out[IN_SETTING_VALUE] = (float)layout;
        }
    }
    xrKeyboardSync(ctx, out);
}

// "Send keys" is waiting for a combo (xr_hostkeys.h): a modifier lights, any
// other key completes it and the whole chord goes once, through the same path
// as a Host sheet cell. Returns 1 if the press was the chord's.
static int chordPress(XrCtx* ctx, int code) {
    int vk = 0, mods = 0;
    int r = xrHostChordKey(&ctx->hostChord, code, &vk, &mods);
    if (r == XR_CHORD_MOD) {
        LOGI("send keys: modifiers 0x%02x", ctx->hostChord.mods);
        return 1;
    }
    if (r == XR_CHORD_SEND) {
        XrHostSeq seq;
        memset(&seq, 0, sizeof(seq));
        seq.count = 1;
        seq.step[0].kind = XR_HOST_STEP_TAP;
        seq.step[0].vk = vk;
        seq.step[0].mods = mods;
        xrHostRunStart(&ctx->hostRun, &seq);
        LOGEV("send keys: vk 0x%02x with modifiers 0x%02x", vk, mods);
        return 1;
    }
    return 0;
}

int xrKeyboardPress(XrCtx* ctx, float* out, int key, int hand, int64_t now) {
    if (key < 0 || key >= KB_MAX_KEYS) {
        return 0;
    }
    int code = ctx->kbCodes[ctx->kbState][key];
    if (code == KB_CODE_LAYOUT) {
        xrKeyboardSetLayout(ctx, out, ctx->kbLayout == KB_LAYOUT_FULL ? KB_LAYOUT_COMPACT
                                                                       : KB_LAYOUT_FULL);
        return 1;
    }
    if (ctx->hostChord.active && !ctx->reportOpen && chordPress(ctx, code)) {
        return 1;
    }
    if ((ctx->kbLayoutShown & 1) != KB_LAYOUT_FULL) {
        // Native (PLAN 4b.13): a modifier pressed starts its hold clock for the
        // lock (xrKeyboardFrame), and a tap on a locked one lets it go
        if (!ctx->reportOpen) {
            ctx->kbStickyHand = hand;
            kbStickyPress(&ctx->kbSticky, code, now);
        }
        return 0;
    }
    KbFull* k = &ctx->kbFull;
    KbfEvent ev;
    int before = kbfDownMods(k);
    kbfPress(k, key, hand, now, &ev);
    emit(out, &ev);
    ctx->kbMods = kbfDownMods(k);
    if (ctx->kbMods != before) {
        LOGI("full keyboard modifiers 0x%02x -> 0x%02x", before, ctx->kbMods);
    }
    return 1;
}

// Native (PLAN 4b.13): the Compact keyboard's locked modifiers. A modifier held
// for the Full keyboard's hold time locks and stays lit through the keys typed
// after it; a locked Shift keeps the capitals sheet.
static void compactSticky(XrCtx* ctx, int64_t now, int live) {
    KbSticky* s = &ctx->kbSticky;
    if (!ctx->kbOpen || !live || ctx->reportOpen || ctx->launcher != LAUNCHER_NONE) {
        kbStickyInit(s);
        ctx->kbStickyHand = -1;
        return;
    }
    int hand = ctx->kbStickyHand;
    int down = hand >= 0 && hand < SRC_COUNT && ctx->triggerDown[hand];
    int bit = kbStickyTick(s, down, (long long)now, (long long)ctx->kbFull.holdNs);
    if (bit != 0) {
        LOGI("keyboard modifier 0x%02x locked", bit);
    }
    ctx->kbMods = kbStickyMods(s, ctx->kbMods);
    ctx->kbState = kbStickySheet(s, ctx->kbState);
}

void xrKeyboardFrame(XrCtx* ctx, float* out, int64_t now, int live) {
    xrKeyboardSync(ctx, out);
    xrHostFrame(ctx, out, now, live);
    if ((ctx->kbLayoutShown & 1) != KB_LAYOUT_FULL) {
        compactSticky(ctx, now, live);
        return;
    }
    KbFull* k = &ctx->kbFull;
    if (!ctx->kbOpen || !live || ctx->launcher != LAUNCHER_NONE) {
        letGoOfAll(ctx, out);
        ctx->kbMods = kbfDownMods(k);
        return;
    }
    int before = kbfDownMods(k);
    if (k->held >= 0) {
        KbfEvent ev;
        int hand = k->hand;
        int stillDown = hand >= 0 && hand < SRC_COUNT && ctx->triggerDown[hand];
        if (!stillDown) {
            if (out == NULL || out[IN_KEY] < 0.0f) {
                kbfRelease(k, now, &ev);
                emit(out, &ev);
            }
        }
        else {
            kbfTick(k, now, &ev);
            emit(out, &ev);
        }
    }
    ctx->kbMods = kbfDownMods(k);
    if (ctx->kbMods != before) {
        LOGI("full keyboard modifiers 0x%02x -> 0x%02x", before, ctx->kbMods);
    }
}
