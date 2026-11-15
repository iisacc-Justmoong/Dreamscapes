#include "App/StorageViewTypes.h"
#include "App/Views/Home/CanvasPreview.h"
#include "App/ApplicationLifetime.h"
#include "App/ApplicationShutdownCoordinator.h"
#include <backend/runtime/appentry.h>
#include "App/Generation/GenerationController.h"
#ifdef DREAMSCAPES_NATIVE_REVIEW_QUEUE_PRESENTER
#include "App/Views/Result/NativeReviewQueuePresenterMac.h"
#endif
#ifdef DREAMSCAPES_NATIVE_REVIEW_INSPECTOR_PRESENTER
#include "App/Views/Result/NativeReviewInspectorPresenterMac.h"
#endif
#ifdef DREAMSCAPES_NATIVE_REVIEW_GRID_PRESENTER
#include "App/Views/Result/NativeReviewGridPresenterMac.h"
#endif
#ifdef DREAMSCAPES_NATIVE_HOME_QUEUE_PRESENTER
#include "App/Views/Home/NativeHomeQueuePresenterMac.h"
#endif
#ifdef DREAMSCAPES_NATIVE_HOME_SELECTED_FILE_PRESENTER
#include "App/Views/Home/NativeHomeSelectedFilePresenterMac.h"
#endif
#ifdef DREAMSCAPES_NATIVE_HOME_RECENT_FILES_PRESENTER
#include "App/Views/Home/NativeHomeRecentFilesPresenterMac.h"
#endif
#ifdef DREAMSCAPES_NATIVE_HOME_RECENT_GENERATIONS_PRESENTER
#include "App/Views/Home/NativeHomeRecentGenerationsPresenterMac.h"
#endif
#ifdef DREAMSCAPES_NATIVE_LICENSE_PRESENTER
#include "App/Native/NativeLicensePresenter.h"
#endif
#include "App/Generation/AdvancedImageParameters.h"
#include <QtQml/qqml.h>
#include <iiSocietyHelper.h>
#include <QGuiApplication>
#include <QIcon>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QThreadPool>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#ifdef DREAMSCAPES_NATIVE_FAILURE_PRESENTER
#include "App/Generation/NativeFailurePresenter.h"
#endif
#ifdef DREAMSCAPES_WITH_LOCAL_MCP
#include "App/Mcp/DreamscapesMcp.h"
#endif
#ifdef DREAMSCAPES_LOCAL_RUNTIME_PROBE
void dreamscapesLocalRuntimeProbe(QObject *root);
#endif

int main(int argc, char *argv[])
{
    lvrs::QmlAppLaunchSpec application;
    application.bootstrap.applicationName = QStringLiteral("Dreamscapes");
    application.bootstrap.quickStyleName = QStringLiteral("Basic");
    application.moduleUri = QStringLiteral("Dreamscapes");
    application.rootObject = QStringLiteral("Main");
    application.configureEngine = [](QQmlApplicationEngine &engine) {
        registerStorageViewTypes();
        dreamscapes::installCanvasPreviewProvider(engine);
        configureApplicationLifetime(engine);
        auto *shutdown = new ApplicationShutdownCoordinator(&engine);
#ifdef DREAMSCAPES_NATIVE_HOME_QUEUE_PRESENTER
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject *root, const QUrl &) {
                if (root) (void)dreamscapes::home::installNativeHomeQueuePresenter(root, &engine, shutdown);
            });
#endif
#ifdef DREAMSCAPES_NATIVE_HOME_SELECTED_FILE_PRESENTER
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject *root, const QUrl &) {
                if (root) (void)dreamscapes::home::installNativeHomeSelectedFilePresenter(root, &engine, shutdown);
            });
#endif
#ifdef DREAMSCAPES_NATIVE_HOME_RECENT_FILES_PRESENTER
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject *root, const QUrl &) {
                if (root) (void)dreamscapes::home::installNativeHomeRecentFilesPresenter(root, &engine, shutdown);
            });
#endif
#ifdef DREAMSCAPES_NATIVE_HOME_RECENT_GENERATIONS_PRESENTER
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject *root, const QUrl &) {
                if (root) (void)dreamscapes::home::installNativeHomeRecentGenerationsPresenter(root, &engine, shutdown);
            });
#endif
#ifdef DREAMSCAPES_NATIVE_REVIEW_QUEUE_PRESENTER
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject *root, const QUrl &) {
                if (root) (void)dreamscapes::review::installNativeReviewQueuePresenter(root, &engine, shutdown);
            });
#endif
#ifdef DREAMSCAPES_NATIVE_REVIEW_INSPECTOR_PRESENTER
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject *root, const QUrl &) {
                if (root) (void)dreamscapes::review::installNativeReviewInspectorPresenter(root, &engine, shutdown);
            });
#endif
#ifdef DREAMSCAPES_NATIVE_REVIEW_GRID_PRESENTER
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject *root, const QUrl &) {
                if (root) (void)dreamscapes::review::installNativeReviewGridPresenter(root, &engine, shutdown);
            });
