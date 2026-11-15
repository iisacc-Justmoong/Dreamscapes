#include "App/Views/Home/NativeHomeComposerChoiceView.h"
#include "App/Views/Home/NativeHomeComposerSession.h"
#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/GenerationController.h"
#include "iiGUI/WidgetState.h"
#include <iiSDKCore/Runtime/FrameLoop.h>
#include <SharedStorage.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
}
template<class T> T take(ii::core::Result<T> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
    return std::move(*value);
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state, std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id == id || node.id.ends_with(std::string("/") + std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing choice widget " + std::string(id));
}
ii::gui::Widget entry(const std::shared_ptr<ii::gui::detail::WidgetState> &state, const std::string &label) {
    for (const auto &node : take(state->snapshots()))
        if (node.id.ends_with("/homeChoiceEntry") && node.text == label) return take(state->find(node.id));
    throw std::runtime_error("Missing model menu entry " + label);
}
void exercise() {
    using namespace ii::gui;
    QTemporaryDir root(QDir::tempPath() + "/dreamscapes-choice-XXXXXX.societycontainer");
    check(root.isValid() && iiSocietyContainer::SocietyDrive::create(root.path()).has_value(), "isolated Society fixture unavailable");
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.ini").toUtf8());
    qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
    for (const auto &name : {"one.safetensors", "two.safetensors"}) {
        QFile file(root.filePath(QString("Models/") + name));
        check(file.open(QIODevice::WriteOnly) && file.write("fixture") == 7, "model fixture unavailable");
    }
    GenerationRuntime runtime; runtime.modelPreferencesFile = root.filePath("models.conf"); runtime.failureReportsDirectory = root.path();
    runtime.inspectImageModel = [](const auto &) { return iiLocalDiffusion::ImageModelInspection{}; };
    auto generation = std::make_unique<GenerationController>(runtime); generation->setForeground(false);
    check(generation->connectStorage(root.path()), "fixture storage connection failed");
    GenerationComposerModel draft; draft.setGeneration(generation.get());
    HomeComposerChoices options; options.media = {{"Image", "Image"}, {"Video", "Video"}};
    NativeHomeComposerSession session(&draft, options, "Random", {});
    auto composer = session.model();
    auto model = HomeComposerChoiceModel::create(composer, HomeComposerChoiceRole::model, "Model", "Choose model");
    check(model->choices.get().size() == 2, "real controller model list did not reach the native choice model");
    const auto firstId = composer->snapshot.get().modelId;
    HomeComposerChoiceRow other;
    for (const auto &choice : model->choices.get()) if (choice.key != firstId) other = choice;
    check(!other.key.empty(), "fixture needs two distinct models");
    ii::runtime::EventLoop events; detail::WidgetFrameHooks hooks;
    hooks.events = [&]() -> ii::core::Result<ii::runtime::EventLoop *> { return &events; };
    auto state = std::make_shared<detail::WidgetState>([](ii::runtime::Task task) { task(); }, WidgetAppearance{}, hooks);
    auto host = take(state->content_host({}, "root", {320,60}));
    good(host.set_content([&](WidgetBuilder &builder) { return buildHomeComposerChoice(builder, {}, model); }));
    (void)take(state->drawing_snapshot({320,60}));
    good(find(state,"homeChoiceButton").activate()); good(entry(state,other.label).activate());
    check(generation->selectedModel().toStdString() == other.key && composer->snapshot.get().modelId == other.key &&
          generation->jobs().isEmpty(), "LVUI model selection did not reach the controller or started a job");
    check(generation->setModelModalities(QString::fromStdString(other.key), {"video"}), "fixture reclassification failed");
    good(composer->dispatch(HomeComposerCommandKind::chooseMedia, "Video"));
    check(model->choices.get().size() == 1 && model->choices.get().front().key == other.key &&
          take(find(state,"homeChoiceButton").snapshot()).text == other.label,
          "native menu did not follow the controller's video inventory");
    state->close(); model->close();
}
}
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
