// Moonlight XR Native launcher (lane wave2-bind-editor): see mlxrbindings.h. GPLv3.
#include "mlxrbindings.h"
#include "mlxrhandoff.h"

#include <QAbstractItemModel>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <cstring>
#include <functional>

// xr/core/xr_bind.c and what it includes, compiled into the launcher by launcher/build.sh. Included
// last: xr_shared.h is a header of plain #defines for the VR client.
extern "C" {
#include "xr_bind.h"
}

struct MlxrBindings::State
{
    XrBindSet set;
    XrBindErrors errs;
    QByteArray text;
    // xrBindLoadFile's result: 1 wrote the default file, 0 read it, -1 could not
    int loadResult = 0;
};

static std::unique_ptr<MlxrBindings::State> newState()
{
    std::unique_ptr<MlxrBindings::State> s(new MlxrBindings::State());
    xrBindSetInit(&s->set);
    std::memset(&s->errs, 0, sizeof(s->errs));
    return s;
}

// The core's reading of a text
static std::unique_ptr<MlxrBindings::State> parseState(const QByteArray& text)
{
    std::unique_ptr<MlxrBindings::State> s = newState();
    s->text = text;
    xrBindParse(&s->set, text.constData(), &s->errs);
    return s;
}

static const char* const kKind[] = { "none", "key", "mouse", "wheel", "pad", "stick", "app" };

static const char* const kMouse[] = { "left", "right", "middle", "x1", "x2" };
static const char* const kWheel[] = { "up", "down", "left", "right" };
static const char* const kPad[] = { "a", "b", "x", "y", "lb", "rb", "lt", "rt", "ls", "rs", "start", "back", "guide",
                                    "dpad_up", "dpad_down", "dpad_left", "dpad_right" };
static const char* const kStick[] = { "scroll", "mouse", "wasd", "arrows", "dpad", "pad_left", "pad_right" };
static const char* const kApp[] = { "keyboard", "ray", "recentre", "cog", "3d", "gamepad", "bringhere" };

// The chords the core refuses in every mode (docs/dev/bindings.md "Never bindable"); selfCheck() asks the
// core that it still does
struct Reserved
{
    const char* chord;
    const char* kind;
};
static const Reserved kReserved[] = {
    { "left.view+left.grip", "switch" },
    { "left.stick_click+right.stick_click", "switch" },
    { "left.trigger+right.trigger+left.grip+right.grip", "switch" },
    { "left.view+right.menu+left.bumper+right.bumper", "disconnect" },
};

static int bitCount(quint64 m)
{
    int n = 0;
    for (; m; m &= m - 1) {
        n++;
    }
    return n;
}

static QString modeWord(int mode)
{
    return mode == XR_BIND_MODE_GAMEPAD ? QStringLiteral("gamepad") : QStringLiteral("pointer");
}

// "right.x" or "left.view+right.menu" -> the controls as bits
static bool parseSources(const QString& expr, quint64* bits, QString* err)
{
    const QStringList parts = expr.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        *err = QObject::tr("No control named.");
        return false;
    }
    quint64 b = 0;
    for (const QString& p : parts) {
        const QString name = p.trimmed().toLower();
        const QByteArray n = name.toUtf8();
        const int s = xrBindSourceFind(n.constData());
        if (s < 0) {
            *err = QObject::tr("'%1' is not a control (left.trigger, right.a, ...).").arg(name);
            return false;
        }
        if (b & XR_BIND_BIT(s)) {
            *err = QObject::tr("'%1' is in the chord twice.").arg(name);
            return false;
        }
        b |= XR_BIND_BIT(s);
    }
    if (bitCount(b) > XR_BIND_CHORD_MAX_PARTS) {
        *err = QObject::tr("A chord is at most %1 controls.").arg(XR_BIND_CHORD_MAX_PARTS);
        return false;
    }
    *bits = b;
    return true;
}

