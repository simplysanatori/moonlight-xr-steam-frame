// Test-only UI hooks shared by the launcher and by the stock moonlight-qt 6.1.0 build that
// tests/launcher/build-stock.sh makes for the screenshot comparison. Header only; does nothing
// unless the environment asks. GPLv3.
//   MLXR_LAUNCHER_PAGE=settings | apps[:N] | bindings | profiles[:N[:App name]]
//                                              pushes that page (N = the PC's index in the grid; settings is
//                                              the page main.qml's settingsPageUrl() names, if it has one;
//                                              bindings is the Controller bindings page, launcher only; profiles is the
//                                              Stream profiles page, opened as an app's menu does when N is given)
//   MLXR_LAUNCHER_SCROLL=Y                     scrolls the pushed page to content y (to see further down)
//   MLXR_LAUNCHER_CLICK=name[,name]            clicks those items of the pushed page (a row: toggles it or opens
//                                              its dialog), then calls the page's saveAll() if it has one. Items
//                                              are found by objectName, also inside a Repeater or Loader
//   MLXR_LAUNCHER_INVOKE=method[:arg[|arg[|arg]]][;...]   calls those functions of the pushed page (QML functions
//                                              taking up to three values), after the clicks
//   MLXR_LAUNCHER_WINDOW=WxH                   resizes the window (to see a whole Settings page)
//   MLXR_LAUNCHER_SCREENSHOT="/p/a.png@1500,/p/b.png@3000"   grabs the window at those times, then quits
#pragma once

#include <QCoreApplication>
#include <unistd.h>
#include <QDebug>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

