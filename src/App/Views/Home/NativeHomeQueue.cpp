#include "NativeHomeQueue.h"
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
HomeQueueModel::HomeQueueModel(HomeQueueStrings strings) : strings_(std::move(strings)) {
    if (!strings_.counts) strings_.counts = [](std::size_t running, std::size_t waiting, std::size_t failed) {
        return std::to_string(running) + " running · " + std::to_string(waiting) +
               " waiting · " + std::to_string(failed) + " failed";
    };
    heading.set(strings_.heading); openLabel.set(strings_.openQueue); currentSession.set(strings_.currentSession);
    deviceLabel.set(strings_.device); modelLabel.set(strings_.model); executionLabel.set(strings_.execution);
    detail.set(strings_.detail);
    const auto initialized = setSnapshot({});
    if (!initialized) throw std::runtime_error(initialized.error().message);
}

ii::core::Result<void> HomeQueueModel::setSnapshot(const HomeQueueSnapshot &snapshot) {
    if (owner_ != std::this_thread::get_id())
        return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Home queue requires its owner thread");
    if (updating_) return ii::core::fail(ii::core::ErrorCode::busy, "Home queue update is already in progress");
    if (snapshot.jobs.kind() != ii::core::ValueKind::array)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home queue jobs must be an array");
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_};
    updating_ = true;
    std::size_t running = 0, waiting = 0, failed = 0;
    for (const auto &job : vm::array(snapshot.jobs)) {
        const auto state = vm::text(vm::get(job, "state"));
        if (state == "running" || state == "connecting-host" || state == "downloading") ++running;
        else if (state == "queued") ++waiting;
        else if (state == "failed") ++failed;
    }
    const auto nextStatus = !snapshot.statusText.empty() ? snapshot.statusText
        : running ? strings_.generating : waiting ? strings_.queued : strings_.ready;
    const auto nextSummary = strings_.counts(running, waiting, failed);
    status.set(nextStatus); summary.set(nextSummary); device.set(snapshot.deviceName);
    model.set(snapshot.modelName.empty() ? strings_.unavailable : snapshot.modelName);
    execution.set(running ? strings_.running : strings_.idle);
    return {};
}
void HomeQueueModel::requireOwner() const {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Home queue requires its owner thread");
}
void HomeQueueModel::setOpenQueueHandler(std::function<void()> handler) {
    requireOwner(); openQueue_ = std::move(handler); canOpenQueue.set(bool(openQueue_));
}
void HomeQueueModel::openQueue() {
    requireOwner(); const auto handler = openQueue_; if (handler) handler();
}
} // namespace dreamscapes::home

namespace {
std::shared_ptr<dreamscapes::home::HomeQueueModel> requireModel(
    std::shared_ptr<dreamscapes::home::HomeQueueModel> model) {
    if (!model) throw std::invalid_argument("Home queue requires a model");
    return model;
}
}
NativeHomeQueueBackend::NativeHomeQueueBackend()
    : NativeHomeQueueBackend(std::make_shared<dreamscapes::home::HomeQueueModel>()) {}
NativeHomeQueueBackend::NativeHomeQueueBackend(std::shared_ptr<dreamscapes::home::HomeQueueModel> source)
    : model(requireModel(std::move(source))), heading(model->heading), openLabel(model->openLabel), currentSession(model->currentSession),
      deviceLabel(model->deviceLabel), modelLabel(model->modelLabel), executionLabel(model->executionLabel),
      status(model->status), summary(model->summary), device(model->device), modelName(model->model),
      execution(model->execution), detail(model->detail), canOpenQueue(model->canOpenQueue) {}
