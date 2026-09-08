#include <backend/runtime/appentry.h>
#include "App/Generation/GenerationController.h"
#include "App/Views/Result/ImageFileExporter.h"
#include "App/Views/Result/PhotoLibraryExporter.h"
#include <QtQml/qqml.h>
#include <iiSocietyHelper.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char *argv[])
{
    lvrs::QmlAppLaunchSpec application;
    application.bootstrap.applicationName = QStringLiteral("Dreamscapes");
    application.bootstrap.quickStyleName = QStringLiteral("Basic");
    application.moduleUri = QStringLiteral("Dreamscapes");
    application.rootObject = QStringLiteral("Main");
    application.configureEngine = [](QQmlApplicationEngine &engine) {
        qmlRegisterType<GenerationController>("Dreamscapes.Storage", 1, 0, "GenerationController");
        qmlRegisterType<ImageFileExporter>("Dreamscapes.Storage", 1, 0, "ImageFileExporter");
        qmlRegisterType<PhotoLibraryExporter>("Dreamscapes.Storage", 1, 0, "PhotoLibraryExporter");
        auto *helper = new iiSocietyHelper::Helper(&engine);
        helper->setObjectName(QStringLiteral("societyHelper"));
        engine.rootContext()->setContextProperty(QStringLiteral("societyHelper"), helper);
        const auto updateActivity = [helper](Qt::ApplicationState state) {
            if (!helper->isRunning() && !helper->start({"com.iisacc.dreamscapes", "Dreamscapes", DREAMSCAPES_APP_VERSION})) return;
            helper->setActivity(state == Qt::ApplicationActive ? iiSocietyHelper::Activity::Foreground
                                                               : iiSocietyHelper::Activity::Background);
        };
        QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, helper, updateActivity);
        updateActivity(QGuiApplication::applicationState());
    };
    for (int index = 1; index + 1 < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == "--society-container")
            application.initialProperties.insert("initialContainerPath", QString::fromLocal8Bit(argv[++index]));
    }

    return lvrs::runBootstrappedQmlApp(argc, argv, application);
}