static QString sourcesText(quint64 bits)
{
    QStringList names;
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        if (bits & XR_BIND_BIT(i)) {
            names << QString::fromLatin1(xrBindSourceName(i));
        }
    }
    return names.join('+');
}

// The file matches an app's name case blind in ASCII only, the way the core does
static int appIndexOf(const XrBindSet& s, const QString& app)
{
    const QByteArray want = app.toUtf8().toLower();
    for (int i = 0; i < s.appCount; i++) {
        if (QByteArray(s.apps[i]).toLower() == want) {
            return i;
        }
    }
    return -1;
}

// The file's rule for exactly this control or chord in this scope (app "" = the [pointer]/[gamepad] sections)
static int findRuleIdx(const XrBindSet& s, const QString& app, int mode, quint64 bits)
{
    int idx = -1;
    if (!app.isEmpty()) {
        idx = appIndexOf(s, app);
        if (idx < 0) {
            return -1;
        }
    }
    for (int i = 0; i < s.ruleCount; i++) {
        const XrBindRule& r = s.rules[i];
        if (r.mode == mode && r.sources == bits && (app.isEmpty() ? r.app < 0 : r.app == idx)) {
            return i;
        }
    }
    return -1;
}

static QString formatAction(const XrBindAction& a)
{
    char buf[96];
    xrBindActionFormat(&a, buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

static bool isBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

// ---- the singleton ----------------------------------------------------------------------------

MlxrBindings* MlxrBindings::get()
{
    static MlxrBindings* s_Bindings = nullptr;
    if (!s_Bindings) {
        s_Bindings = new MlxrBindings();
    }
    return s_Bindings;
}

MlxrBindings::MlxrBindings(QObject* parent) : QObject(parent)
{
    // The file the headset's client reads: $MLXR_CONFIG_DIR/bindings.ini, else ~/.config/moonlight-xr/
    m_path = MlxrHandoff::nativeConfigDir() + "/bindings.ini";
    d = newState();
    selfCheck();
    reload();
    const QString ops = qEnvironmentVariable("MLXR_LAUNCHER_BIND_OPS");
    if (!ops.isEmpty()) {
        runOps(ops);
    }
}

MlxrBindings::~MlxrBindings() = default;

void MlxrBindings::reload()
{
    std::unique_ptr<State> next = newState();
    const QByteArray path = m_path.toLocal8Bit();
    next->loadResult = xrBindLoadFile(&next->set, path.constData(), &next->errs);
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly)) {
        next->text = f.readAll();
    }
    d = std::move(next);
    refreshDerived();
    m_revision++;
    qInfo().noquote() << "MLXR_BINDINGS: loaded" << (d->loadResult == 1 ? "(wrote the default file)" : d->loadResult < 0 ? "(failed)" : "")
                      << d->set.ruleCount << "rules," << d->errs.count << "errors";
    dumpFile();
    emit changed();
}

void MlxrBindings::refreshDerived()
{
    m_errors.clear();
    for (int i = 0; i < d->errs.count && i < XR_BIND_MAX_ERRORS; i++) {
        m_errors << QString::fromUtf8(d->errs.errors[i].text);
    }
    m_errorCount = d->errs.count;
    m_fileError = d->loadResult < 0 && d->errs.count > 0 ? QString::fromUtf8(d->errs.errors[0].text) : QString();
    m_fileApps.clear();
    for (int i = 0; i < d->set.appCount; i++) {
        m_fileApps << QString::fromUtf8(d->set.apps[i]);
    }
}

