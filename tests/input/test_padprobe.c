// padprobe: the pad probe's classification and text for made-up SDL devices (no SDL): a pad with
// Steam Input's copy, SteamVR's Frame controller (skipped), a DualSense with sensors, the 2026
// Steam Controllers, duplicates, a joystick with no mapping, and the watch lines.
#include <string.h>

#include "check.h"
#include "Limelight.h"
#include "padprobe.h"

static PadProbe probe;

static PadProbeDev dev(int id, const char* name, unsigned vendor, unsigned product, GpSdlKind kind, const char* serial) {
    PadProbeDev d;
    memset(&d, 0, sizeof(d));
    d.index = id;
    d.id = id;
    d.isController = 1;
    d.playerIndex = -1;
    d.axes = 6;
    d.buttons = 15;
    d.hats = 1;
    snprintf(d.battery, sizeof(d.battery), "wired");
    snprintf(d.sdlType, sizeof(d.sdlType), "%s", "TEST");
    snprintf(d.info.name, sizeof(d.info.name), "%s", name);
    snprintf(d.info.guid, sizeof(d.info.guid), "03000000%02x%02x0000%02x%02x000000000000", vendor & 0xff, (vendor >> 8) & 0xff,
             product & 0xff, (product >> 8) & 0xff);
    snprintf(d.info.serial, sizeof(d.info.serial), "%s", serial);
    snprintf(d.info.mapping, sizeof(d.info.mapping), "a:b0,b:b1,x:b2,y:b3");
    d.info.vendor = (uint16_t)vendor;
    d.info.product = (uint16_t)product;
    d.info.buttons = A_FLAG | B_FLAG | X_FLAG | Y_FLAG | PLAY_FLAG | BACK_FLAG;
    d.info.caps = LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE;
    gpClassify(&d.info, kind); // what gpSdlDescribe does after reading SDL
    return d;
}

static char* text(const PadProbeDev* d) {
    static char buf[8192];
    padProbeFormat(&probe, d, buf, sizeof(buf));
    return buf;
}

static char* summary(void) {
    static char buf[8192];
    padProbeSummary(&probe, buf, sizeof(buf));
    return buf;
}

static void testPidTable(void) {
    // moonlight-qt 6.2.0 (app/streaming/input/gamepad.cpp) and SDL's controller_list.h
    static const unsigned pids[] = { 0x1101, 0x1102, 0x1105, 0x1106, 0x1142, 0x1201, 0x1202, 0x1205, 0x1302, 0x1303, 0x1304, 0x1305 };
    for (size_t i = 0; i < sizeof(pids) / sizeof(pids[0]); i++) {
        CHECK(gpSteamControllerPid(pids[i]) == 1);
        CHECK(gpSteamControllerPidName(pids[i]) != NULL);
        CHECK(gpControllerType(GP_SDL_UNKNOWN, 0x28de, pids[i]) == LI_CTYPE_STEAM);
        CHECK(gpControllerType(GP_SDL_OTHER, 0x28de, pids[i]) == LI_CTYPE_STEAM);
        CHECK(gpControllerType(GP_SDL_UNKNOWN, 0x054c, pids[i]) == LI_CTYPE_UNKNOWN); // vendor matters
    }
    // Not Steam Controllers: Steam's virtual pad, other ids, and the ones before the 2015 pad
    CHECK(!gpSteamControllerPid(0x11ff) && gpSteamControllerPidName(0x11ff) == NULL);
    CHECK(!gpSteamControllerPid(0x1301) && !gpSteamControllerPid(0x1306) && !gpSteamControllerPid(0x0000));
    CHECK(gpControllerType(GP_SDL_UNKNOWN, 0x28de, 0x11ff) == LI_CTYPE_UNKNOWN);
    CHECK(gpIsSteamVirtualGamepad(0x28de, 0x11ff) && !gpIsSteamVirtualGamepad(0x045e, 0x11ff));
    // SDL's own type comes first (as moonlight-qt)
    CHECK(gpControllerType(GP_SDL_XBOX360, 0x28de, 0x1302) == LI_CTYPE_XBOX);
    CHECK(gpControllerType(GP_SDL_XBOXONE, 0x045e, 0x0b13) == LI_CTYPE_XBOX);
    CHECK(gpControllerType(GP_SDL_PS3, 0x054c, 0x0268) == LI_CTYPE_PS);
    CHECK(gpControllerType(GP_SDL_PS4, 0x054c, 0x09cc) == LI_CTYPE_PS);
    CHECK(gpControllerType(GP_SDL_PS5, 0x054c, 0x0ce6) == LI_CTYPE_PS);
    CHECK(gpControllerType(GP_SDL_SWITCH_PRO, 0x057e, 0x2009) == LI_CTYPE_NINTENDO);
    CHECK(gpControllerType(GP_SDL_JOYCON_PAIR, 0x057e, 0x2008) == LI_CTYPE_NINTENDO);
    CHECK(gpControllerType(GP_SDL_UNKNOWN, 0x2dc8, 0x6001) == LI_CTYPE_UNKNOWN); // 8BitDo in a mode SDL does not type
    CHECK(gpControllerType(GP_SDL_OTHER, 0x1234, 0x5678) == LI_CTYPE_UNKNOWN);
}

