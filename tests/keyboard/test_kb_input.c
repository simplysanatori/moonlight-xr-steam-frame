// Moonlight XR Native, tests/keyboard: the two keyboard layouts on the input
// side (xr_keyboard.c, kbKeyAt, kbPanelPose) through a headset-free context:
// every key of both layouts is reached by a ray and lands on the drawn key,
// the layout key flips and is stored in config.ini, a Full key becomes key
// events, and everything held is let go when the keyboard goes away, focus is
// lost or the stream ends. GPLv3.
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "scene.h"
#include "xr_keyboard.h"

#define MS 1000000LL

static XrPosef aimAt(Vec3 from, Vec3 target) {
    Vec3 d = vecNorm(vecSub(target, from));
    Vec3 z = { -d.x, -d.y, -d.z };
    Vec3 up = { 0, 1, 0 };
    Vec3 x = vecNorm(vecCross(up, z));
    Vec3 y = vecCross(z, x);
    XrPosef p = { quatFromBasis(x, y, z), { from.x, from.y, from.z } };
    return p;
}

static Vec3 onPlane(XrPosef pose, float w, float h, float u, float v) {
    Vec3 local = { (u - 0.5f) * w, (0.5f - v) * h, 0.0f };
    Vec3 off = quatRotate(pose.orientation, local);
    Vec3 p = { pose.position.x + off.x, pose.position.y + off.y, pose.position.z + off.z };
    return p;
}

static void fresh(float* out) {
    for (int i = 0; i < IN_SLOTS; i++) out[i] = 0.0f;
    out[IN_KEY] = -1.0f;
    out[IN_SETTING] = -1.0f;
}

static void openKeyboard(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->kbOpen = 1;
    c->kbState = KB_STATE_LOWER;
    xrKeyboardSync(c, NULL);
    c->kbPose = kbPanelPose(c, &c->kbW, &c->kbH);
}

// A ray from the hand at a point (u, v) of the keyboard comes back as that key
static int rayKey(XrCtx* c, float u, float v) {
    static const Vec3 HAND = { 0.2f, -0.3f, -0.25f };
    XrPosef aim = aimAt(HAND, onPlane(c->kbPose, c->kbW, c->kbH, u, v));
    float pu, pv;
    if (!screenProject(aim, c->kbPose, c->kbW, c->kbH, 0.0f, 0, &pu, &pv)) return -2;
    return kbKeyAt(c, pu, pv);
}

