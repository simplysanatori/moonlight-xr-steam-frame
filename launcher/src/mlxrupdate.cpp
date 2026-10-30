// Moonlight XR Native launcher (lane launcher-bundle, PLAN 6.2): see mlxrupdate.h. GPLv3.
#include "mlxrupdate.h"
#include "mlxrhandoff.h"
#include "mlxrversion.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

static void say(const QString& text)
{
    qInfo().noquote() << "MLXR_LAUNCHER_UPDATE:" << text;
}

MlxrUpdate* MlxrUpdate::get()
{
    static MlxrUpdate* s_Me = nullptr;
    if (!s_Me) {
        s_Me = new MlxrUpdate();
    }
    return s_Me;
}

MlxrUpdate::MlxrUpdate(QObject* parent) : QObject(parent) {}

QString MlxrUpdate::installedVersion()
{
    return QString(MLXR_VERSION).trimmed();
}

static QList<int> versionParts(const QString& v, bool* ok)
{
    static const QRegularExpression re("^[vV]?(\\d+(?:\\.\\d+){0,3})");
    const QRegularExpressionMatch m = re.match(v.trimmed());
    QList<int> parts;
    if (ok) {
        *ok = m.hasMatch();
    }
    if (m.hasMatch()) {
        for (const QString& p : m.captured(1).split('.')) {
            parts << p.toInt();
        }
    }
    return parts;
}

int MlxrUpdate::compareVersions(const QString& a, const QString& b)
{
    const QList<int> pa = versionParts(a, nullptr), pb = versionParts(b, nullptr);
    for (int i = 0; i < qMax(pa.size(), pb.size()); i++) {
        const int x = pa.value(i), y = pb.value(i);
        if (x != y) {
            return x < y ? -1 : 1;
        }
    }
    return 0;
}

bool MlxrUpdate::parseRelease(const QByteArray& json, Release* out, QString* why)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (!doc.isObject()) {
        *why = "the answer is not a JSON object (" + err.errorString() + ")";
        return false;
    }
    const QJsonObject o = doc.object();
    if (o.value("draft").toBool() || o.value("prerelease").toBool()) {
        *why = "the latest release is a draft or a pre-release";
        return false;
    }
    const QString tag = o.value("tag_name").toString().trimmed();
    bool ok = false;
    const QList<int> parts = versionParts(tag, &ok);
    if (!ok) {
        *why = "the latest release's tag \"" + tag.left(40) + "\" is not a version";
        return false;
    }
    QStringList text;
    for (int p : parts) {
        text << QString::number(p);
    }
    out->version = text.join('.');
    // Only this project's own release pages are opened
    const QString page = o.value("html_url").toString();
    out->url = page.startsWith(QString(kReleasesUrl) + "/") && !page.contains(QRegularExpression("[\\s\"'<>]"))
                   ? page : QString(kReleasesUrl) + "/latest";
    return true;
}

void MlxrUpdate::offer(const Release& r)
{
    const QString installed = installedVersion();
    if (compareVersions(r.version, installed) > 0) {
        m_Version = r.version;
        m_Url = r.url;
        say(QString("version %1 is out (installed %2): %3").arg(r.version, installed, r.url));
        emit updateAvailable(m_Version, m_Url);
    }
    else {
        say(QString("up to date (latest release %1, installed %2)").arg(r.version, installed));
    }
}

void MlxrUpdate::handle(const QByteArray& json, bool keep)
{
    Release r;
    QString why;
    if (!parseRelease(json, &r, &why)) {
        say("check failed: " + why);
        return;
    }
    if (keep) {
        QSettings s;
        s.setValue("mlxr/update_latest", r.version);
        s.setValue("mlxr/update_url", r.url);
        s.sync();
    }
    offer(r);
}

void MlxrUpdate::start()
{
    if (m_Started) {
        return;
    }
    m_Started = true;
    // Stock moonlight-qt (MLXR_HANDOFF=0, no VR client) never asks
    if (!MlxrHandoff::enabled() || qEnvironmentVariable("MLXR_LAUNCHER_UPDATE") == "0") {
        emit finished();
        return;
    }
    QSettings s;
    if (!s.value("mlxr/check_updates", true).toBool()) {
        say("off (Settings, Check for updates)");
        emit finished();
        return;
    }
    const QString inject = qEnvironmentVariable("MLXR_LAUNCHER_UPDATE_JSON");
    if (!inject.isEmpty()) {
        QFile f(inject);
        if (f.open(QIODevice::ReadOnly)) {
            handle(f.readAll(), false);
        }
        else {
            say("check failed: cannot read " + inject);
        }
        emit finished();
        return;
    }
    // At most once a day: in between, the last answer is used. A check time in the future (the clock went back)
    // does not count.
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime last = QDateTime::fromString(s.value("mlxr/update_checked", "").toString(), Qt::ISODate);
    if (last.isValid() && last <= now && last.secsTo(now) < 24 * 3600) {
        const QString latest = s.value("mlxr/update_latest", "").toString();
        say(QString("last checked %1, next check after %2").arg(last.toString(Qt::ISODate), last.addDays(1).toString(Qt::ISODate)));
        if (!latest.isEmpty()) {
            offer({ latest, s.value("mlxr/update_url", QString(kReleasesUrl) + "/latest").toString() });
        }
        emit finished();
        return;
    }
    s.setValue("mlxr/update_checked", now.toString(Qt::ISODate));
    s.sync();

    QString url = kApiUrl;
    const QString test = qEnvironmentVariable("MLXR_LAUNCHER_UPDATE_URL");
    if (test.startsWith("http://127.0.0.1:")) {
        url = test;
    }
    QNetworkRequest req { QUrl(url) };
    req.setHeader(QNetworkRequest::UserAgentHeader, "moonlight-xr-launcher");
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);
    m_Nam = new QNetworkAccessManager(this);
    QNetworkReply* reply = m_Nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReply(reply); });
}

void MlxrUpdate::onReply(QNetworkReply* reply)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        say(QString("check failed: %1").arg(status > 0 ? QString("HTTP %1").arg(status) : reply->errorString()));
    }
    else {
        handle(reply->read(256 * 1024), true);
    }
    m_Nam->deleteLater();
    m_Nam = nullptr;
    emit finished();
}

int MlxrUpdate::selfTest()
{
    MlxrUpdate* u = get();
    QEventLoop loop;
    bool done = false;
    QObject::connect(u, &MlxrUpdate::finished, &loop, [&]() { done = true; loop.quit(); });
    QObject::connect(u, &MlxrUpdate::updateAvailable, [](const QString& v, const QString& url) {
        QTextStream(stdout) << "SELFTEST: update available " << v << " " << url << Qt::endl;
    });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    u->start();
    if (!done) {
        loop.exec();
    }
    QTextStream(stdout) << "SELFTEST: update done installed=" << installedVersion() << Qt::endl;
    return 0;
}