// Checks, at start, that the lists this page offers are still what the core accepts; the test greps the line
void MlxrBindings::selfCheck() const
{
    int bad = 0;
    const auto check = [&bad](const QString& text) {
        XrBindAction a;
        char err[256];
        const QByteArray b = text.toUtf8();
        if (!xrBindActionParse(b.constData(), &a, err, sizeof(err), nullptr)) {
            qWarning().noquote() << "MLXR_BINDINGS: choice not accepted by the core:" << text << "-" << err;
            bad++;
        }
    };
    for (const char* v : kMouse) check(QStringLiteral("mouse:") + v);
    for (const char* v : kWheel) check(QStringLiteral("wheel:") + v);
    for (const char* v : kPad) check(QStringLiteral("pad:") + v);
    for (const char* v : kStick) check(QStringLiteral("stick:") + v);
    for (const char* v : kApp) check(QStringLiteral("app:") + v);
    for (int vk = 1; vk < 255; vk++) {
        const char* n = xrBindKeyName(vk);
        if (n && xrBindKeyFind(n) != vk) {
            qWarning().noquote() << "MLXR_BINDINGS: key name does not read back:" << n;
            bad++;
        }
    }
    for (const Reserved& r : kReserved) {
        for (int mode = 0; mode < XR_BIND_MODES; mode++) {
            const QByteArray t = (QStringLiteral("[") + modeWord(mode) + "]\n" + r.chord + " = none\n").toUtf8();
            std::unique_ptr<State> s = parseState(t);
            if (s->errs.count == 0 || s->set.ruleCount != 0) {
                qWarning().noquote() << "MLXR_BINDINGS: reserved chord not refused by the core:" << r.chord << modeWord(mode);
                bad++;
            }
        }
    }
    if (bad == 0) {
        qInfo().noquote() << "MLXR_BINDINGS: choices ok";
    }
}

void MlxrBindings::dumpFile() const
{
    if (!qEnvironmentVariableIsSet("MLXR_LAUNCHER_BIND_DUMP")) {
        return;
    }
    qInfo().noquote() << "MLXR_BINDINGS_FILE| begin";
    for (const QString& l : serializedFile().split('\n')) {
        qInfo().noquote() << "MLXR_BINDINGS_FILE|" << l;
    }
    qInfo().noquote() << "MLXR_BINDINGS_FILE| end";
}

QString MlxrBindings::serializedFile() const
{
    const int len = xrBindSerialize(&d->set, nullptr, 0);
    QByteArray buf(len + 1, '\0');
    xrBindSerialize(&d->set, buf.data(), buf.size());
    buf.resize(len);
    return QString::fromUtf8(buf);
}

// ---- what the page lists ------------------------------------------------------------------------

QStringList MlxrBindings::sourceNames() const
{
    QStringList l;
    for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
        l << QString::fromLatin1(xrBindSourceName(i));
    }
    return l;
}

QStringList MlxrBindings::choices(const QString& kind) const
{
    QStringList l;
    if (kind == "mouse") for (const char* v : kMouse) l << v;
    else if (kind == "wheel") for (const char* v : kWheel) l << v;
    else if (kind == "pad") for (const char* v : kPad) l << v;
    else if (kind == "stick") for (const char* v : kStick) l << v;
    else if (kind == "app") for (const char* v : kApp) l << v;
    return l;
}

QVariantList MlxrBindings::reservedChords() const
{
    QVariantList l;
    for (const Reserved& r : kReserved) {
        QVariantMap m;
        m["chord"] = QString::fromLatin1(r.chord);
        m["kind"] = QString::fromLatin1(r.kind);
        l << m;
    }
    return l;
}

