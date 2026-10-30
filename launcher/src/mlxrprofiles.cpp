// Moonlight XR Native launcher (lane wave4-launcher): see mlxrprofiles.h. GPLv3.
#include "mlxrprofiles.h"
#include "mlxrprefs.h"

#include "settings/streamingpreferences.h"

#include <QDateTime>
#include <QDebug>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QSettings>
#include <QTextStream>

#include <cstdio>
#include <unistd.h>

namespace {

enum Kind { KInt, KBool, KStr };

// ---- what a profile holds -------------------------------------------------------------

// The stock stream settings (StreamingPreferences), by property name. The enum ones are read as plain ints.
struct StreamField
{
    const char* key;
    Kind kind;
    int lo, hi;
    QVariant (*get)(const StreamingPreferences*);
};

const StreamField kStreamFields[] = {
    { "width", KInt, 256, 8192, [](const StreamingPreferences* p) { return QVariant(p->width); } },
    { "height", KInt, 256, 8192, [](const StreamingPreferences* p) { return QVariant(p->height); } },
    { "fps", KInt, 10, 1000, [](const StreamingPreferences* p) { return QVariant(p->fps); } },
    { "bitrateKbps", KInt, 500, 500000, [](const StreamingPreferences* p) { return QVariant(p->bitrateKbps); } },
    { "audioConfig", KInt, 0, 2, [](const StreamingPreferences* p) { return QVariant(int(p->audioConfig)); } },
    { "videoCodecConfig", KInt, 0, 4, [](const StreamingPreferences* p) { return QVariant(int(p->videoCodecConfig)); } },
    { "enableHdr", KBool, 0, 0, [](const StreamingPreferences* p) { return QVariant(p->enableHdr); } },
    { "gameOptimizations", KBool, 0, 0, [](const StreamingPreferences* p) { return QVariant(p->gameOptimizations); } },
    { "playAudioOnHost", KBool, 0, 0, [](const StreamingPreferences* p) { return QVariant(p->playAudioOnHost); } },
    { "videoDecoderSelection", KInt, 0, 2,
      [](const StreamingPreferences* p) { return QVariant(int(p->videoDecoderSelection)); } },
};

// The launcher's own settings and the VR client's config.ini keys the Settings page mirrors (MlxrPrefs), by
// property name. allowed: the strings a text setting can take, each between bars.
struct PrefField
{
    const char* key;
    Kind kind;
    int lo, hi;
    const char* allowed;
};

const PrefField kPrefFields[] = {
    { "framePacing", KStr, 0, 0, "|latency|balanced|smoothness|" },
    { "ultraLowLatency", KBool, 0, 0, nullptr },
    { "virtualDisplay", KBool, 0, 0, nullptr },
    { "scaleFactor", KInt, 20, 200, nullptr },
    { "customRefreshRate", KStr, 0, 0, nullptr },
    { "matchHeadsetFps", KBool, 0, 0, nullptr },
    { "fullRange", KBool, 0, 0, nullptr },
    { "rumble", KBool, 0, 0, nullptr },
    { "deadzone", KInt, -20, 20, nullptr },
    { "mouseNavButtons", KBool, 0, 0, nullptr },
    { "networkPath", KStr, 0, 0, "|auto|home|adapter|usb|" },
    { "clipboardSync", KBool, 0, 0, nullptr },
    // config.ini of the VR client
    { "perfOverlay", KBool, 0, 0, nullptr },
    { "renderMode", KInt, 0, 1, nullptr },
    { "strength", KInt, 1, 100, nullptr },
    { "strengthIsDefault", KBool, 0, 0, nullptr },
    { "convergence", KInt, 0, 100, nullptr },
    { "convergenceIsDefault", KBool, 0, 0, nullptr },
    { "flipFaceButtons", KBool, 0, 0, nullptr },
    { "notices", KStr, 0, 0, "|all|important|off|" },
    { "scrollSpeed", KInt, 25, 400, nullptr },
    { "doffDisconnect", KInt, 0, 3600, nullptr },
    { "idleDisconnect", KInt, 0, 1440, nullptr },
    { "padMouseChord", KStr, 0, 0, "|start_hold|start_back|off|" },
    { "padMouseScroll", KStr, 0, 0, "|right|left|none|" },
    { "padArbitration", KStr, 0, 0, "|pointer|also_gamepad|sleep|" },
};

// A value read from the launcher's settings, or typed in a test, held to what the setting can take
bool cleanValue(Kind kind, int lo, int hi, const char* allowed, const char* key, const QVariant& in, QVariant* out)
{
    switch (kind) {
    case KInt: {
        bool ok = false;
        int v = in.toInt(&ok);
        if (!ok) {
            return false;
        }
        if (hi > lo) {
            v = qBound(lo, v, hi);
        }
        *out = v;
        return true;
    }
    case KBool: {
        const QString s = in.toString().trimmed().toLower();
        if (in.userType() != QMetaType::Bool && s != "true" && s != "false" && s != "1" && s != "0") {
            return false;
        }
        *out = in.toBool();
        return true;
    }
    case KStr: {
        const QString s = in.toString().trimmed();
        if (allowed) {
            if (!QString::fromLatin1(allowed).contains("|" + s + "|")) {
                return false;
            }
        }
        else if (!qstrcmp(key, "customRefreshRate")) {
            static const QRegularExpression rate("^(\\d{2,3}(\\.\\d{1,3})?)?$");
            if (!rate.match(s).hasMatch()) {
                return false;
            }
        }
        *out = s;
        return true;
    }
    }
    return false;
}

QVariantMap captureCurrent()
{
    QVariantMap m;
    const StreamingPreferences* sp = StreamingPreferences::get();
    for (const StreamField& f : kStreamFields) {
        m.insert(f.key, f.get(sp));
    }
    const MlxrPrefs* mp = MlxrPrefs::get();
    for (const PrefField& f : kPrefFields) {
        m.insert(f.key, mp->property(f.key));
    }
    return m;
}

// Puts the values (as far as they are there and sensible) into the settings in force, through the properties, so
// a page that shows them hears of it
void applyValues(const QVariantMap& v)
{
    StreamingPreferences* sp = StreamingPreferences::get();
    for (const StreamField& f : kStreamFields) {
        QVariant c;
        if (v.contains(f.key) && cleanValue(f.kind, f.lo, f.hi, nullptr, f.key, v.value(f.key), &c)) {
            sp->setProperty(f.key, c);
        }
    }
    MlxrPrefs* mp = MlxrPrefs::get();
    for (const PrefField& f : kPrefFields) {
        QVariant c;
        if (v.contains(f.key) && cleanValue(f.kind, f.lo, f.hi, f.allowed, f.key, v.value(f.key), &c)) {
            mp->setProperty(f.key, c);
        }
    }
}

// What a saved or typed set of values comes to, each held to its setting
QVariantMap cleanMap(const QVariantMap& in)
{
    QVariantMap out;
    for (const StreamField& f : kStreamFields) {
        QVariant c;
        if (in.contains(f.key) && cleanValue(f.kind, f.lo, f.hi, nullptr, f.key, in.value(f.key), &c)) {
            out.insert(f.key, c);
        }
    }
    for (const PrefField& f : kPrefFields) {
        QVariant c;
        if (in.contains(f.key) && cleanValue(f.kind, f.lo, f.hi, f.allowed, f.key, in.value(f.key), &c)) {
            out.insert(f.key, c);
        }
    }
    return out;
}

QString normKey(const QString& s)
{
    return s.simplified().toLower();
}

bool sameIniValue(const QString& a, const QString& b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

QString summaryOf(const QVariantMap& v)
{
    QStringList p;
    if (v.contains("width") && v.contains("height")) {
        p << QString("%1x%2").arg(v.value("width").toInt()).arg(v.value("height").toInt());
    }
    const QString custom = v.value("customRefreshRate").toString();
    if (!custom.isEmpty()) {
        p << QObject::tr("%1 Hz").arg(custom);
    }
    else if (v.value("matchHeadsetFps", true).toBool()) {
        p << QObject::tr("headset rate");
    }
    else if (v.contains("fps")) {
        p << QObject::tr("%1 Hz").arg(v.value("fps").toInt());
    }
    if (v.contains("bitrateKbps")) {
        p << QObject::tr("%1 Mbps").arg(v.value("bitrateKbps").toInt() / 1000.0, 0, 'g', 4);
    }
    switch (v.value("videoCodecConfig", 0).toInt()) {
    case StreamingPreferences::VCC_FORCE_H264:
        p << QObject::tr("H.264");
        break;
    case StreamingPreferences::VCC_FORCE_HEVC:
    case StreamingPreferences::VCC_FORCE_HEVC_HDR_DEPRECATED:
        p << QObject::tr("HEVC");
        break;
    case StreamingPreferences::VCC_FORCE_AV1:
        p << QObject::tr("AV1");
        break;
    default:
        break;
    }
    switch (v.value("audioConfig", 0).toInt()) {
    case StreamingPreferences::AC_51_SURROUND:
        p << QObject::tr("5.1 sound");
        break;
    case StreamingPreferences::AC_71_SURROUND:
        p << QObject::tr("7.1 sound");
        break;
    default:
        break;
    }
    if (v.value("enableHdr").toBool()) {
        p << QObject::tr("HDR");
    }
    if (v.value("renderMode").toInt() == 1) {
        p << QObject::tr("3D");
    }
    if (v.value("virtualDisplay").toBool()) {
        const int scale = v.value("scaleFactor", 100).toInt();
        p << (scale != 100 ? QObject::tr("virtual display %1%").arg(scale) : QObject::tr("virtual display"));
    }
    if (v.value("videoDecoderSelection").toInt() == StreamingPreferences::VDS_FORCE_SOFTWARE) {
        p << QObject::tr("software decoding");
    }
    return p.join(", ");
}

QString printable(const QVariant& v)
{
    if (v.userType() == QMetaType::Bool) {
        return v.toBool() ? "true" : "false";
    }
    return v.toString();
}

} // namespace

// ---- the singleton and the file ---------------------------------------------------------

MlxrProfiles* MlxrProfiles::get()
{
    static MlxrProfiles* s_Profiles = nullptr;
    if (!s_Profiles) {
        s_Profiles = new MlxrProfiles();
    }
    return s_Profiles;
}

MlxrProfiles::MlxrProfiles(QObject* parent) : QObject(parent)
{
    load();
    recoverPending();
}

void MlxrProfiles::load()
{
    QSettings s;
    m_profiles.clear();
    const int n = s.beginReadArray("mlxr_profiles");
    for (int i = 0; i < n; i++) {
        s.setArrayIndex(i);
        Profile p;
        p.name = s.value("name").toString().simplified();
        QVariantMap raw;
        for (const QString& key : s.childKeys()) {
            if (key.startsWith("v_")) {
                raw.insert(key.mid(2), s.value(key));
            }
        }
        p.values = cleanMap(raw);
        if (!p.name.isEmpty() && indexOf(p.name) < 0) {
            m_profiles.append(p);
        }
    }
    s.endArray();
    m_use.clear();
    const int u = s.beginReadArray("mlxr_profile_use");
    for (int i = 0; i < u; i++) {
        s.setArrayIndex(i);
        Use use;
        use.pc = s.value("pc").toString().simplified();
        use.app = s.value("app").toString().simplified();
        use.profile = s.value("profile").toString().simplified();
        if (!use.pc.isEmpty() && indexOf(use.profile) >= 0) {
            m_use.append(use);
        }
    }
    s.endArray();
}

void MlxrProfiles::save()
{
    QSettings s;
    s.remove("mlxr_profiles");
    s.beginWriteArray("mlxr_profiles", int(m_profiles.size()));
    for (int i = 0; i < m_profiles.size(); i++) {
        s.setArrayIndex(i);
        s.setValue("name", m_profiles[i].name);
        for (auto it = m_profiles[i].values.constBegin(); it != m_profiles[i].values.constEnd(); ++it) {
            s.setValue("v_" + it.key(), it.value());
        }
    }
    s.endArray();
    s.remove("mlxr_profile_use");
    s.beginWriteArray("mlxr_profile_use", int(m_use.size()));
    for (int i = 0; i < m_use.size(); i++) {
        s.setArrayIndex(i);
        s.setValue("pc", m_use[i].pc);
        s.setValue("app", m_use[i].app);
        s.setValue("profile", m_use[i].profile);
    }
    s.endArray();
    s.sync();
}

void MlxrProfiles::touched()
{
    m_revision++;
    emit changed();
}

int MlxrProfiles::indexOf(const QString& name) const
{
    const QString k = normKey(name);
    for (int i = 0; i < m_profiles.size(); i++) {
        if (normKey(m_profiles[i].name) == k) {
            return i;
        }
    }
    return -1;
}

QStringList MlxrProfiles::names() const
{
    QStringList l;
    for (const Profile& p : m_profiles) {
        l << p.name;
    }
    return l;
}

QVariantList MlxrProfiles::assignments() const
{
    QVariantList l;
    for (const Use& u : m_use) {
        QVariantMap m;
        m.insert("pc", u.pc);
        m.insert("app", u.app);
        m.insert("profile", u.profile);
        l << m;
    }
    return l;
}

void MlxrProfiles::setLastCheck(const QStringList& hints, bool reached)
{
    m_lastCheck = hints.join("\n");
    m_lastCheckWhen = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") +
                      (reached ? QString() : QString(" ") + QObject::tr("(the PC did not answer)"));
    emit lastCheckChanged();
}

// ---- the profiles ----------------------------------------------------------------------

QString MlxrProfiles::nameProblem(const QString& name) const
{
    const QString n = name.simplified();
    if (n.isEmpty()) {
        return tr("A profile needs a name.");
    }
    if (n.size() > 48) {
        return tr("Use a name of 48 letters or fewer.");
    }
    if (n.compare("Default", Qt::CaseInsensitive) == 0) {
        return tr("Default is the settings on the Settings page; pick another name.");
    }
    if (indexOf(n) >= 0) {
        return tr("There is a profile called %1 already.").arg(n);
    }
    return QString();
}

QString MlxrProfiles::create(const QString& name)
{
    const QString why = nameProblem(name);
    if (!why.isEmpty()) {
        return why;
    }
    Profile p;
    p.name = name.simplified();
    p.values = cleanMap(captureCurrent());
    m_profiles.append(p);
    save();
    touched();
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: created" << p.name << "-" << summaryOf(p.values);
    return QString();
}

QString MlxrProfiles::duplicate(const QString& name)
{
    const int i = indexOf(name);
    if (i < 0) {
        return tr("There is no profile called %1.").arg(name);
    }
    const QString base = m_profiles[i].name + " " + tr("copy");
    QString copy = base;
    for (int n = 2; indexOf(copy) >= 0 && n < 1000; n++) {
        copy = base + " " + QString::number(n);
    }
    if (copy.size() > 48) {
        copy = copy.left(48).trimmed();
    }
    if (indexOf(copy) >= 0) {
        return tr("Could not find a free name for the copy.");
    }
    Profile p = m_profiles[i];
    p.name = copy;
    m_profiles.append(p);
    save();
    touched();
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: duplicated" << m_profiles[i].name << "as" << copy;
    return QString();
}

QString MlxrProfiles::rename(const QString& from, const QString& to)
{
    const int i = indexOf(from);
    if (i < 0) {
        return tr("There is no profile called %1.").arg(from);
    }
    const QString n = to.simplified();
    // Only the letters' case may change on its own
    if (normKey(n) != normKey(m_profiles[i].name)) {
        const QString why = nameProblem(n);
        if (!why.isEmpty()) {
            return why;
        }
    }
    else if (n.isEmpty() || n.size() > 48) {
        return nameProblem(n);
    }
    const QString old = m_profiles[i].name;
    m_profiles[i].name = n;
    for (Use& u : m_use) {
        if (normKey(u.profile) == normKey(old)) {
            u.profile = n;
        }
    }
    save();
    touched();
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: renamed" << old << "to" << n;
    return QString();
}

QString MlxrProfiles::remove(const QString& name)
{
    const int i = indexOf(name);
    if (i < 0) {
        return tr("There is no profile called %1.").arg(name);
    }
    const QString old = m_profiles[i].name;
    m_profiles.removeAt(i);
    for (int k = int(m_use.size()) - 1; k >= 0; k--) {
        if (normKey(m_use[k].profile) == normKey(old)) {
            m_use.removeAt(k);
        }
    }
    save();
    touched();
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: deleted" << old;
    return QString();
}

QString MlxrProfiles::updateFromCurrent(const QString& name)
{
    const int i = indexOf(name);
    if (i < 0) {
        return tr("There is no profile called %1.").arg(name);
    }
    m_profiles[i].values = cleanMap(captureCurrent());
    save();
    touched();
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: updated" << m_profiles[i].name << "-" << summaryOf(m_profiles[i].values);
    return QString();
}

QString MlxrProfiles::loadIntoSettings(const QString& name)
{
    const int i = indexOf(name);
    if (i < 0) {
        return tr("There is no profile called %1.").arg(name);
    }
    applyValues(m_profiles[i].values);
    // The Settings page's own values now: saved, like the page does when it closes. The config.ini keys go
    // through save() too (it writes the ones that differ from what the file held)
    StreamingPreferences::get()->save();
    MlxrPrefs::get()->save();
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: loaded" << m_profiles[i].name << "into the settings";
    return QString();
}

QString MlxrProfiles::summary(const QString& name) const
{
    const int i = indexOf(name);
    return i < 0 ? QString() : summaryOf(m_profiles[i].values);
}

QString MlxrProfiles::currentSummary() const
{
    return summaryOf(captureCurrent());
}

QString MlxrProfiles::dumpCurrent() const
{
    const QVariantMap m = captureCurrent();
    QStringList l;
    for (const StreamField& f : kStreamFields) {
        l << QString::fromLatin1(f.key) + "=" + printable(m.value(f.key));
    }
    for (const PrefField& f : kPrefFields) {
        l << QString::fromLatin1(f.key) + "=" + printable(m.value(f.key));
    }
    return l.join(' ');
}

// ---- where they are used ---------------------------------------------------------------

QString MlxrProfiles::assignedTo(const QString& pc, const QString& app) const
{
    for (const Use& u : m_use) {
        if (normKey(u.pc) == normKey(pc) && normKey(u.app) == normKey(app)) {
            return u.profile;
        }
    }
    return QString();
}

QString MlxrProfiles::profileFor(const QString& pc, const QString& app) const
{
    if (!app.simplified().isEmpty()) {
        const QString own = assignedTo(pc, app);
        if (!own.isEmpty()) {
            return own;
        }
    }
    return assignedTo(pc, QString());
}

QString MlxrProfiles::assign(const QString& pc, const QString& app, const QString& profile)
{
    const QString p = pc.simplified();
    const QString a = app.simplified();
    if (p.isEmpty()) {
        return tr("Pick a PC first.");
    }
    const bool none = profile.simplified().isEmpty() || profile.simplified().compare("Default", Qt::CaseInsensitive) == 0;
    int at = -1;
    for (int i = 0; i < m_use.size(); i++) {
        if (normKey(m_use[i].pc) == normKey(p) && normKey(m_use[i].app) == normKey(a)) {
            at = i;
        }
    }
    if (none) {
        if (at >= 0) {
            m_use.removeAt(at);
            save();
            touched();
        }
        qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: " << p << "/" << (a.isEmpty() ? QString("every app") : a) << "uses the Default";
        return QString();
    }
    const int pi = indexOf(profile);
    if (pi < 0) {
        return tr("There is no profile called %1.").arg(profile);
    }
    if (at >= 0) {
        m_use[at].profile = m_profiles[pi].name;
    }
    else {
        Use nu;
        nu.pc = p;
        nu.app = a;
        nu.profile = m_profiles[pi].name;
        m_use.append(nu);
    }
    save();
    touched();
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE:" << p << "/" << (a.isEmpty() ? QString("every app") : a) << "uses" << m_profiles[pi].name;
    return QString();
}

// ---- a stream --------------------------------------------------------------------------

std::shared_ptr<MlxrProfiles::Applied> MlxrProfiles::beginLaunch(const QString& pc, const QString& app)
{
    const QString name = profileFor(pc, app);
    if (name.isEmpty()) {
        return nullptr;
    }
    const int pi = indexOf(name);
    if (pi < 0) {
        return nullptr;
    }
    std::shared_ptr<Applied> a = std::make_shared<Applied>();
    a->profile = m_profiles[pi].name;

    StreamingPreferences* sp = StreamingPreferences::get();
    MlxrPrefs* mp = MlxrPrefs::get();
    for (const StreamField& f : kStreamFields) {
        a->before.insert(f.key, sp->property(f.key));
    }
    const QVector<MlxrPrefs::IniEntry> iniBefore = mp->profileIniEntries();
    applyValues(m_profiles[pi].values);
    const QVector<MlxrPrefs::IniEntry> iniNow = mp->profileIniEntries();

    // The VR client reads its config.ini keys from the file: what the file holds now is what goes back
    const QString ini = mp->configIniPath();
    for (int i = 0; i < iniNow.size() && i < iniBefore.size(); i++) {
        const QString old = MlxrPrefs::iniValue(ini, iniNow[i].section, iniNow[i].key, iniBefore[i].value);
        if (!sameIniValue(old, iniNow[i].value)) {
            Applied::Ini e;
            e.section = iniNow[i].section;
            e.key = iniNow[i].key;
            e.old = old;
            e.applied = iniNow[i].value;
            a->ini.append(e);
        }
    }
    if (!a->ini.isEmpty()) {
        // noted before the file is touched, so a launcher that dies during the stream is put right at its next start
        QSettings s;
        s.remove("mlxr_profile_pending");
        s.beginWriteArray("mlxr_profile_pending", int(a->ini.size()));
        for (int i = 0; i < a->ini.size(); i++) {
            s.setArrayIndex(i);
            s.setValue("section", a->ini[i].section);
            s.setValue("key", a->ini[i].key);
            s.setValue("old", a->ini[i].old);
            s.setValue("applied", a->ini[i].applied);
        }
        s.endArray();
        s.sync();
        for (const Applied::Ini& e : a->ini) {
            MlxrPrefs::setIniValue(ini, e.section, e.key, e.applied);
        }
    }
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: using" << a->profile << "for" << pc << "/" << app << "-"
                      << summaryOf(m_profiles[pi].values) << "(" << a->ini.size() << "config.ini keys changed)";
    return a;
}

MlxrProfiles::Applied::~Applied()
{
    end();
}

void MlxrProfiles::Applied::end()
{
    if (ended) {
        return;
    }
    ended = true;
    StreamingPreferences* sp = StreamingPreferences::get();
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
        sp->setProperty(it.key().toLatin1().constData(), it.value());
    }
    MlxrPrefs* mp = MlxrPrefs::get();
    const QString file = mp->configIniPath();
    for (const Ini& e : ini) {
        // a key the headset changed while the stream ran keeps the headset's value
        if (sameIniValue(MlxrPrefs::iniValue(file, e.section, e.key, e.applied), e.applied)) {
            MlxrPrefs::setIniValue(file, e.section, e.key, e.old);
        }
        else {
            qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: config.ini" << e.section << e.key << "was changed during the stream, left as it is";
        }
    }
    if (!ini.isEmpty()) {
        QSettings s;
        s.remove("mlxr_profile_pending");
        s.sync();
    }
    mp->reloadAll();
    qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: ended" << profile << "- the settings are back";
}

