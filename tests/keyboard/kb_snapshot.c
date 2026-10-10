// Moonlight XR Native, tests/keyboard: mlxr-kb-snapshot DIR. The keyboard
// panel flat at its native size in each state (Compact with its layout chip;
// Full plain, Shift one-shot, Ctrl locked, Caps on, Win+Alt) and the Full
// keyboard in the space under the picture with a key under the ray. GPLv3.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "scene.h"
#include "xr_keyboard.h"

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

static const char* g_dir;

static void flat(Settings* st, const char* name, const UiView* base) {
    static uint8_t px[KB_TEX_W * KB_TEX_H * 4];
    static uint8_t out[KB_TEX_W * KB_TEX_H * 4];
    UiCanvas cv;
    ui_cv_init(&cv, px, KB_TEX_W, KB_TEX_H);
    ui_cv_clear(&cv);
    UiView v = *base;
    (void)st;
    ui_draw_panel(UI_PANEL_KEYBOARD, &cv, &v, NULL);
    // premultiplied over a dark floor, as the compositor shows it
    for (int i = 0; i < KB_TEX_W * KB_TEX_H; i++) {
        float a = px[i * 4 + 3] / 255.0f;
        for (int c = 0; c < 3; c++) {
            float bg = 40.0f;
            out[i * 4 + c] = (uint8_t)fminf(255.0f, px[i * 4 + c] + bg * (1.0f - a));
        }
        out[i * 4 + 3] = 255;
    }
    char path[600];
    snprintf(path, sizeof path, "%s/%s.png", g_dir, name);
    stbi_write_png(path, KB_TEX_W, KB_TEX_H, 4, out, KB_TEX_W * 4);
    printf("%s\n", path);
}

int main(int argc, char** argv) {
    g_dir = argc > 1 ? argv[1] : "kb-snapshots";
    mkdir(g_dir, 0755);
    Settings st;
    settings_defaults(&st);
    UiView v;
    ui_view_init(&v, &st);
    v.kb_chip = 1;
    flat(&st, "kb-compact-chip", &v);
    v.kb_sheet = KB_STATE_FN;
    flat(&st, "kb-compact-fn-chip", &v);
    v.kb_sheet = KB_STATE_LOWER;
    v.kb_layout = KB_LAYOUT_FULL;
    flat(&st, "kb-full-plain", &v);
    v.kb_oneshot = KBF_LSHIFT;
    flat(&st, "kb-full-shift-oneshot", &v);
    v.kb_oneshot = KBF_RSHIFT | KBF_LWIN;
    flat(&st, "kb-full-rshift-win-oneshot", &v);
    v.kb_oneshot = 0;
    v.kb_locked = KBF_LCTRL;
    v.kb_oneshot = KBF_LSHIFT;
    flat(&st, "kb-full-ctrl-locked-shift", &v);
    v.kb_locked = 0;
    v.kb_oneshot = 0;
    v.kb_caps = 1;
    flat(&st, "kb-full-caps", &v);
    v.kb_oneshot = KBF_LSHIFT;
    flat(&st, "kb-full-caps-shift", &v);

    // In the space, with the ray on a key
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    s->settings.keyboard_layout = KB_LAYOUT_FULL;
    xrApplySettings(c, &s->settings);
    scene_reset(s);
    c->kbOpen = 1;
    xrKeyboardSync(c, NULL);
    c->kbPose = kbPanelPose(c, &c->kbW, &c->kbH);
    c->kbFull.oneshot = KBF_LCTRL;
    c->kbFull.locked = KBF_RSHIFT;
    c->kbMods = c->kbFull.oneshot | c->kbFull.locked;
    int k = kbfFindVk(0x45);
    c->kbHoverKey = k;
    c->kbKeyDown = 1;
    const float* r = &c->kbKeyRects[k * 4];
    Vec3 local = { ((r[0] + r[2]) * 0.5f - 0.5f) * c->kbW, (0.5f - (r[1] + r[3]) * 0.5f) * c->kbH, 0.0f };
    Vec3 off = quatRotate(c->kbPose.orientation, local);
    Vec3 target = { c->kbPose.position.x + off.x, c->kbPose.position.y + off.y, c->kbPose.position.z + off.z };
    Vec3 hand = { 0.25f, -0.35f, -0.3f };
    scene_ray_to(s, hand, target);
    scene_frame(s, NULL);
    char path[600];
    snprintf(path, sizeof path, "%s/kb-full-in-space.png", g_dir);
    int quads = scene_render(s, 1600, 1200, 84.0f, path);
    printf("%s (%d quads, %u layers)\n", path, quads, s->out.count);
    scene_free(s);
    return 0;
}
