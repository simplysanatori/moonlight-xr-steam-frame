// Moonlight XR Native launcher: see mlxrprefs.h. GPLv3.
#include "mlxrprefs.h"
#include "mlxrhandoff.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

// ---- PC VR (lane wave4-pcvr) ---------------------------------------------------------------------------

void MlxrPrefs::pcvrSay(const QString& status, bool busy)
{
    m_pcvrStatus = status;
    m_pcvrBusy = busy;
    emit pcvrChanged();
}

QString MlxrPrefs::pcvrProbeSummary(const QByteArray& json, int exitCode, const QString& stderrText, bool* ok)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    if (ok) {
        *ok = o.value("ok").toBool();
    }
    QString hint = o.value("hint").toString();
    if (hint.isEmpty()) {
        // no JSON at all: python or the script did not run (-1), timed out (-2), or crashed
        hint = stderrText.simplified().left(300);
        if (hint.isEmpty()) {
            hint = QObject::tr("steam_cef.py gave no answer (exit %1)").arg(exitCode);
        }
        if (ok) {
            *ok = false;
        }
    }
    return hint;
}

void MlxrPrefs::pcvrProbe()
{
    MlxrHandoff::runSteamCef({ "--probe" }, 20000, this, [this](int code, const QByteArray& out, const QString& err) {
        bool ok = false;
        const QString hint = pcvrProbeSummary(out, code, err, &ok);
        qInfo().noquote() << "Steam CEF preflight:" << (ok ? "ok" : "NOT OK") << "-" << hint;
        pcvrSay(hint, false);
    });
}

void MlxrPrefs::pcvrRefresh()
{
    if (m_pcvrBusy) {
        return;
    }
    pcvrSay(QObject::tr("Asking Steam..."), true);
    MlxrHandoff::runSteamCef({ "--probe" }, 20000, this, [this](int code, const QByteArray& out, const QString& err) {
        bool ok = false;
        const QString hint = pcvrProbeSummary(out, code, err, &ok);
        const QJsonObject o = QJsonDocument::fromJson(out).object();
        qInfo().noquote() << "Steam CEF preflight:" << (ok ? "ok" : "NOT OK") << "-" << hint;
        if (!o.value("reachable").toBool() || !o.value("context").toBool()) {
            m_pcvrDevices.clear();
            m_pcvrApps.clear();
            pcvrSay(hint, false);
            return;
        }
        MlxrHandoff::runSteamCef({ "--devices", "--seconds", "3" }, 25000, this,
                                 [this, hint, ok](int dcode, const QByteArray& dout, const QString& derr) {
            const QJsonDocument doc = QJsonDocument::fromJson(dout);
            if (dcode != 0 || !doc.isArray()) {
                m_pcvrDevices.clear();
                pcvrSay(QObject::tr("Steam did not list the paired PCs: %1").arg(derr.isEmpty() ? hint : derr.simplified().left(300)), false);
                return;
            }
            m_pcvrDevices.clear();
            for (const QJsonValue& v : doc.array()) {
                m_pcvrDevices << v.toObject().toVariantMap();
            }
            // The only paired PC is the one to stream from, unless one was chosen
            if (pcvrClient.isEmpty() && m_pcvrDevices.size() == 1) {
                pcvrClient = m_pcvrDevices[0].toMap().value("clientId").toString();
                pcvrClientName = m_pcvrDevices[0].toMap().value("clientName").toString();
                emit changed();
            }
            m_pcvrBase = (ok ? QObject::tr("Steam is ready.") : hint) + " " + QObject::tr("%n paired PC(s) found.", "", m_pcvrDevices.size());
            pcvrSay(m_pcvrBase, true);
            pcvrLoadApps();
        });
    });
}

