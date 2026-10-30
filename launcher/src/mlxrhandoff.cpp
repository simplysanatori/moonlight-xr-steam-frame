// Moonlight XR Native launcher: the hand-off to the native OpenXR client. GPLv3.
// See mlxrhandoff.h and docs/dev/reports/launcher.md.
#include "mlxrhandoff.h"

#include "backend/identitymanager.h"
#include "backend/nvcomputer.h"
#include "backend/nvapp.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"
#include "streaming/uihooks.h"
#include "streaming/mlxrprefs.h"
#include "backend/computermanager.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <memory>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QProcessEnvironment>
#include <QReadLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSslCertificate>
#include <QTextStream>

#include <signal.h>
#include <unistd.h>
#include <sys/prctl.h>

namespace MlxrHandoff {

static const char* const kFlatpakConf =
    ".var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project/Moonlight.conf";

static bool envSet(const char* name)
{
    return !qEnvironmentVariableIsEmpty(name);
}

QString nativeConfigDir()
{
    QString d = qEnvironmentVariable("MLXR_CONFIG_DIR");
    if (d.isEmpty()) {
        d = QDir::homePath() + "/.config/moonlight-xr";
    }
    return d;
}

QString clientPath()
{
    QString p = qEnvironmentVariable("MLXR_NATIVE_CLIENT");
    if (p.isEmpty()) {
        p = QCoreApplication::applicationDirPath() + "/moonlight-xr";
    }
    QFileInfo fi(p);
    if (fi.isFile() && fi.isExecutable()) {
        return fi.absoluteFilePath();
    }
    return QString();
}

bool enabled()
{
    if (qEnvironmentVariable("MLXR_HANDOFF") == "0") {
        return false;
    }
    if (envSet("MLXR_LAUNCHER_DRYRUN")) {
        return true;
    }
    return !clientPath().isEmpty();
}

static QString shellQuote(const QString& s)
{
    if (!s.isEmpty() && QRegularExpression("^[A-Za-z0-9_@%+=:,./-]+$").match(s).hasMatch()) {
        return s;
    }
    QString q = s;
    q.replace("'", "'\\''");
    return "'" + q + "'";
}

QString commandLine(const Plan& plan)
{
    QStringList parts;
    parts << shellQuote(plan.program);
    for (const QString& a : plan.arguments) {
        parts << shellQuote(a);
    }
    return parts.join(' ');
}

int steamVrRateFor(int fps, bool match, bool customRate)
{
    if (match || customRate) {
        return 0;
    }
    return fps == 72 || fps == 90 || fps == 120 || fps == 144 ? fps : 0;
}

// Native (lane refresh): SteamVR runs each app at the one rate its per-app setting names, and the
// OpenXR runtime offers only that rate, so the VR client cannot ask for another. The rate picked
// on the Settings page is therefore written there first, as packaging/install.sh does.
bool setSteamVrRefreshRate(int hz)
{
    if (hz <= 0) {
        return false;
    }
    bool idOk = false;
    const qulonglong appId = qEnvironmentVariable("SteamAppId").trimmed().toULongLong(&idOk);
    if (!idOk || appId == 0) {
        qInfo().noquote() << QString("SteamVR refresh rate: %1 Hz not set, the launcher was not started from its Steam "
                                     "shortcut (no SteamAppId); the stream follows the rate SteamVR reports").arg(hz);
        return false;
    }
    const QString key = QString("steam.app.%1.preferredRefreshRate").arg(appId);
    QString vrcmd = qEnvironmentVariable("MLXR_VRCMD");
    const bool own = vrcmd.isEmpty();
    if (own) {
        if (envSet("MLXR_LAUNCHER_DRYRUN")) {
            qInfo().noquote() << QString("SteamVR refresh rate: dry run, %1 would be set to %2 Hz").arg(key).arg(hz);
            return false;
        }
        vrcmd = "/opt/steamvr/bin/linuxarm64/vrcmd";
    }
    if (!QFileInfo(vrcmd).isExecutable()) {
        qWarning().noquote() << QString("SteamVR refresh rate: %1 Hz not set, no vrcmd at %2; the stream follows the "
                                        "rate SteamVR reports").arg(hz).arg(vrcmd);
        return false;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("LD_LIBRARY_PATH");
    if (own) {
        // vrcmd talks to the running SteamVR, which keeps steamvr.vrsettings (install.sh's check)
        QProcess pg;
        pg.setProcessEnvironment(env);
        pg.start("pgrep", { "-x", "vrserver" });
        if (!pg.waitForFinished(3000) || pg.exitStatus() != QProcess::NormalExit || pg.exitCode() != 0) {
            qWarning().noquote() << QString("SteamVR refresh rate: %1 Hz not set, SteamVR is not running; the stream "
                                            "follows the rate SteamVR reports").arg(hz);
            return false;
        }
    }
    QProcess p;
    p.setProcessEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(vrcmd, { "--set-settings-int", key, QString::number(hz) });
    if (!p.waitForStarted(3000)) {
        qWarning().noquote() << QString("SteamVR refresh rate: %1 Hz not set, vrcmd did not start (%2); the stream "
                                        "follows the rate SteamVR reports").arg(hz).arg(p.errorString());
        return false;
    }
    if (!p.waitForFinished(5000)) {
        p.kill();
        p.waitForFinished(1000);
        qWarning().noquote() << QString("SteamVR refresh rate: %1 Hz not set, vrcmd did not finish in 5 s; the stream "
                                        "follows the rate SteamVR reports").arg(hz);
        return false;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qWarning().noquote() << QString("SteamVR refresh rate: %1 Hz not set, vrcmd failed (exit %2): %3; the stream "
                                        "follows the rate SteamVR reports")
                                    .arg(hz).arg(p.exitCode()).arg(QString::fromUtf8(p.readAll()).simplified().left(200));
        return false;
    }
    qInfo().noquote() << QString("SteamVR refresh rate: %1 set to %2 Hz (vrcmd), the headset's rate for this app "
                                 "from its next VR session").arg(key).arg(hz);
    return true;
}

Plan makePlan(const NvComputer& c, const NvApp& app, const StreamingPreferences& p)
{
    Plan plan;
    plan.program = clientPath();
    if (plan.program.isEmpty()) {
        plan.program = "moonlight-xr"; // dry runs without a client installed
    }
    plan.workingDir = QFileInfo(plan.program).absolutePath();

    QString uuid;
    {
        QReadLocker lock(&c.lock);
        uuid = c.uuid;
    }

    QStringList a;
    // The uuid is how the native client's hosts.ini names this PC (exportPairing wrote it)
    a << "--host" << uuid;
    a << "--app" << QString::number(app.id);
    // The headset shows 144 Hz at most (the VR client lowers it further to SteamVR's own rate).
    // Moonlight's flat settings can say more (Game Mode's gamescope reports the screen's rate),
    // which a Sunshine host would turn into a virtual display at that rate.
    // Native (lane refresh): the ceiling was 90.
    int fps = p.fps;
    if (fps > kMaxHeadsetFps) {
        qWarning() << "The FPS setting" << fps << "is above the headset's" << kMaxHeadsetFps
                   << "Hz: asking for" << kMaxHeadsetFps << "fps";
        fps = kMaxHeadsetFps;
    }
    a << "--mode" << QString("%1x%2x%3").arg(p.width).arg(p.height).arg(fps);
    if (p.bitrateKbps > 0) {
        a << "--bitrate" << QString::number(p.bitrateKbps);
    }
    // The native client streams H.264 or HEVC; AV1 and "auto" leave its own choice (HEVC, H.264 fallback)
    if (p.videoCodecConfig == StreamingPreferences::VCC_FORCE_H264) {
        a << "--h264";
    }
    if (p.videoDecoderSelection == StreamingPreferences::VDS_FORCE_SOFTWARE) {
        a << "--sw-decode";
    }
    switch (p.audioConfig) {
    case StreamingPreferences::AC_51_SURROUND:
        a << "--audio" << "51";
        break;
    case StreamingPreferences::AC_71_SURROUND:
        a << "--audio" << "71";
        break;
    default:
        break; // stereo is the default
    }
    if (p.packetSize > 0) {
        a << "--packet-size" << QString::number(p.packetSize);
    }
    // Lane artemis-settings: the Artemis-style Settings page. Host settings (stock keys), then the
    // page's own (mlxrprefs.h); only what differs from the VR client's defaults is passed
    if (!p.gameOptimizations) {
        a << "--no-sops";
    }
    if (p.playAudioOnHost) {
        a << "--local-audio";
    }
    MlxrPrefs* prefs = MlxrPrefs::get();
    a << prefs->clientArgs();
    plan.clipboardSync = prefs->clipboardSync;
    // Native (lane refresh): a fixed display rate picked on the Settings page goes to SteamVR too
    plan.steamVrRefreshRate = steamVrRateFor(p.fps, prefs->matchHeadsetFps, prefs->refreshRate() > 0);
    // The network path (Auto, Wi-Fi, Steam Frame Wireless Adapter, USB cable): the flat launcher's setting
    // "networkPath", auto|home|adapter|usb. An unknown value is left to the VR client's own config.ini.
    {
        const QString np = QSettings().value("networkPath", "auto").toString().trimmed().toLower();
        if (np == "auto" || np == "home" || np == "adapter" || np == "usb") {
            a << "--network" << np;
        }
    }
    a << "--exit-to-caller";
    // Debugging and tests: extra flags for the client, e.g. "--seconds 30"
    const QString extra = qEnvironmentVariable("MLXR_LAUNCHER_EXTRA_ARGS");
    if (!extra.isEmpty()) {
        a << QProcess::splitCommand(extra);
    }
    plan.arguments = a;
    return plan;
}

// ---- pairing --------------------------------------------------------------------

static bool writeAtomic(const QString& path, const QByteArray& data, QFile::Permissions perms, QString* error)
{
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = QObject::tr("Cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(data);
    if (!f.commit()) {
        *error = QObject::tr("Cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    QFile::setPermissions(path, perms);
    return true;
}

static QString safeName(const QString& uuid)
{
    QString s = uuid;
    s.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return s.isEmpty() ? QString("host") : s;
}

static bool isLoopback(const QString& a)
{
    return a.startsWith("127.") || a == "::1" || a == "localhost";
}

struct HostRecord {
    QString uuid;
    QList<QPair<QString, QString>> fields;
};

static QList<HostRecord> readHosts(const QString& path)
{
    QList<HostRecord> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return out;
    }
    HostRecord* cur = nullptr;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        if (line.startsWith('[')) {
            int e = line.indexOf(']');
            out.append(HostRecord());
            cur = &out.last();
            cur->uuid = line.mid(1, e > 0 ? e - 1 : -1);
        }
        else if (cur) {
            int eq = line.indexOf('=');
            if (eq > 0) {
                cur->fields.append({ line.left(eq).trimmed(), line.mid(eq + 1).trimmed() });
            }
        }
    }
    return out;
}

static QString oneLine(QString s)
{
    s.replace('\n', ' ').replace('\r', ' ');
    return s;
}

bool exportPairing(const NvComputer& c, QString* error)
{
    const QString dir = nativeConfigDir();
    if (!QDir().mkpath(dir)) {
        *error = QObject::tr("Cannot create %1").arg(dir);
        return false;
    }
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    // ---- the client identity: the host pinned THIS certificate when the PC was paired here ----
    IdentityManager* im = IdentityManager::get();
    QByteArray cert = im->getCertificate();
    QByteArray key = im->getPrivateKey();
    QString uid = im->getUniqueId().trimmed().toLower();
    if (cert.isEmpty() || key.isEmpty()) {
        *error = QObject::tr("The launcher has no client identity to share with the VR client");
        return false;
    }
    if (!cert.endsWith('\n')) cert += '\n';
    if (!key.endsWith('\n')) key += '\n';
    const QString pemPath = dir + "/client.pem", keyPath = dir + "/client.key", uidPath = dir + "/uniqueid";
    QFile oldPem(pemPath);
    bool same = false;
    if (oldPem.open(QIODevice::ReadOnly)) {
        same = oldPem.readAll().trimmed() == cert.trimmed();
        oldPem.close();
    }
    if (!same) {
        if (QFileInfo::exists(pemPath)) {
            // A different identity was there (hosts paired with it stay on it): keep it
            const QString stamp = QDateTime::currentDateTime().toString("yyyyMMddHHmmss");
            QFile::copy(pemPath, pemPath + ".bak-" + stamp);
            QFile::copy(keyPath, keyPath + ".bak-" + stamp);
            qWarning() << "Replacing the VR client identity in" << dir << "(old one kept as *.bak-" + stamp + ")";
        }
        if (!writeAtomic(keyPath, key, QFile::ReadOwner | QFile::WriteOwner, error) ||
            !writeAtomic(pemPath, cert, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther, error)) {
            return false;
        }
    }
    {
        QFile u(uidPath);
        QByteArray want = uid.rightJustified(16, '0').toLatin1() + "\n";
        if (!uid.isEmpty() && (!u.open(QIODevice::ReadOnly) || u.readAll() != want)) {
            u.close();
            if (!writeAtomic(uidPath, want, QFile::ReadOwner | QFile::WriteOwner, error)) {
                return false;
            }
        }
    }

    // ---- the host ----
    QString uuid, name, mac;
    QStringList addrs; // preference order: the address that answers now, then the saved ones
    QByteArray srvPem;
    int httpPort = 47989, httpsPort = 47984;
    {
        QReadLocker lock(&c.lock);
        uuid = c.uuid;
        name = c.name;
        if (c.macAddress.size() == 6 && c.macAddress != QByteArray(6, '\0')) {
            QStringList m;
            for (uchar b : c.macAddress) {
                m << QString("%1").arg(b, 2, 16, QChar('0'));
            }
            mac = m.join(':');
        }
        const NvAddress order[] = { c.activeAddress, c.manualAddress, c.localAddress, c.remoteAddress, c.ipv6Address };
        bool first = true;
        for (const NvAddress& a : order) {
            if (a.isNull() || a.address().isEmpty() || isLoopback(a.address()) || addrs.contains(a.address())) {
                continue;
            }
            if (first) {
                httpPort = a.port() ? a.port() : 47989;
                first = false;
            }
            addrs << a.address();
        }
        if (c.activeHttpsPort) {
            httpsPort = c.activeHttpsPort;
        }
        if (!c.serverCert.isNull()) {
            srvPem = c.serverCert.toPem();
        }
    }
    if (uuid.isEmpty()) {
        *error = QObject::tr("This PC has no id yet (it has not answered once)");
        return false;
    }
    if (srvPem.isEmpty()) {
        *error = QObject::tr("%1 is not paired: pair it in Moonlight first").arg(name);
        return false;
    }
    if (!srvPem.endsWith('\n')) srvPem += '\n';
    QDir().mkpath(dir + "/servercerts");
    QFile::setPermissions(dir + "/servercerts", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QString rel = "servercerts/" + safeName(uuid) + ".pem";
    if (!writeAtomic(dir + "/" + rel, srvPem, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther,
                     error)) {
        return false;
    }

    QList<HostRecord> all = readHosts(dir + "/hosts.ini");
    // Keep the addresses this PC was known by before: the launcher knows only some of them at a time
    // (mDNS finds one, the other link may be asleep), and the VR client chooses the network path
    // (Wi-Fi or the Steam Frame Wireless Adapter) among all of them
    for (const HostRecord& h : all) {
        if (h.uuid.compare(uuid, Qt::CaseInsensitive) != 0) {
            continue;
        }
        for (const auto& kv : h.fields) {
            if (kv.first != "address" && kv.first != "extra_addresses") {
                continue;
            }
            for (const QString& old : kv.second.split(',', Qt::SkipEmptyParts)) {
                const QString o = old.trimmed();
                if (!o.isEmpty() && !isLoopback(o) && !addrs.contains(o) && addrs.size() < 8) {
                    addrs << o;
                }
            }
        }
    }

    HostRecord rec;
    rec.uuid = oneLine(uuid).replace(']', '_');
    rec.fields.append({ "name", oneLine(name) });
    if (!addrs.isEmpty()) {
        rec.fields.append({ "address", addrs.first() });
        if (addrs.size() > 1) {
            rec.fields.append({ "extra_addresses", addrs.mid(1).join(',') });
        }
    }
    if (!mac.isEmpty()) {
        rec.fields.append({ "mac", mac });
    }
    rec.fields.append({ "http_port", QString::number(httpPort) });
    rec.fields.append({ "https_port", QString::number(httpsPort) });
    rec.fields.append({ "cert", rel });

    bool replaced = false;
    for (HostRecord& h : all) {
        if (h.uuid.compare(rec.uuid, Qt::CaseInsensitive) == 0) {
            h = rec;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        all.append(rec);
    }
    QByteArray out = "# Moonlight XR hosts. Pinned server certificates live in servercerts/.\n";
    for (const HostRecord& h : all) {
        out += "\n[" + h.uuid.toUtf8() + "]\n";
        for (const auto& kv : h.fields) {
            out += kv.first.toUtf8() + " = " + kv.second.toUtf8() + "\n";
        }
    }
    if (!writeAtomic(dir + "/hosts.ini", out, QFile::ReadOwner | QFile::WriteOwner, error)) {
        return false;
    }
    qInfo() << "Shared" << name << "(" << uuid << ") with the VR client in" << dir
            << "; address" << (addrs.isEmpty() ? QString("-") : addrs.first());
    return true;
}

// ---- running the client -----------------------------------------------------------

static QString consoleLogPath()
{
    QString d = qEnvironmentVariable("MLXR_LOG_DIR");
    if (d.isEmpty()) {
        d = QDir::homePath() + "/Games/MoonlightXRNative";
    }
    QDir().mkpath(d);
    return d + "/moonlight-console.log";
}

static QString lastErrorLine(const QByteArray& tail)
{
    const QList<QByteArray> lines = tail.split('\n');
    for (int i = lines.size() - 1; i >= 0; i--) {
        int at = lines[i].indexOf("MLXR_ERROR: ");
        if (at >= 0) {
            return QString::fromUtf8(lines[i].mid(at + 12)).trimmed();
        }
    }
    return QString();
}

// In the headset (Game Mode under SteamVR), SteamVR shows this window as the game's flat screen
// in its dashboard, and closes the dashboard when the VR client's session starts; nothing opens
// it again when the client exits, so the headset would show nothing. Ask Steam to put the flat
// screen back up (Steam's own call, through its local debugging port, steam_cef.py).
static bool inGameMode()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP") == "gamescope";
}

// ---- Moonlight XR Native (lane wave4-pcvr): PC VR, Valve's Steam Link stream --------------------
// steam_cef.py is the same script the installer and tools/frame-deploy.sh drive Steam with; PLAN 4.1
// (the hand-off), 4.2 (the picker's lists, MlxrPrefs) and 4.9 (the preflight, MlxrPrefs::pcvrProbe).

static QPointer<QProcess> s_pcvrProcess;
static QElapsedTimer s_pcvrSince;
static bool s_pcvrStarted = false;

QString steamCefPath()
{
    const QString p = qEnvironmentVariable("MLXR_STEAM_CEF");
    return p.isEmpty() ? QCoreApplication::applicationDirPath() + "/steam_cef.py" : p;
}

QProcess* runSteamCef(const QStringList& args, int timeoutMs, QObject* context,
                      const std::function<void(int, const QByteArray&, const QString&)>& done)
{
    QObject* owner = context ? context : static_cast<QObject*>(qApp);
    const QString cef = steamCefPath();
    if (!QFileInfo::exists(cef)) {
        const QString why = QObject::tr("steam_cef.py was not found (%1)").arg(cef);
        QTimer::singleShot(0, owner, [done, why]() { done(-1, QByteArray(), why); });
        return nullptr;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("LD_LIBRARY_PATH");   // the Steam launch options' libraries are not python's
    QProcess* p = new QProcess(owner);
    p->setProcessEnvironment(env);
    auto finished = std::make_shared<bool>(false);   // done() runs once: finished, failed to start or timed out
    auto timedOut = std::make_shared<bool>(false);
    QTimer* timer = new QTimer(p);
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, p, [p, timedOut]() {
        *timedOut = true;
        p->kill();
    });
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p,
                     [p, done, finished, timedOut](int code, QProcess::ExitStatus status) {
        if (*finished) {
            return;
        }
        *finished = true;
        const QByteArray out = p->readAllStandardOutput();
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed();
        done(*timedOut ? -2 : status == QProcess::NormalExit ? code : -1, out, err);
        p->deleteLater();
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [p, done, finished](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || *finished) {
            return;
        }
        *finished = true;
        done(-1, QByteArray(), QObject::tr("python3 could not be started: %1").arg(p->errorString()));
        p->deleteLater();
    });
    p->start("python3", QStringList { "-I", cef } + args);
    timer->start(timeoutMs);
    return p;
}

QStringList pcvrArguments(const QString& app, const QString& client)
{
    QStringList a { "--pcvr" };
    if (!app.trimmed().isEmpty()) {
        a << "--app" << app.trimmed();
    }
    if (!client.trimmed().isEmpty()) {
        a << "--client" << client.trimmed();
    }
    return a;
}

void startPcvr()
{
    QSettings s;
    const QString app = s.value("mlxr/pcvr_app").toString();
    const QString client = s.value("mlxr/pcvr_client").toString();
    const QStringList args = pcvrArguments(app, client);
    s_pcvrStarted = true;
    s_pcvrSince.start();
    const QString line = "MLXR_PCVR: python3 -I steam_cef.py " + args.join(' ');
    if (!qEnvironmentVariable("MLXR_LAUNCHER_DRYRUN").isEmpty()) {
        QTextStream(stdout) << line << Qt::endl;
        qInfo().noquote() << line;
        const QString outPath = qEnvironmentVariable("MLXR_LAUNCHER_DRYRUN_OUT");
        if (!outPath.isEmpty()) {
            QFile f(outPath);
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                f.write((line + "\n").toUtf8());
            }
        }
        return;
    }
    qInfo().noquote() << "Handing the headset to PC VR (Steam Link):" << args.join(' ')
                      << (app.isEmpty() ? "(no game chosen: Steam's library opens)" : "");
    s_pcvrProcess = runSteamCef(args, 40000, qApp, [](int code, const QByteArray& out, const QString& err) {
        if (code == 0) {
            qInfo().noquote() << "Asked Steam for PC VR: done" << QString::fromUtf8(out).trimmed();
        }
        else {
            qWarning().noquote() << QString("Asked Steam for PC VR: failed (exit %1): %2 %3; run steam_cef.py --probe for the "
                                            "Steam port").arg(code).arg(err.left(300), QString::fromUtf8(out).left(200));
        }
    });
}

bool pcvrHeldOff()
{
    return s_pcvrStarted && s_pcvrSince.isValid() && s_pcvrSince.elapsed() < 20000;
}

void waitForPcvr(int ms)
{
    if (!s_pcvrProcess || s_pcvrProcess->state() == QProcess::NotRunning) {
        return;
    }
    QEventLoop loop;
    QObject::connect(s_pcvrProcess.data(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// Moonlight XR Native (lane launcher-return): one request, logged with `when`; also used by the
// flat window watch below once the window has drawn again
static void askSteamForFlatScreen(const QString& when)
{
    if (pcvrHeldOff()) {
        qInfo().noquote() << "Not asking Steam for the flat screen" << when + ": the headset was handed to PC VR";
        return;
    }
    const QString appId = qEnvironmentVariable("SteamAppId");
    const QString cef = QCoreApplication::applicationDirPath() + "/steam_cef.py";
    if (!inGameMode() || appId.isEmpty() || !QFileInfo::exists(cef)) {
        return;
    }
    const QString js = QString("SteamClient.OpenVR.VROverlay.ShowDashboard('valve.steam.desktopgame.%1')").arg(appId);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("LD_LIBRARY_PATH");
    QProcess* p = new QProcess(qApp);
    p->setProcessEnvironment(env);
    QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p,
                     [p, when](int code, QProcess::ExitStatus) {
        qInfo().noquote() << "Asked Steam to show the flat screen again" << when + ":"
                          << (code == 0 ? "done" : "failed") << p->readAll().trimmed();
        p->deleteLater();
    });
    p->setProcessChannelMode(QProcess::MergedChannels);
    p->start("python3", { "-I", cef, js });
}

static void reopenFlatScreen()
{
    // Asked right away the dashboard opens before this window is back up and SteamVR shows
    // nothing in it; so again once the window has been shown and drawn (StreamSegue shows it as
    // soon as this returns; the flat window watch asks once more after its first frame)
    for (int ms : { 0, 1000, 2500 }) {
        QTimer::singleShot(ms, qApp, [ms]() {
            askSteamForFlatScreen(QString("after %1 ms").arg(ms));
        });
    }
}

// ---- Lane launcher-return: the flat window around a hand-off ------------------------
// Owner's run 2026-10-11: after the in-VR Exit the flat window did not come back, although the
// stock handler ran and Steam was asked three times. Nothing in the old log said whether Qt had
// the window shown, exposed and drawn. This watch logs that, and in Game Mode shows the window
// again when it is not back (docs/dev/reports/launcher-return.md).
class FlatWindowWatch : public QObject
{
public:
    explicit FlatWindowWatch(QWindow* window)
        : QObject(window), m_window(window)
    {
        setObjectName("mlxrFlatWindowWatch");
        m_exposed = window->isExposed();
        window->installEventFilter(this);
        connect(window, &QWindow::visibleChanged, this, [this](bool visible) { onVisible(visible); });
        connect(window, &QWindow::activeChanged, this, [this]() {
            log(m_window->isActive() ? "flat window active (has the input focus)" : "flat window inactive");
        });
        if (QQuickWindow* q = qobject_cast<QQuickWindow*>(window)) {
            connect(q, &QQuickWindow::frameSwapped, this, [this]() { onFrame(); });
            connect(q, &QQuickWindow::sceneGraphInvalidated, this, [this]() { log("flat window scene graph released"); });
        }
        m_tick.setInterval(100);
        connect(&m_tick, &QTimer::timeout, this, [this]() { onTick(); });
        log("flat window watched from the hand-off on");
    }

    // A new hand-off: whatever a previous return still had pending must not touch this stream
    void handOff()
    {
        m_return++;
        m_returning = false;
        m_tick.stop();
    }

    void startReturn()
    {
        m_return++;
        m_returnClock.start();
        m_framesSinceReturn = 0;
        m_attempt = 0;
        m_testMiss = qEnvironmentVariableIntValue("MLXR_LAUNCHER_TEST_RETURN_MISS");
        if (!m_window->isVisible()) {
            // quitAfter (the CLI's stream command): stock quits, or shows the error dialog, without showing it
            m_returning = false;
            log("flat window after the stream stays hidden (stock: the launcher quits after a CLI stream)");
            return;
        }
        m_returning = true;
        log("flat window after the stream");
        m_tickClock.start();
        m_tick.start();
        const int token = m_return;
        QTimer::singleShot(1000, this, [this, token]() { deadline(token, 1); });
        QTimer::singleShot(3000, this, [this, token]() { deadline(token, 2); });
        QTimer::singleShot(6000, this, [this, token]() { deadline(token, 3); });
        check();
    }

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == m_window) {
            if (e->type() == QEvent::Expose) {
                // QGuiApplication has set isExposed() before it delivers the event
                const bool exposed = m_window->isExposed();
                if (exposed != m_exposed) {
                    m_exposed = exposed;
                    log(exposed ? "flat window exposed" : "flat window no longer exposed");
                    check();
                }
            }
            else if (e->type() == QEvent::PlatformSurface) {
                const auto t = static_cast<QPlatformSurfaceEvent*>(e)->surfaceEventType();
                log(t == QPlatformSurfaceEvent::SurfaceCreated ? "flat window platform surface created"
                                                               : "flat window platform surface about to be destroyed");
            }
        }
        return false;
    }

private:
    QString state() const
    {
        QString s;
        QDebug(&s).nospace() << "visible " << m_window->isVisible() << ", exposed " << m_window->isExposed()
                             << ", active " << m_window->isActive() << ", " << m_window->visibility();
        return s;
    }

    // "<what>: visible true, exposed false, active false, QWindow::FullScreen"
    void log(const QString& what) const
    {
        qInfo().noquote() << what + ": " + state();
    }

    qint64 sinceReturn() const
    {
        return m_returnClock.isValid() ? m_returnClock.elapsed() : -1;
    }

    void onVisible(bool visible)
    {
        if (visible) {
            m_shownClock.start();
            m_framesSinceShow = 0;
            log("flat window shown");
        }
        else if (m_reshowing) {
            log("flat window hidden to show it again");
        }
        else if (m_returning) {
            // Nothing in the launcher hides it after the stream; if this shows up, something did
            log(QString("flat window hidden again %1 ms after the stream").arg(sinceReturn()));
        }
        else {
            log("flat window hidden");
        }
    }

    void onFrame()
    {
        if (m_framesSinceShow++ == 0 && m_shownClock.isValid()) {
            qInfo().noquote() << QString("flat window drew its first frame %1 ms after it was shown").arg(m_shownClock.elapsed());
        }
        m_framesSinceReturn++;
        check();
    }

    // Back = shown, exposed and drawn since the return (or since the last re-show)
    void check()
    {
        if (!m_returning || m_testMiss > 0 || !m_window->isVisible() || !m_window->isExposed() || m_framesSinceReturn == 0) {
            return;
        }
        m_returning = false;
        qInfo().noquote() << QString("flat window exposed after %1 ms and drawn%2").arg(sinceReturn())
                             .arg(m_attempt ? QString(" (shown again %1 time(s))").arg(m_attempt) : QString());
        // The three timed requests may all come before the window is back up; one more now that it is
        askSteamForFlatScreen("once the window had drawn");
    }

    void deadline(int token, int step)
    {
        if (token != m_return || !m_returning) {
            return;
        }
        const bool testing = m_testMiss > 0;
        if (testing) {
            m_testMiss--;
        }
        if (step == 3) {
            m_returning = false;
            log(QString("flat window still not exposed and drawn %1 ms after the stream (%2 frames); nothing more to try")
                    .arg(sinceReturn()).arg(m_framesSinceReturn));
            return;
        }
        if (!inGameMode() && !qEnvironmentVariableIsSet("MLXR_LAUNCHER_TEST_RETURN_MISS")) {
            // On a desktop the window manager owns this; stock does nothing more either
            log(QString("flat window not exposed and drawn after %1 ms (%2 frames); not in Game Mode, left to the window manager")
                    .arg(sinceReturn()).arg(m_framesSinceReturn));
            return;
        }
        m_attempt++;
        log(QString("flat window not exposed, showing it again (%1 ms after the stream, %2 frames%3)")
                .arg(sinceReturn()).arg(m_framesSinceReturn).arg(testing ? ", test miss" : ""));
        reshow();
    }

    void reshow()
    {
        m_framesSinceReturn = 0;
        m_reshowing = true;
        m_window->setVisible(false);
        m_reshowing = false;
        m_window->setVisible(true);
        m_window->raise();
        m_window->requestActivate();
        // main.qml stops polling on the hide and starts it on the show only when active; the
        // app grid must refresh either way (as patch 0003 does after the stream)
        if (!m_window->property("pollingActive").toBool()) {
            if (QQmlEngine* engine = qmlEngine(m_window)) {
                if (ComputerManager* cm = engine->singletonInstance<ComputerManager*>("ComputerManager", "ComputerManager")) {
                    cm->startPolling();
                    m_window->setProperty("pollingActive", true);
                }
            }
        }
        check();
    }

    // A UI thread blocked (in a frame swap, say) draws nothing and runs no timers: the gap shows here
    void onTick()
    {
        const qint64 gap = m_tickClock.restart();
        if (gap > 1000) {
            qInfo().noquote() << QString("flat window: the UI thread did not run for %1 ms (%2 ms after the stream)").arg(gap).arg(sinceReturn());
        }
        if (sinceReturn() > 10000) {
            m_tick.stop();
        }
    }

    QWindow* m_window;
    bool m_exposed = false;
    bool m_returning = false;
    bool m_reshowing = false;
    int m_return = 0;
    int m_attempt = 0;
    int m_testMiss = 0;
    int m_framesSinceShow = 0;
    int m_framesSinceReturn = 0;
    QElapsedTimer m_shownClock;
    QElapsedTimer m_returnClock;
    QElapsedTimer m_tickClock;
    QTimer m_tick;
};

static FlatWindowWatch* flatWindowWatchOf(QWindow* window)
{
    // (no Q_OBJECT here, so findChild<FlatWindowWatch*> is not available)
    QObject* o = window ? window->findChild<QObject*>("mlxrFlatWindowWatch", Qt::FindDirectChildrenOnly) : nullptr;
    return dynamic_cast<FlatWindowWatch*>(o);
}

void watchFlatWindow(QWindow* window)
{
    if (!window) {
        return;
    }
    FlatWindowWatch* w = flatWindowWatchOf(window);
    if (!w) {
        w = new FlatWindowWatch(window);
    }
    w->handOff();
}

void flatWindowReturn(QWindow* window)
{
    if (FlatWindowWatch* w = flatWindowWatchOf(window)) {
        w->startReturn();
    }
}

bool fadesFlatWindow()
{
    return inGameMode() && qEnvironmentVariable("MLXR_LAUNCHER_FADE_WINDOW") != "0";
}

void fadeFlatWindow(QWindow* window, bool out)
{
    if (!window || window->property("mlxrFaded").toBool() == out) {
        return;
    }
    if (out && QGuiApplication::platformName() != "xcb") {
        qWarning().noquote() << "the flat window fade needs X11 (Qt platform is"
                             << QGuiApplication::platformName() + "); the window will stay in front";
    }
    window->setProperty("mlxrFaded", out);
    // On xcb this sets _NET_WM_WINDOW_OPACITY. gamescope rebuilds its list of windows to show and
    // focus every frame and skips those at opacity 0, so the overlay goes on the next frame.
    window->setOpacity(out ? 0.0 : 1.0);
    qInfo().noquote() << (out ? "flat window faded out for the stream (stays mapped, gamescope keeps a keyboard focus)"
                              : "flat window faded back in after the stream");
}

// ---- Lane artemis-settings: Artemis's Smart Clipboard Sync with Apollo ------------------
// Apollo serves actions/clipboard only to a client that streams right now, so the VR client does the
// requests (app/main.c: after the stream is up, before it stops); the launcher owns the desktop
// clipboard and hands the text over in two files in the VR client's config dir (0600).
static QString clipboardFile(const char* name)
{
    return nativeConfigDir() + "/" + name;
}

static void clipboardBeforeStream()
{
    QDir().mkpath(nativeConfigDir());
    QFile::remove(clipboardFile("clipboard-from-host.txt"));
    QClipboard* cb = QGuiApplication::clipboard();
    // tests: MLXR_LAUNCHER_TEST_CLIPBOARD puts text on the clipboard first
    if (cb && qEnvironmentVariableIsSet("MLXR_LAUNCHER_TEST_CLIPBOARD")) {
        cb->setText(qEnvironmentVariable("MLXR_LAUNCHER_TEST_CLIPBOARD"));
    }
    const QByteArray text = cb ? cb->text().toUtf8() : QByteArray();
    QString error;
    writeAtomic(clipboardFile("clipboard-to-host.txt"), text, QFile::ReadOwner | QFile::WriteOwner, &error);
    qInfo().noquote() << "MLXR_LAUNCHER_CLIPBOARD: handing" << text.size() << "bytes to the VR client for the host";
}

static void clipboardAfterStream()
{
    QFile::remove(clipboardFile("clipboard-to-host.txt"));
    QFile f(clipboardFile("clipboard-from-host.txt"));
    if (!f.open(QIODevice::ReadOnly)) {
        const QString none = "MLXR_LAUNCHER_CLIPBOARD: nothing came back from the host";
        QTextStream(stdout) << none << Qt::endl;
        qInfo().noquote() << none;
        return;
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();
    f.remove();
    if (QClipboard* cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
    const QString line = QString("MLXR_LAUNCHER_CLIPBOARD: took %1 bytes from the host").arg(text.toUtf8().size());
    QTextStream(stdout) << line << Qt::endl;
    qInfo().noquote() << line;
}

Result run(const Plan& plan, const std::function<void()>& onStarted, const std::function<void()>& onStreaming)
{
    Result r;
    s_pcvrStarted = false;   // a new hand-off: the flat screen is asked for again after it
    // Native (lane refresh): before the VR client's session starts, so SteamVR runs it at that rate
    if (plan.steamVrRefreshRate > 0) {
        setSteamVrRefreshRate(plan.steamVrRefreshRate);
    }
    if (plan.clipboardSync) {
        clipboardBeforeStream();
    }
    struct After {
        bool on;
        ~After() { if (on) clipboardAfterStream(); }
    } after { plan.clipboardSync };
    const QString cmd = commandLine(plan);
    const QString dry = qEnvironmentVariable("MLXR_LAUNCHER_DRYRUN");
    if (!dry.isEmpty()) {
        // Prints what would run instead of running it. "fail:CODE:message" also plays a failing client.
        r.dryRun = true;
        const QString line = "MLXR_LAUNCHER_DRYRUN: " + cmd;
        QTextStream(stdout) << line << Qt::endl;
        qInfo().noquote() << line;
        const QString outPath = qEnvironmentVariable("MLXR_LAUNCHER_DRYRUN_OUT");
        if (!outPath.isEmpty()) {
            QFile f(outPath);
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                f.write((cmd + "\n").toUtf8());
            }
        }
        if (onStarted) onStarted();
        if (onStreaming) onStreaming();
        if (dry.startsWith("fail:")) {
            const QStringList p = dry.split(':');
            r.exitCode = p.value(1, "1").toInt();
            r.message = p.mid(2).join(':');
            if (r.exitCode == kExitPcvr) {
                r.disconnected = r.pcvr = true;
                r.message.clear();
                startPcvr();
            }
        }
        return r;
    }

    QProcess proc;
    proc.setProgram(plan.program);
    proc.setArguments(plan.arguments);
    proc.setWorkingDirectory(plan.workingDir);
    proc.setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // What the Steam launch options put there for games, and what this Qt app set for itself,
    // do not belong to the VR client (packaging/moonlight-xr.sh does the same)
    for (const char* v : { "LD_LIBRARY_PATH", "MESA_GL_VERSION_OVERRIDE", "MESA_GLSL_VERSION_OVERRIDE", "QSG_RENDER_LOOP",
                           "QML_DISK_CACHE_PATH", "QT_SCALE_FACTOR", "SDL_VIDEODRIVER", "SDL_VIDEO_WAYLAND_WMCLASS", "SDL_VIDEO_X11_WMCLASS" }) {
        env.remove(v);
    }
    if (qEnvironmentVariableIsEmpty("MLXR_LOG_DIR")) {
        env.insert("MLXR_LOG_DIR", QDir::homePath() + "/Games/MoonlightXRNative");
    }
    proc.setProcessEnvironment(env);
    // If the launcher dies (closed, killed), the client must not outlive it with the host session running
    proc.setChildProcessModifier([]() { prctl(PR_SET_PDEATHSIG, SIGTERM); });

    QFile log(consoleLogPath());
    log.open(QIODevice::WriteOnly | QIODevice::Truncate);
    log.write(("== " + QDateTime::currentDateTime().toString(Qt::ISODate) + " launcher runs " + cmd + "\n").toUtf8());
    log.flush();

    QByteArray tail;
    bool streaming = false;
    QObject::connect(&proc, &QProcess::readyRead, [&]() {
        const QByteArray d = proc.readAll();
        log.write(d);
        log.flush();
        tail += d;
        if (!streaming && tail.contains("MLXR_STREAMING")) {
            streaming = true;
            if (onStreaming) onStreaming();
        }
        if (tail.size() > 32768) {
            tail.remove(0, tail.size() - 16384);
        }
    });
    QEventLoop loop;
    QObject::connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &loop, &QEventLoop::quit);
    QObject::connect(&proc, &QProcess::errorOccurred, [&](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            loop.quit();
        }
    });
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit);