static void testIgnoreRules(void) {
    GpInfo i;
    memset(&i, 0, sizeof(i));
    snprintf(i.name, sizeof(i.name), "Valve Frame Controller (Left)");
    snprintf(i.guid, sizeof(i.guid), "03000000de280000ff11000000000000");
    i.vendor = 0x28de;
    CHECK(gpIgnoreReason(&i, NULL) == 1);
    i.vendor = 0x045e; // the name alone is not enough
    CHECK(gpIgnoreReason(&i, NULL) == 0);
    snprintf(i.name, sizeof(i.name), "Steam Virtual Gamepad");
    i.vendor = 0x28de;
    CHECK(gpIgnoreReason(&i, NULL) == 0);
    CHECK(gpIgnoreReason(&i, "") == 0);
    CHECK(gpIgnoreReason(&i, "0300000000000000aaaa000000000000, 03000000DE280000FF11000000000000") == 2); // either separator, any case
    CHECK(gpIgnoreReason(&i, "03000000de280000ff11") == 0); // whole GUIDs only
    CHECK(gpIgnoreReasonText(1)[0] && gpIgnoreReasonText(2)[0] && !gpIgnoreReasonText(0)[0]);
}

static void testMixedSet(void) {
    padProbeInit(&probe);
    PadProbeDev xbox = dev(1, "Xbox Wireless Controller", 0x045e, 0x0b13, GP_SDL_XBOXONE, "");
    PadProbeDev copy = dev(2, "Steam Virtual Gamepad", 0x28de, 0x11ff, GP_SDL_XBOX360, "");
    PadProbeDev frame = dev(3, "Valve Frame Controller", 0x28de, 0x2000, GP_SDL_UNKNOWN, "");
    PadProbeDev ds = dev(4, "DualSense Wireless Controller", 0x054c, 0x0ce6, GP_SDL_PS5, "e8:47:3a:00:00:01");
    ds.hasGyro = ds.hasAccel = 1;
    ds.gyroHz = ds.accelHz = 250.0f;
    ds.touchpads = 1;
    ds.hasLed = 1;
    ds.playerIndex = 0;
    ds.info.caps |= LI_CCAP_TRIGGER_RUMBLE;
    snprintf(ds.battery, sizeof(ds.battery), "medium (70%% or less)");
    PadProbeDev sc = dev(5, "Steam Controller", 0x28de, 0x1302, GP_SDL_UNKNOWN, "");
    sc.hasGyro = sc.hasAccel = 1;
    sc.touchpads = 2;

    const char* ignore = NULL;
    PadProbeDev* px = padProbeAdd(&probe, &xbox, ignore);
    PadProbeDev* pc = padProbeAdd(&probe, &copy, ignore);
    PadProbeDev* pf = padProbeAdd(&probe, &frame, ignore);
    PadProbeDev* pd = padProbeAdd(&probe, &ds, ignore);
    PadProbeDev* ps = padProbeAdd(&probe, &sc, ignore);
    CHECK(px && pc && pf && pd && ps);

    // Classification: what the app would announce
    CHECK(px->info.type == LI_CTYPE_XBOX && !px->info.mirror && px->number == 0);
    CHECK(pc->info.mirror == 1 && pc->info.type == LI_CTYPE_XBOX && pc->number == 0);
    CHECK(pf->ignored == 1 && pf->number == -1);
    CHECK(pd->info.type == LI_CTYPE_PS && pd->number == 1 && pd->ignored == 0);
    CHECK(ps->info.type == LI_CTYPE_STEAM && ps->number == 2);

    // The copy and its pad are linked both ways; the others are not linked
    CHECK(px->partner == (int)(pc - probe.dev) && pc->partner == (int)(px - probe.dev));
    CHECK(pf->partner == -1 && pd->partner == -1 && ps->partner == -1);

    const char* t = text(px);
    CHECK(strstr(t, "[1] Xbox Wireless Controller"));
    CHECK(strstr(t, "fork type Xbox (LI_CTYPE 1)"));
    CHECK(strstr(t, "045e:0b13"));
    CHECK(strstr(t, "real pad; its Steam Input copy is [2] Steam Virtual Gamepad"));
    CHECK(strstr(t, "controller #0, shared with [2]: one pad to the host"));
    CHECK(strstr(t, "sensors     gyro no; accel no"));

    t = text(pc);
    CHECK(strstr(t, "Steam Virtual Gamepad (28de:11ff): Steam Input's copy, merged into [1] Xbox Wireless Controller (controller #0)"));
    CHECK(strstr(t, "shared with [1]"));
    CHECK(strstr(t, "several real pads are attached")); // three real pads here

    t = text(pf);
    CHECK(strstr(t, "SKIPPED: SteamVR's own Frame controller"));
    CHECK(!strstr(t, "controller #"));

    t = text(pd);
    CHECK(strstr(t, "sdl type    TEST -> fork type PlayStation (LI_CTYPE 2)"));
    CHECK(strstr(t, "serial      e8:47:3a:00:00:01"));
    CHECK(strstr(t, "sensors     gyro yes, 250 Hz; accel yes, 250 Hz"));
    CHECK(strstr(t, "rumble      yes; trigger rumble yes"));
    CHECK(strstr(t, "touchpads 1; LED yes; battery medium (70% or less); player index 0"));
    CHECK(strstr(t, "caps: analog-triggers rumble trigger-rumble, buttons 0x"));
    CHECK(strstr(t, "steam       no\n"));

    t = text(ps);
    CHECK(strstr(t, "fork type Steam (LI_CTYPE 4)"));
    CHECK(strstr(t, "Steam Controller (Triton, USB) (28de:1302), on the app's Steam Controller list"));
    CHECK(strstr(t, "no Steam Input copy listed"));
    CHECK(strstr(t, "sensors     gyro yes; accel yes"));
    CHECK(strstr(t, "touchpads 2"));

    t = summary();
    CHECK(strstr(t, "pad #0  [1] Xbox Wireless Controller  (Xbox, 045e:0b13) + Steam Input copy [2]"));
    CHECK(strstr(t, "pad #1  [4] DualSense Wireless Controller  (PlayStation, 054c:0ce6)"));
    CHECK(strstr(t, "pad #2  [5] Steam Controller  (Steam, 28de:1302)"));
    CHECK(!strstr(t, "pad #3"));
    CHECK(strstr(t, "skipped:  [3] Valve Frame Controller (SteamVR's own Frame controller"));
    // the copy is listed with its pad, not as a pad of its own
    CHECK(!strstr(t, "[2] Steam Virtual Gamepad  ("));

    // The copy goes first: the pad keeps its number, the other is a pad on its own again
    PadProbeDev gone;
    CHECK(padProbeRemove(&probe, 2, &gone) == 1 && gone.id == 2);
    CHECK(padProbeRemove(&probe, 2, NULL) == 0);
    CHECK(padProbeFind(&probe, 2) == NULL && padProbeFind(&probe, 1) == px && px->partner == -1);
    CHECK(strstr(text(px), "steam       no\n"));
    CHECK(strstr(summary(), "pad #0  [1] Xbox Wireless Controller") && !strstr(summary(), "Steam Input copy ["));
    padProbeDestroy(&probe);
}

