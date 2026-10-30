// Moonlight XR Native, tests/m4 (lane wave4-apollo, PLAN 4.6, 4b.4, 4b.5): the Host sheet's PC page on
// the input side. The header button flips between the pages; "Paste from PC" asks the app for the host's
// clipboard; a host command asks first and then asks the app to run it by number; "Send text to PC"
// puts the keyboard up as a field of its own (Compact, nothing to the host as keys) and Enter hands the
// text to the app; and a host that is not Apollo has no commands to press. GPLv3.
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "scene.h"
#include "xr_host.h"
#include "xr_keyboard.h"

// A press at the middle of a rectangle (u0, v0, u1, v1) of the sheet
static int pressRect(XrCtx* c, const float r[4], int* tick) {
    return xrHostPress(c, (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f, tick);
}

static int pressCell(XrCtx* c, int cell, int* tick) {
    float r[4];
    xrHostCellRect(cell, r);
    return pressRect(c, r, tick);
}

static void toPcPage(XrCtx* c) {
    float r[4];
    int tick = 0;
    xrHostPageRect(r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_PAGE && tick == 1);
}

static void apollo(XrCtx* c, int commands) {
    memset(&c->hostInfo, 0, sizeof(c->hostInfo));
    c->hostInfo.apollo = 1;
    c->hostInfo.clipRead = c->hostInfo.clipSet = 1;
    c->hostInfo.cmdCount = commands;
    c->hostInfo.cmdWhy = commands ? XR_HOSTINFO_OK : XR_HOSTINFO_NONE_SET;
    static const char* const NAMES[3] = { "Bubbles", "Sleep PC", "Lock PC" };
    for (int i = 0; i < commands && i < 3; i++) snprintf(c->hostInfo.cmd[i], XR_HOST_CMD_NAME, "%s", NAMES[i]);
}

static void testPages(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    apollo(c, 3);
    xrHostOpen(c);
    CHECK(c->hostOpen && c->hostPage == XR_HOST_PAGE_MAIN && c->hostActKind == XR_HOSTACT_NONE);

    // the main page's Send keys cell is not on the PC page
    float send[4];
    xrHostCellRect(XR_HOST_CELL_SEND_KEYS, send);
    float u = (send[0] + send[2]) * 0.5f, v = (send[1] + send[3]) * 0.5f;
    CHECK(xrHostZone(c, u, v) == XR_HOST_CELL_SEND_KEYS);
    toPcPage(c);
    CHECK(c->hostPage == XR_HOST_PAGE_PC && c->hostOpen);
    CHECK(xrHostZone(c, u, v) == XR_HOST_ZONE_NONE);
    // and back
    toPcPage(c);
    CHECK(c->hostPage == XR_HOST_PAGE_MAIN);
    CHECK(xrHostZone(c, u, v) == XR_HOST_CELL_SEND_KEYS);

    // opening again starts on the main page
    toPcPage(c);
    xrHostClose(c);
    xrHostOpen(c);
    CHECK(c->hostPage == XR_HOST_PAGE_MAIN);
}

static void testPaste(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    apollo(c, 3);
    xrHostOpen(c);
    toPcPage(c);
    int tick = 0;
    CHECK(pressCell(c, XR_HOST_PC_PASTE, &tick) == XR_HOST_PC_PASTE && tick == 1);
    CHECK(c->hostActKind == XR_HOSTACT_PASTE && !c->hostOpen);
    CHECK(!c->hostCompose.active);
}

static void testCommands(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    apollo(c, 3);
    xrHostOpen(c);
    toPcPage(c);
    int tick = 0;
    // a command asks first, naming nothing yet to the app
    CHECK(pressCell(c, XR_HOST_PC_CMD_FIRST + 1, &tick) == XR_HOST_PC_CMD_FIRST + 1);
    CHECK(c->hostConfirm == XR_HOST_PC_CMD_FIRST + 1 && c->hostActKind == XR_HOSTACT_NONE && c->hostOpen);
    // with the question up, the page button is not a zone
    float r[4];
    xrHostPageRect(r);
    CHECK(xrHostZone(c, (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f) == XR_HOST_ZONE_NONE);
    // Cancel changes nothing
    xrHostConfirmCancelRect(r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_CONFIRM_CANCEL);
    CHECK(c->hostConfirm == -1 && c->hostActKind == XR_HOSTACT_NONE && c->hostOpen);
    // Run: the second command, by its number, and the sheet goes away
    CHECK(pressCell(c, XR_HOST_PC_CMD_FIRST + 1, &tick) == XR_HOST_PC_CMD_FIRST + 1);
    xrHostConfirmSendRect(r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_CONFIRM_SEND);
    CHECK(c->hostActKind == XR_HOSTACT_CMD && c->hostActArg == 1 && strcmp(c->hostActText, "Sleep PC") == 0);
    CHECK(c->hostConfirm == -1 && !c->hostOpen);

    // only the commands the host listed are cells
    scene_reset(s);
    c->hostActKind = XR_HOSTACT_NONE;
    apollo(c, 1);
    xrHostOpen(c);
    toPcPage(c);
    xrHostCellRect(XR_HOST_PC_CMD_FIRST + 1, r);
    CHECK(xrHostZone(c, (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f) == XR_HOST_ZONE_NONE);
    CHECK(pressCell(c, XR_HOST_PC_CMD_FIRST + 1, &tick) == XR_HOST_ZONE_NONE && c->hostConfirm == -1);

    // a host that is not Apollo has none; the clipboard entries are still there to say why not
    memset(&c->hostInfo, 0, sizeof(c->hostInfo));
    c->hostInfo.cmdWhy = XR_HOSTINFO_NOT_APOLLO;
    CHECK(pressCell(c, XR_HOST_PC_CMD_FIRST, &tick) == XR_HOST_ZONE_NONE && c->hostConfirm == -1);
    CHECK(pressCell(c, XR_HOST_PC_PASTE, &tick) == XR_HOST_PC_PASTE && c->hostActKind == XR_HOSTACT_PASTE);
    c->hostActKind = XR_HOSTACT_NONE;
}

static void testCompose(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    apollo(c, 3);
    xrKeyboardSetLayout(c, NULL, KB_LAYOUT_FULL);
    CHECK(xrKeyboardLayout(c) == KB_LAYOUT_FULL);
    xrHostOpen(c);
    toPcPage(c);
    int tick = 0;
    CHECK(pressCell(c, XR_HOST_PC_COPY, &tick) == XR_HOST_PC_COPY);
    CHECK(!c->hostOpen && c->hostCompose.active && c->kbOpen && c->hostActKind == XR_HOSTACT_NONE);
    // the keyboard is the Compact one, QWERTY, for as long as it takes the text
    xrKeyboardSync(c, NULL);
    CHECK(xrKeyboardLayout(c) == KB_LAYOUT_COMPACT && xrKeyboardMap(c) == KB_MAP_QWERTY);

    CHECK(xrHostComposeFeed(c, 'h') == 1 && xrHostComposeFeed(c, 'i') == 1 && xrHostComposeFeed(c, 'x') == 1);
    CHECK(strcmp(c->hostCompose.text, "hix") == 0);
    CHECK(xrHostComposeFeed(c, 8) == 1 && strcmp(c->hostCompose.text, "hi") == 0);
    // keys that are not characters are the field's too, so they never reach the host
    CHECK(xrHostComposeFeed(c, KB_CODE_VK + 0x41) == 1 && strcmp(c->hostCompose.text, "hi") == 0);
    CHECK(xrHostComposeFeed(c, 0xE9) == 1 && strcmp(c->hostCompose.text, "hi\xC3\xA9") == 0);
    // Enter hands the text to the app and puts the keyboard away
    CHECK(xrHostComposeFeed(c, 13) == 1);
    CHECK(c->hostActKind == XR_HOSTACT_COPY && strcmp(c->hostActText, "hi\xC3\xA9") == 0);
    CHECK(!c->hostCompose.active && !c->kbOpen);
    xrKeyboardSync(c, NULL);
    CHECK(xrKeyboardLayout(c) == KB_LAYOUT_FULL);   // the choice comes back
    // nothing composing: the key is the keyboard's
    CHECK(xrHostComposeFeed(c, 'a') == 0);
    c->hostActKind = XR_HOSTACT_NONE;

    // Enter with nothing typed sends nothing
    xrHostOpen(c);
    toPcPage(c);
    pressCell(c, XR_HOST_PC_COPY, &tick);
    CHECK(xrHostComposeFeed(c, 13) == 1 && c->hostActKind == XR_HOSTACT_NONE && !c->hostCompose.active);

    // Hide (the keyboard put away) or nothing reaching the host ends it with nothing sent
    xrHostOpen(c);
    toPcPage(c);
    pressCell(c, XR_HOST_PC_COPY, &tick);
    xrHostComposeFeed(c, 'a');
    c->kbOpen = 0;
    float out[IN_SLOTS];
    for (int i = 0; i < IN_SLOTS; i++) out[i] = -1.0f;
    xrHostFrame(c, out, 1000, 1);
    CHECK(!c->hostCompose.active && c->hostActKind == XR_HOSTACT_NONE);
    xrHostOpen(c);
    toPcPage(c);
    pressCell(c, XR_HOST_PC_COPY, &tick);
    xrHostComposeFeed(c, 'a');
    xrHostFrame(c, NULL, 2000, 0);
    CHECK(!c->hostCompose.active);
    xrKeyboardSync(c, NULL);
}

// PC VR (PLAN 4.1, main's) and the PC page's button share the header, which is the same on both pages:
// each press reaches its own, and PC VR asked from the PC page switches, not runs a command
static void testBesidePcvr(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    apollo(c, 3);
    xrHostOpen(c);
    float r[4];
    int tick = 0;
    xrHostPcvrRect(r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_PCVR && c->hostConfirm == XR_HOST_CONFIRM_PCVR);
    xrHostConfirmCancelRect(r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_CONFIRM_CANCEL && c->hostConfirm == -1 && !c->pcvrRequest);
    toPcPage(c);
    CHECK(c->hostPage == XR_HOST_PAGE_PC && c->hostConfirm == -1);
    xrHostPcvrRect(r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_PCVR && c->hostConfirm == XR_HOST_CONFIRM_PCVR);
    xrHostConfirmSendRect(r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_CONFIRM_SEND);
    CHECK(c->pcvrRequest == 1 && c->hostActKind == XR_HOSTACT_NONE && !c->hostOpen && c->hostConfirm == -1);
    c->pcvrRequest = 0;

    // Lane pcvr-steamvr: with a game from the launcher the question offers it beside SteamVR
    setenv("MLXR_PCVR_GAME", " Beat Saber ", 1);
    xrHostOpen(c);
    CHECK(strcmp(c->pcvrGame, "Beat Saber") == 0);
    xrHostPcvrRect(r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_PCVR && c->hostConfirm == XR_HOST_CONFIRM_PCVR);
    xrHostPcvrChoiceRect(1, 1, r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_CONFIRM_GAME);
    CHECK(c->pcvrRequest == 2 && !c->hostOpen && c->hostConfirm == -1);
    c->pcvrRequest = 0;
    xrHostOpen(c);
    xrHostPcvrRect(r);
    pressRect(c, r, &tick);
    xrHostPcvrChoiceRect(0, 1, r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_CONFIRM_SEND && c->pcvrRequest == 1);
    c->pcvrRequest = 0;
    xrHostOpen(c);
    xrHostPcvrRect(r);
    pressRect(c, r, &tick);
    xrHostPcvrChoiceRect(2, 1, r);
    CHECK(pressRect(c, r, &tick) == XR_HOST_ZONE_CONFIRM_CANCEL && !c->pcvrRequest && c->hostOpen);
    xrHostClose(c);
    // none: the game's place is no button
    unsetenv("MLXR_PCVR_GAME");
    xrHostOpen(c);
    CHECK(c->pcvrGame[0] == '\0');
    xrHostPcvrRect(r);
    pressRect(c, r, &tick);
    xrHostPcvrChoiceRect(1, 1, r);
    CHECK(pressRect(c, r, &tick) != XR_HOST_ZONE_CONFIRM_GAME && !c->pcvrRequest);
    xrHostClose(c);
}

int main(void) {
    Scene* s = scene_new();
    testPages(s);
    testBesidePcvr(s);
    testPaste(s);
    testCommands(s);
    testCompose(s);
    scene_free(s);
    return checksDone("m4_hostpc");
}
