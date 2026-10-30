// Moonlight XR Native launcher (lane launcher-bundle, PLAN 6.4): see mlxronstart.h. GPLv3.
#include "mlxronstart.h"

#include "backend/computermanager.h"
#include "backend/nvapp.h"
#include "backend/nvcomputer.h"
#include "settings/streamingpreferences.h"
#include "streaming/session.h"

#include <QQmlEngine>
#include <QReadLocker>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

static void say(const QString& text)
{
    qInfo().noquote() << "MLXR_LAUNCHER_ONSTART:" << text;
}

MlxrOnStart* MlxrOnStart::get()
{
    static MlxrOnStart* s_Me = nullptr;
    if (!s_Me) {
        s_Me = new MlxrOnStart();
    }
    return s_Me;
}

MlxrOnStart::MlxrOnStart(QObject* parent) : QObject(parent) {}

MlxrOnStart::Target MlxrOnStart::target()
{
    QSettings s;
    Target t;
    t.mode = s.value("mlxr/on_start", "none").toString();
    if (t.mode == "desktop") {
        t.pc = s.value("mlxr/last_pc", "").toString();
        t.app = "Desktop";
        if (t.pc.isEmpty()) {
            t.why = "no stream has gone to a PC yet, so there is no last PC";
        }
    }
    else if (t.mode == "app") {
        t.pc = s.value("mlxr/on_start_pc", "").toString();
        t.app = s.value("mlxr/on_start_app", "").toString();
        if (t.pc.isEmpty() || t.app.isEmpty()) {
            t.why = "no app chosen";
        }
    }
    else {
        t.mode = "none";
        t.why = "off";
    }
    return t;
}

bool MlxrOnStart::matches(NvComputer& c, const QString& pc)
{
    QReadLocker lock(&c.lock);
    return c.name.compare(pc, Qt::CaseInsensitive) == 0 || c.uuid.compare(pc, Qt::CaseInsensitive) == 0;
}

