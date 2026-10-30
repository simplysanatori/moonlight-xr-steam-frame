// Moonlight XR Native launcher (lane wave4-launcher): settings profiles, after Artemis's Settings Profiles. New file,
// added to app/streaming by launcher/build.sh. GPLv3.
//
// A profile is a name and a saved copy of the stream settings the Settings page has: the stock keys
// (resolution, refresh rate, bitrate, codec, HDR, surround sound, host options, software decoding), the launcher's
// own (mlxrprefs.h: pacing, virtual display, scale factor, custom rate, range, rumble, dead zone, network, clipboard
// sync ...) and the VR client's config.ini keys the page mirrors (3D, stats, face buttons, notices, scroll speed,
// disconnect times, gamepad as mouse). Profiles live in the launcher's settings (QSettings, "mlxr_profiles").
//
// A profile is used for a PC, or for one app of a PC, keyed the way the headset keys its per-app layout: the PC's
// name and the app's name, case ignored ("mlxr_profile_use"). An app's own profile wins over its PC's. No profile
// for the PC and app means the settings as they are on the Settings page, the "Default".
//
// When a stream starts (MlxrHandoff::makePlan), beginLaunch() puts the profile's values into the settings in force
// and writes its config.ini keys, which only lasts as long as the Applied object that comes back. Nothing is saved
// to the launcher's settings: the Settings page's own values were never changed on disk. When it ends the stream
// settings go back, a config.ini key goes back to the value it had unless the headset changed it meanwhile, and
// MlxrPrefs reads its settings again. The config.ini keys are written to disk, so what has to go back is also noted
// in the launcher's settings ("mlxr_profile_pending") until then; if the launcher is killed during a stream, the next
// start puts them back.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <memory>

class MlxrProfiles : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList names READ names NOTIFY changed)
    // [{pc, app, profile}] in the order they were made; app "" is the PC's every app
    Q_PROPERTY(QVariantList assignments READ assignments NOTIFY changed)
    // Counts up on every change, for QML bindings to depend on
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // What the check before the last stream found (mlxrpreflight.h), one hint to a line, "" when nothing was wrong
    Q_PROPERTY(QString lastCheck READ lastCheck NOTIFY lastCheckChanged)
    Q_PROPERTY(QString lastCheckWhen READ lastCheckWhen NOTIFY lastCheckChanged)

public:
    // The profile in force for one stream. Its destructor ends it.
    struct Applied
    {
        ~Applied();
        QString profile;
        // the stream settings (StreamingPreferences) as they were: the property values
        QVariantMap before;
        // config.ini keys the profile changed: what they were and what the profile made them
        struct Ini {
            QString section, key, old, applied;
        };
        QVector<Ini> ini;
        bool ended = false;
        void end();
    };

    static MlxrProfiles* get();

    QStringList names() const;
    int revision() const { return m_revision; }
    QVariantList assignments() const;
    QString lastCheck() const { return m_lastCheck; }
    QString lastCheckWhen() const { return m_lastCheckWhen; }
    void setLastCheck(const QStringList& hints, bool reached);

    // ---- the profiles; each change is written at once. They return "" or why it could not be done ----
    // A new profile holding the settings in force now
    Q_INVOKABLE QString create(const QString& name);
    // "<name> copy", then "<name> copy 2" ...
    Q_INVOKABLE QString duplicate(const QString& name);
    Q_INVOKABLE QString rename(const QString& from, const QString& to);
    // Also takes it away from every PC and app that used it
    Q_INVOKABLE QString remove(const QString& name);
    // Puts the settings in force now into the profile
    Q_INVOKABLE QString updateFromCurrent(const QString& name);
    // Makes the profile's values the Settings page's own (saved), so they stay without the profile
    Q_INVOKABLE QString loadIntoSettings(const QString& name);
    // "2560x1440, 90 Hz, 98 Mbps, HEVC ..."
    Q_INVOKABLE QString summary(const QString& name) const;
    // The same for the settings in force now
    Q_INVOKABLE QString currentSummary() const;
    // The profile name can be used (not empty, not "Default", not there yet), else why not
    Q_INVOKABLE QString nameProblem(const QString& name) const;

    // ---- where they are used ----
    // The profile in force for the PC and app: the app's own, else the PC's, else "" (Default)
    Q_INVOKABLE QString profileFor(const QString& pc, const QString& app) const;
    // The profile set for exactly this scope (app "" is the PC's every app), else ""
    Q_INVOKABLE QString assignedTo(const QString& pc, const QString& app) const;
    // profile "" (or Default) takes the scope's profile away
    Q_INVOKABLE QString assign(const QString& pc, const QString& app, const QString& profile);

    // ---- a stream ----
    // Puts the PC's and app's profile in force; null when there is none (the Default)
    std::shared_ptr<Applied> beginLaunch(const QString& pc, const QString& app);

    // The text of the settings in force, one key = value to a line, for tests
    QString dumpCurrent() const;
    // tests/launcher: MLXR_LAUNCHER_SELFTEST=profileops:<op>~<op>... (see the .cpp); returns the exit code
    static int selfTestOps(const QString& script);

signals:
    void changed();
    void lastCheckChanged();

private:
    explicit MlxrProfiles(QObject* parent = nullptr);

    struct Profile
    {
        QString name;
        QVariantMap values;
    };
    struct Use
    {
        QString pc, app, profile;
    };

    int indexOf(const QString& name) const;
    void load();
    void save();
    void recoverPending();
    void touched();

    QVector<Profile> m_profiles;
    QVector<Use> m_use;
    int m_revision = 0;
    QString m_lastCheck, m_lastCheckWhen;
};