// The config.ini keys of a launcher that was killed during a stream with a profile
void MlxrProfiles::recoverPending()
{
    QSettings s;
    const int n = s.beginReadArray("mlxr_profile_pending");
    if (n <= 0) {
        s.endArray();
        return;
    }
    const QString file = MlxrPrefs::get()->configIniPath();
    for (int i = 0; i < n; i++) {
        s.setArrayIndex(i);
        const QString section = s.value("section").toString();
        const QString key = s.value("key").toString();
        const QString old = s.value("old").toString();
        const QString applied = s.value("applied").toString();
        if (!section.isEmpty() && !key.isEmpty() && sameIniValue(MlxrPrefs::iniValue(file, section, key, applied), applied)) {
            MlxrPrefs::setIniValue(file, section, key, old);
            qInfo().noquote() << "MLXR_LAUNCHER_PROFILE: put config.ini" << section << key << "back to" << old << "(the last stream ended without it)";
        }
    }
    s.endArray();
    s.remove("mlxr_profile_pending");
    s.sync();
    MlxrPrefs::get()->reloadAll();
}

// ---- tests ------------------------------------------------------------------------------

// MLXR_LAUNCHER_SELFTEST=profileops:op~op~...   each op is name|field|field, one line of output each
//   create|N  duplicate|N  rename|A|B  remove|N  update|N  load|N  assign|PC|APP|N  set|property|value  save  dump
int MlxrProfiles::selfTestOps(const QString& script)
{
    MlxrProfiles* P = get();
    QTextStream out(stdout);
    for (const QString& op : script.split('~', Qt::SkipEmptyParts)) {
        const QStringList f = op.split('|');
        const QString cmd = f.value(0);
        QString why;
        if (cmd == "create") {
            why = P->create(f.value(1));
        }
        else if (cmd == "duplicate") {
            why = P->duplicate(f.value(1));
        }
        else if (cmd == "rename") {
            why = P->rename(f.value(1), f.value(2));
        }
        else if (cmd == "remove") {
            why = P->remove(f.value(1));
        }
        else if (cmd == "update") {
            why = P->updateFromCurrent(f.value(1));
        }
        else if (cmd == "load") {
            why = P->loadIntoSettings(f.value(1));
        }
        else if (cmd == "assign") {
            why = P->assign(f.value(1), f.value(2), f.value(3));
        }
        else if (cmd == "set") {
            const QByteArray key = f.value(1).toLatin1();
            QObject* target = StreamingPreferences::get()->metaObject()->indexOfProperty(key.constData()) >= 0
                                  ? static_cast<QObject*>(StreamingPreferences::get())
                                  : static_cast<QObject*>(MlxrPrefs::get());
            bool isNum = false;
            const int num = f.value(2).toInt(&isNum);
            const QVariant val = isNum ? QVariant(num) : QVariant(f.value(2));
            if (target->metaObject()->indexOfProperty(key.constData()) < 0 || !target->setProperty(key.constData(), val)) {
                why = "cannot set " + f.value(1);
            }
        }
        else if (cmd == "save") {
            StreamingPreferences::get()->save();
            MlxrPrefs::get()->save();
        }
        else if (cmd == "dump") {
            for (const Profile& p : P->m_profiles) {
                out << "SELFTEST: profile \"" << p.name << "\": " << summaryOf(p.values) << Qt::endl;
            }
            for (const Use& u : P->m_use) {
                out << "SELFTEST: use \"" << u.pc << "\" / \"" << u.app << "\" -> \"" << u.profile << "\"" << Qt::endl;
            }
            out << "SELFTEST: current: " << P->currentSummary() << Qt::endl;
            out << "SELFTEST: values: " << P->dumpCurrent() << Qt::endl;
            continue;
        }
        else {
            why = "unknown op " + cmd;
        }
        out << "SELFTEST: " << op << " -> " << (why.isEmpty() ? QString("ok") : why) << Qt::endl;
    }
    out.flush();
    fflush(stdout);
    fflush(stderr);
    _exit(0);
}
