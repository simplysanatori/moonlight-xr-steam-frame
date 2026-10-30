// The in world keyboard's sheets and its held Ctrl, Alt and Win
#include "check.h"
#include "xr_keys.h"

// The codes a press on each kind of key carries
#define KEY_C 'c'
#define KEY_D 'd'
#define KEY_TAB 9
#define KEY_F4 (KB_CODE_VK + 0x73)
#define KEY_ESC (KB_CODE_VK + 0x1B)

static void testSheets(void) {
    // ?123 to the symbols, Fn to its own sheet, ?123 in Fn's place back, and
    // ABC from either to the letters
    KbPress p = kbPress(KB_STATE_LOWER, 0, KB_CODE_SYMBOLS);
    CHECK(p.sheet == KB_STATE_SYMBOLS);
    p = kbPress(p.sheet, 0, KB_CODE_FN);
    CHECK(p.sheet == KB_STATE_FN);
    CHECK(kbPress(KB_STATE_FN, 0, KB_CODE_FN).sheet == KB_STATE_SYMBOLS);
    CHECK(kbPress(KB_STATE_FN, 0, KB_CODE_SYMBOLS).sheet == KB_STATE_LOWER);
    CHECK(kbPress(KB_STATE_SYMBOLS, 0, KB_CODE_SYMBOLS).sheet == KB_STATE_LOWER);
    CHECK(kbPress(KB_STATE_UPPER, 0, KB_CODE_SYMBOLS).sheet == KB_STATE_SYMBOLS);
    // None of them types or touches the modifiers
    p = kbPress(KB_STATE_SYMBOLS, KB_MOD_CTRL, KB_CODE_FN);
    CHECK(p.code == -1);
    CHECK(p.mods == KB_MOD_CTRL);
    CHECK(!p.hide);

    // Shift as it always was: one shot over the letters, sticky over the row
    // above them
    p = kbPress(KB_STATE_LOWER, 0, KB_CODE_SHIFT);
    CHECK(p.sheet == KB_STATE_UPPER);
    CHECK(kbPress(KB_STATE_UPPER, 0, KB_CODE_SHIFT).sheet == KB_STATE_LOWER);
    p = kbPress(KB_STATE_UPPER, 0, 'Q');
    CHECK(p.code == 'Q');
    CHECK(p.sheet == KB_STATE_LOWER);
    p = kbPress(KB_STATE_UPPER, 0, '!');
    CHECK(p.code == '!');
    CHECK(p.sheet == KB_STATE_UPPER);
}

static void testModifiers(void) {
    // Ctrl+C: lit on the Fn sheet, across to the letters, c goes with it and
    // it goes out
    KbPress p = kbPress(KB_STATE_FN, 0, KB_CODE_CTRL);
    CHECK(p.mods == KB_MOD_CTRL);
    CHECK(p.code == -1);
    p = kbPress(p.sheet, p.mods, KB_CODE_SYMBOLS);
    CHECK(p.sheet == KB_STATE_LOWER);
    CHECK(p.mods == KB_MOD_CTRL);
    p = kbPress(p.sheet, p.mods, KEY_C);
    CHECK(p.code == KEY_C);
    CHECK(p.codeMods == KB_MOD_CTRL);
    CHECK(p.mods == 0);

    // Alt+Tab, both on the Fn sheet
    p = kbPress(KB_STATE_FN, 0, KB_CODE_ALT);
    p = kbPress(p.sheet, p.mods, KEY_TAB);
    CHECK(p.code == KEY_TAB);
    CHECK(p.codeMods == KB_MOD_ALT);
    CHECK(p.mods == 0);
    CHECK(p.sheet == KB_STATE_FN);

    // Win+D
    p = kbPress(KB_STATE_FN, 0, KB_CODE_WIN);
    p = kbPress(p.sheet, p.mods, KB_CODE_SYMBOLS);
    p = kbPress(p.sheet, p.mods, KEY_D);
    CHECK(p.code == KEY_D);
    CHECK(p.codeMods == KB_MOD_WIN);
    CHECK(p.mods == 0);

    // Pressed again it goes out with nothing sent
    p = kbPress(KB_STATE_FN, KB_MOD_WIN, KB_CODE_WIN);
    CHECK(p.mods == 0);
    CHECK(p.code == -1);

    // Ctrl and Alt together, then F4
    p = kbPress(KB_STATE_FN, 0, KB_CODE_CTRL);
    p = kbPress(p.sheet, p.mods, KB_CODE_ALT);
    CHECK(p.mods == (KB_MOD_CTRL | KB_MOD_ALT));
    p = kbPress(p.sheet, p.mods, KEY_F4);
    CHECK(p.code == KEY_F4);
    CHECK(p.codeMods == (KB_MOD_CTRL | KB_MOD_ALT));
    CHECK(p.mods == 0);

    // Ctrl, then shift for a capital: Ctrl+Shift+T goes as one
    p = kbPress(KB_STATE_LOWER, KB_MOD_CTRL, KB_CODE_SHIFT);
    CHECK(p.mods == KB_MOD_CTRL);
    p = kbPress(p.sheet, p.mods, 'T');
    CHECK(p.code == 'T');
    CHECK(p.codeMods == KB_MOD_CTRL);
    CHECK(p.sheet == KB_STATE_LOWER);
    CHECK(p.mods == 0);

    // A key with nothing lit goes with nothing
    p = kbPress(KB_STATE_FN, 0, KEY_ESC);
    CHECK(p.code == KEY_ESC);
    CHECK(p.codeMods == 0);

    // Hiding the keyboard lets go of them
    p = kbPress(KB_STATE_LOWER, KB_MOD_CTRL | KB_MOD_WIN, KB_CODE_HIDE);
    CHECK(p.hide);
    CHECK(p.mods == 0);
    CHECK(p.code == -1);

    // A blank does nothing at all
    p = kbPress(KB_STATE_FN, KB_MOD_ALT, 0);
    CHECK(p.code == -1);
    CHECK(p.mods == KB_MOD_ALT);
    CHECK(p.sheet == KB_STATE_FN);

    CHECK(kbModifierBit(KB_CODE_CTRL) == KB_MOD_CTRL);
    CHECK(kbModifierBit(KB_CODE_ALT) == KB_MOD_ALT);
    CHECK(kbModifierBit(KB_CODE_WIN) == KB_MOD_WIN);
    CHECK(kbModifierBit(KB_CODE_SHIFT) == 0);
    CHECK(kbModifierBit('c') == 0);
}

