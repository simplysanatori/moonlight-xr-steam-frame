// Moonlight XR Native launcher: the hand-off from moonlight-qt's stream start to the
// native OpenXR client (moonlight-xr). New file, added to app/ by launcher/build.sh. GPLv3.
//
//   enabled()        the native client is found (MLXR_NATIVE_CLIENT, else next to this binary)
//   exportPairing()  writes this launcher's client identity, the host and its pinned server
//                    certificate into the native client's config dir (hosts.ini, servercerts/)
//   makePlan()       the exact command line: --host UUID --app ID --mode WxHxFPS --bitrate KBPS ...
//   run()            runs it, keeps the Qt UI alive meanwhile, reports how it ended
//   seedLauncherConfig()   first run only: copies Moonlight Qt's (flatpak) config
//   (lane artemis-settings) the Artemis-style Settings page's own settings: mlxrprefs.h
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

class QQmlApplicationEngine;
class QWindow;
class NvComputer;
class NvApp;
class StreamingPreferences;

namespace MlxrHandoff {

struct Plan {
    QString program;
    QStringList arguments;
    QString workingDir;
    bool clipboardSync = false; // Artemis's Smart Clipboard Sync (Apollo): the clipboard files below
    // Native (lane refresh): the display rate picked on the Settings page (72, 90, 120 or 144), which
    // run() sets as SteamVR's per-app preferredRefreshRate before the client starts; 0 leaves it
    // ("Match headset", a custom rate, 30 or 60 fps)
    int steamVrRefreshRate = 0;
};

struct Result {
    int exitCode = 0;          // the client's exit code; 0 normal
    bool startFailed = false;  // the process did not start
    bool crashed = false;      // killed by a signal
    bool dryRun = false;
    bool disconnected = false; // left with the disconnect chord: the host's app still runs (exit code 11)
    bool ok() const { return exitCode == 0 || disconnected; }
    QString message;           // for the error dialog; empty when exitCode == 0
};

// The hand-off is used when the native client is found and MLXR_HANDOFF is not 0
bool enabled();
// MLXR_NATIVE_CLIENT, else <dir of this binary>/moonlight-xr, else empty
QString clientPath();
// $MLXR_CONFIG_DIR, else ~/.config/moonlight-xr (the native client's own rule)
QString nativeConfigDir();

Plan makePlan(const NvComputer& computer, const NvApp& app, const StreamingPreferences& prefs);
// "program arg arg ...", quoted for a shell (logs, the dry run)
QString commandLine(const Plan& plan);

bool exportPairing(const NvComputer& computer, QString* error);

// onStarted() is called once the child runs (or at once in a dry run); onStreaming() once the
// client prints "MLXR_STREAMING" (the headset shows the picture; at once in a dry run)
Result run(const Plan& plan, const std::function<void()>& onStarted,
           const std::function<void()>& onStreaming = nullptr);

// Moonlight XR Native (lane launcher-return): the flat window around a hand-off.
// watchFlatWindow() is called for each hand-off. The first call starts the watch: from then on
// the window's visibility, exposure, activation, platform surface and the first frame after each
// show are logged ("flat window ..." Qt Info lines). Later calls cancel what a previous return
// still had pending, so it never touches the new stream.
// flatWindowReturn() is called right after sessionFinished(), once the stock handler has shown the
// window again. It waits until the window is exposed and has drawn a frame and logs how long that
// took. In Game Mode (gamescope) it hides and shows the window again if that has not happened
// after 1 s, once more 2 s later, and asks Steam for the flat screen again once the window drew.
// On a desktop it only logs. MLXR_LAUNCHER_TEST_RETURN_MISS=N (tests): the first N checks count
// as missed and the Game Mode steps run anywhere.
void watchFlatWindow(QWindow* window);
void flatWindowReturn(QWindow* window);

// Game Mode: the flat window during a stream (owner's crash 2026-10-27). Hiding (unmapping) it
// destroys its surface, and gamescope then has no keyboard focus: a volume or power key restores
// that empty focus after handing the key to Steam, and wlroots aborts gamescope ("Assertion
// `surface' failed" in wlr_seat_keyboard_notify_enter), which takes Steam and SteamVR with it.
// So in Game Mode the window stays mapped at opacity 0 instead: gamescope never picks a window
// at opacity 0 for focus or for its screen (the headset shows what it shows for a hidden one),
// while the keyboard focus keeps its live surface. fadeFlatWindow(w, true) fades it out,
// fadeFlatWindow(w, false) brings it back (a no-op unless it was faded).
// MLXR_LAUNCHER_FADE_WINDOW=0 hides it as before.
bool fadesFlatWindow();
void fadeFlatWindow(QWindow* window, bool out);

// Copies Moonlight Qt's config into this launcher's on its first run (no-op later).
// Call after the application and organization names are set, before any QSettings use.
void seedLauncherConfig();

// MLXR_LAUNCHER_SELFTEST="<host name|uuid>:<app name|id>": no UI. Starts a Session for it
// from the saved hosts and exits with the hand-off's result. For tests/launcher.
// Also (lane launcher2): "bitrate:<W>x<H>x<FPS>[x444]" prints the default bitrate
// StreamingPreferences::getDefaultBitrate gives, "scale" prints the Game Mode UI scale.
int selfTest(const QString& spec);

// Moonlight XR Native (lane refresh): the Steam Frame's display rates. The VR client never streams
// above the rate SteamVR runs the display at, 144 at most.
constexpr int kMaxHeadsetFps = 144;
// The rate SteamVR's per-app setting is set to for this frame rate choice, or 0 to leave it:
// 72, 90, 120 and 144 are display rates; "Match headset" (match), a custom rate and 30 or 60
// fps leave SteamVR's own rate alone.
int steamVrRateFor(int fps, bool match, bool customRate);
// Sets steam.app.<SteamAppId>.preferredRefreshRate to hz through vrcmd, the way
// packaging/install.sh sets the app's rate, and logs what happened. Needs SteamAppId (the Steam
// shortcut's appid) and a running SteamVR; vrcmd missing or failing is logged and the stream goes
// on at the rate SteamVR reports. MLXR_VRCMD: another vrcmd (tests; no SteamVR check then). In a
// dry run without MLXR_VRCMD nothing runs. Returns true when vrcmd set it.
bool setSteamVrRefreshRate(int hz);

// Moonlight XR Native (lane launcher2): the default video bitrate in hand-off mode is
// twice stock's getDefaultBitrate for the same resolution and frame rate (stock's resolution
// table and square-root frame-rate factor included), rounded to 500 kbps and capped at the
// Settings slider's maximum. `stockMbps` is stock's figure before it is rounded to whole Mbps.
// 1080p60 40000, 1080p90 49000, 1440p60 80000, 1440p72 87500, 1440p90 98000, 4K 150000;
// (lane refresh) 1440p120 113000, 1440p144 124000.
constexpr int kMaxDefaultBitrateKbps = 150000; // the slider's maximum (stock: 150000 unless "unlock")
int headsetDefaultBitrateKbps(double stockMbps);

// Moonlight XR Native (lane launcher2): the UI scale in Game Mode (XDG_CURRENT_DESKTOP is
// gamescope), where the flat UI is a 1920x1080 window shown on the headset's flat screen. 1.5 by
// default, MLXR_LAUNCHER_SCALE overrides it (a number, clamped 1.0..2.5); 1.0 outside Game Mode.
double gameModeScale();
// Sets QT_SCALE_FACTOR from gameModeScale() in Game Mode. Call before the QGuiApplication exists.
// An existing QT_SCALE_FACTOR is kept unless MLXR_LAUNCHER_SCALE is set.
void applyGameModeScale();

// Test hooks for the UI, called after main.qml loaded:
//   MLXR_LAUNCHER_SCREENSHOT, MLXR_LAUNCHER_PAGE: see uihooks.h (QT_QPA_PLATFORM=offscreen
//                           QT_QUICK_BACKEND=software to run headless)
//   MLXR_LAUNCHER_DEMO="host:app"  opens the stream segue for that saved PC and app, as clicking it would
void installTestHooks(QQmlApplicationEngine& engine);

}