QVariantList MlxrBindings::keyGroups() const
{
    static const char* const titles[] = { "Letters", "Digits", "Function keys", "Editing", "Navigation", "Modifiers",
                                          "Numpad", "Punctuation", "Media and browser", "Other" };
    const int groupCount = int(sizeof(titles) / sizeof(titles[0]));
    QList<QStringList> keys;
    for (int i = 0; i < groupCount; i++) {
        keys << QStringList();
    }
    for (int vk = 1; vk < 255; vk++) {
        const char* n = xrBindKeyName(vk);
        if (!n) {
            continue;
        }
        int g;
        if (vk >= 0x41 && vk <= 0x5A) g = 0;
        else if (vk >= 0x30 && vk <= 0x39) g = 1;
        else if (vk >= 0x70 && vk <= 0x87) g = 2;
        else if (vk >= 0x60 && vk <= 0x6F) g = 6;
        else if (vk == 0x1B || vk == 0x09 || vk == 0x0D || vk == 0x20 || vk == 0x08 || vk == 0x14) g = 3;
        else if ((vk >= 0x21 && vk <= 0x28) || vk == 0x2D || vk == 0x2E) g = 4;
        else if (vk == 0x5B || vk == 0x5C || (vk >= 0xA0 && vk <= 0xA5)) g = 5;
        else if ((vk >= 0xBA && vk <= 0xC0) || (vk >= 0xDB && vk <= 0xDE)) g = 7;
        else if (vk == 0xA6 || vk == 0xA7 || (vk >= 0xAD && vk <= 0xB3)) g = 8;
        else g = 9;
        keys[g] << QString::fromLatin1(n);
    }
    QVariantList out;
    for (int i = 0; i < groupCount; i++) {
        if (keys[i].isEmpty()) {
            continue;
        }
        QVariantMap m;
        m["title"] = QString::fromLatin1(titles[i]);
        m["keys"] = keys[i];
        out << m;
    }
    return out;
}

QStringList MlxrBindings::modelNames(QObject* model) const
{
    QStringList l;
    QAbstractItemModel* m = qobject_cast<QAbstractItemModel*>(model);
    if (!m) {
        return l;
    }
    const int role = m->roleNames().key("name", -1);
    if (role < 0) {
        return l;
    }
    for (int i = 0; i < m->rowCount(); i++) {
        l << m->data(m->index(i, 0), role).toString();
    }
    return l;
}

// ---- what the file says -------------------------------------------------------------------------

QVariantMap MlxrBindings::describe(const QString& action) const
{
    QVariantMap m;
    XrBindAction a;
    char err[256];
    const QByteArray b = action.trimmed().toUtf8();
    if (!xrBindActionParse(b.constData(), &a, err, sizeof(err), nullptr)) {
        m["ok"] = false;
        m["error"] = QString::fromUtf8(err);
        return m;
    }
    const QString text = formatAction(a);
    m["ok"] = true;
    m["text"] = text;
    m["kind"] = QString::fromLatin1(a.kind >= 0 && a.kind < 7 ? kKind[a.kind] : "none");
    m["mods"] = a.mods;
    if (a.kind == XR_BIND_ACT_KEY) {
        const char* n = xrBindKeyName(a.code);
        m["key"] = n ? QString::fromLatin1(n) : QString();
        m["value"] = n ? QString::fromLatin1(n) : QStringLiteral("0x") + QStringLiteral("%1").arg(a.code, 2, 16, QLatin1Char('0')).toUpper();
    }
    else {
        m["key"] = QString();
        m["value"] = a.kind == XR_BIND_ACT_NONE ? QString() : text.section(':', 1);
    }
    return m;
}

QVariantMap MlxrBindings::normalizeSources(const QString& text) const
{
    QVariantMap m;
    quint64 bits = 0;
    QString err;
    if (!parseSources(text, &bits, &err)) {
        m["ok"] = false;
        m["error"] = err;
        return m;
    }
    m["ok"] = true;
    m["expr"] = sourcesText(bits);
    return m;
}