#endif
#ifdef DREAMSCAPES_NATIVE_LICENSE_PRESENTER
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject* root, const QUrl&) {
                if (!root) return;
                auto* generation = root->findChild<GenerationController*>(QStringLiteral("generationController"));
                if (generation)
                    (void)dreamscapes::native::installNativeLicensePresenter(root, generation->account(), &engine, shutdown);
            });
#endif
#ifdef DREAMSCAPES_WITH_LOCAL_MCP
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [&engine, shutdown](QObject* root, const QUrl&) { if (root) installDreamscapesMcp(root, &engine, shutdown); });
#endif
        QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/Dreamscapes.png")));
        QGuiApplication::setDesktopFileName(QStringLiteral("com.iisacc.dreamscapes"));
#ifdef DREAMSCAPES_LOCAL_RUNTIME_PROBE
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, &engine,
            [](QObject *root, const QUrl &) { if (root) dreamscapesLocalRuntimeProbe(root); });
#endif
        qmlRegisterType<GenerationController>("Dreamscapes.Storage", 1, 0, "GenerationController");
        qmlRegisterType<AdvancedImageParameters>("Dreamscapes.Storage", 1, 0, "AdvancedImageParameters");
        // Presence has no need to discover a second full storage mirror.
        auto *helper = new iiSocietyHelper::Helper(iiSocietyHelper::FileSystem::Initialization::Deferred, &engine);
        helper->setObjectName(QStringLiteral("societyHelper"));
        engine.rootContext()->setContextProperty(QStringLiteral("societyHelper"), helper);
#ifdef DREAMSCAPES_NATIVE_FAILURE_PRESENTER
        auto *failurePresenter = new dreamscapes::failure::NativeFailurePresenter(&engine);
        engine.rootContext()->setContextProperty(QStringLiteral("nativeFailurePresenter"), failurePresenter);
        shutdown->add([failurePresenter](std::function<void()> done) {
            failurePresenter->shutdown(std::move(done));
        });
#endif
        const auto updateActivity = [helper, shutdown](Qt::ApplicationState state) {
            if (shutdown->isClosing()) return;
            if (!helper->isRunning()) {
                if (!helper->isStarting() && !helper->isStopping())
                    (void)helper->startAsync({"com.iisacc.dreamscapes", "Dreamscapes", DREAMSCAPES_APP_VERSION});
                return;
            }
            helper->setActivity(state == Qt::ApplicationActive ? iiSocietyHelper::Activity::Foreground
                                                               : iiSocietyHelper::Activity::Background);
        };
        QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, helper, updateActivity);
        QObject::connect(helper, &iiSocietyHelper::Helper::runningChanged, helper,
            [updateActivity] { updateActivity(QGuiApplication::applicationState()); });
        shutdown->add([helper, shutdown](std::function<void()> done) {
            QObject::connect(helper, &iiSocietyHelper::Helper::stopFinished, shutdown,
                             [done] { done(); }, Qt::SingleShotConnection);
            if (!helper->stopAsync()) { helper->stop(); done(); }
        });
        shutdown->add([&engine, helper, shutdown](std::function<void()> done) {
            GenerationController *generation = nullptr;
            for (auto *root : engine.rootObjects()) {
                if (auto *window = qobject_cast<QQuickWindow *>(root)) window->contentItem()->setEnabled(false);
                if (!generation) generation = root->findChild<GenerationController *>(QStringLiteral("generationController"));
                for (auto *files : root->findChildren<iiSocietyHelper::FileSystem *>()) files->prepareForShutdown();
            }
            helper->fileSystem()->prepareForShutdown();
            if (generation) generation->prepareForShutdown();
            const QPointer<GenerationController> controller(generation);
            const auto drainSdk = [shutdown, done] {
                auto *worker = QThread::create([] { iiSocietyHelper::FileSystem::quiesceBackgroundWork(); });
                QObject::connect(worker, &QThread::finished, shutdown, [done] { done(); });
                QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
                worker->start();
            };
            const auto drainController = [controller, shutdown, drainSdk] {
                if (controller) controller->finishShutdownAsync(shutdown, drainSdk);
                else {
                    auto *worker = QThread::create([] { QThreadPool::globalInstance()->waitForDone(); });
                    QObject::connect(worker, &QThread::finished, shutdown, drainSdk);
                    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
                    worker->start();
                }
            };
#if !defined(Q_OS_IOS) && !defined(Q_OS_ANDROID)
            GenerationProcess::shutdownAsync(shutdown, drainController);
#else
            drainController();
#endif
        });
        updateActivity(QGuiApplication::applicationState());
    };
    for (int index = 1; index + 1 < argc; ++index) {
        if (QString::fromLocal8Bit(argv[index]) == "--society-container")
            application.initialProperties.insert("initialContainerPath", QString::fromLocal8Bit(argv[++index]));
    }

    return lvrs::runBootstrappedQmlApp(argc, argv, application);
}
