// Moonlight XR Native launcher (lane launcher-bundle, PLAN 6.4): the Settings page's "On start". When the
// launcher starts normally (its PC grid, not a command line), it waits for the chosen PC to be online and then
// starts the stream, as a click on the app would. New file, added to app/ by launcher/build.sh. GPLv3.
//
//   mlxr/on_start = none (default) | desktop | app      (MlxrPrefs.onStart)
//   desktop: the app "Desktop" of the PC the last stream went to (mlxr/last_pc, written by makePlan())
//   app:     mlxr/on_start_app of mlxr/on_start_pc (names, matched case-insensitively; the PC also by uuid)
//
// main.qml calls start(ComputerManager) once, in hand-off mode, when the first page is the PC grid. The PC is woken
// (as the command line's stream does) and watched through ComputerManager's own polling. Once it is online, paired
// and its app list has the app, sessionReady() hands main.qml a Session, which pushes the stream segue if the grid
// is still the page shown. Another app already running on the PC, a PC that is not paired, or no answer within
// 2 minutes: one log line and nothing else; the grid stays as it is. Every step is logged as MLXR_LAUNCHER_ONSTART.
// The command line form (moonlight-xr-launcher stream "PC" "Desktop", the "Desktop in VR" Steam shortcut of
// packaging/install.sh --desktop-shortcut) is stock moonlight-qt's and does not go through here.
#pragma once

#include <QObject>
#include <QString>

class ComputerManager;
class NvComputer;
class NvApp;
class Session;
class QTimer;

class MlxrOnStart : public QObject
{
    Q_OBJECT

public:
    static MlxrOnStart* get();

    struct Target {
        QString mode;   // none, desktop, app
        QString pc, app;
        QString why;    // when there is nothing to start: why
        bool ok() const { return !pc.isEmpty() && !app.isEmpty(); }
    };
    // From the launcher's settings now
    static Target target();

    enum class Verdict { Wait, Start, NotPaired, Busy };
    // What to do with this computer now: Wait (not online yet, or the app list does not have the app yet), Start
    // (*app is the app), NotPaired, Busy (*why names the app that runs). The caller holds no lock on c.
    static Verdict judge(NvComputer& c, const QString& appName, NvApp* app, QString* why);
    static bool matches(NvComputer& c, const QString& pc);

    // Once per launcher run (later calls do nothing). MLXR_LAUNCHER_ONSTART_WAIT_MS: how long to wait for the PC
    // (default 120000). MLXR_LAUNCHER_ONSTART_ONLINE=1 (tests): a saved PC counts as online and paired, as the
    // self tests' PCs do.
    Q_INVOKABLE void start(ComputerManager* manager);

    // MLXR_LAUNCHER_SELFTEST=onstart[:running app id]: the target from the settings, then judge() on the saved PC
    // as if it were online, with that app running on it
    static int selfTest(const QString& spec);

signals:
    void sessionReady(QString appName, Session* session);

private slots:
    void onComputerUpdated(NvComputer* computer);
    void onTimeout();

private:
    explicit MlxrOnStart(QObject* parent = nullptr);
    void finish();
    bool m_Started = false;
    bool m_Done = false;
    Target m_Target;
    ComputerManager* m_Manager = nullptr;
    QTimer* m_Timer = nullptr;
};
