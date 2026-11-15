#include "App/Views/Result/NativeReviewQueueBridge.h"
#include "App/Views/Result/NativeReviewQueueView.h"
#include "App/ViewModels/GenerationProjection.h"
#include "iiGUI/WidgetState.h"
#include <QCoreApplication>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
template<class T> T take(ii::core::Result<T> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
    return std::move(*value);
}
ii::core::Result<ii::gui::Widget> findScoped(const std::shared_ptr<ii::gui::detail::WidgetState> &state,
                                           const std::string &id) {
    const auto nodes = state->snapshots();
    if (!nodes) return std::unexpected(nodes.error());
    for (const auto &node : *nodes)
        if (node.id == id || node.id.ends_with("/" + id)) return state->find(node.id);
    return ii::core::fail(ii::core::ErrorCode::not_found, "No mounted widget named " + id);
}
QVariantMap job(QString id, QString state) {
    return {{"id", std::move(id)}, {"state", std::move(state)}, {"mediaType", "Image"}, {"modelName", "Model"}};
}
void exercise() {
    auto source = std::make_unique<dreamscapes::GenerationProjection>();
    auto model = dreamscapes::review::QueueModel::create(std::make_shared<dreamscapes::review::Session>());
    QString canceled, inspected;
    dreamscapes::review::NativeReviewQueueBridge bridge(source.get(), model,
        {.cancel = [&](const auto &id) { canceled = id; }, .failure = [&](const auto &id) { inspected = id; }});
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {260, 600}));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return dreamscapes::review::buildReviewQueue(builder, {}, model);
    }));
    source->setJobs({job("live", "running")});
    source->setInferenceStatus({{"state", "loading"}, {"step", 2}, {"total", 5}});
    (void)take(state->drawing_snapshot({260, 600}));
    check(model->items.get().size() == 1 && model->items.get()[0]->step.get() == 2 &&
          bool(findScoped(state, "jobProgress")), "Live generation data did not reach the native queue");
    good(take(findScoped(state, "cancelJob")).activate());
    check(canceled == "live", "The native button sent the wrong cancellation target");
    source->setJobs({job("live", "running"), job("live", "failed")});
    (void)take(state->drawing_snapshot({260, 600}));
    canceled.clear();
    check(!bridge.lastError().isEmpty() && !findScoped(state, "cancelJob") &&
          !take(take(findScoped(state, "allJobs")).snapshot()).enabled,
          "A rejected source left a stale native action visible");
    source->setJobs({job("live", "failed")});
    (void)take(state->drawing_snapshot({260, 600}));
    good(take(findScoped(state, "failureDetails")).activate());
    check(inspected == "live" && bridge.lastError().isEmpty() &&
          take(take(findScoped(state, "allJobs")).snapshot()).enabled,
          "The recovered failure action targeted the wrong job or left selection disabled");
    source.reset();
    (void)take(state->drawing_snapshot({260, 600}));
    check(!findScoped(state, "failureDetails"), "A destroyed generation source left a native action visible");
    state->close();
}
}
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "The native queue follows the generation projection through its mounted widgets\n";
}