void MlxrPrefs::pcvrLoadApps()
{
    QStringList args { "--apps" };
    if (!pcvrClient.isEmpty()) {
        args << "--client" << pcvrClient;
    }
    MlxrHandoff::runSteamCef(args, 30000, this, [this](int code, const QByteArray& out, const QString& err) {
        const QJsonDocument doc = QJsonDocument::fromJson(out);
        m_pcvrApps.clear();
        if (code == 0 && doc.isArray()) {
            for (const QJsonValue& v : doc.array()) {
                QVariantMap m = v.toObject().toVariantMap();
                m["appid"] = QString::number(static_cast<qlonglong>(v.toObject().value("appid").toDouble()));   // a whole number, not a double
                m_pcvrApps << m;
            }
            pcvrSay(m_pcvrBase + " " + QObject::tr("%n game(s) on the PC.", "", m_pcvrApps.size()), false);
        }
        else {
            pcvrSay(m_pcvrBase + " " + QObject::tr("Steam did not list the PC's games (%1): type a game's Steam app id instead.")
                        .arg(err.isEmpty() ? QString::number(code) : err.simplified().left(200)), false);
        }
    });
}

void MlxrPrefs::pcvrChooseDevice(int index)
{
    const QString before = pcvrClient;
    if (index >= 0 && index < m_pcvrDevices.size()) {
        const QVariantMap d = m_pcvrDevices[index].toMap();
        pcvrClient = d.value("clientId").toString();
        pcvrClientName = d.value("clientName").toString();
    }
    else {
        pcvrClient.clear();
        pcvrClientName.clear();
    }
    emit changed();
    if (pcvrClient != before && !m_pcvrBusy) {
        pcvrSay(m_pcvrStatus, true);
        pcvrLoadApps();
    }
}

void MlxrPrefs::pcvrChooseApp(int index)
{
    if (index >= 0 && index < m_pcvrApps.size()) {
        const QVariantMap a = m_pcvrApps[index].toMap();
        pcvrApp = a.value("appid").toString();
        pcvrAppName = a.value("name").toString();
    }
    else {
        pcvrApp.clear();
        pcvrAppName.clear();
    }
    emit changed();
}

