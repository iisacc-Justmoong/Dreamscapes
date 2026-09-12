#include "App/Generation/GenerationController.h"
#include "LocalRuntimeProbeReady.h"
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QImage>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QDateTime>
#if defined(Q_OS_IOS)
#include "App/Generation/GenerationScreenActivity.h"
#endif

// Opt-in device verification of the production controller. This probe never
// pairs, syncs, downloads models, or receives a host address.
void dreamscapesLocalRuntimeProbe(QObject *root)
{
    const auto args = QCoreApplication::arguments();
    const auto option = [&args](const QString &name) {
        const auto index = args.indexOf(name);
        return index >= 0 && index + 1 < args.size() ? args[index + 1] : QString();
    };
    if (!args.contains("--verify-local-generation")) return;
    auto *controller = root->findChild<GenerationController *>("generationController");
    if (!controller) return;
    const auto model = option("--local-model");
    const auto prompt = option("--local-prompt");
    const auto requestedRatio = option("--local-aspect-ratio");
    const auto aspectRatio = requestedRatio.isEmpty() ? QStringLiteral("1:1") : requestedRatio;
    const int cancelAfter = option("--local-cancel-after-ms").toInt();
    const int repeats = qBound(1, option("--local-repeat").toInt(), 3);
    const auto output = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QFile::remove(output + "/local-generation-screen.png");
    QFile::remove(output + "/local-generation-lifecycle.jsonl");
    const auto capturedImage = std::make_shared<QUrl>();
    const auto write = [controller, root, output, capturedImage] {
        const auto property = [root](const char *object, const char *name) {
            auto *item = root->findChild<QObject *>(object);
            return item ? QJsonValue::fromVariant(item->property(name)) : QJsonValue();
        };
        QJsonObject state{{"schema", "dreamscapes-local-verification-v1"},
            {"connected", controller->connected()}, {"runtimeAvailable", controller->runtimeAvailable()},
            {"container", controller->containerPath()}, {"selectedModel", controller->selectedModel()},
            {"error", controller->errorString()}, {"step", controller->previewStep()},
            {"observedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"foreground", controller->foreground()}, {"keepsScreenAwake", controller->keepsScreenAwake()},
            {"backgroundExecution", QJsonObject::fromVariantMap(controller->backgroundExecutionStatus())},
            {"inferenceStatus", QJsonObject::fromVariantMap(controller->inferenceStatus())},
            {"ui", QJsonObject{{"promptHeight", property("promptField", "height")},
                {"imageStatus", property("generatedImage", "status")},
                {"imageSource", property("generatedImage", "source")},
                {"statusText", property("resultStatus", "text")},
                {"resultVisible", root->property("resultVisible").toBool()}}},
            {"steps", controller->previewTotalSteps()}, {"busy", controller->busy()},
            {"jobs", QJsonArray::fromVariantList(controller->jobs())},
            {"result", QJsonObject::fromVariantMap(controller->latestResult())}};
#if defined(Q_OS_IOS)
        state.insert("nativeRuntime", QJsonObject::fromVariantMap(nativeGenerationScreenStatus()));
#endif
        QSaveFile file(output + "/local-generation-verification.json");
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(state).toJson()); file.commit();
        }
        QFile lifecycle(output + "/local-generation-lifecycle.jsonl");
        if (lifecycle.open(QIODevice::WriteOnly | QIODevice::Append))
            lifecycle.write(QJsonDocument(state).toJson(QJsonDocument::Compact) + '\n');
        if (controller->foreground() && !controller->busy() && !controller->latestImage().isEmpty()
            && controller->latestImage() != *capturedImage && dreamscapesProbeImageReady(property("generatedImage", "status"))) {
            if (auto *window = qobject_cast<QQuickWindow *>(root)) {
                if (window->grabWindow().save(output + "/local-generation-screen.png"))
                    *capturedImage = controller->latestImage();
            }
        }
    };
    QObject::connect(controller, &GenerationController::jobsChanged, root, [controller, root, write] {
        write();
        if (!controller->latestImage().isEmpty()) QTimer::singleShot(500, root, write);
    });
    QObject::connect(controller, &GenerationController::previewChanged, root, write);
    QObject::connect(controller, &GenerationController::errorChanged, root, write);
    QObject::connect(controller, &GenerationController::inferenceStatusChanged, root, write);
    QObject::connect(controller, &GenerationController::foregroundChanged, root, write);
    auto *heartbeat = new QTimer(root);
    heartbeat->setInterval(1000);
    QObject::connect(heartbeat, &QTimer::timeout, root, write);
    heartbeat->start();
    QTimer::singleShot(1500, root, [controller, root, model, prompt, aspectRatio, cancelAfter, repeats, write] {
        controller->refreshModels();
        controller->setSelectedModel(model);
        if (!model.isEmpty() && controller->selectedModel() == model && !prompt.isEmpty()) {
            const auto id = controller->enqueue(prompt, aspectRatio);
            if (!id.isEmpty()) {
                root->setProperty("resultVisible", true);
                for (int run = 1; run < repeats; ++run) controller->enqueue(prompt, aspectRatio);
                if (cancelAfter > 0)
                    QTimer::singleShot(cancelAfter, root, [controller, id] { controller->cancel(id); });
            }
        }
        write();
    });
}
