#pragma once
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QWindow>

inline void configureApplicationLifetime(QQmlApplicationEngine &engine)
{
#ifdef Q_OS_MACOS
    // Window closure keeps the runtime alive; Quit remains an explicit action.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, &engine,
        [&engine](Qt::ApplicationState state) {
            if (state != Qt::ApplicationActive) return;
            // Cocoa also propagates Active when a windowless app's Dock icon is clicked.
            for (QObject *root : engine.rootObjects()) {
                if (auto *window = qobject_cast<QWindow *>(root)) {
                    if (!window->isVisible()) {
                        window->show();
                        window->raise();
                        window->requestActivate();
                    }
                    return;
                }
            }
        });
#else
    Q_UNUSED(engine);
#endif
}
