// Moonlight XR Native launcher (lane wave4-launcher): see mlxrpreflight.h. GPLv3.
#include "mlxrpreflight.h"
#include "mlxrprefs.h"

#include "backend/nvaddress.h"
#include "backend/nvapp.h"
#include "backend/nvcomputer.h"
#include "backend/nvhttp.h"
#include "settings/streamingpreferences.h"

#include <QDebug>
#include <QPair>
#include <QReadLocker>
#include <QSslCertificate>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

namespace MlxrPreflight {

Facts parseServerInfo(const QString& xml)
{
    Facts f;
    QXmlStreamReader r(xml);
    bool inMode = false;
    Mode cur;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isEndElement()) {
            if (inMode && r.name() == QLatin1String("DisplayMode")) {
                f.modes.append(cur);
                inMode = false;
            }
            continue;
        }
        if (!r.isStartElement()) {
            continue;
        }
        const QString n = r.name().toString();
        if (n == QLatin1String("root")) {
            f.valid = r.attributes().value("status_code") == QLatin1String("200");
            continue;
        }
        if (n == QLatin1String("DisplayMode")) {
            inMode = true;
            cur = Mode();
            continue;
        }
        if (n == QLatin1String("SupportedDisplayMode")) {
            continue; // the list of DisplayMode elements: read them one by one
        }
        const QString text = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
        if (inMode) {
            if (n == QLatin1String("Width")) {
                cur.width = text.toInt();
            }
            else if (n == QLatin1String("Height")) {
                cur.height = text.toInt();
            }
            else if (n == QLatin1String("RefreshRate")) {
                cur.refresh = text.toInt();
            }
        }
        else if (n == QLatin1String("hostname")) {
            f.hostname = text;
        }
        else if (n == QLatin1String("state")) {
            f.state = text;
        }
        else if (n == QLatin1String("currentgame")) {
            f.currentGame = text.toInt();
        }
        else if (n == QLatin1String("currentgameuuid")) {
            f.currentGameUuid = text;
        }
        else if (n == QLatin1String("VirtualDisplayCapable")) {
            f.vdCapable = text == QLatin1String("true");
        }
        else if (n == QLatin1String("VirtualDisplayDriverReady")) {
            f.vdReady = text == QLatin1String("true");
        }
        else if (n == QLatin1String("Permission")) {
            bool ok = false;
            const qlonglong v = text.toLongLong(&ok);
            f.permission = (ok && v >= 0 && v <= 0x7fffffffLL) ? int(v) : -1;
        }
        else if (n == QLatin1String("PairStatus")) {
            f.paired = text == QLatin1String("1");
        }
        else if (n == QLatin1String("ServerCodecModeSupport")) {
            bool ok = false;
            const int v = text.toInt(&ok);
            f.codecSupport = ok ? v : 1;
        }
    }
    // GFE 2.8+ keeps currentgame at the last game: only trust it while the host is busy (as the stock client does)
    if (!f.state.endsWith(QLatin1String("_SERVER_BUSY"))) {
        f.currentGame = 0;
        f.currentGameUuid.clear();
    }
    f.apollo = f.vdCapable || f.permission >= 0;
    f.vdReady = f.vdReady && f.vdCapable;
    return f;
}

QVector<App> parseAppList(const QString& xml)
{
    QVector<App> apps;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) {
            continue;
        }
        const QString n = r.name().toString();
        if (n == QLatin1String("App")) {
            apps.append(App());
        }
        else if (!apps.isEmpty()) {
            if (n == QLatin1String("AppTitle")) {
                apps.last().name = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            }
            else if (n == QLatin1String("ID")) {
                apps.last().id = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed().toInt();
            }
            else if (n == QLatin1String("UUID")) {
                apps.last().uuid = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
            }
        }
    }
    return apps;
}

QString appUuidFor(const QVector<App>& apps, int appId, const QString& appName)
{
    for (const App& a : apps) {
        if (appId != 0 && a.id == appId) {
            return a.uuid;
        }
    }
    for (const App& a : apps) {
        if (!appName.isEmpty() && a.name.compare(appName, Qt::CaseInsensitive) == 0) {
            return a.uuid;
        }
    }
    return QString();
}