    qInfo().noquote() << "Starting the VR client:" << cmd;
    proc.start();
    if (!proc.waitForStarted(10000)) {
        r.startFailed = true;
        r.exitCode = 127;
        r.message = QObject::tr("The VR client could not be started (%1): %2").arg(plan.program, proc.errorString());
        return r;
    }
    if (onStarted) onStarted();
    // The Qt window stays up and answers paints and network replies, but takes no clicks or keys
    // while the headset has the stream (the original does the same while its SDL window runs)
    if (proc.state() != QProcess::NotRunning) {
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    if (proc.state() != QProcess::NotRunning) {
        // The launcher is quitting under a running stream: end the client the way SteamVR quitting does
        qWarning() << "Launcher quitting; asking the VR client to stop";
        proc.terminate();
        if (!proc.waitForFinished(20000)) {
            proc.kill();
            proc.waitForFinished(5000);
        }
    }
    tail += proc.readAll();
    if (proc.exitStatus() == QProcess::CrashExit) {
        r.crashed = true;
        r.exitCode = -1;
        r.message = QObject::tr("The VR client crashed. See %1").arg(consoleLogPath());
    }
    else {
        r.exitCode = proc.exitCode();
        if (r.exitCode == 11) {
            r.disconnected = true; // not an error: no dialog, the app list shows the app as running
        }
        else if (r.exitCode == kExitPcvr) {
            r.disconnected = r.pcvr = true; // the same, and Steam is asked to stream the PC below
        }
        else if (r.exitCode != 0) {
            r.message = lastErrorLine(tail);
            if (r.message.isEmpty()) {
                r.message = QObject::tr("The VR client stopped with code %1. See %2").arg(r.exitCode).arg(consoleLogPath());
            }
        }
    }
    log.write(QString("\n== VR client exited: code %1%2\n").arg(r.exitCode).arg(r.crashed ? " (crashed)" : "").toUtf8());
    if (r.pcvr) {
        startPcvr();   // not the flat screen: the headset goes to Valve's stream
    }
    else {
        reopenFlatScreen();
    }
    qInfo().noquote() << "The VR client exited with" << r.exitCode << r.message;
    return r;
}

// ---- first run: the config -------------------------------------------------------

// Game Mode (gamescope) counts as a desktop for Moonlight, so the UI opens windowed at its
// 1280x600, which gamescope letterboxes on its 16:9 screen. Once, the UI display mode is set to
// Moonlight's own Fullscreen there; Settings changes it back as usual.
static void fullScreenInGameMode()
{
    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP") != "gamescope" || !QFileInfo::exists(QSettings().fileName())) {
        return;
    }
    QSettings s;
    if (s.value("mlxr/ui_fullscreen_set", false).toBool()) {
        return;
    }
    s.setValue("uidisplaymode", 2); // StreamingPreferences::UI_FULLSCREEN
    s.setValue("mlxr/ui_fullscreen_set", true);
    s.sync();
    qInfo() << "Game Mode: the UI display mode set to Fullscreen";
}

