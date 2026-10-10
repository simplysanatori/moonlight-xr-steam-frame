// Test-only UI hooks shared by the launcher and by the stock moonlight-qt 6.1.0 build that
// tests/launcher/build-stock.sh makes for the screenshot comparison. Header only; does nothing
// unless the environment asks. GPLv3.
//   MLXR_LAUNCHER_PAGE=settings | apps[:N]      pushes that page (N = the PC's index in the grid)
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

inline void pushPage(QQmlApplicationEngine& engine, QObject* root, const QString& page)
{
    QObject* stack = root->findChild<QObject*>("stackView");
    if (!stack) {
        qWarning() << "MLXR_LAUNCHER_PAGE: no stackView";
        return;
    }
    QUrl url("qrc:/gui/SettingsView.qml");
    QVariantMap props;
    if (page.startsWith("apps")) {
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
    QJSValue js = engine.newQObject(stack);
    js.property("push").callWithInstance(js, { engine.newQObject(obj) });
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
