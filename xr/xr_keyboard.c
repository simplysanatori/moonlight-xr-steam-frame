// Moonlight XR Native: the headset keyboard's two layouts. See xr_keyboard.h.
// GPLv3.
#include "xr_keyboard.h"

#include "mlxr_log.h"
#include "panels.h"

int xrKeyboardLayout(XrCtx* ctx) {
    return ctx->reportOpen ? KB_LAYOUT_COMPACT : ctx->kbLayout;
}

void xrKeyboardFillTables(XrCtx* ctx) {
    memset(ctx->kbKeyRects, 0, sizeof(ctx->kbKeyRects));
    memset(ctx->kbCodes, 0, sizeof(ctx->kbCodes));
    int layout = xrKeyboardLayout(ctx);
    if (layout == KB_LAYOUT_FULL) {
        ctx->kbKeyCount = KBF_KEY_COUNT;
        for (int k = 0; k < KBF_KEY_COUNT; k++) {
            kbfKeyRect(k, &ctx->kbKeyRects[k * 4]);
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
                ctx->kbCodes[sheet][k] = ui_kb_key_code(sheet, k);
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
    ctx->kbLayoutShown = layout | (ctx->reportOpen ? 2 : 0);
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
    int layout = xrKeyboardLayout(ctx) | (ctx->reportOpen ? 2 : 0);
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
        ctx->kbPose = kbPanelPose(ctx, &ctx->kbW, &ctx->kbH);
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
    if ((ctx->kbLayoutShown & 1) != KB_LAYOUT_FULL) {
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

void xrKeyboardFrame(XrCtx* ctx, float* out, int64_t now, int live) {
    xrKeyboardSync(ctx, out);
    if ((ctx->kbLayoutShown & 1) != KB_LAYOUT_FULL) {
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