QVariantMap MlxrBindings::binding(const QString& app, int mode, const QString& expr) const
{
    QVariantMap m;
    quint64 bits = 0;
    QString err;
    if (!parseSources(expr, &bits, &err)) {
        m["ok"] = false;
        m["error"] = err;
        return m;
    }
    const auto lookup = [bits](const XrBindTable& t, int* from) {
        XrBindAction a = { XR_BIND_ACT_NONE, 0, 0 };
        *from = XR_BIND_FROM_DEFAULT;
        if (bitCount(bits) == 1) {
            for (int i = 0; i < XR_BIND_SRC_COUNT; i++) {
                if (bits == XR_BIND_BIT(i)) {
                    a = t.single[i];
                    *from = t.from[i];
                }
            }
        }
        else {
            for (int i = 0; i < t.chordCount; i++) {
                if (t.chords[i].parts == bits) {
                    a = t.chords[i].action;
                    *from = t.chords[i].from;
                }
            }
        }
        return a;
    };
    XrBindTable t;
    const QByteArray appBytes = app.toUtf8();
    xrBindResolve(&d->set, mode, app.isEmpty() ? nullptr : appBytes.constData(), &t);
    int from = 0;
    const XrBindAction a = lookup(t, &from);
    XrBindTable dt;
    xrBindResolve(nullptr, mode, nullptr, &dt);
    int dfrom = 0;
    const XrBindAction da = lookup(dt, &dfrom);
    m["ok"] = true;
    m["action"] = formatAction(a);
    m["from"] = from;
    m["explicit"] = findRuleIdx(d->set, app, mode, bits) >= 0;
    m["defaultAction"] = formatAction(da);
    return m;
}

QVariantList MlxrBindings::chords(const QString& app, int mode) const
{
    QVariantList out;
    XrBindTable t;
    const QByteArray appBytes = app.toUtf8();
    xrBindResolve(&d->set, mode, app.isEmpty() ? nullptr : appBytes.constData(), &t);
    QList<quint64> seen;
    for (int i = 0; i < t.chordCount; i++) {
        QVariantMap m;
        m["expr"] = sourcesText(t.chords[i].parts);
        m["action"] = formatAction(t.chords[i].action);
        m["from"] = t.chords[i].from;
        m["explicit"] = findRuleIdx(d->set, app, mode, t.chords[i].parts) >= 0;
        out << m;
        seen << t.chords[i].parts;
    }
    // The scope's own chords set to none (a default chord switched off) are not in the table
    const int idx = app.isEmpty() ? -1 : appIndexOf(d->set, app);
    for (int i = 0; i < d->set.ruleCount; i++) {
        const XrBindRule& r = d->set.rules[i];
        if (r.mode != mode || bitCount(r.sources) < 2 || seen.contains(r.sources)) {
            continue;
        }
        if (app.isEmpty() ? r.app >= 0 : r.app != idx) {
            continue;
        }
        QVariantMap m;
        m["expr"] = sourcesText(r.sources);
        m["action"] = formatAction(r.action);
        m["from"] = app.isEmpty() ? XR_BIND_FROM_MODE : XR_BIND_FROM_APP;
        m["explicit"] = true;
        out << m;
        seen << r.sources;
    }
    return out;
}

// ---- edits --------------------------------------------------------------------------------------

QString MlxrBindings::commit(std::unique_ptr<State> next, const QString& what)
{
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        return tr("Cannot create %1").arg(QFileInfo(m_path).absolutePath());
    }
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly) || f.write(next->text) != next->text.size() || !f.commit()) {
        return tr("Cannot write %1: %2").arg(m_path, f.errorString());
    }
    next->loadResult = 0;
    d = std::move(next);
    refreshDerived();
    m_revision++;
    qInfo().noquote() << "MLXR_BINDINGS: saved" << what << "-" << d->set.ruleCount << "rules," << d->errs.count << "errors";
    dumpFile();
    emit changed();
    return QString();
}

