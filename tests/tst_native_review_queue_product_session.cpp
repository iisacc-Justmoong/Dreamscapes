#include "App/Generation/GenerationController.h"
#include "App/ViewModels/WorkspaceSession.h"
#include "App/Views/Result/GenerationReviewModel.h"
#include "App/Views/Result/NativeReviewQueueSession.h"
#include "App/Views/Result/NativeReviewQueueView.h"
#include "iiGUI/WidgetState.h"
#include <SharedStorage.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

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
void exercise() {
    using namespace dreamscapes;
    QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-review-product-XXXXXX.societycontainer"));
    check(root.isValid(), "isolated Society fixture unavailable");
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.ini").toUtf8());
    qunsetenv("SOCIETY_CONTAINER_PATH");
    check(iiSocietyContainer::SocietyDrive::create(root.path()).has_value(), "fixture creation failed");
    QFile weight(root.filePath("Models/example.safetensors"));
    check(weight.open(QIODevice::WriteOnly) && weight.write("test weight") == 11, "fixture weight unavailable");
    weight.close();
    GenerationRuntime runtime;
    runtime.nativeInference = true; runtime.imageExtent = 64; runtime.steps = 1;
    runtime.modelPreferencesFile = root.filePath("models.conf"); runtime.failureReportsDirectory = root.path();
    runtime.inspectImageModel = [](const auto &) { return iiLocalDiffusion::ImageModelInspection{}; };
    auto controller = std::make_unique<GenerationController>(runtime);
    controller->setForeground(false);
    WorkspaceSession workspace;
    workspace.setGeneration(controller.get());
    GenerationReviewModel reviewModel;
    QStringList selected;
    QString canceled;
    auto product = std::make_unique<review::NativeReviewQueueSession>(&workspace, reviewModel.nativeSession(),
        review::NativeReviewQueueActions{
            .cancel = [&](const QString &id) {
                canceled = id;
                if (auto *current = qobject_cast<GenerationController *>(workspace.generation())) (void)current->cancel(id);
            },
            .selection = [&](const QString &id) { selected.append(id); }});
    const auto model = product->model();
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto rootView = take(state->content_host({}, "root", {260, 600}));
    good(rootView.set_content([&](ii::gui::WidgetBuilder &builder) { return review::buildReviewQueue(builder, {}, model); }));
    check(controller->connectStorage(root.path()), "isolated storage connection failed");
    check(!controller->enqueue("fixture", "1:1", 2, 7).isEmpty(), "stopped test queue submission failed");
    (void)take(state->drawing_snapshot({260, 600}));
    check(model->items.get().size() == 2 && model->queued.get() == 2, "controller jobs did not reach native widgets");
    auto list = take(findScoped(state, "jobs"));
    auto first = model->items.get().front();
    const auto bounds = take(take(list.keyed_content_item(first->token, "jobListener")).snapshot()).bounds;
    good(state->input({.kind = ii::gui::WidgetInputKind::pointer_down, .x = bounds.x + 2, .y = bounds.y + 2, .buttons = 1}));
    good(state->input({.kind = ii::gui::WidgetInputKind::pointer_up, .x = bounds.x + 2, .y = bounds.y + 2}));
    check(selected == QStringList{QString::fromStdString(first->id)} && reviewModel.jobFilter() == selected.front(),
          "native queue selection did not reach the actual product review session and host");
    good(take(findScoped(state, "allJobs")).activate());
    check(selected.size() == 2 && selected.back().isEmpty() && reviewModel.jobFilter().isEmpty(),
          "All jobs did not clear the shared product filter");
    good(take(findScoped(state, "cancelJob")).activate());
    check(canceled == QString::fromStdString(first->id) && model->canceled.get() == 1 && !first->canCancel.get(),
          "native cancellation did not reach the stopped product generation queue");
    auto replacement = std::make_unique<GenerationController>(runtime);
    replacement->setForeground(false);
    workspace.setGeneration(replacement.get());
    check(model->items.get().empty() && model->empty.get(), "controller replacement kept stale native jobs");
    const auto beforeRetired = selected.size();
    first->select();
    check(selected.size() == beforeRetired, "a retired job reached the replacement controller's host");
    product.reset();
    const auto before = selected.size();
    first->select();
    check(selected.size() == before && !model->active(), "a retired native row reached the product host");
    state->close();
    bool rejectedThread = false;
    const auto nativeReviewSession = reviewModel.nativeSession();
    std::thread other([&] {
        try { review::NativeReviewQueueSession invalid(&workspace, nativeReviewSession); }
        catch (const std::logic_error &) { rejectedThread = true; }
    });
    other.join();
    check(rejectedThread, "a product queue session was created off its source's owner thread");
    auto retiring = std::make_unique<review::NativeReviewQueueSession>(&workspace, reviewModel.nativeSession());
    auto retiredModel = retiring->model();
    auto subscription = retiredModel->selectionEnabled.observe([&](bool enabled, std::uint64_t) {
        if (!enabled) retiring.reset();
    });
    retiring->close();
    check(!retiring && !retiredModel->active(), "a view retirement callback broke product session teardown");
}
}
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native review widgets use the product generation and review sessions\n";
}