static void testCodes(void) {
    // The codes ride in floats, and the highest virtual key has to come back
    // exactly
    float top = (float)(KB_CODE_VK + 0xFF);
    CHECK((int)top == KB_CODE_VK + 0xFF);
    // And the bits are the host protocol's own
    CHECK(KB_MOD_CTRL == 0x02);
    CHECK(KB_MOD_ALT == 0x04);
    CHECK(KB_MOD_WIN == 0x08);
}

// The media keys row (PLAN 3.11): six host keys, one code each, in a row under the main block
static void testMedia(void) {
    static const int VK[KB_MEDIA_KEYS] = { 0xB1, 0xB3, 0xB0, 0xAD, 0xAE, 0xAF };
    for (int i = 0; i < KB_MEDIA_KEYS; i++) {
        CHECK(kbMediaVk(i) == VK[i]);
        CHECK(kbMediaCode(i) == KB_CODE_VK + VK[i]);
        CHECK(kbMediaIndex(kbMediaCode(i)) == i);
        // typed whole, with no modifier of its own
        KbPress p = kbPress(KB_STATE_LOWER, 0, kbMediaCode(i));
        CHECK(p.code == kbMediaCode(i) && p.codeMods == 0 && p.sheet == KB_STATE_LOWER);
        float r[4];
        kbMediaRect(i, r);
        CHECK(r[0] >= 0.0f && r[2] <= 1.0f && r[1] >= 0.0f && r[3] <= 1.0f && r[2] > r[0] && r[3] > r[1]);
        if (i > 0) {
            float q[4];
            kbMediaRect(i - 1, q);
            CHECK(q[2] <= r[0] && q[1] == r[1] && q[3] == r[3]);
        }
    }
    CHECK(kbMediaVk(-1) == 0 && kbMediaVk(KB_MEDIA_KEYS) == 0 && kbMediaCode(KB_MEDIA_KEYS) == 0);
    CHECK(kbMediaIndex('a') == -1 && kbMediaIndex(KB_CODE_VK + 0x41) == -1);
    // the row is under the main block, which was laid out against KB_MAIN_TEX_H rows
    float first[4], low[4];
    kbMediaRect(0, first);
    CHECK_NEAR(kbMainV(1.0f), (double)KB_MAIN_TEX_H / KB_TEX_H, 1e-6);
    low[3] = kbMainV(0.963f);
    CHECK(first[1] > low[3]);
    CHECK(KB_TEX_H > KB_MAIN_TEX_H);
}