static void testSteamBehindSteamInput(void) {
    // A Steam Controller that Steam Input has claimed: SDL lists only the Virtual Gamepad
    padProbeInit(&probe);
    PadProbeDev copy = dev(7, "Steam Virtual Gamepad", 0x28de, 0x11ff, GP_SDL_XBOX360, "");
    PadProbeDev* pc = padProbeAdd(&probe, &copy, NULL);
    CHECK(pc && pc->number == 0 && pc->partner == -1);
    const char* t = text(pc);
    CHECK(strstr(t, "Steam Input's copy, no real pad joined to it (controller #0)"));
    CHECK(strstr(t, "the copy has no sensors"));
    CHECK(strstr(summary(), "(Steam Input copy, no real pad)"));

    // The real pad turns up after its copy and joins it
    PadProbeDev real = dev(8, "Xbox Controller", 0x045e, 0x02ea, GP_SDL_XBOX360, "");
    PadProbeDev* pr = padProbeAdd(&probe, &real, NULL);
    CHECK(pr && pr->number == 0 && pr->partner == (int)(pc - probe.dev) && pc->partner == (int)(pr - probe.dev));
    CHECK(strstr(text(pc), "merged into [8] Xbox Controller"));

    // Two real pads: a second copy joins the lowest-numbered pad without one, and the text says so
    PadProbeDev real2 = dev(9, "Xbox Controller 2", 0x045e, 0x02ea, GP_SDL_XBOX360, "");
    PadProbeDev copy2 = dev(10, "Steam Virtual Gamepad", 0x28de, 0x11ff, GP_SDL_XBOX360, "");
    PadProbeDev* p2 = padProbeAdd(&probe, &real2, NULL);
    PadProbeDev* c2 = padProbeAdd(&probe, &copy2, NULL);
    CHECK(p2 && p2->number == 1 && c2 && c2->number == 1 && c2->partner == (int)(p2 - probe.dev));
    CHECK(strstr(text(c2), "several real pads are attached"));
    padProbeDestroy(&probe);
}

