#include "NativeHomeComposerSession.h"
#include "App/Generation/AdvancedImageParameters.h"
#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/GenerationController.h"
#include <QThread>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
NativeHomeComposerSession::NativeHomeComposerSession(GenerationComposerModel *composer, HomeComposerChoices choices,
        std::string randomSeedLabel, Callbacks callbacks, QObject *parent)
    : QObject(parent), model_(HomeComposerModel::create()), composer_(composer), choices_(std::move(choices)),
      randomSeedLabel_(std::move(randomSeedLabel)), callbacks_(std::move(callbacks)) {
    if (!composer || composer->thread() != QThread::currentThread())
        throw std::invalid_argument("Native Home composer requires a controller on its owner thread");
    const QPointer<NativeHomeComposerSession> guard(this);
    auto attached = model_->setHandler([guard](const HomeComposerCommand &value) -> ii::core::Result<void> {
        if (!guard) return ii::core::fail(ii::core::ErrorCode::closed, "Home composer session was retired");
        return guard->command(value);
    });
    if (!attached) throw std::runtime_error(attached.error().message);
    QObject::connect(composer, &GenerationComposerModel::snapshotChanged, this, [this](const QVariantMap &) { refresh(); });
    QObject::connect(composer, &GenerationComposerModel::modelsChanged, this, [this] { modelsDirty_ = true; refresh(); });
    QObject::connect(composer, &GenerationComposerModel::bindingsChanged, this, [this] { bindParameterStore(); refresh(); });
    QObject::connect(composer, &QObject::destroyed, this, [this] {
        composer_ = nullptr; QObject::disconnect(parameterChanged_); QObject::disconnect(parameterDestroyed_);
        QObject::disconnect(generationDestroyed_); parameterStore_ = nullptr; generation_ = nullptr;
        (void)model_->setHandler({});
    });
    bindParameterStore();
    auto initial = synchronize();
    if (!initial) throw std::invalid_argument(initial.error().message);
}
NativeHomeComposerSession::~NativeHomeComposerSession() {
    callbacks_ = {}; QObject::disconnect(parameterChanged_); QObject::disconnect(parameterDestroyed_); QObject::disconnect(generationDestroyed_);
    if (model_ && model_->active()) (void)model_->setHandler({});
}
bool NativeHomeComposerSession::isFor(const GenerationComposerModel *value) const { return composer_.data() == value; }
void NativeHomeComposerSession::bindParameterStore() {
    auto *store = composer_ ? qobject_cast<AdvancedImageParameters *>(composer_->parameterStore()) : nullptr;
    if (store != parameterStore_) {
        QObject::disconnect(parameterChanged_); QObject::disconnect(parameterDestroyed_); parameterStore_ = store;
        presetsDirty_ = true;
        if (store) {
            parameterChanged_ = QObject::connect(store, &AdvancedImageParameters::presetsChanged, this, [this] { presetsDirty_ = true; refresh(); });
            parameterDestroyed_ = QObject::connect(store, &QObject::destroyed, this, [this] { parameterStore_ = nullptr; presetsDirty_ = true; refresh(); });
        }
    }
    auto *generation = composer_ ? composer_->generation() : nullptr;
    if (generation != generation_) {
        QObject::disconnect(generationDestroyed_); generation_ = generation;
        modelsDirty_ = true;
        if (generation) generationDestroyed_ = QObject::connect(generation, &QObject::destroyed, this, [this] { generation_ = nullptr; modelsDirty_ = true; refresh(); });
    }
}
void NativeHomeComposerSession::refresh() {
    const QPointer<NativeHomeComposerSession> guard(this);
    auto result = synchronize();
    if (guard && !result && callbacks_.failure) { auto report = callbacks_.failure; report(result.error()); }
}
ii::core::Result<void> NativeHomeComposerSession::setChoices(HomeComposerChoices choices) {
    if (thread() != QThread::currentThread()) return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Composer session requires its owner thread");
    auto previous = std::move(choices_); choices_ = std::move(choices);
    const QPointer<NativeHomeComposerSession> guard(this);
    auto result = synchronize();
    if (!result && guard) choices_ = std::move(previous);
    return result;
}
ii::core::Result<void> NativeHomeComposerSession::setAttachments(std::vector<HomeComposerAttachment> attachments) {
    return setInputState(std::move(attachments), inputError_);
}
ii::core::Result<void> NativeHomeComposerSession::setInputState(std::vector<HomeComposerAttachment> attachments, std::string error) {
    if (thread() != QThread::currentThread()) return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Composer session requires its owner thread");
    auto previous = std::move(attachments_); auto previousError = std::move(inputError_);
    attachments_ = std::move(attachments); inputError_ = std::move(error);
    const QPointer<NativeHomeComposerSession> guard(this);
    auto result = synchronize();
    if (!result && guard) { attachments_ = std::move(previous); inputError_ = std::move(previousError); }
    return result;
}
ii::core::Result<void> NativeHomeComposerSession::synchronize() {
    using ii::core::ErrorCode;
    if (thread() != QThread::currentThread()) return ii::core::fail(ErrorCode::wrong_executor, "Composer session requires its owner thread");
    if (!composer_) return ii::core::fail(ErrorCode::closed, "Generation composer controller is unavailable");
    try {
        auto state = HomeComposerSnapshot{};
        state.prompt = composer_->prompt().toStdString(); state.negativePrompt = composer_->negativePrompt().toStdString();
        state.mediaType = composer_->mediaType().toStdString(); state.modelId = composer_->selectedModel().toStdString();
        state.aspectRatio = composer_->aspectRatio().toStdString();
        state.outputCount = composer_->generationCount(); state.videoDuration = composer_->videoDuration(); state.videoFps = composer_->videoFps();
        state.shortSide = composer_->shortSide(); state.seed = composer_->seed(); state.video = composer_->video();
        state.submitting = composer_->submitting(); state.generationPending = composer_->generationPending();
        state.presetName = composer_->selectedPresetName().toStdString();
        state.errorText = composer_->errorText().isEmpty() ? inputError_ : composer_->errorText().toStdString();
        const auto size = composer_->outputSize(); state.width = size.value("width").toInt(); state.height = size.value("height").toInt();
        state.resolutionLabel = QString::number(state.width).toStdString() + " × " + QString::number(state.height).toStdString();
        state.seedLabel = state.seed < 0 ? randomSeedLabel_ : QString::number(state.seed, 'f', 0).toStdString();
        if (modelsDirty_) {
            std::vector<HomeComposerChoice> models;
            for (const auto &entry : composer_->modelChoices()) {
                const auto item = entry.toMap(); const auto id = item.value("id").toString(); auto name = item.value("name").toString();
                if (name.isEmpty()) name = id;
                models.push_back({id.toStdString(), name.toStdString()});
            }
            modelChoices_ = std::move(models); modelsDirty_ = false;
        }
        if (presetsDirty_) {
            std::vector<HomeComposerChoice> presets;
            if (parameterStore_) for (const auto &name : parameterStore_->presets())
                presets.push_back({name.toStdString(), name.toStdString()});
            presetChoices_ = std::move(presets); presetsDirty_ = false;
        }
        state.choices = choices_; state.choices.models = modelChoices_; state.choices.presets = presetChoices_;
        state.modelLabel = state.modelId;
        if (const auto found = std::ranges::find_if(modelChoices_, [&](const auto &choice) { return choice.key == state.modelId; });
            found != modelChoices_.end()) state.modelLabel = found->label;
        state.attachments = attachments_;
        auto owner = model_;
        return owner->setSnapshot(std::move(state));
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot project the Home generation controller", std::current_exception()); }
}
ii::core::Result<void> NativeHomeComposerSession::command(const HomeComposerCommand &cmd) {
    using ii::core::ErrorCode; using Kind = HomeComposerCommandKind;
    if (thread() != QThread::currentThread()) return ii::core::fail(ErrorCode::wrong_executor, "Composer session requires its owner thread");
    if (!composer_) return ii::core::fail(ErrorCode::closed, "Generation composer controller is unavailable");
    const auto text = QString::fromStdString(cmd.value);
    const QPointer<GenerationComposerModel> controller(composer_);
    auto number = [&]() -> std::optional<int> { bool ok = false; const int result = text.toInt(&ok); return ok ? std::optional(result) : std::nullopt; };
    switch (cmd.kind) {
    case Kind::editPrompt: composer_->setPrompt(text); return {};
    case Kind::editNegativePrompt: composer_->setNegativePrompt(text); return {};
    case Kind::chooseMedia: composer_->chooseMedia(text); return {};
    case Kind::chooseModel: {
        auto *generation = qobject_cast<GenerationController *>(composer_->generation());
        if (!generation) return ii::core::fail(ErrorCode::closed, "Generation controller is unavailable");
        if (composer_->video()) generation->setSelectedVideoModel(text); else generation->setSelectedModel(text);
        return {};
    }
    case Kind::chooseRatio: composer_->setAspectRatio(text); return {};
    case Kind::chooseResolution: if (auto value = number()) { composer_->setShortSide(*value); return {}; } break;
    case Kind::chooseCount: if (auto value = number()) { composer_->setGenerationCount(*value); return {}; } break;
    case Kind::chooseDuration: if (auto value = number()) { composer_->setVideoDuration(*value); return {}; } break;
    case Kind::chooseFrameRate: if (auto value = number()) { composer_->setVideoFps(*value); return {}; } break;
    case Kind::applySeed: if (composer_->applySeedText(text)) return {}; break;
    case Kind::randomSeed: controller->setSeed(-1); if (controller) controller->setErrorText({}); return {};
    case Kind::loadPreset: if (composer_->loadPreset(text)) return {}; break;
    case Kind::savePreset: if (composer_->savePreset(text)) return {}; break;
    case Kind::chooseAttachments:
        if (callbacks_.chooseAttachments) { auto call = callbacks_.chooseAttachments; return call(); }
        return ii::core::fail(ErrorCode::unsupported, "Attachment selection is unavailable");
    case Kind::removeAttachment:
        if (callbacks_.removeAttachment) { auto call = callbacks_.removeAttachment; return call(cmd.value); }
        return ii::core::fail(ErrorCode::unsupported, "Attachment removal is unavailable");
    case Kind::paint:
        if (callbacks_.paint) { auto call = callbacks_.paint; return call(); }
        return ii::core::fail(ErrorCode::unsupported, "Paint canvas is unavailable");
    case Kind::advanced:
        if (callbacks_.advanced) { auto call = callbacks_.advanced; auto params = composer_->parameters(composer_->selectedModel(), true); return call(std::move(params)); }
        return ii::core::fail(ErrorCode::unsupported, "Advanced settings are unavailable");
    case Kind::submit: composer_->submit(); return {};
    }
    return ii::core::fail(ErrorCode::invalid_argument, "Home composer action could not be completed");
}
} // namespace dreamscapes::home
