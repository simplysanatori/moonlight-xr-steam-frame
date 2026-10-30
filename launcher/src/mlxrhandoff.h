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
//   (lane wave4-pcvr) startPcvr()  exit code 12: hands the headset to Valve's Steam Link stream of the PC
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>
#include <memory>

#include "mlxrprofiles.h"

class QObject;
class QProcess;
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
    // Native (lane wave4-launcher): the settings profile in force for this stream (mlxrprofiles.h); the settings
    // go back when the last copy of the Plan is gone. Null for the Default.
    std::shared_ptr<MlxrProfiles::Applied> profile;
    // What the check before the stream found (mlxrpreflight.h), one hint to a line; shown as a toast and added to an
    // error dialog if the stream fails
    QStringList hints;
    // More environment for the VR client: MLXR_APP_UUID, the app's UUID from an Apollo host (launch by appuuid)
    QMap<QString, QString> extraEnv;
};

struct Result {
    int exitCode = 0;          // the client's exit code; 0 normal
    bool startFailed = false;  // the process did not start
    bool crashed = false;      // killed by a signal
    bool dryRun = false;
    bool disconnected = false; // left with the disconnect chord: the host's app still runs (exit code 11)
    bool pcvr = false;         // left with the Host sheet's Switch to PC VR (exit code 12): run() has started Steam's side
                               // of it (startPcvr()); the host's app still runs, so disconnected is set too
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
// So in Game Mode the window stays mapped at opacity 0 instead. gamescope rebuilds the list of
// windows it shows and focuses every frame, and a window at opacity 0 is left off it. The overlay
// goes away on the next frame, while the keyboard focus keeps its live wl_surface. At opacity 1
// the window comes back as a fresh overlay whose focus starts dirty. Needs Qt on xcb (warns
// otherwise). fadeFlatWindow(w, true) fades it out, fadeFlatWindow(w, false) brings it back
// (a no-op unless it was faded). MLXR_LAUNCHER_FADE_WINDOW=0 hides it as before.
bool fadesFlatWindow();
void fadeFlatWindow(QWindow* window, bool out);

// ---- Moonlight XR Native (lane wave4-pcvr): PC VR, Valve's Steam Link stream (PLAN 4.1, 4.2, 4.9) ----
// The VR client's "Switch to PC VR" (Host sheet) ends its session with this exit code. The host's
// session keeps running, as after the disconnect chord. run() then asks Steam, through its debugging
// port (packaging/steam_cef.py), to stream the chosen game from the PC: Apps.StreamGame(app, client, 0),
// or, with no game chosen, to open the library to press "Stream from your PC" there. This is Valve's
// stream, not Moonlight's; when it ends Steam brings this window back.
constexpr int kExitPcvr = 12;
// MLXR_STEAM_CEF (tests), else <dir of this binary>/steam_cef.py
QString steamCefPath();
// Runs `python3 -I steam_cef.py <args>` without blocking the UI. done(code, stdout, stderr) is called once
// on the GUI thread: code is the script's exit code (0 ok, 1 Steam refused, 2 usage, 3 the CEF port does
// not answer, 4 a call is missing), -1 when python or the script could not be started, -2 on a timeout.
// context owns the process (the application when null). Returns the process (null when it was not started).
QProcess* runSteamCef(const QStringList& args, int timeoutMs, QObject* context,
                      const std::function<void(int, const QByteArray&, const QString&)>& done);
// steam_cef.py's arguments for the hand-off: --pcvr [--app APP] [--client CLIENT]
QStringList pcvrArguments(const QString& app, const QString& client);
// The hand-off itself: reads the chosen game (mlxr/pcvr_app) and PC (mlxr/pcvr_client) from the launcher's
// settings and starts steam_cef.py --pcvr. A dry run (MLXR_LAUNCHER_DRYRUN) prints "MLXR_PCVR: ..." and
// starts nothing.
void startPcvr();
// True for 20 s after startPcvr(): the flat screen is not asked for again then (it would pull the dashboard
// back to the flat window over Valve's stream)
bool pcvrHeldOff();
// Tests: waits (up to ms) for the process startPcvr() started
void waitForPcvr(int ms);

// Copies Moonlight Qt's config into this launcher's on its first run (no-op later).
// Call after the application and organization names are set, before any QSettings use.
void seedLauncherConfig();

// MLXR_LAUNCHER_SELFTEST="<host name|uuid>:<app name|id>": no UI. Starts a Session for it
// from the saved hosts and exits with the hand-off's result. For tests/launcher.
// Also (lane launcher2): "bitrate:<W>x<H>x<FPS>[x444]" prints the default bitrate
// StreamingPreferences::getDefaultBitrate gives, "scale" prints the Game Mode UI scale.
// (lane wave4-pcvr) "pcvr:probe", "pcvr:devices", "pcvr:apps[:CLIENT]" run that steam_cef.py command and print
// "SELFTEST: pcvr <what> rc=N <its output>"; "pcvr:go" runs the hand-off with the saved settings.
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
constexpr int kMaxDefaultBitrateKbps = 150000; // the default curve's cap (stock's slider maximum; the page's slider itself goes to 500000 since lane wave4b-abr)
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
