#include "App/Views/Result/NativeReviewQueueView.h"
#include "iiGUI/WidgetState.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace dreamscapes;
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
vm::Value job(std::string id, std::string state) {
    return vm::Value(vm::Object{{"id", vm::Value(std::move(id))}, {"state", vm::Value(std::move(state))},
                                {"mediaType", vm::Value("Image")}, {"modelName", vm::Value("Model")}});
}
class Preview final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::string address;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view value, std::uint32_t, std::uint32_t) const override {
        address = value; return {};
    }
};
void exercise() {
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {260, 600}));
    auto session = std::make_shared<review::Session>();
    auto queue = review::QueueModel::create(session);
    auto images = std::make_shared<Preview>();
    std::string canceled, inspected;
    queue->setCancelHandler([&](const auto &id) { canceled = id; });
    queue->setFailureHandler([&](const auto &id) { inspected = id; });
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) { return review::buildReviewQueue(builder, {}, queue, images); }));
    (void)take(state->drawing_snapshot({260, 600}));
    check(bool(findScoped(state, "emptyQueue")), "An empty queue did not display its empty state");
    review::QueueSnapshot snapshot{.jobs = vm::Value(vm::Array{job("one", "running"), job("two", "failed")}),
        .inference = vm::Value(vm::Object{{"state", vm::Value("denoising")}, {"step", vm::Value(3)}, {"total", vm::Value(10)}}),
        .previewJobId = "one", .previewSource = "file:///preview.png"};
    good(queue->setSnapshot(snapshot));
    (void)take(state->drawing_snapshot({260, 600}));
    auto list = take(findScoped(state, "jobs"));
    const auto first = queue->items.get()[0];
    check(images->address == "file:///preview.png", "The active preview did not reach the supplied image resolver");
    auto firstRow = take(list.keyed_content_item(first->token, "jobListener"));
    const auto bounds = take(firstRow.snapshot()).bounds;
    good(state->input({.kind = ii::gui::WidgetInputKind::pointer_down, .x = bounds.x + 2, .y = bounds.y + 2, .buttons = 1}));
    good(state->input({.kind = ii::gui::WidgetInputKind::pointer_up, .x = bounds.x + 2, .y = bounds.y + 2}));
    check(session->jobFilter() == "one", "Selecting a queue row did not filter the shared session");
    good(take(findScoped(state, "cancelJob")).activate());
    good(take(findScoped(state, "failureDetails")).activate());
    check(canceled == "one" && inspected == "two" && session->jobFilter() == "one",
          "A row action reached the wrong job or changed its selection");
    good(take(root.content_item("allJobs")).activate());
    check(session->jobFilter().empty(), "All jobs did not clear the queue filter");
    check(bool(findScoped(state, "jobProgress")), "Active progress was not rendered");
    const auto runningHeight = take(firstRow.snapshot()).bounds.height;
    const auto previousRevision = queue->layoutRevision.get();
    snapshot.jobs = vm::Value(vm::Array{job("one", "failed"), job("two", "failed")});
    good(queue->setSnapshot(snapshot));
    (void)take(state->drawing_snapshot({260, 600}));
    check(queue->layoutRevision.get() != previousRevision && !findScoped(state, "jobProgress") &&
          bool(findScoped(state, "failureDetails")), "A changed row kept stale progress or actions");
    check(take(firstRow.snapshot()).bounds.height < runningHeight,
          "The queue did not remeasure a row after its progress bar disappeared");
    vm::Array many;
    for (int i = 0; i < 1000; ++i) many.push_back(job(std::to_string(i), "completed"));
    snapshot.jobs = vm::Value(std::move(many));
    good(queue->setSnapshot(snapshot));
    (void)take(state->drawing_snapshot({260, 600}));
    check(take(state->snapshots()).size() < 200, "The queue instantiated every offscreen job");
    auto viewport = take(findScoped(state, "jobs/viewport"));
    check(take(viewport.snapshot()).bounds.height > 350,
          "The queue viewport did not expand to use the available panel height");
    good(viewport.set_scroll_offset(3000));
    (void)take(state->drawing_snapshot({260, 600}));
    const auto visibleBounds = take(viewport.snapshot()).bounds;
    std::string anchor;
    std::string visibleRows;
    float anchorY = 0;
    for (const auto &token : take(list.keyed_content_keys())) {
        auto listener = list.keyed_content_item(token, "jobListener");
        if (!listener) continue;
        const auto rowBounds = take(listener->snapshot()).bounds;
        visibleRows += token + ":" + std::to_string(rowBounds.y) + "/" + std::to_string(rowBounds.height) + " ";
        if (rowBounds.y >= visibleBounds.y && rowBounds.y + rowBounds.height <= visibleBounds.y + visibleBounds.height) {
            anchor = token; anchorY = rowBounds.y; break;
        }
    }
    if (anchor.empty()) throw std::runtime_error("No visible row: viewport=" + std::to_string(visibleBounds.y) + "/" +
        std::to_string(visibleBounds.height) + " rows=" + visibleRows);
    auto updated = vm::array(snapshot.jobs);
    updated[900] = job("900", "failed");
    snapshot.jobs = vm::Value(std::move(updated));
    const auto beforeOffscreen = queue->layoutRevision.get();
    good(queue->setSnapshot(snapshot));
    (void)take(state->drawing_snapshot({260, 600}));
    const auto anchored = take(take(list.keyed_content_item(anchor, "jobListener")).snapshot()).bounds;
    check(queue->layoutRevision.get() != beforeOffscreen && std::abs(anchored.y - anchorY) < 0.5f,
          "An offscreen row-height change shifted the visible queue anchor");
    state->close();
}
}
int main() {
    try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native queue widgets project actions, progress and virtual rows\n";
}