QStringList hintsFor(const Facts& f, const Request& r, const QVector<App>& apps)
{
    QStringList h;
    const QString pc = r.pc.isEmpty() ? QObject::tr("The PC") : r.pc;

    // ---- Apollo's permissions for this client (only known from an answer to this paired client) ----
    if (f.permission >= 0 && f.paired) {
        const bool launch = (f.permission & kPermLaunch) != 0;
        if (!launch) {
            h << QObject::tr("%1 does not let this client launch apps (Apollo permission Launch Apps), so the stream will be refused. "
                             "In Apollo's web page open Clients and turn Launch Apps on for this client. A client paired after the "
                             "first one gets only List Apps and View Streams.").arg(pc);
        }
        else if (!(f.permission & kPermView)) {
            h << QObject::tr("%1 does not let this client view streams (Apollo permission View Streams). "
                             "Turn it on in Apollo's web page, Clients.").arg(pc);
        }
        if (launch) {
            QStringList missing;
            if (!(f.permission & kPermInputController)) {
                missing << QObject::tr("gamepad");
            }
            if (!(f.permission & kPermInputMouse)) {
                missing << QObject::tr("mouse");
            }
            if (!(f.permission & kPermInputKbd)) {
                missing << QObject::tr("keyboard");
            }
            if (!missing.isEmpty()) {
                h << QObject::tr("%1 will ignore this client's %2 input (Apollo permissions Controller, Mouse, Keyboard). "
                                 "Turn them on in Apollo's web page, Clients.").arg(pc, missing.join(", "));
            }
        }
        if (r.clipboardSync && !((f.permission & kPermClipboardSet) && (f.permission & kPermClipboardRead))) {
            h << QObject::tr("Smart Clipboard Sync is on, but %1 does not give this client the clipboard permissions "
                             "(Clipboard Set and Clipboard Read). Turn them on in Apollo's web page, Clients.").arg(pc);
        }
    }
    else if (r.clipboardSync && !f.apollo) {
        h << QObject::tr("Smart Clipboard Sync is on, but %1 does not look like Apollo, which is the only host that has it.").arg(pc);
    }

    // ---- the virtual display ----
    if (r.virtualDisplay) {
        if (!f.apollo) {
            h << QObject::tr("Use Virtual Display is on, but %1 does not look like Apollo. Sunshine and GeForce Experience have no "
                             "virtual display, so the setting does nothing.").arg(pc);
        }
        else if (!f.paired) {
            // an answer over plain HTTP: Apollo does not say there whether it can make one
        }
        else if (!f.vdCapable) {
            h << QObject::tr("Use Virtual Display is on, but Apollo on %1 says it cannot make a virtual display "
                             "(it needs Windows and Apollo's virtual display driver).").arg(pc);
        }
        else if (!f.vdReady) {
            h << QObject::tr("Use Virtual Display is on, but Apollo's virtual display driver is not ready on %1. "
                             "Repair or reinstall Apollo with the driver and restart the PC, or turn Use Virtual Display off.").arg(pc);
        }
    }

    // ---- another app is running ----
    if (f.currentGame != 0) {
        const bool same = (r.appId != 0 && r.appId == f.currentGame) ||
                          (!r.appUuid.isEmpty() && !f.currentGameUuid.isEmpty() &&
                           r.appUuid.compare(f.currentGameUuid, Qt::CaseInsensitive) == 0);
        if (!same) {
            QString running = QObject::tr("app %1").arg(f.currentGame);
            for (const App& a : apps) {
                if (a.id == f.currentGame ||
                    (!f.currentGameUuid.isEmpty() && a.uuid.compare(f.currentGameUuid, Qt::CaseInsensitive) == 0)) {
                    running = a.name;
                    break;
                }
            }
            const QString mine = r.appName.isEmpty() ? QObject::tr("this app") : r.appName;
            h << QObject::tr("%1 is already running %2, maybe for another client. Starting %3 ends it, or the PC refuses "
                             "while another client is streaming it.").arg(pc, running, mine);
        }
    }

    // ---- the picture ----
    if (!f.modes.isEmpty() && r.width > 0 && r.height > 0 && !(r.virtualDisplay && f.vdCapable)) {
        bool listed = false;
        QVector<QPair<int, int>> sizes;
        for (const Mode& m : f.modes) {
            listed = listed || (m.width == r.width && m.height == r.height);
            const QPair<int, int> s(m.width, m.height);
            if (!sizes.contains(s)) {
                sizes.append(s);
            }
        }
        if (!listed) {
            std::sort(sizes.begin(), sizes.end(), [](const QPair<int, int>& a, const QPair<int, int>& b) {
                return a.first * a.second > b.first * b.second;
            });
            QStringList shown;
            for (int i = 0; i < sizes.size() && i < 6; i++) {
                shown << QString("%1x%2").arg(sizes[i].first).arg(sizes[i].second);
            }
            QString list = shown.join(", ");
            if (sizes.size() > 6) {
                list += ", ...";
            }
            QString text = QObject::tr("%1x%2 is not one of the resolutions %3 lists (%4). It may be scaled or refused.")
                               .arg(r.width).arg(r.height).arg(pc, list);
            if (f.apollo && f.vdCapable) {
                text += " " + QObject::tr("Use Virtual Display makes the PC match it.");
            }
            h << text;
        }
    }
    if (r.hdr && !(f.codecSupport & kScm10Bit)) {
        h << QObject::tr("HDR is on, but %1 does not advertise a 10-bit codec (HEVC Main10 or AV1 10-bit), so it cannot send HDR. "
                         "The Frame shows SDR in any case.").arg(pc);
    }
    if ((r.codec == StreamingPreferences::VCC_FORCE_HEVC || r.codec == StreamingPreferences::VCC_FORCE_HEVC_HDR_DEPRECATED) &&
        !(f.codecSupport & kScmHevc)) {
        h << QObject::tr("Prefer HEVC is set, but %1 does not advertise HEVC, so it will send H.264.").arg(pc);
    }
    return h;
}