// Sticky modifiers on the Compact keyboard (PLAN 4b.13): tap lights one for the next key, hold
// locks it, a tap on a locked one lets it go; the same for Shift as the capitals sheet
static void testSticky(void) {
    const long long HOLD = 500000000LL;
    KbSticky s;
    kbStickyInit(&s);
    CHECK(kbStickyBit(KB_CODE_CTRL) == KB_MOD_CTRL && kbStickyBit(KB_CODE_ALT) == KB_MOD_ALT
          && kbStickyBit(KB_CODE_WIN) == KB_MOD_WIN && kbStickyBit(KB_CODE_SHIFT) == KB_STICKY_SHIFT
          && kbStickyBit('c') == 0 && kbStickyBit(KB_CODE_FN) == 0);
    CHECK(KB_STICKY_SHIFT == 1);

    // A tap: lit by kbPress as ever, never locked
    KbPress p = kbPress(KB_STATE_FN, 0, KB_CODE_CTRL);
    CHECK(kbStickyPress(&s, KB_CODE_CTRL, 0) == 0 && p.mods == KB_MOD_CTRL);
    CHECK(kbStickyTick(&s, 1, HOLD / 2, HOLD) == 0);
    CHECK(kbStickyTick(&s, 0, HOLD / 2 + 1, HOLD) == 0);     // let go before the hold time
    CHECK(kbStickyTick(&s, 1, 10 * HOLD, HOLD) == 0 && s.locked == 0);
    CHECK(kbStickyMods(&s, p.mods) == KB_MOD_CTRL);
    // ...and goes out with the next key
    p = kbPress(KB_STATE_LOWER, p.mods, 'c');
    CHECK(p.codeMods == KB_MOD_CTRL && p.mods == 0 && kbStickyMods(&s, p.mods) == 0);

    // A hold: locks at the hold time, not before, and says which bit
    kbStickyInit(&s);
    p = kbPress(KB_STATE_FN, 0, KB_CODE_CTRL);
    kbStickyPress(&s, KB_CODE_CTRL, 1000);
    CHECK(kbStickyTick(&s, 1, 1000 + HOLD - 1, HOLD) == 0);
    CHECK(kbStickyTick(&s, 1, 1000 + HOLD, HOLD) == KB_MOD_CTRL);
    CHECK(s.locked == KB_MOD_CTRL);
    CHECK(kbStickyTick(&s, 1, 1000 + 2 * HOLD, HOLD) == 0);   // once
    // it stays lit through the keys typed after it, Ctrl+C then Ctrl+V
    for (int i = 0; i < 2; i++) {
        p = kbPress(KB_STATE_LOWER, kbStickyMods(&s, p.mods), i ? 'v' : 'c');
        CHECK(p.codeMods == KB_MOD_CTRL);
        p.mods = kbStickyMods(&s, p.mods);
        CHECK(p.mods == KB_MOD_CTRL);
    }
    // a tap on it lets it go, and kbPress puts the light out
    CHECK(kbStickyPress(&s, KB_CODE_CTRL, 5 * HOLD) == 1 && s.locked == 0);
    p = kbPress(KB_STATE_FN, p.mods, KB_CODE_CTRL);
    CHECK(p.mods == 0 && kbStickyMods(&s, p.mods) == 0);

    // Holding a modifier that is already lit (a second touch cancels it, the hold then locks it)
    kbStickyInit(&s);
    p = kbPress(KB_STATE_FN, 0, KB_CODE_ALT);                 // tapped before: lit
    p = kbPress(KB_STATE_FN, p.mods, KB_CODE_ALT);            // pressed again: kbPress puts it out
    kbStickyPress(&s, KB_CODE_ALT, 0);
    CHECK(p.mods == 0);
    CHECK(kbStickyTick(&s, 1, HOLD, HOLD) == KB_MOD_ALT);
    CHECK(kbStickyMods(&s, p.mods) == KB_MOD_ALT);            // locked, so lit again

    // Two at once, and each its own
    kbStickyPress(&s, KB_CODE_WIN, 0);
    CHECK(kbStickyTick(&s, 1, HOLD, HOLD) == KB_MOD_WIN);
    CHECK(kbStickyMods(&s, 0) == (KB_MOD_ALT | KB_MOD_WIN));
    CHECK(kbStickyPress(&s, KB_CODE_ALT, 2 * HOLD) == 1);
    CHECK(kbStickyMods(&s, 0) == KB_MOD_WIN);

    // A press on another key drops the hold in progress
    kbStickyInit(&s);
    kbStickyPress(&s, KB_CODE_CTRL, 0);
    kbStickyPress(&s, 'x', 10);
    CHECK(kbStickyTick(&s, 1, 10 * HOLD, HOLD) == 0 && s.locked == 0);

    // Shift: the capitals sheet stays through the keys typed
    kbStickyInit(&s);
    p = kbPress(KB_STATE_LOWER, 0, KB_CODE_SHIFT);
    kbStickyPress(&s, KB_CODE_SHIFT, 0);
    CHECK(p.sheet == KB_STATE_UPPER);
    CHECK(kbStickyTick(&s, 1, HOLD, HOLD) == KB_STICKY_SHIFT);
    CHECK(kbStickyMods(&s, 0) == 0);                          // a sheet, not a lit modifier
    p = kbPress(KB_STATE_UPPER, 0, 'Q');
    CHECK(p.sheet == KB_STATE_LOWER && kbStickySheet(&s, p.sheet) == KB_STATE_UPPER);
    CHECK(kbStickySheet(&s, KB_STATE_SYMBOLS) == KB_STATE_SYMBOLS);   // only the lower sheet is lifted
    CHECK(kbStickySheet(&s, KB_STATE_FN) == KB_STATE_FN);
    // a tap on it ends it
    CHECK(kbStickyPress(&s, KB_CODE_SHIFT, 3 * HOLD) == 1 && s.locked == 0);
    p = kbPress(KB_STATE_UPPER, 0, KB_CODE_SHIFT);
    CHECK(p.sheet == KB_STATE_LOWER && kbStickySheet(&s, p.sheet) == KB_STATE_LOWER);
}

int main(void) {
    testSheets();
    testModifiers();
    testCodes();
    testMedia();
    testSticky();
    return checksDone("xr_keys");
}
