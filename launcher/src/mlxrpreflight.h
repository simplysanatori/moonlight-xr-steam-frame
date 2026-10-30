// Moonlight XR Native launcher (lane wave4-launcher): the check before a stream starts. New file, added to
// app/streaming by launcher/build.sh. GPLv3.
//
// Right before the VR client is started the launcher asks the PC (serverinfo, and the app list) what the host
// would say about this stream and turns the known trouble into hints, which MlxrHandoff::makePlan puts in the Plan:
//   * Apollo's permissions for this client (the Permission bits in serverinfo): no Launch Apps, no View Streams,
//     no controller / mouse / keyboard input, no clipboard
//   * Use Virtual Display with a PC that is not Apollo, cannot make a virtual display, or whose driver is not ready
//   * the PC is already running another app
//   * the stream's resolution is not one of the modes the PC lists
//   * HDR is on but the PC advertises no 10-bit codec; Prefer HEVC but the PC has no HEVC
// The app list is read too, for the app's UUID: Apollo launches by `appuuid`, which survives the host's app
// list being renumbered. The launcher passes it to the VR client in MLXR_APP_UUID (the app id stays the fallback).
//
// The question goes out the way the launcher polls the PC (NvHTTP: the pinned certificate over HTTPS with this
// client's certificate, so Apollo knows who asks). It is short (2 s for serverinfo, 3 s for the app list) and a PC
// that does not answer only means no hints. Nothing is sent in a dry run, and MLXR_LAUNCHER_PREFLIGHT=0 (or the
// Settings page's switch) turns it off. MLXR_LAUNCHER_PREFLIGHT_URL=http://host:port (tests) asks that plain HTTP
// server instead of the PC, even in a dry run.
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

class NvComputer;
class NvApp;
class StreamingPreferences;
class MlxrPrefs;

namespace MlxrPreflight {

// Apollo's per-client permission bits (Apollo crypto.h; the same values as backend/backend.h MLXR_PERM_*)
constexpr int kPermInputController = 0x00000100;
constexpr int kPermInputMouse = 0x00000800;
constexpr int kPermInputKbd = 0x00001000;
constexpr int kPermClipboardSet = 0x00010000;
constexpr int kPermClipboardRead = 0x00020000;
constexpr int kPermList = 0x01000000;
constexpr int kPermView = 0x02000000;
constexpr int kPermLaunch = 0x04000000;

// <ServerCodecModeSupport> bits (Limelight.h SCM_*): any HEVC, any 10-bit
constexpr int kScmHevc = 0x00000100 | 0x00000200 | 0x00080000 | 0x00100000;
constexpr int kScm10Bit = 0x00000200 | 0x00100000 | 0x00020000 | 0x00400000;

struct Mode {
    int width = 0, height = 0, refresh = 0;
};

// What serverinfo says
struct Facts {
    bool valid = false;            // an answer with status_code 200
    QString hostname;
    QString state;                 // e.g. SUNSHINE_SERVER_BUSY
    int currentGame = 0;           // the app id running; 0 unless the state says busy
    QString currentGameUuid;       // Apollo's <currentgameuuid>
    bool apollo = false;           // VirtualDisplayCapable or Permission present (the backend's rule)
    int permission = -1;           // <Permission> bits of this client, -1 when absent
    // <PairStatus> 1: the answer came over HTTPS to this paired client. Apollo says Permission 0 and leaves out
    // the virtual display over plain HTTP, so those are only believed from such an answer.
    bool paired = false;
    bool vdCapable = false;
    bool vdReady = false;
    int codecSupport = 1;          // <ServerCodecModeSupport>, H.264 only when absent
    QVector<Mode> modes;
};

struct App {
    int id = 0;
    QString name;
    QString uuid;                  // Apollo's <UUID>
};

// The stream that is about to start
struct Request {
    QString pc;
    QString appName;
    int appId = 0;
    QString appUuid;
    int width = 0, height = 0, fps = 0;
    bool hdr = false;
    int codec = 0;                 // StreamingPreferences::VideoCodecConfig
    bool virtualDisplay = false;
    bool clipboardSync = false;
};

Facts parseServerInfo(const QString& xml);
QVector<App> parseAppList(const QString& xml);
// The app's UUID in the list: by id, else by name; "" when the host sends none
QString appUuidFor(const QVector<App>& apps, int appId, const QString& appName);
// What is wrong, one sentence a hint, most important first; empty when nothing is
QStringList hintsFor(const Facts& facts, const Request& request, const QVector<App>& apps);

struct Result {
    bool enabled = false;          // the check was made (not off, not a dry run)
    bool reached = false;          // the PC answered
    QStringList hints;
    QString appUuid;
    QString error;                 // why it did not answer
};

// Asks the PC (see the top) about this stream. Call after the settings (and a profile) are in force.
Result check(const NvComputer& computer, const NvApp& app, const StreamingPreferences& prefs, const MlxrPrefs& launcherPrefs);

}