static void seedOnce();

void seedLauncherConfig()
{
    seedOnce();
    fullScreenInGameMode();
}

static void seedOnce()
{
    if (envSet("MLXR_NO_SEED")) {
        return;
    }
    const QString dest = QSettings().fileName();
    if (QFileInfo::exists(dest)) {
        return; // only ever on the first run
    }
    QString src = qEnvironmentVariable("MLXR_SEED_CONF");
    if (src.isEmpty()) {
        const QString flatpak = QDir::homePath() + "/" + kFlatpakConf;
        const QString native = QDir::homePath() + "/.config/Moonlight Game Streaming Project/Moonlight.conf";
        src = QFileInfo::exists(flatpak) ? flatpak : native;
    }
    // The stream settings for the headset: Moonlight Qt's defaults (or the imported ones) were chosen
    // for a flat window (720p, or 1080p at 144 here). The user changes them in Settings as usual.
    auto headsetDefaults = [](QSettings& s) {
        s.setValue("width", 2560);
        s.setValue("height", 1440);
        s.setValue("fps", 90);
        // Moonlight XR Native (lane launcher2): the same function as the Settings page's
        // reset, so the first run and a later change of resolution agree (98000 for 1440p at 90)
        s.setValue("bitrate", StreamingPreferences::getHandoffDefaultBitrate(2560, 1440, 90, false));
    };
    if (!QFileInfo::exists(src) || QFileInfo(src) == QFileInfo(dest)) {
        QSettings s;
        headsetDefaults(s);
        s.sync();
        qInfo() << "No Moonlight Qt config to import; starting fresh with the headset's stream settings";
        return;
    }
    QDir().mkpath(QFileInfo(dest).absolutePath());
    if (!QFile::copy(src, dest)) {
        qWarning() << "Could not copy" << src << "to" << dest;
        return;
    }
    QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner);
    // The copy has the identity, the paired PCs (addresses, MACs, pinned certificates, app lists) and
    // every setting; the stream settings are then set for the headset.
    QSettings s;
    headsetDefaults(s);
    s.setValue("hdr", false);
    s.setValue("yuv444", false);
    s.setValue("videocfg", 0);
    s.setValue("audiocfg", 0);
    s.setValue("packetsize", 0);
    s.setValue("mlxr/seeded_from", src);
    s.sync();
    qInfo() << "First run: imported Moonlight Qt's config from" << src << "into" << dest;
}

