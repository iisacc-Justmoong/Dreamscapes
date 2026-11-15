#include "NativeHomeQueueSession.h"
#include "NativeHomeQueueBridge.h"
#include "App/ViewModels/GenerationProjection.h"
#include "App/ViewModels/GenerationProjectionBinding.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <QObject>
#include <QPointer>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
NativeHomeQueueSession::NativeHomeQueueSession(WorkspaceSession *workspace, HomeQueueStrings strings,
                                               std::function<void()> openQueue)
    : model_(std::make_shared<HomeQueueModel>(std::move(strings))),
      projection_(std::make_unique<GenerationProjection>()) {
    if (!workspace) throw std::invalid_argument("Native Home queue requires a workspace session");
    source_ = std::make_unique<GenerationProjectionBinding>(workspace, projection_.get());
    const QPointer<WorkspaceSession> guard(workspace);
    std::function<void()> action;
    if (openQueue) action = [guard, openQueue = std::move(openQueue)] { if (guard) openQueue(); };
    bridge_ = std::make_unique<NativeHomeQueueBridge>(projection_.get(), model_, std::move(action));
    workspaceDestroyed_ = QObject::connect(workspace, &QObject::destroyed, [this] {
        if (bridge_) bridge_->detach();
    });
}
NativeHomeQueueSession::~NativeHomeQueueSession() {
    QObject::disconnect(workspaceDestroyed_);
    bridge_.reset();
    source_.reset();
    projection_.reset();
}
void NativeHomeQueueSession::setRuntimeNames(QString model, QString device) {
    if (bridge_) bridge_->setRuntimeNames(std::move(model), std::move(device));
}
} // namespace dreamscapes::home