static void testDuplicateAndUnmapped(void) {
    padProbeInit(&probe);
    PadProbeDev a = dev(1, "Pad", 0x045e, 0x0b13, GP_SDL_XBOXONE, "AA:BB:CC:00:11:22");
    PadProbeDev b = dev(2, "Pad (second node)", 0x045e, 0x0b13, GP_SDL_XBOXONE, "AA:BB:CC:00:11:22");
    PadProbeDev js = dev(3, "Generic USB Joystick", 0x1234, 0x5678, GP_SDL_UNKNOWN, "");
    js.isController = 0;
    js.sdlType[0] = 0;
    PadProbeDev* pa = padProbeAdd(&probe, &a, NULL);
    PadProbeDev* pb = padProbeAdd(&probe, &b, NULL);
    PadProbeDev* pj = padProbeAdd(&probe, &js, NULL);
    CHECK(pa && pb && pj);
    CHECK(pa->number == 0 && pb->number == -1 && pb->dupOf == (int)(pa - probe.dev));
    CHECK(strstr(text(pb), "skipped: the same pad as [1] (same serial, vendor and product)"));
    CHECK(pj->number == -1 && pj->ignored == 0);
    const char* t = text(pj);
    CHECK(strstr(t, "joystick without a game controller mapping"));
    CHECK(strstr(t, "not used (no game controller mapping)"));
    t = summary();
    CHECK(strstr(t, "not used: [3] Generic USB Joystick"));
    CHECK(strstr(t, "skipped:  [2] Pad (second node) (same pad as [1])"));
    // The pad it duplicated going away clears the reference
    CHECK(padProbeRemove(&probe, 1, NULL) == 1 && pb->dupOf == -1);
    padProbeDestroy(&probe);

    // MLXR_GAMEPAD_IGNORE: the GUID in the list is skipped, case aside
    padProbeInit(&probe);
    PadProbeDev x = dev(1, "Some Pad", 0x045e, 0x0b13, GP_SDL_XBOXONE, "");
    char ignore[64];
    snprintf(ignore, sizeof(ignore), "%s", x.info.guid);
    for (char* c = ignore; *c; c++) if (*c >= 'a' && *c <= 'f') *c = (char)(*c - 'a' + 'A');
    PadProbeDev* px = padProbeAdd(&probe, &x, ignore);
    CHECK(px && px->ignored == 2 && px->number == -1);
    CHECK(strstr(text(px), "SKIPPED: its GUID is in MLXR_GAMEPAD_IGNORE"));
    padProbeDestroy(&probe);

    // Sixteen numbers, then none free
    padProbeInit(&probe);
    int last = 0;
    for (int i = 0; i < GP_MAX; i++) {
        PadProbeDev p = dev(100 + i, "P", 0x045e, 0x0b13, GP_SDL_XBOXONE, "");
        PadProbeDev* q = padProbeAdd(&probe, &p, NULL);
        CHECK(q && q->number == i);
        last = i;
    }
    CHECK(last == GP_MAX - 1);
    PadProbeDev extra = dev(200, "Extra", 0x045e, 0x0b13, GP_SDL_XBOXONE, "");
    PadProbeDev* pe = padProbeAdd(&probe, &extra, NULL);
    CHECK(pe && pe->number == -1 && strstr(text(pe), "no controller number free"));
    padProbeDestroy(&probe);
}

