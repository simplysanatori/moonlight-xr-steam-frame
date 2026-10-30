// Moonlight XR Native launcher: see mlxrprefs.h. GPLv3.
#include "mlxrprefs.h"
#include "mlxrhandoff.h"

#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTimer>
#include <QtMath>

static const char* const kPacing[] = { "latency", "balanced", "smoothness" };

MlxrPrefs* MlxrPrefs::get()
{
    static MlxrPrefs* s_Prefs = nullptr;
    if (!s_Prefs) {
        s_Prefs = new MlxrPrefs();
    }
    return s_Prefs;
}

MlxrPrefs::MlxrPrefs(QObject* parent) : QObject(parent)
{
    load();
    // Native (lane wave4c-usb): a cable plugged in after the app started shows up within ten seconds
    rescanUsb();
    QTimer* usbTimer = new QTimer(this);
    usbTimer->setInterval(10000);
    connect(usbTimer, &QTimer::timeout, this, &MlxrPrefs::rescanUsb);
    usbTimer->start();
}

QString MlxrPrefs::usbCableInterface(const QString& netRoot)
{
    const QDir dir(netRoot);
    const QStringList names = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& name : names) {
        bool usb = name.startsWith("usb");
        if (!usb) {
            QFile u(netRoot + "/" + name + "/uevent");
            usb = u.open(QIODevice::ReadOnly) && QString::fromUtf8(u.readAll()).contains("DEVTYPE=gadget");
        }
        if (!usb) {
            continue;
        }
        QFile c(netRoot + "/" + name + "/carrier");
        if (c.open(QIODevice::ReadOnly) && QString::fromUtf8(c.readAll()).trimmed() == "1") {
            return name;
        }
    }
    return QString();
}

void MlxrPrefs::rescanUsb()
{
    const QString root = qEnvironmentVariable("MLXR_LAUNCHER_NET_ROOT", "/sys/class/net");
    const QString name = usbCableInterface(root);
    const bool now = !name.isEmpty();
    if (now == m_usbCable && m_usbScanned) {
        return;
    }
    m_usbCable = now;
    m_usbScanned = true;
    qInfo().noquote() << "MLXR_LAUNCHER_USB:" << (now ? QString("cable on ") + name : QString("no cable"));
    emit usbCableChanged();
}

QString MlxrPrefs::configIniPath() const
{
    return MlxrHandoff::nativeConfigDir() + "/config.ini";
}

int MlxrPrefs::strengthFromSeparation(int sep)
{
    return qBound(1, qRound(sep * 100.0 / 15.0), 100);
}

int MlxrPrefs::separationFromStrength(int strength)
{
    return qBound(0, qRound(strength * 15.0 / 100.0), 15);
}

static bool iniBool(const QString& v)
{
    const QString s = v.trimmed().toLower();
    return s == "true" || s == "1" || s == "on" || s == "yes";
}