// ---- self test --------------------------------------------------------------------

// The saved PC (by name or uuid) and its app (by name or id), from the launcher's config
static NvComputer* loadComputer(const QString& spec, NvApp* app, QString* why)
{
    const int colon = spec.lastIndexOf(':');
    const QString hostKey = colon < 0 ? spec : spec.left(colon);
    const QString appKey = colon < 0 ? QString() : spec.mid(colon + 1);

    NvComputer* found = nullptr;
    QSettings settings;
    int n = settings.beginReadArray("hosts");
    for (int i = 0; i < n && !found; i++) {
        settings.setArrayIndex(i);
        NvComputer* c = new NvComputer(settings);
        if (c->uuid.compare(hostKey, Qt::CaseInsensitive) == 0 || c->name.compare(hostKey, Qt::CaseInsensitive) == 0) {
            found = c;
        }
        else {
            delete c;
        }
    }
    settings.endArray();
    if (!found) {
        *why = "no saved PC " + hostKey;
        return nullptr;
    }
    for (const NvApp& a : found->appList) {
        if (QString::number(a.id) == appKey || a.name.compare(appKey, Qt::CaseInsensitive) == 0) {
            *app = a;
        }
    }
    if (!app->id) {
        *why = "no app " + appKey + " on " + found->name;
        delete found;
        return nullptr;
    }
    // The PC would be online and paired here, as the UI only streams from such
    found->state = NvComputer::CS_ONLINE;
    found->pairState = NvComputer::PS_PAIRED;
    if (found->activeAddress.isNull()) {
        found->activeAddress = !found->manualAddress.isNull() ? found->manualAddress : found->localAddress;
    }
    return found;
}