Result check(const NvComputer& c, const NvApp& app, const StreamingPreferences& p, const MlxrPrefs& prefs)
{
    Result res;
    const QString url = qEnvironmentVariable("MLXR_LAUNCHER_PREFLIGHT_URL");
    if (qEnvironmentVariable("MLXR_LAUNCHER_PREFLIGHT") == "0" || !prefs.preflight) {
        return res;
    }
    // A dry run is a test of the command line: no questions to the PC unless a test points them at its own server
    if (url.isEmpty() && !qEnvironmentVariableIsEmpty("MLXR_LAUNCHER_DRYRUN")) {
        return res;
    }
    res.enabled = true;

    NvAddress address;
    uint16_t httpsPort = 0;
    QSslCertificate cert;
    Request req;
    {
        QReadLocker lock(&c.lock);
        address = c.activeAddress;
        httpsPort = c.activeHttpsPort;
        cert = c.serverCert;
        req.pc = c.name;
    }
    if (!url.isEmpty()) {
        const QUrl u(url);
        address = NvAddress(u.host(), uint16_t(u.port(DEFAULT_HTTP_PORT)));
        httpsPort = 0;
        cert = QSslCertificate();
    }
    if (address.isNull()) {
        res.error = QObject::tr("no address for the PC");
        qInfo().noquote() << "MLXR_LAUNCHER_PREFLIGHT: not answered:" << res.error;
        return res;
    }

    QString infoXml, appsXml;
    try {
        NvHTTP http(address, httpsPort, cert);
        infoXml = http.getServerInfo(NvHTTP::NVLL_NONE, true);
        const bool tls = !cert.isNull() && http.httpsPort() != 0;
        try {
            appsXml = http.openConnectionToString(tls ? http.m_BaseUrlHttps : http.m_BaseUrlHttp, "applist", QString(), 3000,
                                                  NvHTTP::NVLL_NONE);
        } catch (...) {
            // the hints do not need the list; the app's UUID and the other app's name do
            appsXml.clear();
        }
    } catch (const GfeHttpResponseException& e) {
        res.error = e.toQString();
    } catch (const QtNetworkReplyException& e) {
        res.error = e.toQString();
    } catch (...) {
        res.error = QObject::tr("the PC did not answer");
    }
    if (infoXml.isEmpty()) {
        if (res.error.isEmpty()) {
            res.error = QObject::tr("the PC did not answer");
        }
        qInfo().noquote() << "MLXR_LAUNCHER_PREFLIGHT: not answered:" << res.error;
        return res;
    }
    const Facts facts = parseServerInfo(infoXml);
    if (!facts.valid) {
        res.error = QObject::tr("the PC's answer was not understood");
        qInfo().noquote() << "MLXR_LAUNCHER_PREFLIGHT: not understood";
        return res;
    }
    res.reached = true;
    const QVector<App> apps = parseAppList(appsXml);
    res.appUuid = appUuidFor(apps, app.id, app.name);

    req.appName = app.name;
    req.appId = app.id;
    req.appUuid = res.appUuid;
    req.width = p.width;
    req.height = p.height;
    req.fps = p.fps;
    req.hdr = p.enableHdr;
    req.codec = int(p.videoCodecConfig);
    req.virtualDisplay = prefs.virtualDisplay;
    req.clipboardSync = prefs.clipboardSync;
    res.hints = hintsFor(facts, req, apps);

    qInfo().noquote() << "MLXR_LAUNCHER_PREFLIGHT:" << (facts.apollo ? "Apollo" : "not Apollo") << "permission"
                      << facts.permission << "virtual display"
                      << (facts.vdCapable ? (facts.vdReady ? "ready" : "driver not ready") : "no") << "state" << facts.state
                      << "running" << facts.currentGame << "modes" << facts.modes.size() << "app uuid"
                      << (res.appUuid.isEmpty() ? QString("none") : res.appUuid);
    for (const QString& hint : res.hints) {
        qInfo().noquote() << "MLXR_LAUNCHER_HINT:" << hint;
    }
    return res;
}

}