static void testLines(void) {
    char out[256];
    CHECK(padProbeButtons(0, out, sizeof(out)) == 1 && !strcmp(out, "-"));
    padProbeButtons(A_FLAG | LB_FLAG | PLAY_FLAG, out, sizeof(out));
    CHECK(!strcmp(out, "A+LB+Start"));
    padProbeButtons(PLAY_FLAG | BACK_FLAG | LB_FLAG | RB_FLAG, out, sizeof(out));
    CHECK(!strcmp(out, "LB+RB+Start+Back")); // the disconnect chord
    padProbeButtons(SPECIAL_FLAG | PADDLE2_FLAG | TOUCHPAD_FLAG | MISC_FLAG, out, sizeof(out));
    CHECK(!strcmp(out, "Guide+Misc+P2+Touchpad"));
    char tiny[6];
    padProbeButtons(A_FLAG | B_FLAG | X_FLAG | Y_FLAG, tiny, sizeof(tiny)); // truncated, still terminated
    CHECK(strlen(tiny) == 5);

    padProbeCaps(0, out, sizeof(out));
    CHECK(!strcmp(out, "none"));
    padProbeCaps(LI_CCAP_GYRO | LI_CCAP_ACCEL | LI_CCAP_RGB_LED | LI_CCAP_DUAL_TOUCHPAD, out, sizeof(out));
    CHECK(!strcmp(out, "accel gyro led dual-touchpad"));
    CHECK(!strcmp(padProbeTypeName(LI_CTYPE_STEAM), "Steam") && !strcmp(padProbeTypeName(LI_CTYPE_NINTENDO), "Nintendo")
          && !strcmp(padProbeTypeName(LI_CTYPE_UNKNOWN), "Unknown") && !strcmp(padProbeTypeName(99), "?"));

    PadOutPacket pk;
    memset(&pk, 0, sizeof(pk));
    pk.buttonFlags = A_FLAG | B_FLAG;
    pk.leftTrigger = 10;
    pk.rightTrigger = 255;
    pk.leftStickX = -32768;
    pk.leftStickY = 5;
    pk.rightStickX = 7;
    pk.rightStickY = 32767;
    char line[256];
    padProbeStateLine(line, sizeof(line), 12.3456, 3, "btn a down", &pk);
    CHECK(!strcmp(line, "   12.346 [3] btn a down                 btn=A+B lt=10 rt=255 L=-32768,5 R=7,32767"));

    const float g[3] = { 0.012f, -0.5f, 1.0f };
    padProbeSensorLine(line, sizeof(line), 0.5, 4, "gyro", g);
    CHECK(!strcmp(line, "    0.500 [4] gyro  x=0.012 y=-0.500 z=1.000 rad/s"));
    padProbeSensorLine(line, sizeof(line), 0.5, 4, "accel", g);
    CHECK(strstr(line, "accel") && strstr(line, "m/s^2"));
}

int main(void) {
    testPidTable();
    testIgnoreRules();
    testMixedSet();
    testSteamBehindSteamInput();
    testDuplicateAndUnmapped();
    testLines();
    return checksDone("padprobe");
}
