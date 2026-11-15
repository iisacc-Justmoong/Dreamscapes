#include "App/Views/Home/NativeHomeQueueBridge.h"
#include "App/ViewModels/GenerationProjection.h"
#include "App/ViewModels/ViewData.h"
#include <QCoreApplication>
#include <QUrl>
#include <QVariantMap>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void exercise() {
    using namespace dreamscapes;
    int formats = 0;
    home::HomeQueueStrings strings;
    strings.counts = [&formats](std::size_t running, std::size_t waiting, std::size_t failed) {
        ++formats;
        return std::to_string(running) + " running · " + std::to_string(waiting) + " waiting · " + std::to_string(failed) + " failed";
    };
    auto model = std::make_shared<home::HomeQueueModel>(std::move(strings));
    auto projection = std::make_unique<GenerationProjection>();
    projection->setJobs({QVariantMap{{"id", "a"}, {"state", "queued"}}});
    int opened = 0;
    auto bridge = std::make_unique<home::NativeHomeQueueBridge>(projection.get(), model, [&] { ++opened; });
    check(model->summary.get() == "0 running · 1 waiting · 0 failed", "initial jobs were not projected");
    bridge->setRuntimeNames("LTX 2.3", "Metal");
    check(model->model.get() == "LTX 2.3" && model->device.get() == "Metal", "runtime names lost");
    projection->setJobs({QVariantMap{{"id", "a"}, {"state", "running"}},
                         QVariantMap{{"id", "b"}, {"state", "failed"}}});
    check(model->summary.get() == "1 running · 0 waiting · 1 failed", "job changes were not projected");
    ViewData legacy;
    const QVariantList mixed{QVariantMap{{"state", "connecting-host"}}, QVariantMap{{"state", "downloading"}},
        QVariantMap{{"state", "running"}}, QVariantMap{{"state", "queued"}}, QVariantMap{{"state", "failed"}},
        QVariantMap{{"state", "completed"}}, QVariantMap{{"state", 17}}, QVariantMap{}};
    projection->setJobs(mixed);
    const auto running = legacy.facts()->stateCount(mixed, {"running", "connecting-host", "downloading"});
    const auto waiting = legacy.facts()->stateCount(mixed, {"queued"});
    const auto failed = legacy.facts()->stateCount(mixed, {"failed"});
    const auto expected = QStringLiteral("%1 running · %2 waiting · %3 failed")
        .arg(running).arg(waiting).arg(failed).toStdString();
    check(model->summary.get() == expected, "native queue differs from current Home state counts");
    formats = 0;
    projection->applySourceInputs({{"previewImage", QUrl("file:///first.png")}, {"previewJobId", "a"}});
    projection->applySourceInputs({{"previewImage", QUrl("file:///second.png")}, {"previewJobId", "a"}});
    check(formats == 0, "preview images reprojected the Home queue without changing its state");
    model->openQueue(); check(opened == 1, "open action not delivered");
    projection.reset();
    check(!model->canOpenQueue.get(), "destroyed projection left an active action");
    model->openQueue(); check(opened == 1, "retired action was delivered");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native Home queue follows generation projection and safely detaches\n";
}
