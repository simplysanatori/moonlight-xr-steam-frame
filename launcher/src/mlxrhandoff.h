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

// Copies Moonlight Qt's config into this launcher's on its first run (no-op later).
// Call after the application and organization names are set, before any QSettings use.
void seedLauncherConfig();

// MLXR_LAUNCHER_SELFTEST="<host name|uuid>:<app name|id>": no UI. Starts a Session for it
// from the saved hosts and exits with the hand-off's result. For tests/launcher.
int selfTest(const QString& spec);

// Test hooks for the UI, called after main.qml loaded:
//   MLXR_LAUNCHER_SCREENSHOT, MLXR_LAUNCHER_PAGE: see uihooks.h (QT_QPA_PLATFORM=offscreen
//                           QT_QUICK_BACKEND=software to run headless)
//   MLXR_LAUNCHER_DEMO="host:app"  opens the stream segue for that saved PC and app, as clicking it would
void installTestHooks(QQmlApplicationEngine& engine);

}
