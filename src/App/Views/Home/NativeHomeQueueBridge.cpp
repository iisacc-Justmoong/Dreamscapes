#include "NativeHomeQueueBridge.h"
#include "App/ViewModels/GenerationProjection.h"
#include "App/ViewModels/ViewData.h"
#include <QThread>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
NativeHomeQueueBridge::NativeHomeQueueBridge(GenerationProjection *projection, std::shared_ptr<HomeQueueModel> model,
                                             std::function<void()> openQueue, QObject *parent)
    : QObject(parent), projection_(projection), model_(model), openQueue_(std::move(openQueue)) {
    if (!projection || !model) throw std::invalid_argument("Home queue requires a generation projection and model");
    if (projection->thread() != QThread::currentThread() || thread() != QThread::currentThread())
        throw std::logic_error("Home queue bridge requires the owner thread");
    attached_ = true;
    const QPointer<NativeHomeQueueBridge> weak(this);
    try {
        synchronize();
        if (openQueue_) model->setOpenQueueHandler([weak] {
            if (weak && weak->attached_ && weak->openQueue_) {
                const auto action = weak->openQueue_;
                action();
            }
        });
        connect(projection, &GenerationProjection::jobsChanged, this, [this] { synchronize(); });
        connect(projection, &GenerationProjection::generationStatusTextChanged, this, [this] { synchronize(); });
        connect(projection, &QObject::destroyed, this, [this] { detach(); });
    } catch (...) { try { detach(); } catch (...) {} throw; }
}
NativeHomeQueueBridge::~NativeHomeQueueBridge() { try { detach(); } catch (...) {} }
void NativeHomeQueueBridge::setRuntimeNames(QString model, QString device) {
    if (thread() != QThread::currentThread()) throw std::logic_error("Home queue bridge requires the owner thread");
    modelName_ = std::move(model); deviceName_ = std::move(device); synchronize();
}
void NativeHomeQueueBridge::synchronize() {
    const auto target = model_.lock();
    if (!attached_ || !projection_ || !target) return;
    const HomeQueueSnapshot snapshot{.jobs = nativeValue(projection_->jobs()),
        .statusText = projection_->generationStatusText().toStdString(),
        .modelName = modelName_.toStdString(), .deviceName = deviceName_.toStdString()};
    if (const auto result = target->setSnapshot(snapshot); !result)
        throw std::runtime_error(result.error().message);
}
void NativeHomeQueueBridge::detach() {
    if (!attached_) return;
    attached_ = false;
    if (projection_) disconnect(projection_, nullptr, this, nullptr);
    projection_.clear(); openQueue_ = {};
    if (auto target = model_.lock()) target->setOpenQueueHandler({});
    model_.reset();
}
} // namespace dreamscapes::home