MlxrOnStart::Verdict MlxrOnStart::judge(NvComputer& c, const QString& appName, NvApp* app, QString* why)
{
    const bool assumeOnline = qEnvironmentVariable("MLXR_LAUNCHER_ONSTART_ONLINE") == "1";
    QReadLocker lock(&c.lock);
    if (!assumeOnline && c.state != NvComputer::CS_ONLINE) {
        return Verdict::Wait;
    }
    if (!assumeOnline && c.pairState != NvComputer::PS_PAIRED) {
        return Verdict::NotPaired;
    }
    int found = -1;
    for (int i = 0; i < c.appList.size(); i++) {
        if (c.appList[i].name.compare(appName, Qt::CaseInsensitive) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        return Verdict::Wait;   // the app list comes after the PC is online, and changes
    }
    // Another app running: leave it (the command line asks to quit it; nothing here quits a game by itself)
    if (c.currentGameId != 0 && c.currentGameId != c.appList[found].id) {
        QString running = QString::number(c.currentGameId);
        for (const NvApp& a : c.appList) {
            if (a.id == c.currentGameId) {
                running = a.name;
            }
        }
        if (why) {
            *why = running;
        }
        return Verdict::Busy;
    }
    if (app) {
        *app = c.appList[found];
    }
    return Verdict::Start;
}

void MlxrOnStart::start(ComputerManager* manager)
{
    if (m_Started || !manager) {
        return;
    }
    m_Started = true;
    m_Target = target();
    if (!m_Target.ok()) {
        if (m_Target.mode != "none") {
            say("nothing to start: " + m_Target.why);
        }
        m_Done = true;
        return;
    }
    m_Manager = manager;
    say(QString("waiting for %1 to start %2").arg(m_Target.pc, m_Target.app));
    connect(manager, &ComputerManager::computerStateChanged, this, &MlxrOnStart::onComputerUpdated);
    bool ok = false;
    const int waitMs = qEnvironmentVariable("MLXR_LAUNCHER_ONSTART_WAIT_MS").toInt(&ok);
    m_Timer = new QTimer(this);
    m_Timer->setSingleShot(true);
    connect(m_Timer, &QTimer::timeout, this, &MlxrOnStart::onTimeout);
    m_Timer->start(ok && waitMs > 0 ? waitMs : 120000);
    // The PCs the launcher knows now: wake the one to start (as stream "PC" "App" does), and it may be online already
    for (NvComputer* c : manager->getComputers()) {
        if (matches(*c, m_Target.pc)) {
            c->wake();
        }
    }
    for (NvComputer* c : manager->getComputers()) {
        onComputerUpdated(c);
        if (m_Done) {
            break;
        }
    }
}

void MlxrOnStart::onComputerUpdated(NvComputer* computer)
{
    if (m_Done || !computer || !matches(*computer, m_Target.pc)) {
        return;
    }
    NvApp app;
    QString why;
    switch (judge(*computer, m_Target.app, &app, &why)) {
    case Verdict::Wait:
        return;
    case Verdict::NotPaired:
        say(QString("%1 is not paired: not started").arg(m_Target.pc));
        finish();
        return;
    case Verdict::Busy:
        say(QString("%1 is running %2: %3 not started").arg(m_Target.pc, why, m_Target.app));
        finish();
        return;
    case Verdict::Start:
        break;
    }
    if (qEnvironmentVariable("MLXR_LAUNCHER_ONSTART_ONLINE") == "1") {
        // tests: the PC from the saved list has no address it answered on
        QWriteLocker lock(&computer->lock);
        if (computer->activeAddress.isNull()) {
            computer->activeAddress = !computer->manualAddress.isNull() ? computer->manualAddress : computer->localAddress;
        }
    }
    say(QString("starting %1 on %2").arg(app.name, m_Target.pc));
    finish();
    Session* session = new Session(computer, app, StreamingPreferences::get());
    QQmlEngine::setObjectOwnership(session, QQmlEngine::JavaScriptOwnership);
    emit sessionReady(app.name, session);
}

void MlxrOnStart::onTimeout()
{
    if (m_Done) {
        return;
    }
    say(QString("%1 did not come online with %2 in time: not started").arg(m_Target.pc, m_Target.app));
    finish();
}

void MlxrOnStart::finish()
{
    m_Done = true;
    if (m_Timer) {
        m_Timer->stop();
    }
    if (m_Manager) {
        disconnect(m_Manager, nullptr, this, nullptr);
    }
}

int MlxrOnStart::selfTest(const QString& spec)
{
    QTextStream out(stdout);
    const Target t = target();
    out << "SELFTEST: onstart mode=" << t.mode << " pc=" << t.pc << " app=" << t.app;
    if (!t.ok()) {
        out << " nothing: " << t.why << Qt::endl;
        return 0;
    }
    out << Qt::endl;
    QSettings settings;
    const int n = settings.beginReadArray("hosts");
    NvComputer* found = nullptr;
    for (int i = 0; i < n && !found; i++) {
        settings.setArrayIndex(i);
        NvComputer* c = new NvComputer(settings);
        if (matches(*c, t.pc)) {
            found = c;
        }
        else {
            delete c;
        }
    }
    settings.endArray();
    if (!found) {
        out << "SELFTEST: onstart no saved PC " << t.pc << Qt::endl;
        return 0;
    }
    found->state = NvComputer::CS_ONLINE;
    found->pairState = NvComputer::PS_PAIRED;
    found->currentGameId = spec.section(':', 1).toInt();
    NvApp app;
    QString why;
    const Verdict v = judge(*found, t.app, &app, &why);
    out << "SELFTEST: onstart verdict="
        << (v == Verdict::Start ? QString("start %1 %2").arg(app.id).arg(app.name)
            : v == Verdict::Busy ? "busy " + why : v == Verdict::NotPaired ? QString("notpaired") : QString("wait"))
        << Qt::endl;
    delete found;
    return 0;
}
