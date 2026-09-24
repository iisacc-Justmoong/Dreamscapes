#include "../src/App/ApplicationLifetime.h"
#include <QTimer>
#include <QDebug>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    configureApplicationLifetime(engine);
    engine.loadData("import QtQuick\nWindow { width: 160; height: 120; visible: true; property int retainedValue: 37 }");
    if (engine.rootObjects().size() != 1) return 10;
    auto *window = qobject_cast<QWindow *>(engine.rootObjects().first());
    if (!window) return 11;
    int phase = 0;
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&] {
        qInfo() << "Explicit quit reached at phase" << phase;
    });
    QTimer::singleShot(250, &app, [&] { phase = 1; window->close(); });
    QTimer::singleShot(500, &app, [&] {
        if (window->isVisible()) { app.exit(12); return; }
        // Cocoa propagates this even when a windowless app is already active.
        app.applicationStateChanged(Qt::ApplicationActive);
        if (!window->isVisible() || window->property("retainedValue").toInt() != 37) {
            app.exit(13); return;
        }
        phase = 2;
        window->close();
        QTimer::singleShot(100, &app, [&] {
            if (window->isVisible()) { app.exit(14); return; }
            phase = 3;
            QCoreApplication::quit();
        });
    });
    QTimer::singleShot(2000, &app, [&] { app.exit(15); });
    const int result = app.exec();
    qInfo() << "Event loop result" << result << "phase" << phase;
#ifdef Q_OS_MACOS
    return result == 0 && phase == 3 ? 0 : 1;
#else
    return result == 0 && phase == 1 ? 0 : 1;
#endif
}