void MlxrPrefs::load()
{
    QSettings s;
    framePacing = s.value("mlxr/frame_pacing", "balanced").toString();
    bool known = false;
    for (const char* p : kPacing) {
        known |= framePacing == p;
    }
    if (!known) {
        framePacing = "balanced";
    }
    ultraLowLatency = s.value("mlxr/ultra_low_latency", false).toBool();
    virtualDisplay = s.value("mlxr/virtual_display", false).toBool();
    scaleFactor = qBound(20, s.value("mlxr/scale_factor", 100).toInt(), 200);
    customRefreshRate = s.value("mlxr/custom_refresh_rate", "").toString().trimmed();
    matchHeadsetFps = s.value("mlxr/match_headset_fps", true).toBool();
    fullRange = s.value("mlxr/full_range", false).toBool();
    rumble = s.value("mlxr/rumble", true).toBool();
    deadzone = qBound(-20, s.value("mlxr/deadzone", 0).toInt(), 20);
    mouseNavButtons = s.value("mlxr/mouse_nav_buttons", true).toBool();
    networkPath = s.value("networkPath", "auto").toString();
    if (networkPath != "home" && networkPath != "adapter" && networkPath != "usb") {
        networkPath = "auto";
    }
    clipboardSync = s.value("mlxr/clipboard_sync", false).toBool();
    quickResume = s.value("mlxr/quick_resume", false).toBool();

    // The VR client's defaults (ui/settings.c) when config.ini or the key is absent
    const QString ini = configIniPath();
    perfOverlay = iniBool(iniValue(ini, "display", "stats", "false"));
    renderMode = iniBool(iniValue(ini, "3d", "stereo", "true")) ? 1 : 0;
    const int sep = iniValue(ini, "3d", "separation", "-1").toInt();
    strengthIsDefault = sep < 0;
    // the depth model's own: ZipDepth 6 (MiDaS 5); shown as Artemis's scale
    strength = strengthFromSeparation(sep < 0 ? 6 : sep);
    const int conv = iniValue(ini, "3d", "convergence", "-1").toInt();
    convergenceIsDefault = conv < 0;
    convergence = conv < 0 ? 50 : qBound(0, conv, 100);
    flipFaceButtons = iniBool(iniValue(ini, "vr", "flip_face_buttons", "false"));
    keyboardKeymap = iniValue(ini, "vr", "keyboard_keymap", "qwerty").trimmed().toLower();
    if (keyboardKeymap != "azerty" && keyboardKeymap != "qwertz") {
        keyboardKeymap = "qwerty";
    }
    // the VR client takes the name or its index; anything else is its default, all
    const QString nt = iniValue(ini, "vr", "notices", "all").trimmed().toLower();
    notices = (nt == "important" || nt == "1") ? "important" : (nt == "off" || nt == "2") ? "off" : "all";
    // the VR client's limits, held to its range
    doffDisconnect = qBound(0, iniValue(ini, "stream", "doff_disconnect", "60").trimmed().toInt(), 3600);
    idleDisconnect = qBound(0, iniValue(ini, "stream", "idle_disconnect", "0").trimmed().toInt(), 1440);

    // the VR client's names, anything else is its default
    // the VR client holds it to 25..400 and takes anything else as its default, 100
    bool speedOk = false;
    const int speed = iniValue(ini, "vr", "scroll_speed", "100").trimmed().toInt(&speedOk);
    scrollSpeed = speedOk ? qBound(25, speed, 400) : 100;
    padMouseChord = iniValue(ini, "vr", "pad_mouse_chord", "start_hold").trimmed().toLower();
    if (padMouseChord != "start_back" && padMouseChord != "off") {
        padMouseChord = "start_hold";
    }
    padMouseScroll = iniValue(ini, "vr", "pad_mouse_scroll", "right").trimmed().toLower();
    if (padMouseScroll != "left" && padMouseScroll != "none") {
        padMouseScroll = "right";
    }
    padArbitration = iniValue(ini, "vr", "pad_arbitration", "pointer").trimmed().toLower();
    if (padArbitration != "also_gamepad" && padArbitration != "sleep") {
        padArbitration = "pointer";
    }

    m_iniPerfOverlay = perfOverlay;
    m_iniRenderMode = renderMode;
    m_iniStrength = strength;
    m_iniStrengthDefault = strengthIsDefault;
    m_iniConvergence = convergence;
    m_iniConvergenceDefault = convergenceIsDefault;
    m_iniFlip = flipFaceButtons;
    m_iniKeymap = keyboardKeymap;
    m_iniNotices = notices;
    m_iniScrollSpeed = scrollSpeed;
    m_iniDoff = doffDisconnect;
    m_iniIdle = idleDisconnect;
    m_iniPadMouseChord = padMouseChord;
    m_iniPadMouseScroll = padMouseScroll;
    m_iniPadArbitration = padArbitration;
}

