// Moonlight XR Native launcher: the hand-off to the native OpenXR client. GPLv3.
// See mlxrhandoff.h and launcher/REPORT.md.
#include "mlxrhandoff.h"

#include "backend/identitymanager.h"
#include "backend/nvcomputer.h"
#include "backend/nvapp.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"
#include "streaming/uihooks.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJSValue>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
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

static const int kMaxHeadsetFps = 90;

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
    // The headset shows 90 Hz at most (the VR client lowers it further to SteamVR's own rate).
    // Moonlight's flat settings can say 120 or 144 (Game Mode's gamescope reports the screen's
    // rate), which a Sunshine host would turn into a virtual display at that rate.
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

    QList<HostRecord> all = readHosts(dir + "/hosts.ini");
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
static void reopenFlatScreen()
{
    const QString appId = qEnvironmentVariable("SteamAppId");
    const QString cef = QCoreApplication::applicationDirPath() + "/steam_cef.py";
    if (qEnvironmentVariable("XDG_CURRENT_DESKTOP") != "gamescope" || appId.isEmpty() || !QFileInfo::exists(cef)) {
        return;
    }
    // Asked right away the dashboard opens before this window is back up and SteamVR shows
    // nothing in it; so again once the window has been shown and drawn (StreamSegue shows it as
    // soon as this returns)
    const QString js = QString("SteamClient.OpenVR.VROverlay.ShowDashboard('valve.steam.desktopgame.%1')").arg(appId);
    for (int ms : { 0, 1000, 2500 }) {
        QTimer::singleShot(ms, qApp, [cef, js, ms]() {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.remove("LD_LIBRARY_PATH");
            QProcess* p = new QProcess(qApp);
            p->setProcessEnvironment(env);
            QObject::connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), p,
                             [p, ms](int code, QProcess::ExitStatus) {
                qInfo().noquote() << "Asked Steam to show the flat screen again after" << ms << "ms:"
                                  << (code == 0 ? "done" : "failed") << p->readAll().trimmed();
                p->deleteLater();
            });
            p->setProcessChannelMode(QProcess::MergedChannels);
            p->start("python3", { "-I", cef, js });
        });
    }
}

Result run(const Plan& plan, const std::function<void()>& onStarted, const std::function<void()>& onStreaming)
{
    Result r;
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
                           "QML_DISK_CACHE_PATH", "SDL_VIDEODRIVER", "SDL_VIDEO_WAYLAND_WMCLASS", "SDL_VIDEO_X11_WMCLASS" }) {
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
        else if (r.exitCode != 0) {
            r.message = lastErrorLine(tail);
            if (r.message.isEmpty()) {
                r.message = QObject::tr("The VR client stopped with code %1. See %2").arg(r.exitCode).arg(consoleLogPath());
            }
        }
    }
    log.write(QString("\n== VR client exited: code %1%2\n").arg(r.exitCode).arg(r.crashed ? " (crashed)" : "").toUtf8());
    reopenFlatScreen();
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
    if (!QFileInfo::exists(src) || QFileInfo(src) == QFileInfo(dest)) {
        qInfo() << "No Moonlight Qt config to import; starting fresh";
        return;
    }
    QDir().mkpath(QFileInfo(dest).absolutePath());
    if (!QFile::copy(src, dest)) {
        qWarning() << "Could not copy" << src << "to" << dest;
        return;
    }
    QFile::setPermissions(dest, QFile::ReadOwner | QFile::WriteOwner);
    // The copy has the identity, the paired PCs (addresses, MACs, pinned certificates, app lists) and
    // every setting. The stream settings are set for the headset: Moonlight Qt's were chosen for a
    // flat window (1080p at 144 here). The user changes them in Settings as usual.
    QSettings s;
    s.setValue("width", 2560);
    s.setValue("height", 1440);
    s.setValue("fps", 90);
    s.setValue("bitrate", 80000);
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

int selfTest(const QString& spec)
{
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
    // Leave without static destructors: a test hook, and some library thread (D-Bus) can hold exit up
    const int code = err.isEmpty() ? 0 : 1;
    fflush(stdout);
    fflush(stderr);
    _exit(code);
}


// ---- UI test hooks ------------------------------------------------------------------

void installTestHooks(QQmlApplicationEngine& engine)
{
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