namespace MlxrUiHooks {

// An item of the page by objectName: the QObject tree first, then the visual tree (a Repeater's or
// Loader's items hang off the visual parent)
inline QObject* findByName(QObject* root, const QString& name)
{
    if (QObject* o = root->findChild<QObject*>(name)) {
        return o;
    }
    QQuickItem* item = qobject_cast<QQuickItem*>(root);
    if (!item) {
        return nullptr;
    }
    QList<QQuickItem*> todo = item->childItems();
    while (!todo.isEmpty()) {
        QQuickItem* c = todo.takeFirst();
        if (c->objectName() == name) {
            return c;
        }
        todo += c->childItems();
    }
    return nullptr;
}

inline void pushPage(QQmlApplicationEngine& engine, QObject* root, const QString& page)
{
    QObject* stack = root->findChild<QObject*>("stackView");
    if (!stack) {
        qWarning() << "MLXR_LAUNCHER_PAGE: no stackView";
        return;
    }
    QUrl url("qrc:/gui/SettingsView.qml");
    // The launcher's main.qml picks the Settings page (the Artemis-style one in hand-off mode); the
    // stock build has no such function and keeps the stock page
    QVariant chosen;
    if (root->metaObject()->indexOfMethod("settingsPageUrl()") >= 0 &&
        QMetaObject::invokeMethod(root, "settingsPageUrl", Q_RETURN_ARG(QVariant, chosen)) && chosen.isValid()) {
        url = QUrl(chosen.toString());
    }
    QVariantMap props;
    if (page == "bindings") {
        url = QUrl("qrc:/gui/XrBindingsView.qml");
    }
    else if (page.startsWith("profiles")) {
        url = QUrl("qrc:/gui/XrProfilesView.qml");
        const QStringList parts = page.split(':');
        if (parts.size() >= 2) {
            props["initialPcIndex"] = parts.value(1).toInt();
            props["initialApp"] = parts.mid(2).join(":");
        }
    }
    else if (page.startsWith("apps")) {
        url = QUrl("qrc:/gui/AppView.qml");
        props["computerIndex"] = page.contains(':') ? page.section(':', 1).toInt() : 0;
        props["objectName"] = "Computer";
    }
    QQmlComponent comp(&engine, url);
    QObject* obj = comp.createWithInitialProperties(props, qmlContext(stack));
    if (!obj) {
        qWarning() << "MLXR_LAUNCHER_PAGE:" << comp.errorString();
        return;
    }
    if (QQuickItem* item = qobject_cast<QQuickItem*>(obj)) {
        item->setParentItem(qobject_cast<QQuickItem*>(stack));
    }
    QQmlEngine::setObjectOwnership(obj, QQmlEngine::JavaScriptOwnership);
    qInfo().noquote() << "MLXR_LAUNCHER_PAGE:" << url.toString();
    QJSValue js = engine.newQObject(stack);
    js.property("push").callWithInstance(js, { engine.newQObject(obj) });
    const QString scroll = qEnvironmentVariable("MLXR_LAUNCHER_SCROLL");
    if (!scroll.isEmpty()) {
        QTimer::singleShot(400, obj, [obj, scroll]() { obj->setProperty("contentY", scroll.toDouble()); });
    }
    const QString click = qEnvironmentVariable("MLXR_LAUNCHER_CLICK");
    if (!click.isEmpty()) {
        QTimer::singleShot(700, obj, [obj, click]() {
            for (const QString& name : click.split(',', Qt::SkipEmptyParts)) {
                QObject* target = findByName(obj, name);
                if (!target || !QMetaObject::invokeMethod(target, "clicked")) {
                    qWarning() << "MLXR_LAUNCHER_CLICK: no clickable" << name;
                }
            }
            // a page that can save on demand does (the Artemis-style page): what the clicks changed
            if (obj->metaObject()->indexOfMethod("saveAll()") >= 0) {
                QMetaObject::invokeMethod(obj, "saveAll");
            }
        });
    }
    const QString invoke = qEnvironmentVariable("MLXR_LAUNCHER_INVOKE");
    if (!invoke.isEmpty()) {
        QTimer::singleShot(1100, obj, [obj, invoke]() {
            for (const QString& call : invoke.split(';', Qt::SkipEmptyParts)) {
                const QString name = call.section(':', 0, 0);
                const QStringList args = call.contains(':') ? call.section(':', 1).split('|') : QStringList();
                const QByteArray sig = name.toLatin1() + "(" + QByteArray("QVariant,").repeated(int(args.size())).chopped(args.isEmpty() ? 0 : 1) + ")";
                if (obj->metaObject()->indexOfMethod(sig.constData()) < 0) {
                    qWarning() << "MLXR_LAUNCHER_INVOKE: no function" << sig;
                    continue;
                }
                bool ok = false;
                const QByteArray n = name.toLatin1();
                switch (args.size()) {
                case 0:
                    ok = QMetaObject::invokeMethod(obj, n.constData());
                    break;
                case 1:
                    ok = QMetaObject::invokeMethod(obj, n.constData(), Q_ARG(QVariant, QVariant(args[0])));
                    break;
                case 2:
                    ok = QMetaObject::invokeMethod(obj, n.constData(), Q_ARG(QVariant, QVariant(args[0])), Q_ARG(QVariant, QVariant(args[1])));
                    break;
                default:
                    ok = QMetaObject::invokeMethod(obj, n.constData(), Q_ARG(QVariant, QVariant(args[0])), Q_ARG(QVariant, QVariant(args[1])),
                                                   Q_ARG(QVariant, QVariant(args[2])));
                    break;
                }
                if (!ok) {
                    qWarning() << "MLXR_LAUNCHER_INVOKE: could not call" << sig;
                }
            }
        });
    }
}

inline void install(QQmlApplicationEngine& engine)
{
    QObject* root = engine.rootObjects().value(0);
    if (!root) {
        return;
    }
    const QString size = qEnvironmentVariable("MLXR_LAUNCHER_WINDOW");
    if (size.contains('x')) {
        root->setProperty("width", size.section('x', 0, 0).toInt());
        root->setProperty("height", size.section('x', 1, 1).toInt());
    }
    const QString page = qEnvironmentVariable("MLXR_LAUNCHER_PAGE");
    if (!page.isEmpty()) {
        QTimer::singleShot(600, root, [&engine, root, page]() { pushPage(engine, root, page); });
    }
    const QString shots = qEnvironmentVariable("MLXR_LAUNCHER_SCREENSHOT");
    if (!shots.isEmpty()) {
        int last = 0;
        for (const QString& entry : shots.split(',', Qt::SkipEmptyParts)) {
            const int at = entry.lastIndexOf('@');
            const QString path = at < 0 ? entry : entry.left(at);
            const int ms = at < 0 ? 1500 : entry.mid(at + 1).toInt();
            last = qMax(last, ms);
            QTimer::singleShot(ms, root, [root, path]() {
                if (QQuickWindow* w = qobject_cast<QQuickWindow*>(root)) {
                    const QImage img = w->grabWindow();
                    qInfo() << "Screenshot" << path << img.size() << (img.save(path) ? "saved" : "FAILED");
                }
            });
        }
        // Leave at once: after QCoreApplication::quit() this app (stock too) can take minutes to exit
        // when offscreen, which is no use to a test
        QTimer::singleShot(last + 700, QCoreApplication::instance(), []() {
            fflush(stdout);
            fflush(stderr);
            _exit(0);
        });
    }
}

}