void MlxrPrefs::save()
{
    QSettings s;
    s.setValue("mlxr/frame_pacing", framePacing);
    s.setValue("mlxr/ultra_low_latency", ultraLowLatency);
    s.setValue("mlxr/virtual_display", virtualDisplay);
    s.setValue("mlxr/scale_factor", qBound(20, scaleFactor, 200));
    s.setValue("mlxr/custom_refresh_rate", customRefreshRate.trimmed());
    s.setValue("mlxr/match_headset_fps", matchHeadsetFps);
    s.setValue("mlxr/full_range", fullRange);
    s.setValue("mlxr/rumble", rumble);
    s.setValue("mlxr/deadzone", qBound(-20, deadzone, 20));
    s.setValue("mlxr/mouse_nav_buttons", mouseNavButtons);
    s.setValue("networkPath", networkPath);
    s.setValue("mlxr/clipboard_sync", clipboardSync);
    s.setValue("mlxr/quick_resume", quickResume);
    s.sync();

    // Only what changed here: the headset may have changed the others since (the cog saves the file)
    const QString ini = configIniPath();
    if (perfOverlay != m_iniPerfOverlay) {
        setIniValue(ini, "display", "stats", perfOverlay ? "true" : "false");
    }
    if (renderMode != m_iniRenderMode) {
        setIniValue(ini, "3d", "stereo", renderMode ? "true" : "false");
    }
    if (strength != m_iniStrength || strengthIsDefault != m_iniStrengthDefault) {
        setIniValue(ini, "3d", "separation", strengthIsDefault ? "-1" : QString::number(separationFromStrength(strength)));
    }
    if (convergence != m_iniConvergence || convergenceIsDefault != m_iniConvergenceDefault) {
        setIniValue(ini, "3d", "convergence", convergenceIsDefault ? "-1" : QString::number(qBound(0, convergence, 100)));
    }
    if (flipFaceButtons != m_iniFlip) {
        setIniValue(ini, "vr", "flip_face_buttons", flipFaceButtons ? "true" : "false");
    }
    if (keyboardKeymap != m_iniKeymap) {
        setIniValue(ini, "vr", "keyboard_keymap", keyboardKeymap);
    }
    if (notices != m_iniNotices) {
        setIniValue(ini, "vr", "notices", notices);
    }
    if (scrollSpeed != m_iniScrollSpeed) {
        setIniValue(ini, "vr", "scroll_speed", QString::number(qBound(25, scrollSpeed, 400)));
    }
    if (doffDisconnect != m_iniDoff) {
        setIniValue(ini, "stream", "doff_disconnect", QString::number(qBound(0, doffDisconnect, 3600)));
    }
    if (idleDisconnect != m_iniIdle) {
        setIniValue(ini, "stream", "idle_disconnect", QString::number(qBound(0, idleDisconnect, 1440)));
    }
    if (padMouseChord != m_iniPadMouseChord) {
        setIniValue(ini, "vr", "pad_mouse_chord", padMouseChord);
    }
    if (padMouseScroll != m_iniPadMouseScroll) {
        setIniValue(ini, "vr", "pad_mouse_scroll", padMouseScroll);
    }
    if (padArbitration != m_iniPadArbitration) {
        setIniValue(ini, "vr", "pad_arbitration", padArbitration);
    }
    m_iniPerfOverlay = perfOverlay;
    m_iniRenderMode = renderMode;
    m_iniStrength = strength;
    m_iniStrengthDefault = strengthIsDefault;
    m_iniConvergence = convergence;
    m_iniConvergenceDefault = convergenceIsDefault;
    m_iniFlip = flipFaceButtons;
    m_iniKeymap = keyboardKeymap;
    m_iniNotices = notices;
    m_iniScrollSpeed = scrollSpeed;
    m_iniDoff = doffDisconnect;
    m_iniIdle = idleDisconnect;
    m_iniPadMouseChord = padMouseChord;
    m_iniPadMouseScroll = padMouseScroll;
    m_iniPadArbitration = padArbitration;
}

int MlxrPrefs::headsetMaxFps() const
{
    return MlxrHandoff::kMaxHeadsetFps;
}

double MlxrPrefs::refreshRate() const
{
    bool ok = false;
    const double r = customRefreshRate.trimmed().toDouble(&ok);
    if (!ok || r < 10.0) {
        return 0;
    }
    return qMin(r, double(headsetMaxFps()));
}

QStringList MlxrPrefs::clientArgs() const
{
    QStringList a;
    if (framePacing != "balanced") {
        a << "--pacing" << framePacing;
    }
    if (ultraLowLatency) {
        a << "--video-priority" << "rt";
    }
    if (virtualDisplay) {
        a << "--virtual-display";
    }
    if (scaleFactor != 100) {
        a << "--scale-factor" << QString::number(qBound(20, scaleFactor, 200));
    }
    if (const double r = refreshRate(); r > 0) {
        a << "--refresh-rate" << QString::number(r, 'f', 3).remove(QRegularExpression("\\.?0+$"));
    }
    else if (matchHeadsetFps) {
        a << "--fps" << "match"; // the VR client resolves the headset's rate before the launch
    }
    if (fullRange) {
        a << "--full-range";
    }
    if (!rumble) {
        a << "--no-rumble";
    }
    if (deadzone != 0) {
        a << "--deadzone" << QString::number(deadzone);
    }
    if (!mouseNavButtons) {
        a << "--no-mouse-nav-buttons";
    }
    if (clipboardSync) {
        a << "--clipboard-sync";
    }
    return a;
}

// ---- the headset's Wi-Fi, for the Network connection row -------------------------------

