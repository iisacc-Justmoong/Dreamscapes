#include "App/Views/Home/NativeHomeComposerSession.h"
#include "App/Generation/AdvancedImageParameters.h"
#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/GenerationController.h"
#include "App/Generation/HomeGenerationInputs.h"
#include <SharedStorage.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace dreamscapes::home;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) {
    if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); }
}
class Inputs final : public QObject, public dreamscapes::HomeGenerationInputs {
  public:
    bool painted = false; QVariantList references; QString error;
    QString capturedPrompt, capturedModel, capturedRatio; int capturedCount = 0, requests = 0;
    QUrl added;
    bool hasContent() const override { return painted; }
    QVariantList attachments() const override { return references; }
    QString inputError() const override { return error; }
    bool addAttachment(const QUrl &value) override { added = value; return true; }
    QVariantMap generationParameters(const QString &prompt, const QString &model, const QString &ratio, int count) override {
        ++requests; capturedPrompt = prompt; capturedModel = model; capturedRatio = ratio; capturedCount = count;
        return {{"referenceImages", references}};
    }
};
void exerciseInputs() {
    GenerationComposerModel draft;
    draft.setPrompt("  reference prompt  "); draft.setAspectRatio("16:9"); draft.setGenerationCount(4);
    auto inputs = std::make_unique<Inputs>();
    inputs->references = {QVariantMap{{"source", QUrl("file:///one.png")}}};
    draft.setCanvas(inputs.get());
    check(draft.canvas() == inputs.get() && draft.addAttachment(QUrl("file:///new.png")) && inputs->added == QUrl("file:///new.png"),
          "non-view input provider was not accepted");
    const auto settings = draft.parameters("model-id", true);
    check(inputs->requests == 1 && inputs->capturedPrompt == "reference prompt" && inputs->capturedModel == "model-id" &&
          inputs->capturedRatio == "16:9" && inputs->capturedCount == 4 && settings.value("referenceImages").toList() == inputs->references,
          "non-view input provider lost generation parameters");
    (void)draft.parameters("model-id", false);
    check(inputs->requests == 1, "excluding inputs still read the provider");
    inputs.reset();
    check(!draft.canvas() && !draft.addAttachment(QUrl("file:///stale.png")) &&
          draft.parameters("model-id", true).value("referenceImages").toList().isEmpty(),
          "destroyed input provider remained attached");
    QObject unrelated; draft.setCanvas(&unrelated);
    check(!draft.canvas(), "object without the input interface was accepted");
}
void exercise() {
    QTemporaryDir temp; check(temp.isValid(), "temporary preset directory unavailable");
    AdvancedImageParameters parameters(temp.filePath("presets.json"));
    auto composer = std::make_unique<GenerationComposerModel>();
    composer->setParameterStore(&parameters);
    HomeComposerChoices choices;
    choices.media = {{"Image", "Image"}, {"Video", "Video"}, {"Audio", "Audio"}, {"Board", "Board"}};
    choices.ratios = {{"1:1", "1:1"}, {"16:9", "16:9"}};
    choices.resolutions = {{"512", "512"}, {"1024", "1024"}};
    choices.counts = {{"1", "1"}, {"4", "4"}};
    choices.durations = {{"3", "3 seconds"}}; choices.frameRates = {{"24", "24 FPS"}};
    int generated = 0, focused = 0, attached = 0, painted = 0, advanced = 0;
    QString mediaAction, generatedPrompt; std::string removed;
    QObject::connect(composer.get(), &GenerationComposerModel::generateRequested,
        [&](QString prompt, QString, QString, int) { ++generated; generatedPrompt = std::move(prompt); });
    QObject::connect(composer.get(), &GenerationComposerModel::focusPromptRequested, [&] { ++focused; });
    QObject::connect(composer.get(), &GenerationComposerModel::mediaActionRequested,
        [&](QString action) { mediaAction = std::move(action); });
    NativeHomeComposerSession session(composer.get(), choices, "Random", {
        .chooseAttachments = [&]() -> ii::core::Result<void> { ++attached; return {}; },
        .paint = [&]() -> ii::core::Result<void> { ++painted; return {}; },
        .removeAttachment = [&](std::string key) -> ii::core::Result<void> { removed = std::move(key); return {}; },
        .advanced = [&](QVariantMap values) -> ii::core::Result<void> { ++advanced; check(values.contains("prompt"), "advanced settings lack prompt"); return {}; }
    });
    auto model = session.model();
    check(model->snapshot.get().choices.models.empty() && model->snapshot.get().seedLabel == "Random",
          "session invented a model or lost the random label");
    good(model->dispatch(HomeComposerCommandKind::submit));
    check(focused == 1 && generated == 0, "empty prompt did not use the existing focus path");
    good(model->dispatch(HomeComposerCommandKind::editPrompt, "  장면\n설명  "));
    good(model->dispatch(HomeComposerCommandKind::editNegativePrompt, "blur"));
    good(model->dispatch(HomeComposerCommandKind::chooseRatio, "16:9"));
    good(model->dispatch(HomeComposerCommandKind::chooseResolution, "512"));
    good(model->dispatch(HomeComposerCommandKind::chooseCount, "4"));
    good(model->dispatch(HomeComposerCommandKind::applySeed, "123"));
    check(composer->prompt() == QString::fromUtf8("  장면\n설명  ") && composer->negativePrompt() == "blur" &&
          composer->aspectRatio() == "16:9" && composer->generationCount() == 4 && composer->seed() == 123 &&
          model->snapshot.get().seed == 123 && model->snapshot.get().seedLabel == "123",
          "native commands did not reach the existing draft");
    good(model->dispatch(HomeComposerCommandKind::savePreset, "Studio"));
    check(model->snapshot.get().choices.presets.size() == 1 && model->snapshot.get().presetName == "Studio",
          "saved preset did not reach the native choices");
    good(model->dispatch(HomeComposerCommandKind::editPrompt, "temporary"));
    good(model->dispatch(HomeComposerCommandKind::loadPreset, "Studio"));
    check(composer->prompt() == QString::fromUtf8("장면\n설명"), "preset did not restore the original draft");
    good(model->dispatch(HomeComposerCommandKind::chooseMedia, "Video"));
    good(model->dispatch(HomeComposerCommandKind::chooseDuration, "3"));
    good(model->dispatch(HomeComposerCommandKind::chooseFrameRate, "24"));
    check(model->snapshot.get().video && composer->videoDuration() == 3 && composer->videoFps() == 24,
          "video controls did not update the original controller");
    good(model->dispatch(HomeComposerCommandKind::chooseMedia, "Audio"));
    check(mediaAction == "audio" && composer->mediaType() == "Video", "audio navigation mutated the generation draft");
    good(session.setAttachments({{"ref-a", "reference.png", "file:///reference.png", "1 MB"}}));
    good(model->dispatch(HomeComposerCommandKind::chooseAttachments));
    good(model->dispatch(HomeComposerCommandKind::removeAttachment, "ref-a"));
    good(model->dispatch(HomeComposerCommandKind::paint));
    good(model->dispatch(HomeComposerCommandKind::advanced));
    check(attached == 1 && removed == "ref-a" && painted == 1 && advanced == 1, "host commands were not delivered");
    good(model->dispatch(HomeComposerCommandKind::submit));
    check(generated == 1 && generatedPrompt == QString::fromUtf8("장면\n설명"), "submission bypassed the existing trimmed signal path");
    composer->setGenerationPending(true);
    check(!model->canSubmit.get() && !model->dispatch(HomeComposerCommandKind::submit) && generated == 1,
          "pending generation allowed another submission");
    composer->setGenerationPending(false);
    check(!model->dispatch(HomeComposerCommandKind::applySeed, "not a number") && !composer->errorText().isEmpty(),
          "invalid seed did not use the existing validation");
    good(model->dispatch(HomeComposerCommandKind::randomSeed));
    check(composer->seed() == -1 && composer->errorText().isEmpty(), "random seed did not clear the previous error");
    auto invalidChoices = choices; invalidChoices.ratios.push_back({"1:1", "duplicate"});
    check(!session.setChoices(std::move(invalidChoices)), "duplicate choices were accepted");
    good(session.setAttachments({{"ref-b", "other.png", "", ""}}));
    check(model->snapshot.get().attachments.front().key == "ref-b", "invalid options damaged subsequent synchronization");
    composer.reset();
    check(!model->canSubmit.get() && !model->dispatch(HomeComposerCommandKind::submit), "destroyed controller retained a live command");

    GenerationComposerModel otherComposer;
    std::unique_ptr<NativeHomeComposerSession> retiring;
    retiring = std::make_unique<NativeHomeComposerSession>(&otherComposer, choices, "Random", NativeHomeComposerSession::Callbacks{});
    auto retainedModel = retiring->model();
    auto observer = retainedModel->snapshot.observe([&](const auto &, std::uint64_t) { retiring.reset(); });
    otherComposer.setPrompt("retire while publishing");
    check(!retiring && !retainedModel->canSubmit.get() && !retainedModel->dispatch(HomeComposerCommandKind::submit),
          "session retired during snapshot publication left a live command");

    GenerationComposerModel commandComposer;
    std::unique_ptr<NativeHomeComposerSession> commandSession;
    NativeHomeComposerSession::Callbacks callbacks;
    callbacks.paint = [&]() -> ii::core::Result<void> { commandSession.reset(); return {}; };
    commandSession = std::make_unique<NativeHomeComposerSession>(&commandComposer, choices, "Random", std::move(callbacks));
    auto commandModel = commandSession->model();
    good(commandModel->dispatch(HomeComposerCommandKind::paint));
    check(!commandSession && !commandModel->canSubmit.get() && !commandModel->dispatch(HomeComposerCommandKind::paint),
          "session retired during a command left callbacks active");
}
void exerciseModels() {
    QTemporaryDir root(QDir::tempPath() + "/dreamscapes-composer-XXXXXX.societycontainer");
    check(root.isValid(), "isolated Society fixture unavailable");
    check(iiSocietyContainer::SocietyDrive::create(root.path()).has_value(), "cannot create isolated Society fixture");
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.ini").toUtf8());
    qputenv("SOCIETY_CONTAINER_PATH", root.path().toUtf8());
    for (const auto &name : {"first.safetensors", "second.safetensors"}) {
        QFile file(root.filePath(QString("Models/") + name));
        check(file.open(QIODevice::WriteOnly) && file.write("fixture") == 7, "fixture model unavailable");
    }
    GenerationRuntime runtime; runtime.modelPreferencesFile = root.filePath("models.conf");
    runtime.failureReportsDirectory = root.path();
    runtime.inspectImageModel = [](const auto &) { return iiLocalDiffusion::ImageModelInspection{}; };
    auto generation = std::make_unique<GenerationController>(runtime); generation->setForeground(false);
    check(generation->connectStorage(root.path()), "cannot connect isolated Society fixture");
    GenerationComposerModel draft; draft.setGeneration(generation.get());
    HomeComposerChoices options; options.media = {{"Image", "Image"}, {"Video", "Video"}};
    NativeHomeComposerSession session(&draft, options, "Random", {});
    auto model = session.model(); auto choices = model->snapshot.get().choices.models;
    check(choices.size() == 2 && choices[0].key != choices[1].key, "Society model IDs did not reach the native composer");
    good(model->dispatch(HomeComposerCommandKind::chooseModel, choices[1].key));
    check(generation->selectedModel().toStdString() == choices[1].key && model->snapshot.get().modelId == choices[1].key,
          "native model choice did not reach the real controller");
    check(generation->setModelModalities(QString::fromStdString(choices[1].key), {"video"}), "fixture model classification failed");
    check(model->snapshot.get().choices.models.size() == 1, "image choices did not respond to the modality change");
    good(model->dispatch(HomeComposerCommandKind::chooseMedia, "Video"));
    check(model->snapshot.get().choices.models.size() == 1 && model->snapshot.get().choices.models.front().key == choices[1].key,
          "video choices did not use the real controller's classified models");
    good(model->dispatch(HomeComposerCommandKind::chooseModel, choices[1].key));
    check(generation->selectedVideoModel().toStdString() == choices[1].key, "video model command targeted the wrong selection");
    Inputs videoInputs; videoInputs.references = {QStringLiteral("file:///first.png"), QStringLiteral("file:///second.png")};
    videoInputs.error = "Only one first frame is supported";
    draft.setCanvas(&videoInputs); draft.setPrompt("test video input");
    check(draft.enqueue().isEmpty() && draft.requestError() == videoInputs.error && generation->jobs().isEmpty(),
          "video input validation lost the provider error or started generation");
    draft.setCanvas(nullptr);
    generation.reset();
    check(model->snapshot.get().choices.models.empty() && !model->dispatch(HomeComposerCommandKind::chooseModel, choices[1].key),
          "retired generation controller left stale model choices");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { exerciseInputs(); exercise(); exerciseModels(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