// Moonlight XR Native (lane launcher2): the default bitrate rule, see the header
int headsetDefaultBitrateKbps(double stockMbps)
{
    return qMin(int(qRound(2.0 * stockMbps * 1000.0 / 500.0)) * 500, kMaxDefaultBitrateKbps);
}

double gameModeScale()
{
    if (!inGameMode()) {
        return 1.0;
    }
    double scale = 1.5;
    bool ok = false;
    const double asked = qEnvironmentVariable("MLXR_LAUNCHER_SCALE").trimmed().toDouble(&ok);
    if (ok && asked == asked) { // not NaN
        scale = asked;
    }
    return qBound(1.0, scale, 2.5);
}

void applyGameModeScale()
{
    // Hand-off mode only: with MLXR_HANDOFF=0 the launcher is stock moonlight-qt. (enabled() needs the
    // QCoreApplication for the client path, which does not exist yet, so only the switch is checked.)
    if (!inGameMode() || qEnvironmentVariable("MLXR_HANDOFF") == "0") {
        return;
    }
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR") && qEnvironmentVariable("MLXR_LAUNCHER_SCALE").isEmpty()) {
        return; // the owner's own setting
    }
    qputenv("QT_SCALE_FACTOR", QByteArray::number(gameModeScale(), 'g', 4));
}

