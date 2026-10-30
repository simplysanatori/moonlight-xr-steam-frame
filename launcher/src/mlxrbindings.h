// Moonlight XR Native launcher (lane wave2-bind-editor): the data side of the Controller bindings page
// (launcher/qml/XrBindingsView.qml). New file, added to app/streaming by launcher/build.sh. GPLv3.
//
// The launcher has no parser of its own for ~/.config/moonlight-xr/bindings.ini: xr/core/xr_bind.c, the
// file the headset's client reads it with, is compiled into the launcher (build.sh copies it, and the
// two files it needs, next to this one) and every question about the file goes to it: xrBindLoadFile
// (which writes the commented default file on the first run), xrBindParse (the errors with their
// line numbers, the rules), xrBindActionParse (what a typed or picked action means), xrBindResolve (what a
// control does for an app, and where that came from), xrBindSerialize (the test's canonical text of the
// file).
//
// Saving does not rewrite the file from the serializer, which would lose every comment. Each edit is
// a change to the file's own lines, found by the line numbers the parser reports: a changed binding
// replaces the value on its line (a comment after it stays), a reset removes the line, a new binding
// is added after the last line of its section (or in a new section at the end of the file, since a
// section may appear more than once). Every other byte, comments and the lines the parser refused
// included, is kept. An edit is written only when the core reads the result back with the new binding
// in it; otherwise the file is left as it was and the core's own error comes back. The file is written
// at once (QSaveFile) on every change, so nothing is pending when the page closes.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

class MlxrBindings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString path READ path CONSTANT)
    // The core's errors for the file as it is now, each "line:column: what" (at most 16 are kept)
    Q_PROPERTY(QStringList errors READ errors NOTIFY changed)
    // How many there are in all
    Q_PROPERTY(int errorCount READ errorCount NOTIFY changed)
    // Why the file could not be read or written, "" when it can
    Q_PROPERTY(QString fileError READ fileError NOTIFY changed)
    // The [app "Name"] sections the file has
    Q_PROPERTY(QStringList fileApps READ fileApps NOTIFY changed)
    // Counts up on every change, for QML bindings to depend on
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    // The file's text with what the core made of it (defined in the .cpp)
    struct State;

    static MlxrBindings* get();
    ~MlxrBindings() override;

    QString path() const { return m_path; }
    QStringList errors() const { return m_errors; }
    int errorCount() const { return m_errorCount; }
    QString fileError() const { return m_fileError; }
    QStringList fileApps() const { return m_fileApps; }
    int revision() const { return m_revision; }

    // Reads the file again (writing the default one when there is none, as the headset's client does)
    Q_INVOKABLE void reload();

    // ---- what the page lists ----
    // The core's names for the controls, in its order ("left.trigger" ... "right.y_touch")
    Q_INVOKABLE QStringList sourceNames() const;
    // [{title, keys: [name, ...]}] for the key picker: every key the core has a name for, in groups
    Q_INVOKABLE QVariantList keyGroups() const;
    // The values of an action kind that is not a key: mouse, wheel, pad, stick, app
    Q_INVOKABLE QStringList choices(const QString& kind) const;
    // [{chord, kind}] the chords the core never lets a binding take; kind is "switch" or "disconnect"
    Q_INVOKABLE QVariantList reservedChords() const;

    // ---- what the file says ----
    // One control or chord ("right.x", "left.view+right.menu") in one mode (0 pointer, 1 gamepad) for
    // an app ("" = every app): {ok, action, from, explicit, defaultAction} with action as the file
    // writes it, from 0 default / 1 the mode's section / 2 the app's section, explicit true when the
    // file has a line for exactly this scope
    Q_INVOKABLE QVariantMap binding(const QString& app, int mode, const QString& expr) const;
    // The chords bound for that scope (the defaults', the file's and the scope's own lines, with the
    // ones set to none): [{expr, action, from, explicit}]
    Q_INVOKABLE QVariantList chords(const QString& app, int mode) const;
    // An action text as the core reads it: {ok, error, kind, value, mods, key, text}
    Q_INVOKABLE QVariantMap describe(const QString& action) const;
    // A control or chord typed by hand, as the core's names: {ok, error, expr}
    Q_INVOKABLE QVariantMap normalizeSources(const QString& text) const;

    // ---- edits (each is written to the file at once). Return "" or why it could not be done ----
    Q_INVOKABLE QString setBinding(const QString& app, int mode, const QString& expr, const QString& action);
    // Takes the file's line for that control or chord away, so the layer below shows through
    Q_INVOKABLE QString resetBinding(const QString& app, int mode, const QString& expr);
    // Takes away every line of the scope: both modes of every app when app is "", else that app's
    Q_INVOKABLE QString resetAll(const QString& app);

    // ---- helpers ----
    // The "name" role of a list model (the launcher's PC list and app list) as strings
    Q_INVOKABLE QStringList modelNames(QObject* model) const;
    // The file as the core's serializer writes what it parsed from it (canonical, comments gone)
    Q_INVOKABLE QString serializedFile() const;

signals:
    void changed();

private:
    explicit MlxrBindings(QObject* parent = nullptr);

    QString setRule(const QString& app, int mode, quint64 bits, const QString& action);
    // Takes away the file's lines of a scope: mode -1 for both, bits 0 for every control
    QString removeRules(const QString& app, int mode, quint64 bits);
    // Writes the edited text and makes it the current state
    QString commit(std::unique_ptr<State> next, const QString& what);
    void refreshDerived();
    void selfCheck() const;
    void runOps(const QString& script);
    void dumpFile() const;

    QString m_path;
    std::unique_ptr<State> d;
    QStringList m_errors;
    int m_errorCount = 0;
    QString m_fileError;
    QStringList m_fileApps;
    int m_revision = 0;
};