QString MlxrPrefs::wifiLabelFromIw(const QString& iw)
{
    // Interface blocks: "Interface wlan0" ... "ssid X" ... "type managed" ... "channel 140 (5700 MHz)"
    QString ssid, type;
    int mhz = 0;
    QString found;
    auto finish = [&]() {
        if (found.isEmpty() && type == "managed" && !ssid.isEmpty()) {
            const QString band = mhz <= 0 ? QString() : mhz < 3000 ? "2.4 GHz" : mhz < 5925 ? "5 GHz" : "6 GHz";
            found = band.isEmpty() ? QString("Wi-Fi (%1)").arg(ssid) : QString("Wi-Fi (%1, %2)").arg(ssid, band);
        }
        ssid.clear();
        type.clear();
        mhz = 0;
    };
    static const QRegularExpression chan("^channel\\s+\\d+\\s+\\((\\d+)\\s*MHz\\)");
    for (const QString& raw : iw.split('\n')) {
        const QString t = raw.trimmed();
        if (t.startsWith("Interface ") || t.startsWith("Unnamed/") || t.startsWith("phy#")) {
            finish();
        }
        else if (t.startsWith("ssid ")) {
            ssid = t.mid(5).trimmed();
        }
        else if (t.startsWith("type ")) {
            type = t.mid(5).trimmed();
        }
        else if (const QRegularExpressionMatch m = chan.match(t); m.hasMatch()) {
            mhz = m.captured(1).toInt();
        }
    }
    finish();
    return found.isEmpty() ? QString("Wi-Fi") : found;
}

QString MlxrPrefs::wifiLabel() const
{
    QString text;
    const QString file = qEnvironmentVariable("MLXR_LAUNCHER_IW_FILE");
    if (!file.isEmpty()) {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(f.readAll());
        }
    }
    else {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove("LD_LIBRARY_PATH");
        p.setProcessEnvironment(env);
        p.start("iw", { "dev" });
        if (p.waitForFinished(1500)) {
            text = QString::fromUtf8(p.readAllStandardOutput());
        }
        else {
            p.kill();
            p.waitForFinished(500);
        }
    }
    const QString label = wifiLabelFromIw(text);
    qInfo().noquote() << "MLXR_LAUNCHER_WIFI:" << label;
    return label;
}

// ---- config.ini ----------------------------------------------------------------------

static QRegularExpression keyLine(const QString& key)
{
    return QRegularExpression("^\\s*(#\\s*)?" + QRegularExpression::escape(key) + "\\s*=\\s*(.*?)\\s*$");
}

QString MlxrPrefs::iniValue(const QString& path, const QString& section, const QString& key, const QString& def)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return def;
    }
    const QRegularExpression re = keyLine(key);
    QString cur, commented, activeVal;
    bool haveCommented = false, haveActive = false;
    for (const QString& raw : QString::fromUtf8(f.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            cur = line.mid(1, line.size() - 2).trimmed();
            continue;
        }
        if (cur.compare(section, Qt::CaseInsensitive) != 0) {
            continue;
        }
        const QRegularExpressionMatch m = re.match(raw);
        if (!m.hasMatch()) {
            continue;
        }
        if (m.captured(1).isEmpty()) {
            activeVal = m.captured(2); // an active line wins (the last one, as the VR client reads them in order)
            haveActive = true;
            continue;
        }
        if (!haveCommented) {
            commented = m.captured(2);
            haveCommented = true;
        }
    }
    return haveActive ? activeVal : haveCommented ? commented : def;
}

bool MlxrPrefs::setIniValue(const QString& path, const QString& section, const QString& key, const QString& value)
{
    QStringList lines;
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            lines = QString::fromUtf8(f.readAll()).split('\n');
            if (!lines.isEmpty() && lines.last().isEmpty()) {
                lines.removeLast();
            }
        }
    }
    const QRegularExpression re = keyLine(key);
    const QString want = key + " = " + value;
    QString cur;
    int sectionEnd = -1, active = -1, commented = -1;
    bool inSection = false;
    for (int i = 0; i < lines.size(); i++) {
        const QString t = lines[i].trimmed();
        if (t.startsWith('[') && t.endsWith(']')) {
            inSection = t.mid(1, t.size() - 2).trimmed().compare(section, Qt::CaseInsensitive) == 0;
            if (inSection) {
                sectionEnd = i + 1;
            }
            continue;
        }
        if (!inSection) {
            continue;
        }
        if (!t.isEmpty()) {
            sectionEnd = i + 1;
        }
        const QRegularExpressionMatch m = re.match(lines[i]);
        if (m.hasMatch()) {
            if (m.captured(1).isEmpty()) {
                active = i;
            }
            else if (commented < 0) {
                commented = i;
            }
        }
    }
    if (active >= 0) {
        lines[active] = want;
    }
    else if (commented >= 0) {
        lines[commented] = want;
    }
    else if (sectionEnd >= 0) {
        lines.insert(sectionEnd, want);
    }
    else {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) {
            lines << QString();
        }
        lines << "[" + section + "]" << want;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool existed = QFileInfo::exists(path);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    out.write((lines.join('\n') + "\n").toUtf8());
    if (!out.commit()) {
        return false;
    }
    if (!existed) {
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    }
    return true;
}