// "bitrate:2560x1440x90[x444]" and "scale" for tests/launcher: no UI, nothing else runs
static int selfTestValue(const QString& spec)
{
    if (spec == "scale") {
        QTextStream(stdout) << "SELFTEST: scale=" << QByteArray::number(gameModeScale(), 'g', 4)
                            << " QT_SCALE_FACTOR=" << qEnvironmentVariable("QT_SCALE_FACTOR") << Qt::endl;
        return 0;
    }
    const QStringList p = spec.mid(8).split('x');
    if (p.size() < 3) {
        QTextStream(stderr) << "SELFTEST: bitrate:<W>x<H>x<FPS>[x444]" << Qt::endl;
        return 20;
    }
    QTextStream(stdout) << "SELFTEST: bitrate=" << StreamingPreferences::getDefaultBitrate(p[0].toInt(), p[1].toInt(), p[2].toInt(), p.size() > 3 && p[3] == "444")
                        << " handoff=" << (enabled() ? "yes" : "no") << Qt::endl;
    return 0;
}

// "pcvr:probe", "pcvr:devices", "pcvr:apps[:CLIENT]", "pcvr:go" for tests/launcher: no UI
static int selfTestPcvr(const QString& spec)
{
    const QStringList p = spec.split(':');
    const QString what = p.value(1);
    if (what == "go") {
        startPcvr();
        waitForPcvr(20000);
        return 0;
    }
    QStringList args;
    if (what == "probe") {
        args << "--probe";
    }
    else if (what == "devices") {
        args << "--devices" << "--seconds" << "1";
    }
    else if (what == "apps") {
        args << "--apps";
        if (!p.value(2).isEmpty()) {
            args << "--client" << p.value(2);
        }
    }
    else {
        QTextStream(stderr) << "SELFTEST: pcvr:probe|devices|apps[:CLIENT]|go" << Qt::endl;
        return 20;
    }
    int rc = -3;
    QByteArray out;
    QString err;
    QEventLoop loop;
    runSteamCef(args, 30000, qApp, [&](int c, const QByteArray& o, const QString& e) { rc = c; out = o; err = e; loop.quit(); });
    loop.exec();
    QTextStream(stdout) << "SELFTEST: pcvr " << what << " rc=" << rc << " " << QString::fromUtf8(out).trimmed() << " "
                        << err.simplified().left(300) << Qt::endl;
    return 0;
}

