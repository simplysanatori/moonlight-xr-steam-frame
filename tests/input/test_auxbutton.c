// Native (lane a11y-2): the headset button's evdev reader: the device found by name and key
// capability in a made-up sysfs tree (not by event number), an unreadable one reported as such,
// KEY_SELECT presses and releases read through a pipe standing in for the device, other keys and
// repeats skipped, the grab taken on open and let go on close, and a device that goes away closed.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input.h>

#include "auxbutton.h"
#include "check.h"

static void writeFile(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

// <root>/sys/class/input/eventN/device/{name,capabilities/key}, and <root>/dev/eventN
static void fakeDevice(const char* root, int n, const char* name, const char* caps, int mode) {
    char p[512];
    snprintf(p, sizeof p, "%s/sys/class/input/event%d/device/capabilities", root, n);
    char cmd[600];
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", p);
    CHECK(system(cmd) == 0);
    snprintf(p, sizeof p, "%s/sys/class/input/event%d/device/name", root, n);
    writeFile(p, name);
    snprintf(p, sizeof p, "%s/sys/class/input/event%d/device/capabilities/key", root, n);
    writeFile(p, caps);
    snprintf(p, sizeof p, "%s/dev", root);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/dev/event%d", root, n);
    writeFile(p, "");
    chmod(p, (mode_t)mode);
}

// KEY_SELECT is 353: word 5 bit 33 with 64-bit words, word 11 bit 1 with 32-bit ones
static const char* selectCaps(void) {
    return sizeof(unsigned long) == 8 ? "200000000 0 0 0 0 0" : "2 0 0 0 0 0 0 0 0 0 0 0";
}

static void testCaps(void) {
    CHECK(auxDevKeyInCaps(selectCaps(), KEY_SELECT));
    CHECK(!auxDevKeyInCaps(selectCaps(), KEY_VOLUMEUP));
    CHECK(!auxDevKeyInCaps("0", KEY_SELECT));
    CHECK(!auxDevKeyInCaps("", KEY_SELECT));
    // volume up (115) and down (114) only: not the button
    CHECK(!auxDevKeyInCaps(sizeof(unsigned long) == 8 ? "c000000000000 0" : "c0000 0 0 0", KEY_SELECT));
}

static void testFind(void) {
    char root[] = "/tmp/mlxr-auxbtn-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char sys[600], dev[600];
    snprintf(sys, sizeof sys, "%s/sys", root);
    snprintf(dev, sizeof dev, "%s/dev", root);
    AuxDevInfo info;
    // nothing there
    CHECK(auxDevFind(sys, dev, &info) == 0 && !info.found);
    // a keyboard, a gpio-keys without the key, then the button at a high number
    fakeDevice(root, 2, "AT Translated Set 2 keyboard", selectCaps(), 0644);
    fakeDevice(root, 3, "gpio-keys", "0", 0644);
    fakeDevice(root, 11, "gpio-keys", selectCaps(), 0644);
    CHECK(auxDevFind(sys, dev, &info) == 1 && info.found && info.readable && info.err == 0);
    CHECK(strcmp(info.sysName, "event11") == 0);
    CHECK(strstr(info.path, "/dev/event11") != NULL);
    // unreadable (not as root, who reads anything)
    if (geteuid() != 0) {
        char p[600];
        snprintf(p, sizeof p, "%s/dev/event11", root);
        chmod(p, 0200);
        CHECK(auxDevFind(sys, dev, &info) == 1 && info.found && !info.readable && info.err == EACCES);
    }
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    CHECK(system(cmd) == 0);
}

static int g_grabs[8], g_ngrabs;
static int fakeGrab(int fd, int on) {
    (void)fd;
    if (g_ngrabs < 8) g_grabs[g_ngrabs++] = on;
    return 0;
}

static void put(int fd, int type, int code, int value) {
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.type = (unsigned short)type;
    e.code = (unsigned short)code;
    e.value = value;
    CHECK(write(fd, &e, sizeof e) == (ssize_t)sizeof e);
}

static void testRead(void) {
    int p[2];
    CHECK(pipe2(p, O_NONBLOCK) == 0);
    AuxDev d;
    auxDevInit(&d);
    d.grab = fakeGrab;
    g_ngrabs = 0;
    auxDevAdopt(&d, p[0], 1);
    CHECK(d.grabbed && g_ngrabs == 1 && g_grabs[0] == 1);
    int edges[16];
    // nothing waiting: no edges, still open
    CHECK(auxDevRead(&d, edges, 16) == 0 && d.fd >= 0);
    // a press and a release with a repeat, a volume key and the sync reports between
    put(p[1], EV_KEY, KEY_SELECT, 1);
    put(p[1], EV_SYN, SYN_REPORT, 0);
    put(p[1], EV_KEY, KEY_SELECT, 2);
    put(p[1], EV_KEY, KEY_VOLUMEUP, 1);
    put(p[1], EV_KEY, KEY_VOLUMEUP, 0);
    put(p[1], EV_KEY, KEY_SELECT, 0);
    put(p[1], EV_SYN, SYN_REPORT, 0);
    CHECK(auxDevRead(&d, edges, 16) == 2 && edges[0] == 1 && edges[1] == 0);
    CHECK(auxDevRead(&d, edges, 16) == 0);
    // the device goes away: closed, the grab let go first
    close(p[1]);
    CHECK(auxDevRead(&d, edges, 16) == -1 && d.fd < 0 && !d.grabbed);
    CHECK(g_ngrabs == 2 && g_grabs[1] == 0);
    // closing twice is fine
    auxDevClose(&d);
    // an open without the grab never grabs, and its close lets nothing go
    CHECK(pipe2(p, O_NONBLOCK) == 0);
    g_ngrabs = 0;
    auxDevAdopt(&d, p[0], 0);
    CHECK(!d.grabbed);
    auxDevClose(&d);
    CHECK(g_ngrabs == 0 && d.fd < 0);
    close(p[1]);
}

static void testParse(void) {
    struct input_event ev[4];
    memset(ev, 0, sizeof ev);
    ev[0].type = EV_KEY; ev[0].code = KEY_SELECT; ev[0].value = 1;
    ev[1].type = EV_KEY; ev[1].code = KEY_SELECT; ev[1].value = 0;
    ev[2].type = EV_KEY; ev[2].code = KEY_SELECT; ev[2].value = 1;
    ev[3].type = EV_KEY; ev[3].code = KEY_SELECT; ev[3].value = 0;
    int edges[2];
    // never past the room given
    CHECK(auxDevParse(ev, 4, edges, 2) == 2 && edges[0] == 1 && edges[1] == 0);
}

int main(void) {
    testCaps();
    testFind();
    testRead();
    testParse();
    return checksDone("input_auxbutton");
}