static void everyKey(Scene* s, int layout, int expectKeys) {
    XrCtx* c = s->ctx;
    openKeyboard(s);
    CHECK(xrKeyboardLayout(c) == layout);
    CHECK(c->kbKeyCount == expectKeys);
    scene_frame(s, NULL);
    UiView v;
    ui_view_init(&v, &s->live);
    v.kb_layout = layout;
    v.kb_chip = 1;
    static uint8_t px[KB_TEX_W * KB_TEX_H * 4];
    UiCanvas cv;
    UiHits hits;
    for (int sheet = 0; sheet < (layout == KB_LAYOUT_FULL ? 1 : KB_STATE_COUNT); sheet++) {
        c->kbState = sheet;
        v.kb_sheet = sheet;
        ui_cv_init(&cv, px, KB_TEX_W, KB_TEX_H);
        ui_cv_clear(&cv);
        ui_hits_clear(&hits);
        ui_draw_panel(UI_PANEL_KEYBOARD, &cv, &v, &hits);
        int seen = 0;
        for (int k = 0; k < c->kbKeyCount; k++) {
            if (c->kbCodes[sheet][k] == 0) continue;
            const float* r = &c->kbKeyRects[k * 4];
            float ku = (r[0] + r[2]) * 0.5f, kv = (r[1] + r[3]) * 0.5f;
            CHECK(kbKeyAt(c, ku, kv) == k);
            // the corners just inside it too
            CHECK(kbKeyAt(c, r[0] + 0.002f, r[1] + 0.004f) == k);
            CHECK(kbKeyAt(c, r[2] - 0.002f, r[3] - 0.004f) == k);
            // the drawn panel has the same key there
            int hi = ui_hits_at(&hits, ku, kv);
            CHECK(hi >= 0 && hits.hit[hi].kind == UI_HIT_KEY && hits.hit[hi].a == k
                  && hits.hit[hi].b == c->kbCodes[sheet][k]);
            // and a ray at it, through the headset's own projection, finds it
            CHECK(rayKey(c, ku, kv) == k);
            seen++;
        }
        CHECK(layout == KB_LAYOUT_FULL ? seen == 88 : seen == (sheet == KB_STATE_FN ? 35 : 44));
        // the gaps between rows hit nothing
        CHECK(kbKeyAt(c, 0.5f, 0.001f) == -1);
        CHECK(kbKeyAt(c, 0.001f, 0.5f) == -1);
    }
    // Compact: the chip beside the home row is the layout key; Full has none outside its keys
    if (layout == KB_LAYOUT_COMPACT) {
        const float* r = &c->kbKeyRects[KB_KEY_LAYOUT * 4];
        float ku = (r[0] + r[2]) * 0.5f, kv = (r[1] + r[3]) * 0.5f;
        CHECK(kbKeyAt(c, ku, kv) == KB_KEY_LAYOUT);
        CHECK(c->kbCodes[KB_STATE_LOWER][KB_KEY_LAYOUT] == KB_CODE_LAYOUT);
        CHECK(rayKey(c, ku, kv) == KB_KEY_LAYOUT);
        int hi = ui_hits_at(&hits, ku, kv);
        CHECK(hi >= 0 && hits.hit[hi].kind == UI_HIT_BUTTON && hits.hit[hi].a == UI_BTN_KB_LAYOUT);
    }
    else {
        CHECK(c->kbCodes[0][KB_KEY_LAYOUT] == 0);
    }
}

static const KbfKey* keyOf(XrCtx* c, int vk, int* index) {
    (void)c;
    *index = kbfFindVk(vk);
    return kbfKey(*index);
}