int selfTest(const QString& spec)
{
    if (spec.startsWith("pcvr:")) {
        const int rc = selfTestPcvr(spec);
        fflush(stdout);
        fflush(stderr);
        _exit(rc);
    }
    if (spec == "scale" || spec.startsWith("bitrate:")) {
        const int rc = selfTestValue(spec);
        fflush(stdout);
        fflush(stderr);
        _exit(rc);
    }
    NvApp app;
    QString why;
    NvComputer* found = loadComputer(spec, &app, &why);
    if (!found) {
        QTextStream(stderr) << "SELFTEST: " << why << Qt::endl;
        fflush(stderr);
        _exit(why.startsWith("no app") ? 21 : 20);
    }
    QTextStream(stdout) << "SELFTEST: " << found->name << " " << found->uuid << " app " << app.id << " " << app.name << Qt::endl;

    Session session(found, app, StreamingPreferences::get());
    QString err, started;
    bool finished = false, deleted = false;
    QEventLoop loop;
    QObject::connect(&session, &Session::displayLaunchError, [&](QString t) { err = t; });
    QObject::connect(&session, &Session::vrStreamStarted, [&]() { started = "yes"; });
    QObject::connect(&session, &Session::sessionFinished, [&](int) { finished = true; });
    QObject::connect(&session, &Session::readyForDeletion, [&]() { deleted = true; loop.quit(); });
    session.exec(nullptr);
    if (!deleted) {
        loop.exec();
    }
    QTextStream(stdout) << "SELFTEST: started=" << (started.isEmpty() ? "no" : "yes") << " finished="
                        << (finished ? "yes" : "no") << " error=" << (err.isEmpty() ? "none" : err) << Qt::endl;
    waitForPcvr(20000);   // the PC VR hand-off (exit code 12) asked Steam from run(); let it finish before _exit
    // Leave without static destructors: a test hook, and some library thread (D-Bus) can hold exit up
    const int code = err.isEmpty() ? 0 : 1;
    fflush(stdout);
    fflush(stderr);
    _exit(code);
}