void MlxrPrefs::pcvrSetAppId(const QString& id)
{
    const QString t = id.trimmed();
    pcvrApp = t;
    pcvrAppName.clear();
    for (const QVariant& a : m_pcvrApps) {
        if (a.toMap().value("appid").toString() == t) {
            pcvrAppName = a.toMap().value("name").toString();
        }
    }
    emit changed();
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
    preflight = s.value("mlxr/preflight", true).toBool();
    pcvrApp = s.value("mlxr/pcvr_app", "").toString().trimmed();
    pcvrAppName = s.value("mlxr/pcvr_app_name", "").toString();
    pcvrClient = s.value("mlxr/pcvr_client", "").toString().trimmed();
    pcvrClientName = s.value("mlxr/pcvr_client_name", "").toString();

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
    // the VR client holds these to their ranges and takes anything else as its default
    bool balanceOk = false;
    const int balance = iniValue(ini, "3d", "eye_balance", "0").trimmed().toInt(&balanceOk);
    eyeBalance = balanceOk ? qBound(-100, balance, 100) : 0;
    bool dimOk = false;
    const int dim = iniValue(ini, "vr", "background_dim", "0").trimmed().toInt(&dimOk);
    backgroundDim = dimOk ? qBound(0, dim, 100) : 0;
    voidColor = iniValue(ini, "vr", "void_color", "#000000").trimmed();
    if (!QRegularExpression("^#?([0-9a-fA-F]{3}|[0-9a-fA-F]{6})$").match(voidColor).hasMatch()) {
        voidColor = "#000000";
    }
    const QString gf = iniValue(ini, "display", "glow_feather", "off").trimmed().toLower();
    glowFeather = (gf == "soft" || gf == "1") ? "soft" : (gf == "wide" || gf == "2") ? "wide" : "off";
    // the VR client takes the name or its index; anything else is its default, depth
    const QString src = iniValue(ini, "3d", "stereo_source", "depth").trimmed().toLower();
    stereoSource = (src == "sbs" || src == "1") ? "sbs" : (src == "top-bottom" || src == "2") ? "top-bottom" : "depth";
    sbsFull = iniBool(iniValue(ini, "3d", "sbs_full", "false"));
    // the VR client holds it to 0..100 and takes anything else as its default, 0
    bool edgeOk = false;
    const int edge = iniValue(ini, "3d", "edge_sharpness", "0").trimmed().toInt(&edgeOk);
    edgeSharpness = edgeOk ? qBound(0, edge, 100) : 0;
    // 0..50, the same way
    bool motionOk = false;
    const int motion = iniValue(ini, "3d", "motion_reset", "0").trimmed().toInt(&motionOk);
    motionReset = motionOk ? qBound(0, motion, 50) : 0;
    // 0..200, default 100
    bool scaleOk = false, globalOk = false;
    const int scale = iniValue(ini, "3d", "depth_scale", "100").trimmed().toInt(&scaleOk);
    const int global = iniValue(ini, "3d", "depth_global", "100").trimmed().toInt(&globalOk);
    depthScale = scaleOk ? qBound(0, scale, 200) : 100;
    depthGlobal = globalOk ? qBound(0, global, 200) : 100;
    flipFaceButtons = iniBool(iniValue(ini, "vr", "flip_face_buttons", "false"));
    keyboardKeymap = iniValue(ini, "vr", "keyboard_keymap", "qwerty").trimmed().toLower();
    if (keyboardKeymap != "azerty" && keyboardKeymap != "qwertz") {
        keyboardKeymap = "qwerty";
    }
    // the VR client takes the name or its index; anything else is its default, all
    const QString nt = iniValue(ini, "vr", "notices", "all").trimmed().toLower();
    notices = (nt == "important" || nt == "1") ? "important" : (nt == "off" || nt == "2") ? "off" : "all";
    // the VR client takes the name, its index, or the true and false the key was a switch with
    const QString cm = iniValue(ini, "vr", "controller_model", "off").trimmed().toLower();
    controllerModel = (cm == "frame" || cm == "1" || cm == "true" || cm == "on" || cm == "yes" || cm == "y") ? "frame"
                    : (cm == "generic" || cm == "2") ? "generic"
                    : (cm == "gun" || cm == "3") ? "gun"
                    : (cm == "custom" || cm == "4") ? "custom" : "off";
    // the VR client's limits, held to its range
    doffDisconnect = qBound(0, iniValue(ini, "stream", "doff_disconnect", "60").trimmed().toInt(), 3600);
    idleDisconnect = qBound(0, iniValue(ini, "stream", "idle_disconnect", "0").trimmed().toInt(), 1440);
    // Native (lane wave4b-abr): the per-path start bitrates, the VR client's rule: 0, or 500..500000 kbps
    auto pathBitrate = [&ini](const char* key) {
        const int v = iniValue(ini, "stream", key, "0").trimmed().toInt();
        return v <= 0 ? 0 : qBound(500, v, 500000);
    };
    bitrateWifi = pathBitrate("bitrate_wifi");
    bitrateAdapter = pathBitrate("bitrate_adapter");
    bitrateWired = pathBitrate("bitrate_wired");
    bitrateUsb = pathBitrate("bitrate_usb");
    // Native (lane wave1-burst): not a number is the VR client's default
    bool idrOk = false;
    const int idrMs = iniValue(ini, "stream", "rfi_idr_ms", "500").trimmed().toInt(&idrOk);
    rfiIdrMs = idrOk ? qBound(0, idrMs, 5000) : 500;
    // Native (lane wave1-failover): off unless the file says on; a length that is not a number is the default
    pathFailover = iniBool(iniValue(ini, "stream", "failover", "off"));
    bool foOk = false;
    const int foMs = iniValue(ini, "stream", "failover_burst_ms", "3000").trimmed().toInt(&foOk);
    failoverBurstMs = foOk ? qBound(500, foMs, 9000) : 3000;

    // the VR client's names, anything else is its default
    // the VR client holds it to 25..400 and takes anything else as its default, 100
    bool speedOk = false;
    const int speed = iniValue(ini, "vr", "scroll_speed", "100").trimmed().toInt(&speedOk);
    scrollSpeed = speedOk ? qBound(25, speed, 400) : 100;
    // the VR client holds it to 100..400 and takes anything else as its default, 100
    bool zoomOk = false;
    const int zoomPct = iniValue(ini, "display", "zoom", "100").trimmed().toInt(&zoomOk);
    zoom = zoomOk ? qBound(100, zoomPct, 400) : 100;
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
    // the VR client takes the name or its index; anything else is its default, auto
    const QString pc = iniValue(ini, "vr", "pad_card", "auto").trimmed().toLower();
    padCard = (pc == "on" || pc == "1") ? "on" : (pc == "off" || pc == "2") ? "off" : "auto";
    firstRunShown = iniBool(iniValue(ini, "vr", "first_run_shown", "false"));
    // the VR client takes the name or its index, anything else is its default
    const QString lg = iniValue(ini, "vr", "lightgun", "auto").trimmed().toLower();
    lightgun = (lg == "on" || lg == "1") ? "on" : (lg == "off" || lg == "2") ? "off" : "auto";
    const QString pg = iniValue(ini, "vr", "pad_gyro", "off").trimmed().toLower();
    padGyro = (pg == "controller" || pg == "1") ? "controller" : (pg == "head" || pg == "2") ? "head" : "off";

    // the VR client's spelling; anything else is its default, auto
    headsetLanguage = normalizeLanguage(iniValue(ini, "ui", "language", "auto"));

    m_iniPerfOverlay = perfOverlay;
    m_iniRenderMode = renderMode;
    m_iniStrength = strength;
    m_iniStrengthDefault = strengthIsDefault;
    m_iniConvergence = convergence;
    m_iniConvergenceDefault = convergenceIsDefault;
    m_iniEyeBalance = eyeBalance;
    m_iniBackgroundDim = backgroundDim;
    m_iniVoidColor = voidColor;
    m_iniGlowFeather = glowFeather;
    m_iniStereoSource = stereoSource;
    m_iniSbsFull = sbsFull;
    m_iniEdgeSharpness = edgeSharpness;
    m_iniMotionReset = motionReset;
    m_iniDepthScale = depthScale;
    m_iniDepthGlobal = depthGlobal;
    m_iniFlip = flipFaceButtons;
    m_iniKeymap = keyboardKeymap;
    m_iniNotices = notices;
    m_iniScrollSpeed = scrollSpeed;
    m_iniZoom = zoom;
    m_iniControllerModel = controllerModel;
    m_iniDoff = doffDisconnect;
    m_iniIdle = idleDisconnect;
    m_iniBitrateWifi = bitrateWifi;
    m_iniBitrateAdapter = bitrateAdapter;
    m_iniBitrateWired = bitrateWired;
    m_iniBitrateUsb = bitrateUsb;
    m_iniRfiIdr = rfiIdrMs;
    m_iniFailover = pathFailover;
    m_iniFailoverBurst = failoverBurstMs;
    m_iniPadMouseChord = padMouseChord;
    m_iniPadMouseScroll = padMouseScroll;
    m_iniPadArbitration = padArbitration;
    m_iniHeadsetLanguage = headsetLanguage;
    m_iniPadCard = padCard;
    m_iniFirstRunShown = firstRunShown;
    m_iniLightgun = lightgun;
    m_iniPadGyro = padGyro;
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
    s.setValue("mlxr/preflight", preflight);
    s.setValue("mlxr/pcvr_app", pcvrApp.trimmed());
    s.setValue("mlxr/pcvr_app_name", pcvrAppName);
    s.setValue("mlxr/pcvr_client", pcvrClient.trimmed());
    s.setValue("mlxr/pcvr_client_name", pcvrClientName);
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
    if (eyeBalance != m_iniEyeBalance) {
        setIniValue(ini, "3d", "eye_balance", QString::number(qBound(-100, eyeBalance, 100)));
    }
    if (backgroundDim != m_iniBackgroundDim) {
        setIniValue(ini, "vr", "background_dim", QString::number(qBound(0, backgroundDim, 100)));
    }
    if (voidColor != m_iniVoidColor) {
        setIniValue(ini, "vr", "void_color", voidColor);
    }
    if (glowFeather != m_iniGlowFeather) {
        setIniValue(ini, "display", "glow_feather", glowFeather);
    }
    if (stereoSource != m_iniStereoSource) {
        setIniValue(ini, "3d", "stereo_source", stereoSource);
    }
    if (sbsFull != m_iniSbsFull) {
        setIniValue(ini, "3d", "sbs_full", sbsFull ? "true" : "false");
    }
    if (edgeSharpness != m_iniEdgeSharpness) {
        setIniValue(ini, "3d", "edge_sharpness", QString::number(qBound(0, edgeSharpness, 100)));
    }
    if (motionReset != m_iniMotionReset) {
        setIniValue(ini, "3d", "motion_reset", QString::number(qBound(0, motionReset, 50)));
    }
    if (depthScale != m_iniDepthScale) {
        setIniValue(ini, "3d", "depth_scale", QString::number(qBound(0, depthScale, 200)));
    }
    if (depthGlobal != m_iniDepthGlobal) {
        setIniValue(ini, "3d", "depth_global", QString::number(qBound(0, depthGlobal, 200)));
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
    if (zoom != m_iniZoom) {
        setIniValue(ini, "display", "zoom", QString::number(qBound(100, zoom, 400)));
    }
    if (controllerModel != m_iniControllerModel) {
        setIniValue(ini, "vr", "controller_model", controllerModel);
    }
    if (doffDisconnect != m_iniDoff) {
        setIniValue(ini, "stream", "doff_disconnect", QString::number(qBound(0, doffDisconnect, 3600)));
    }
    if (idleDisconnect != m_iniIdle) {
        setIniValue(ini, "stream", "idle_disconnect", QString::number(qBound(0, idleDisconnect, 1440)));
    }
    // Native (lane wave4b-abr)
    const auto writeBitrate = [&ini](const char* key, int now, int was) {
        if (now != was) setIniValue(ini, "stream", key, QString::number(now <= 0 ? 0 : qBound(500, now, 500000)));
    };
    writeBitrate("bitrate_wifi", bitrateWifi, m_iniBitrateWifi);
    writeBitrate("bitrate_adapter", bitrateAdapter, m_iniBitrateAdapter);
    writeBitrate("bitrate_wired", bitrateWired, m_iniBitrateWired);
    writeBitrate("bitrate_usb", bitrateUsb, m_iniBitrateUsb);
    if (rfiIdrMs != m_iniRfiIdr) {
        setIniValue(ini, "stream", "rfi_idr_ms", QString::number(qBound(0, rfiIdrMs, 5000)));
    }
    if (pathFailover != m_iniFailover) {
        setIniValue(ini, "stream", "failover", pathFailover ? "on" : "off");
    }
    if (failoverBurstMs != m_iniFailoverBurst) {
        setIniValue(ini, "stream", "failover_burst_ms", QString::number(qBound(500, failoverBurstMs, 9000)));
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
    if (headsetLanguage != m_iniHeadsetLanguage) {
        setIniValue(ini, "ui", "language", normalizeLanguage(headsetLanguage));
    }
    if (padCard != m_iniPadCard) {
        setIniValue(ini, "vr", "pad_card", padCard);
    }
    if (firstRunShown != m_iniFirstRunShown) {
        setIniValue(ini, "vr", "first_run_shown", firstRunShown ? "true" : "false");
    }
    if (lightgun != m_iniLightgun) {
        setIniValue(ini, "vr", "lightgun", lightgun);
    }
    if (padGyro != m_iniPadGyro) {
        setIniValue(ini, "vr", "pad_gyro", padGyro);
    }
    m_iniPerfOverlay = perfOverlay;
    m_iniRenderMode = renderMode;
    m_iniStrength = strength;
    m_iniStrengthDefault = strengthIsDefault;
    m_iniConvergence = convergence;
    m_iniConvergenceDefault = convergenceIsDefault;
    m_iniEyeBalance = eyeBalance;
    m_iniBackgroundDim = backgroundDim;
    m_iniVoidColor = voidColor;
    m_iniGlowFeather = glowFeather;
    m_iniStereoSource = stereoSource;
    m_iniSbsFull = sbsFull;
    m_iniEdgeSharpness = edgeSharpness;
    m_iniMotionReset = motionReset;
    m_iniDepthScale = depthScale;
    m_iniDepthGlobal = depthGlobal;
    m_iniFlip = flipFaceButtons;
    m_iniKeymap = keyboardKeymap;
    m_iniNotices = notices;
    m_iniScrollSpeed = scrollSpeed;
    m_iniZoom = zoom;
    m_iniControllerModel = controllerModel;
    m_iniDoff = doffDisconnect;
    m_iniIdle = idleDisconnect;
    m_iniBitrateWifi = bitrateWifi;
    m_iniBitrateAdapter = bitrateAdapter;
    m_iniBitrateWired = bitrateWired;
    m_iniBitrateUsb = bitrateUsb;
    m_iniRfiIdr = rfiIdrMs;
    m_iniFailover = pathFailover;
    m_iniFailoverBurst = failoverBurstMs;
    m_iniPadMouseChord = padMouseChord;
    m_iniPadMouseScroll = padMouseScroll;
    m_iniPadArbitration = padArbitration;
    m_iniHeadsetLanguage = headsetLanguage;
    m_iniPadCard = padCard;
    m_iniFirstRunShown = firstRunShown;
    m_iniLightgun = lightgun;
    m_iniPadGyro = padGyro;
}

QString MlxrPrefs::normalizeLanguage(const QString& v)
{
    static const QRegularExpression re(QStringLiteral("^([A-Za-z]{2,3})(?:[-_]([A-Za-z]{2,4}))?$"));
    const QRegularExpressionMatch m = re.match(v.trimmed());
    if (!m.hasMatch()) {
        return QStringLiteral("auto");
    }
    const QString lang = m.captured(1).toLower();
    return m.captured(2).isEmpty() ? lang : lang + "-" + m.captured(2).toUpper();
}

void MlxrPrefs::reloadAll()
{
    load();
    emit changed();
}

QVector<MlxrPrefs::IniEntry> MlxrPrefs::profileIniEntries() const
{
    // The same text save() writes for each of these keys
    return {
        { "display", "stats", perfOverlay ? "true" : "false" },
        { "3d", "stereo", renderMode ? "true" : "false" },
        { "3d", "separation", strengthIsDefault ? QString("-1") : QString::number(separationFromStrength(strength)) },
        { "3d", "convergence", convergenceIsDefault ? QString("-1") : QString::number(qBound(0, convergence, 100)) },
        { "3d", "edge_sharpness", QString::number(qBound(0, edgeSharpness, 100)) },
        { "3d", "motion_reset", QString::number(qBound(0, motionReset, 50)) },
        { "3d", "depth_scale", QString::number(qBound(0, depthScale, 200)) },
        { "3d", "depth_global", QString::number(qBound(0, depthGlobal, 200)) },
        { "3d", "stereo_source", stereoSource },
        { "3d", "sbs_full", sbsFull ? "true" : "false" },
        { "stream", "rfi_idr_ms", QString::number(qBound(0, rfiIdrMs, 5000)) },
        { "3d", "eye_balance", QString::number(qBound(-100, eyeBalance, 100)) },
        { "stream", "bitrate_wifi", QString::number(bitrateWifi <= 0 ? 0 : qBound(500, bitrateWifi, 500000)) },
        { "stream", "bitrate_adapter", QString::number(bitrateAdapter <= 0 ? 0 : qBound(500, bitrateAdapter, 500000)) },
        { "stream", "bitrate_wired", QString::number(bitrateWired <= 0 ? 0 : qBound(500, bitrateWired, 500000)) },
        { "stream", "bitrate_usb", QString::number(bitrateUsb <= 0 ? 0 : qBound(500, bitrateUsb, 500000)) },
        { "vr", "lightgun", lightgun },
        { "vr", "pad_gyro", padGyro },
        { "vr", "flip_face_buttons", flipFaceButtons ? "true" : "false" },
        { "vr", "notices", notices },
        { "vr", "scroll_speed", QString::number(qBound(25, scrollSpeed, 400)) },
        { "stream", "doff_disconnect", QString::number(qBound(0, doffDisconnect, 3600)) },
        { "stream", "idle_disconnect", QString::number(qBound(0, idleDisconnect, 1440)) },
        { "vr", "pad_mouse_chord", padMouseChord },
        { "vr", "pad_mouse_scroll", padMouseScroll },
        { "vr", "pad_arbitration", padArbitration },
    };
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
