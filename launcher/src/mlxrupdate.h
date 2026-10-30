// Moonlight XR Native launcher (lane launcher-bundle, PLAN 6.2): the update check. New file, added to app/ by
// launcher/build.sh. GPLv3.
//
// In hand-off mode, with the Settings page's "Check for updates" on (mlxr/check_updates, the default), start() asks
// GitHub's releases API for this project's latest release, at most once a day:
//   GET https://api.github.com/repos/simplysanatori/moonlight-xr-steam-frame/releases/latest
// No other host is asked, nothing about this install is sent (a fixed User-Agent, no version, no id), nothing is
// downloaded or installed. When the release is newer than the installed VERSION, updateAvailable() gives the
// version and its release page, and main.qml shows stock moonlight-qt's update button in the toolbar with
// "Update available for Moonlight XR for Frame: Version N", which opens the page (Steam's browser in Game Mode).
// The answer is kept (mlxr/update_checked, mlxr/update_latest, mlxr/update_url), so the notice also shows on the
// starts in between without asking again. A failure (no network, an HTTP error, an answer that does not parse) is
// one log line and nothing else. Log lines start with MLXR_LAUNCHER_UPDATE.
//
// Tests (no network): MLXR_LAUNCHER_UPDATE=0 never checks; MLXR_LAUNCHER_UPDATE_JSON=<file> takes that file as the
// answer (no request, nothing kept); MLXR_LAUNCHER_UPDATE_URL=http://127.0.0.1:<port>/... asks a local server
// instead (any other address is ignored); MLXR_LAUNCHER_SELFTEST=update runs start() without a UI.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class MlxrUpdate : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateAvailable)
    Q_PROPERTY(QString releaseUrl READ releaseUrl NOTIFY updateAvailable)

public:
    static MlxrUpdate* get();

    static constexpr const char* kApiUrl = "https://api.github.com/repos/simplysanatori/moonlight-xr-steam-frame/releases/latest";
    static constexpr const char* kReleasesUrl = "https://github.com/simplysanatori/moonlight-xr-steam-frame/releases";

    struct Release {
        QString version;   // "1.2.0" (the tag without its v)
        QString url;       // the release's page on github.com
    };
    // The API's answer: false (and *why) for a draft, a pre-release, a tag that is not a version, or no JSON.
    // A page address outside this project's releases is replaced by the releases page.
    static bool parseRelease(const QByteArray& json, Release* out, QString* why);
    // Dotted numbers, missing parts 0: <0, 0, >0. Anything after the numbers ("-dev") is ignored.
    static int compareVersions(const QString& a, const QString& b);
    // The first line of VERSION as built (mlxrversion.h)
    static QString installedVersion();

    // Once per launcher run; see the top of this file
    Q_INVOKABLE void start();

    QString latestVersion() const { return m_Version; }
    QString releaseUrl() const { return m_Url; }

    static int selfTest();

signals:
    void updateAvailable(QString version, QString url);
    void finished();   // the check (or the decision not to check) is over

private:
    explicit MlxrUpdate(QObject* parent = nullptr);
    void handle(const QByteArray& json, bool keep);
    void offer(const Release& r);
    void onReply(QNetworkReply* reply);
    bool m_Started = false;
    QString m_Version, m_Url;
    QNetworkAccessManager* m_Nam = nullptr;
};