// ---- UI test hooks ------------------------------------------------------------------

void installTestHooks(QQmlApplicationEngine& engine)
{
    // Moonlight XR Native (lane wave4-pcvr, PLAN 4.9): in Game Mode, does Steam's debugging port answer?
    // The PC VR hand-off and the flat screen's return depend on it; the result is logged and, on the
    // Settings page, shown with a hint. MLXR_PCVR_PREFLIGHT=1 asks anywhere (tests).
    if ((inGameMode() && enabled()) || !qEnvironmentVariable("MLXR_PCVR_PREFLIGHT").isEmpty()) {
        MlxrPrefs::get()->pcvrProbe();
    }
    QObject* root = engine.rootObjects().value(0);
    if (!root) {
        return;
    }
    const QString demo = qEnvironmentVariable("MLXR_LAUNCHER_DEMO");
    if (!demo.isEmpty()) {
        QTimer::singleShot(800, root, [&engine, root, demo]() {
            NvApp app;
            QString why;
            NvComputer* c = loadComputer(demo, &app, &why);
            QObject* stack = root->findChild<QObject*>("stackView");
            if (!c || !stack) {
                qWarning() << "MLXR_LAUNCHER_DEMO:" << (c ? "no stackView" : why);
                return;
            }
            Session* session = new Session(c, app, StreamingPreferences::get());
            QQmlEngine::setObjectOwnership(session, QQmlEngine::JavaScriptOwnership);
            QQmlComponent comp(&engine, QUrl("qrc:/gui/StreamSegue.qml"));
            QObject* segue = comp.createWithInitialProperties({ { "appName", app.name }, { "session", QVariant::fromValue(session) } },
                                                             qmlContext(stack));
            if (!segue) {
                qWarning() << "MLXR_LAUNCHER_DEMO:" << comp.errorString();
                return;
            }
            QQmlEngine::setObjectOwnership(segue, QQmlEngine::JavaScriptOwnership);
            QJSValue js = engine.newQObject(stack);
            js.property("push").callWithInstance(js, { engine.newQObject(segue) });
        });
    }
    MlxrUiHooks::install(engine);
}

}