int main(void) {
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    float out[IN_SLOTS];

    // Compact is the default and is as it was: 44 keys, hit tables and all
    CHECK(c->kbLayout == KB_LAYOUT_COMPACT);
    everyKey(s, KB_LAYOUT_COMPACT, 44);
    float compactW = c->kbW;

    // The layout key on the Compact keyboard flips to Full and asks for it to be stored
    fresh(out);
    CHECK(xrKeyboardPress(c, out, KB_KEY_LAYOUT, 0, 0) == 1);
    CHECK(c->kbLayout == KB_LAYOUT_FULL && xrKeyboardLayout(c) == KB_LAYOUT_FULL);
    CHECK((int)out[IN_SETTING] == SETTING_KEYBOARD_LAYOUT && (int)out[IN_SETTING_VALUE] == KB_LAYOUT_FULL);
    CHECK(c->kbKeyCount == KBF_KEY_COUNT);
    CHECK(c->kbW > compactW * 1.1f);                 // hangs larger
    CHECK(out[IN_KEY] < 0.0f);                        // no key went to the host

    // config.ini: stored, written, read back, and the context starts on it
    Settings st;
    settings_defaults(&st);
    CHECK(xrStoreSetting(c, &st, (int)out[IN_SETTING], (int)out[IN_SETTING_VALUE]) == 1);
    CHECK(st.keyboard_layout == KB_LAYOUT_FULL);
    char path[256];
    snprintf(path, sizeof path, KB_TMP_DIR "/kb-test-%d.ini", (int)getpid());
    CHECK(settings_save(&st, path) == 0);
    Settings back;
    settings_defaults(&back);
    CHECK(settings_load(&back, path, NULL) == 0);
    CHECK(back.keyboard_layout == KB_LAYOUT_FULL);
    char* text = settings_to_text(&st);
    CHECK(strstr(text, "keyboard_layout = full") != NULL);
    CHECK(strstr(text, "keyboard_hold_ms") != NULL);
    free(text);
    unlink(path);
    XrCtx* c2 = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(c2, 2560, 1440, 90);
    xrApplySettings(c2, &back);
    CHECK(c2->kbLayout == KB_LAYOUT_FULL);
    Settings again = back;
    again.keyboard_layout = 0;
    xrSettingsFromCtx(c2, &again);
    CHECK(again.keyboard_layout == KB_LAYOUT_FULL);
    // the hold time is read too
    back.keyboard_hold_ms = 800;
    xrApplySettings(c2, &back);
    CHECK(c2->kbFull.holdNs == 800 * MS);
    free(c2);
    // the default is Compact, and a setting that is the default is not an active line
    settings_defaults(&back);
    text = settings_to_text(&back);
    CHECK(strstr(text, "#keyboard_layout = compact") != NULL);
    free(text);

    // Full: every key reached
    everyKey(s, KB_LAYOUT_FULL, KBF_KEY_COUNT);

    // The layout key inside Full flips back and stores it
    int lk = -1;
    for (int i = 0; i < KBF_KEY_COUNT; i++) if (kbfKey(i)->kind == KBF_LAYOUT) lk = i;
    fresh(out);
    CHECK(xrKeyboardPress(c, out, lk, 0, 0) == 1);
    CHECK(c->kbLayout == KB_LAYOUT_COMPACT && c->kbKeyCount == 44 && (int)out[IN_SETTING_VALUE] == 0);
    CHECK_NEAR(c->kbW, compactW, 1e-4);
    // a Compact letter is left to pressKey
    fresh(out);
    CHECK(xrKeyboardPress(c, out, 12, 0, 0) == 0);
    xrKeyboardSetLayout(c, out, KB_LAYOUT_FULL);

    // ---- a Full key as events -------------------------------------------------------
    openKeyboard(s);
    int ai, ctrl, shiftL, capsI;
    const KbfKey* a = keyOf(c, 0x41, &ai);
    keyOf(c, 0xA2, &ctrl);
    keyOf(c, 0xA0, &shiftL);
    keyOf(c, 0x14, &capsI);
    CHECK(a != NULL);
    int64_t now = 1000 * MS;
    c->triggerDown[0] = 1;
    fresh(out);
    CHECK(xrKeyboardPress(c, out, ai, 0, now) == 1);
    CHECK((int)out[IN_KEY] == KB_CODE_VK + 0x41 && (int)out[IN_KEY_MODS] == KBF_FLAG_DOWN);
    // held: nothing until the repeat delay, then repeats about 30 a second
    int repeats = 0;
    for (int i = 1; i <= 130; i++) {   // 1.45 s: the delay, then one second of repeats
        fresh(out);
        xrKeyboardFrame(c, out, now + i * 11111111LL, 1);
        if (out[IN_KEY] >= 0.0f) {
            CHECK((int)out[IN_KEY] == KB_CODE_VK + 0x41 && (int)out[IN_KEY_MODS] == KBF_FLAG_DOWN);
            CHECK(i * 11111111LL >= 450 * MS);
            repeats++;
        }
    }
    CHECK(repeats >= 29 && repeats <= 32);
    c->triggerDown[0] = 0;
    fresh(out);
    xrKeyboardFrame(c, out, now + 2000 * MS, 1);
    CHECK((int)out[IN_KEY] == KB_CODE_VK + 0x41 && (int)out[IN_KEY_MODS] == KBF_FLAG_UP);
    fresh(out);
    xrKeyboardFrame(c, out, now + 2011 * MS, 1);
    CHECK(out[IN_KEY] < 0.0f);

    // Ctrl one-shot: IN_KB_MODS carries it, the next key goes with it and then it drops
    c->triggerDown[0] = 1;
    fresh(out);
    xrKeyboardPress(c, out, ctrl, 0, now + 3000 * MS);
    xrKeyboardFrame(c, out, now + 3000 * MS, 1);
    CHECK(c->kbMods == KBF_LCTRL && out[IN_KEY] < 0.0f);
    c->triggerDown[0] = 0;
    fresh(out);
    xrKeyboardFrame(c, out, now + 3060 * MS, 1);
    CHECK(c->kbMods == KBF_LCTRL);                    // tapped: stays lit
    c->triggerDown[0] = 1;
    fresh(out);
    xrKeyboardPress(c, out, ai, 0, now + 3200 * MS);
    CHECK((int)out[IN_KEY_MODS] == (KBF_LCTRL | KBF_FLAG_DOWN));
    c->triggerDown[0] = 0;
    fresh(out);
    xrKeyboardFrame(c, out, now + 3260 * MS, 1);
    CHECK((int)out[IN_KEY_MODS] == (KBF_LCTRL | KBF_FLAG_UP) && c->kbMods == 0);

    // A long press on Shift locks it; the panel is told (lit, locked) and Caps toggles
    c->triggerDown[0] = 1;
    fresh(out);
    xrKeyboardPress(c, out, shiftL, 0, now + 4000 * MS);
    for (int i = 1; i <= 60; i++) {
        fresh(out);
        xrKeyboardFrame(c, out, now + 4000 * MS + i * 11111111LL, 1);
    }
    c->triggerDown[0] = 0;
    fresh(out);
    xrKeyboardFrame(c, out, now + 4800 * MS, 1);
    CHECK(c->kbFull.locked == KBF_LSHIFT && c->kbMods == KBF_LSHIFT);
    c->triggerDown[0] = 1;
    fresh(out);
    xrKeyboardPress(c, out, capsI, 0, now + 5000 * MS);
    CHECK(c->kbFull.caps == 1);
    c->triggerDown[0] = 0;
    fresh(out);
    xrKeyboardFrame(c, out, now + 5060 * MS, 1);

    // Nothing sticks: the panel redraws with the state, and each way out lets go
    scene_frame(s, NULL);
    for (int way = 0; way < 4; way++) {
        // Shift locked, a key held down
        c->kbOpen = 1;
        c->launcher = LAUNCHER_NONE;
        c->reportOpen = 0;
        xrKeyboardSync(c, NULL);
        KbFull* k = &c->kbFull;
        if (k->locked == 0) { k->locked = KBF_LSHIFT; }
        c->triggerDown[0] = 1;
        k->held = -1;
        fresh(out);
        xrKeyboardPress(c, out, ai, 0, now + 6000 * MS);
        c->kbMods = kbfDownMods(k);
        CHECK(c->kbMods == KBF_LSHIFT);
        fresh(out);
        int live = 1;
        if (way == 0) c->kbOpen = 0;                 // closed, dismissed, put away by gamepad mode
        if (way == 1) live = 0;                      // focus lost
        if (way == 2) c->launcher = 1;               // the stream ended: the list is up
        if (way == 3) { c->reportOpen = 1; }         // the report sheet takes over
        xrKeyboardFrame(c, out, now + 6100 * MS, live);
        if (way == 3) {
            // the held key comes up with the layout change, the lock is let go
            CHECK((int)out[IN_KEY] == KB_CODE_VK + 0x41);
        } else {
            CHECK((int)out[IN_KEY] == KB_CODE_VK + 0x41 && (int)out[IN_KEY_MODS] == (KBF_LSHIFT | KBF_FLAG_UP));
        }
        CHECK(c->kbMods == 0 && c->kbFull.locked == 0 && c->kbFull.oneshot == 0 && c->kbFull.held < 0);
        c->reportOpen = 0;
        xrKeyboardSync(c, NULL);
    }
    CHECK(c->kbFull.caps == 1);                      // the host's Caps is not ours to drop

    // The report sheet takes characters: Compact while it is up, Full again after
    openKeyboard(s);
    CHECK(xrKeyboardLayout(c) == KB_LAYOUT_FULL);
    c->reportOpen = 1;
    fresh(out);
    xrKeyboardFrame(c, out, now, 1);
    CHECK(xrKeyboardLayout(c) == KB_LAYOUT_COMPACT && c->kbKeyCount == 44);
    CHECK(c->kbCodes[0][KB_KEY_LAYOUT] == 0);        // no layout chip over the report
    CHECK(c->kbLayout == KB_LAYOUT_FULL);            // the choice is kept
    c->reportOpen = 0;
    fresh(out);
    xrKeyboardFrame(c, out, now, 1);
    CHECK(c->kbKeyCount == KBF_KEY_COUNT);

    // AZERTY and QWERTZ (keyboard_keymap): the context takes the map from the settings, the hit
    // tables are the map's, the report sheet's note field stays QWERTY (it reads a code as the
    // character it types), and the Full keyboard has the extra key beside the left Shift
    for (int map = KB_MAP_AZERTY; map <= KB_MAP_QWERTZ; map++) {
        s->settings.keyboard_layout = KB_LAYOUT_COMPACT;
        s->settings.keyboard_keymap = map;
        xrApplySettings(c, &s->settings);
        CHECK(c->kbMap == map);
        openKeyboard(s);
        CHECK(xrKeyboardMap(c) == map && c->kbKeyCount == 44);
        for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
            for (int k = 0; k < 44; k++) CHECK(c->kbCodes[sheet][k] == ui_kb_map_key_code(map, sheet, k));
        }
        CHECK(c->kbCodes[KB_STATE_LOWER][10] == ui_kb_map_key_code(map, KB_STATE_LOWER, 10));
        c->reportOpen = 1;
        fresh(out);
        xrKeyboardFrame(c, out, now, 1);
        CHECK(xrKeyboardMap(c) == KB_MAP_QWERTY);
        for (int sheet = 0; sheet < KB_STATE_COUNT; sheet++) {
            for (int k = 0; k < 44; k++) CHECK(c->kbCodes[sheet][k] == ui_kb_key_code(sheet, k));
        }
        c->reportOpen = 0;
        fresh(out);
        xrKeyboardFrame(c, out, now, 1);
        CHECK(xrKeyboardMap(c) == map && c->kbCodes[KB_STATE_LOWER][10] == ui_kb_map_key_code(map, KB_STATE_LOWER, 10));
        Settings back = s->settings;
        back.keyboard_keymap = 0;
        xrSettingsFromCtx(c, &back);
        CHECK(back.keyboard_keymap == map);

        s->settings.keyboard_layout = KB_LAYOUT_FULL;
        xrApplySettings(c, &s->settings);
        openKeyboard(s);
        CHECK(xrKeyboardLayout(c) == KB_LAYOUT_FULL && c->kbKeyCount == KBF_KEY_COUNT_MAX);
        const float* iso = &c->kbKeyRects[KBF_ISO_KEY * 4];
        CHECK(kbKeyAt(c, (iso[0] + iso[2]) * 0.5f, (iso[1] + iso[3]) * 0.5f) == KBF_ISO_KEY);
        CHECK(rayKey(c, (iso[0] + iso[2]) * 0.5f, (iso[1] + iso[3]) * 0.5f) == KBF_ISO_KEY);
        int shiftAt = kbfFindVk(0xA0);
        float plainShift[4];
        kbfKeyRect(shiftAt, plainShift);
        CHECK(c->kbKeyRects[shiftAt * 4 + 2] < plainShift[2]);
        fresh(out);
        CHECK(xrKeyboardPress(c, out, KBF_ISO_KEY, 0, now) == 1);
        CHECK((int)out[IN_KEY] == KB_CODE_VK + 0xE2 && (int)out[IN_KEY_MODS] == KBF_FLAG_DOWN);
        kbfInit(&c->kbFull);
    }
    s->settings.keyboard_layout = KB_LAYOUT_COMPACT;
    s->settings.keyboard_keymap = KB_MAP_QWERTY;
    xrApplySettings(c, &s->settings);
    openKeyboard(s);
    CHECK(c->kbKeyCount == 44 && c->kbCodes[KB_STATE_LOWER][11] == 'w');

    // The panel draws: the Full keyboard in its states differs from plain
    scene_frame(s, NULL);
    scene_free(s);
    return checksDone("kb input");
}