QString MlxrBindings::setRule(const QString& app, int mode, quint64 bits, const QString& actionText)
{
    if (mode != XR_BIND_MODE_POINTER && mode != XR_BIND_MODE_GAMEPAD) {
        return tr("Unknown mode.");
    }
    if (d->loadResult < 0) {
        return tr("bindings.ini cannot be used: %1").arg(m_fileError);
    }
    const QString appName = app.trimmed();
    if (!appName.isEmpty() && (appName.contains('"') || appName.contains('\n') || appName.contains('\r') ||
                               appName.toUtf8().size() >= XR_BIND_APP_MAX)) {
        return tr("The file cannot name this app (a name has no double quote and is under %1 bytes).").arg(XR_BIND_APP_MAX);
    }
    XrBindAction act;
    char err[256];
    const QByteArray at = actionText.trimmed().toUtf8();
    if (!xrBindActionParse(at.constData(), &act, err, sizeof(err), nullptr)) {
        return QString::fromUtf8(err);
    }
    const QByteArray fmt = formatAction(act).toLatin1();

    QList<QByteArray> lines = d->text.split('\n');
    const QByteArray cr = d->text.contains("\r\n") ? QByteArray("\r") : QByteArray();
    int touched = 0;
    const int idx = findRuleIdx(d->set, appName, mode, bits);
    if (idx >= 0) {
        // Replace the value on the rule's own line, keeping what is in front of it and any comment after it
        const int L = d->set.rules[idx].line;
        if (L < 1 || L > lines.size()) {
            return tr("The file has changed under the page; reload it.");
        }
        QByteArray& line = lines[L - 1];
        const int eq = line.indexOf('=');
        if (eq < 0) {
            return tr("The file has changed under the page; reload it.");
        }
        int vs = eq + 1;
        while (vs < line.size() && (line[vs] == ' ' || line[vs] == '\t')) {
            vs++;
        }
        int ce = line.size();
        for (int i = vs; i < line.size(); i++) {
            if (line[i] == '#' || line[i] == ';') {
                ce = i;
                break;
            }
        }
        int ve = ce;
        while (ve > vs && isBlank(line[ve - 1])) {
            ve--;
        }
        line = line.left(eq + 1) + ' ' + fmt + line.mid(ve);
        touched = L;
    }
    else {
        QByteArray rule = (appName.isEmpty() ? QString() : modeWord(mode) + ".").toLatin1() + sourcesText(bits).toLatin1() +
                          " = " + fmt + cr;
        // After the last line of the scope's section when it has one; a section may appear again, so a
        // scope without lines gets a section at the end of the file
        const int appIdx = appName.isEmpty() ? -1 : appIndexOf(d->set, appName);
        int after = 0;
        for (int i = 0; i < d->set.ruleCount; i++) {
            const XrBindRule& r = d->set.rules[i];
            const bool same = appName.isEmpty() ? (r.app < 0 && r.mode == mode) : (appIdx >= 0 && r.app == appIdx);
            if (same) {
                after = std::max(after, r.line);
            }
        }
        if (after > 0 && after <= lines.size()) {
            lines.insert(after, rule);
            touched = after + 1;
        }
        else {
            if (!lines.isEmpty() && lines.last().isEmpty()) {
                lines.removeLast();
            }
            if (!lines.isEmpty()) {
                lines << cr;
            }
            lines << (appName.isEmpty() ? QByteArray("[") + modeWord(mode).toLatin1() + "]" : QByteArray("[app \"") + appName.toUtf8() + "\"]") + cr;
            lines << rule << QByteArray();
            touched = lines.size() - 1;
        }
    }
    std::unique_ptr<State> next = parseState(lines.join(QByteArray("\n")));
    const int ri = findRuleIdx(next->set, appName, mode, bits);
    if (ri < 0 || !xrBindActionEqual(&next->set.rules[ri].action, &act)) {
        for (int i = 0; i < next->errs.count && i < XR_BIND_MAX_ERRORS; i++) {
            if (next->errs.errors[i].line == touched) {
                return QString::fromUtf8(next->errs.errors[i].text);
            }
        }
        return tr("The binding was not accepted.");
    }
    return commit(std::move(next), QStringLiteral("%1 %2%3 = %4").arg(modeWord(mode), appName.isEmpty() ? QString() : appName + " ", sourcesText(bits), QString::fromLatin1(fmt)));
}

