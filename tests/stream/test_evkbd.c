// Moonlight XR Native: key map checks, and with --scan a 3 s keyboard scan
// (no grab, nothing forwarded). GPLv3.
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../stream/evdev_keyboard.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void sink(short vk, int down, char mods, void* u) { (void)vk; (void)down; (void)mods; (void)u; }

int main(int argc, char** argv) {
    int ext;
    // Linux key codes follow the QWERTY layout, not the alphabet.
    static const int letters[26] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
                                    KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
                                    KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    for (int i = 0; i < 26; i++) {
        CHECK(evkbd_vk(letters[i], &ext) == 'A' + i);
        CHECK(!ext);
    }
    CHECK(evkbd_vk(KEY_Q, NULL) == 'Q' && evkbd_vk(KEY_M, NULL) == 'M' && evkbd_vk(KEY_A, NULL) == 'A');
    CHECK(evkbd_vk(KEY_1, NULL) == '1' && evkbd_vk(KEY_0, NULL) == '0');
    CHECK(evkbd_vk(KEY_ENTER, &ext) == 0x0D && !ext);
    CHECK(evkbd_vk(KEY_KPENTER, &ext) == 0x0D && ext);
    CHECK(evkbd_vk(KEY_LEFT, &ext) == 0x25 && ext);
    CHECK(evkbd_vk(KEY_LEFTMETA, &ext) == 0x5B && ext);
    CHECK(evkbd_vk(KEY_ESC, &ext) == 0x1B && !ext);
    CHECK(evkbd_vk(KEY_F12, NULL) == 0x7B);
    CHECK(evkbd_vk(KEY_SPACE, NULL) == 0x20);
    CHECK(evkbd_vk(KEY_MAX + 5, NULL) == 0);
    if (argc > 1 && !strcmp(argv[1], "--scan")) {
        evkbd_start(sink, NULL);
        sleep(3);
        evkbd_stop();
    }
    printf("evdev keyboard: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