QString MlxrBindings::removeRules(const QString& app, int mode, quint64 bits)
{
    if (d->loadResult < 0) {
        return tr("bindings.ini cannot be used: %1").arg(m_fileError);
    }
    const QString appName = app.trimmed();
    std::unique_ptr<State> cur = parseState(d->text);
    bool any = false;
    // A control bound twice (the second line is an error and the first one counts) comes back after the
    // first line goes; go round again until it is gone
    for (int pass = 0; pass < 16; pass++) {
        const int idx = appName.isEmpty() ? -1 : appIndexOf(cur->set, appName);
        if (!appName.isEmpty() && idx < 0) {
            break;
        }
        QList<int> doomed;
        for (int i = 0; i < cur->set.ruleCount; i++) {
            const XrBindRule& r = cur->set.rules[i];
            if (appName.isEmpty() ? r.app >= 0 : r.app != idx) continue;
            if (mode >= 0 && r.mode != mode) continue;
            if (bits != 0 && r.sources != bits) continue;
            doomed << r.line;
        }
        if (doomed.isEmpty()) {
            break;
        }
        std::sort(doomed.begin(), doomed.end(), std::greater<int>());
        QList<QByteArray> lines = cur->text.split('\n');
        int last = 0;
        for (int L : doomed) {
            if (L == last || L < 1 || L > lines.size()) continue;
            lines.removeAt(L - 1);
            last = L;
        }
        cur = parseState(lines.join(QByteArray("\n")));
        any = true;
    }
    if (!any) {
        return QString();
    }
    return commit(std::move(cur), QStringLiteral("reset %1 %2").arg(appName.isEmpty() ? QStringLiteral("-") : appName,
                                                                       bits ? sourcesText(bits) : QStringLiteral("all")));
}

QString MlxrBindings::setBinding(const QString& app, int mode, const QString& expr, const QString& action)
{
    quint64 bits = 0;
    QString err;
    if (!parseSources(expr, &bits, &err)) {
        return err;
    }
    return setRule(app, mode, bits, action);
}

QString MlxrBindings::resetBinding(const QString& app, int mode, const QString& expr)
{
    quint64 bits = 0;
    QString err;
    if (!parseSources(expr, &bits, &err)) {
        return err;
    }
    return removeRules(app, mode, bits);
}

QString MlxrBindings::resetAll(const QString& app)
{
    return removeRules(app, -1, 0);
}

// Test hook, MLXR_LAUNCHER_BIND_OPS: edits made at start, "~" between them, "|" between fields:
//   set|<app or ->|<pointer|gamepad>|<control or chord>|<action>
//   reset|<app or ->|<pointer|gamepad>|<control or chord>
//   resetall|<app or ->
void MlxrBindings::runOps(const QString& script)
{
    for (const QString& op : script.split('~', Qt::SkipEmptyParts)) {
        const QStringList f = op.split('|');
        const QString app = f.size() > 1 && f[1] != "-" ? f[1] : QString();
        const int mode = f.size() > 2 && f[2] == "gamepad" ? XR_BIND_MODE_GAMEPAD : XR_BIND_MODE_POINTER;
        QString r;
        if (f[0] == "set" && f.size() >= 5) {
            r = setBinding(app, mode, f[3], f[4]);
        }
        else if (f[0] == "reset" && f.size() >= 4) {
            r = resetBinding(app, mode, f[3]);
        }
        else if (f[0] == "resetall") {
            r = resetAll(app);
        }
        else {
            r = QStringLiteral("unknown op");
        }
        qInfo().noquote() << "MLXR_BINDINGS_OP:" << op << "->" << (r.isEmpty() ? QStringLiteral("ok") : QStringLiteral("error: ") + r);
    }
}
