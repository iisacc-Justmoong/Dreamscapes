#include "GenerationComposerModel.h"
#include "../ViewModels/ViewData.h"
#include "../Views/Home/HomeCanvas.h"
#include "AdvancedImageParameters.h"
#include "GenerationController.h"
#include <QScopedValueRollback>
using namespace dreamscapes;
using namespace dreamscapes::vm;
GenerationComposerModel::GenerationComposerModel(QObject *parent)
    : QObject(parent), state_(viewValue(model_.presentation()).toMap()) {}
QObject *GenerationComposerModel::generation() const {
    return generation_;
}
void GenerationComposerModel::setGeneration(QObject *value) {
    auto *next = qobject_cast<GenerationController *>(value);
    if (generation_ == next)
        return;
    if (generation_)
        disconnect(generation_, nullptr, this, nullptr);
    generation_ = next;
    if (generation_)
        connect(generation_, &GenerationController::modelsChanged, this, &GenerationComposerModel::modelsChanged);
    emit bindingsChanged();
    emit modelsChanged();
}
QObject *GenerationComposerModel::canvas() const {
    return canvas_;
}
void GenerationComposerModel::setCanvas(QObject *value) {
    auto *next = qobject_cast<HomeCanvas *>(value);
    if (canvas_ != next) {
        canvas_ = next;
        emit bindingsChanged();
    }
}
QObject *GenerationComposerModel::parameterStore() const {
    return parameterStore_;
}
void GenerationComposerModel::setParameterStore(QObject *value) {
    auto *next = qobject_cast<AdvancedImageParameters *>(value);
    if (parameterStore_ != next) {
        parameterStore_ = next;
        emit bindingsChanged();
    }
}
QVariantList GenerationComposerModel::modelChoices() const {
    return !generation_ ? QVariantList{} : video() ? generation_->videoModels() : generation_->models();
}
QString GenerationComposerModel::selectedModel() const {
    return !generation_ ? QString() : video() ? generation_->selectedVideoModel() : generation_->selectedModel();
}
QString GenerationComposerModel::selectedModelName() const {
    for (const auto &model : modelChoices())
        if (model.toMap().value("id").toString() == selectedModel()) {
            const auto name = model.toMap().value("name").toString();
            return name.isEmpty() ? selectedModel() : name;
        }
    return selectedModel();
}
void GenerationComposerModel::edit(std::string key, const QVariant &value) {
    if (sharedModel_) {
        sharedModel_->edit(std::move(key), value);
        return;
    }
    const auto reply = model_.edits().send({std::move(key), nativeValue(value)});
    if (reply)
        acceptSnapshot(viewValue(*reply).toMap());
}
void GenerationComposerModel::setSharedModel(GenerationComposerModel *model) {
    if (model == this || sharedModel_ == model)
        return;
    for (auto *owner = model; owner; owner = owner->sharedModel())
        if (owner == this)
            return;
    if (sharedModel_)
        disconnect(sharedModel_, nullptr, this, nullptr);
    sharedModel_ = model;
    if (sharedModel_) {
        connect(sharedModel_, &GenerationComposerModel::snapshotChanged, this,
                &GenerationComposerModel::acceptSnapshot);
        acceptSnapshot(sharedModel_->state_);
    }
    emit bindingsChanged();
}
void GenerationComposerModel::acceptSnapshot(const QVariantMap &next) {
    const auto old = state_;
    state_ = next;
    if (old.value("prompt") != state_.value("prompt"))
        emit promptChanged();
    if (old.value("mediaType") != state_.value("mediaType"))
        emit mediaTypeChanged();
    if (old.value("defaultPrompts") != state_.value("defaultPrompts"))
        emit defaultPromptsChanged();
    if (old.value("appliedDefaultPrompt") != state_.value("appliedDefaultPrompt"))
        emit appliedDefaultPromptChanged();
    if (old.value("promptDefaultsReady") != state_.value("promptDefaultsReady"))
        emit promptDefaultsReadyChanged();
    if (old.value("aspectRatio") != state_.value("aspectRatio"))
        emit aspectRatioChanged();
    if (old.value("generationCount") != state_.value("generationCount"))
        emit generationCountChanged();
    if (old.value("videoDuration") != state_.value("videoDuration"))
        emit videoDurationChanged();
    if (old.value("videoFps") != state_.value("videoFps"))
        emit videoFpsChanged();
    if (old.value("shortSide") != state_.value("shortSide"))
        emit shortSideChanged();
    if (old.value("seed") != state_.value("seed"))
        emit seedChanged();
    if (old.value("negativePrompt") != state_.value("negativePrompt"))
        emit negativePromptChanged();
    if (old.value("appliedNegativePrompt") != state_.value("appliedNegativePrompt"))
        emit appliedNegativePromptChanged();
    if (old.value("loadedPreset") != state_.value("loadedPreset"))
        emit loadedPresetChanged();
    if (old.value("errorText") != state_.value("errorText"))
        emit errorTextChanged();
    if (old.value("selectedPresetName") != state_.value("selectedPresetName"))
        emit selectedPresetNameChanged();
    if (old.value("submitting") != state_.value("submitting"))
        emit submittingChanged();
    if (old.value("generationPending") != state_.value("generationPending"))
        emit generationPendingChanged();
    if (old.value("video") != state_.value("video"))
        emit videoChanged();
    if (old.value("outputSize") != state_.value("outputSize"))
        emit outputSizeChanged();
    if (old.value("usesExplicitParameters") != state_.value("usesExplicitParameters"))
        emit usesExplicitParametersChanged();
    if (old.value("prompt") != state_.value("prompt"))
        emit promptRequested(prompt());
    if (old.value("mediaType") != state_.value("mediaType"))
        emit modelsChanged();
    if (old != state_)
        emit snapshotChanged(state_);
}
void GenerationComposerModel::setPrompt(QString value) {
    edit("prompt", value);
}
void GenerationComposerModel::setMediaType(QString value) {
    edit("mediaType", value);
}
void GenerationComposerModel::setDefaultPrompts(QVariantMap value) {
    edit("defaultPrompts", value);
}
void GenerationComposerModel::setAppliedDefaultPrompt(QString value) {
    edit("appliedDefaultPrompt", value);
}
void GenerationComposerModel::setPromptDefaultsReady(bool value) {
    edit("promptDefaultsReady", value);
}
void GenerationComposerModel::setAspectRatio(QString value) {
    edit("aspectRatio", value);
}
void GenerationComposerModel::setGenerationCount(int value) {
    edit("generationCount", value);
}
void GenerationComposerModel::setVideoDuration(int value) {
    edit("videoDuration", value);
}
void GenerationComposerModel::setVideoFps(int value) {
    edit("videoFps", value);
}
void GenerationComposerModel::setShortSide(int value) {
    edit("shortSide", value);
}
void GenerationComposerModel::setSeed(double value) {
    edit("seed", value);
}
void GenerationComposerModel::setNegativePrompt(QString value) {
    edit("negativePrompt", value);
}
void GenerationComposerModel::setAppliedNegativePrompt(QString value) {
    edit("appliedNegativePrompt", value);
}
void GenerationComposerModel::setLoadedPreset(bool value) {
    edit("loadedPreset", value);
}
void GenerationComposerModel::setErrorText(QString value) {
    edit("errorText", value);
}
void GenerationComposerModel::setSelectedPresetName(QString value) {
    edit("selectedPresetName", value);
}
void GenerationComposerModel::setSubmitting(bool value) {
    edit("submitting", value);
}
void GenerationComposerModel::setGenerationPending(bool value) {
    edit("generationPending", value);
}
QVariantMap GenerationComposerModel::sizeFor(int shorter) const {
    return viewValue(imageSize(aspectRatio().toStdString(), shorter)).toMap();
}
void GenerationComposerModel::applyDefaultPrompt() {
    edit("defaultPrompts", defaultPrompts());
}
void GenerationComposerModel::applyNegativeDefault() {
    edit("defaultPrompts", defaultPrompts());
}
bool GenerationComposerModel::addAttachment(const QUrl &source) {
    return canvas_ && canvas_->addAttachment(source);
}
QVariantMap GenerationComposerModel::generationParameters(const QString &model) {
    return canvas_ ? canvas_->generationParameters(prompt().trimmed(), model, aspectRatio(), generationCount())
                   : QVariantMap{};
}
QVariantMap GenerationComposerModel::parameters(const QString &model, bool includeInputs) {
    auto result = parameterStore_ ? parameterStore_->parameters() : QVariantMap{};
    result["prompt"] = prompt().trimmed();
    result["negativePrompt"] = negativePrompt();
    result["model"] = model;
    result["width"] = outputSize().value("width");
    result["height"] = outputSize().value("height");
    result["outputCount"] = generationCount();
    result["seed"] = seed();
    result["referenceImages"] = QVariantList{};
    if (includeInputs && canvas_ && (canvas_->hasContent() || !canvas_->attachments().isEmpty())) {
        const auto inputs = generationParameters(model);
        if (inputs.isEmpty())
            return {};
        result["referenceImages"] = inputs.value("referenceImages", QVariantList{});
    }
    return result;
}
bool GenerationComposerModel::savePreset(const QString &name) {
    if (!parameterStore_ || !parameterStore_->updateParameters(parameters(selectedModel(), false)) ||
        !parameterStore_->savePreset(name)) {
        setErrorText(parameterStore_ ? parameterStore_->errorString() : tr("Parameter store is unavailable."));
        return false;
    }
    setSelectedPresetName(name);
    setErrorText({});
    return true;
}
bool GenerationComposerModel::loadPreset(const QString &name) {
    if (!parameterStore_ || !parameterStore_->loadPreset(name)) {
        setErrorText(parameterStore_ ? parameterStore_->errorString() : tr("Parameter store is unavailable."));
        return false;
    }
    const auto values = parameterStore_->parameters();
    setPrompt(values.value("prompt").toString());
    setGenerationCount(values.value("outputCount").toInt());
    setSeed(values.value("seed").toDouble());
    setNegativePrompt(values.value("negativePrompt").toString());
    const auto width = values.value("width").toDouble(), height = values.value("height").toDouble(),
               ratio = width / height;
    QString standard;
    for (auto pair : {std::pair{1, 1}, {4, 3}, {3, 4}, {16, 9}, {9, 16}})
        if (std::abs(double(pair.first) / pair.second - ratio) < .01) {
            standard = QString::number(pair.first) + ":" + QString::number(pair.second);
            break;
        }
    setAspectRatio(standard.isEmpty() ? QString::number(width) + ":" + QString::number(height) : standard);
    setShortSide(std::min(width, height));
    setLoadedPreset(true);
    setSelectedPresetName(name);
    if (generation_)
        for (const auto &value : modelChoices())
            if (value.toMap().value("id") == values.value("model")) {
                if (video())
                    generation_->setSelectedVideoModel(values.value("model").toString());
                else
                    generation_->setSelectedModel(values.value("model").toString());
                break;
            }
    setErrorText({});
    return true;
}
void GenerationComposerModel::chooseMedia(const QString &type) {
    if (type == "Image" || type == "Video")
        setMediaType(type);
    else
        emit mediaActionRequested(type.toLower());
}
void GenerationComposerModel::submit() {
    if (submitting() || generationPending())
        return;
    if (prompt().trimmed().isEmpty()) {
        emit focusPromptRequested();
        return;
    }
    emit dismissInputRequested();
    setSubmitting(true);
    emit generateRequested(prompt().trimmed(), mediaType(), aspectRatio(), generationCount());
    setSubmitting(false);
}
QString GenerationComposerModel::enqueue(GenerationComposerModel *desktop) {
    requestError_.clear();
    if (!generation_) {
        requestError_ = tr("The generation runtime is not attached.");
        return {};
    }
    const auto seed = desktop ? desktop->seed() : -1;
    const bool inputs = canvas_ && (canvas_->hasContent() || !canvas_->attachments().isEmpty());
    QString id;
    if (video()) {
        QString firstFrame;
        if (inputs) {
            const auto settings = generationParameters(generation_->selectedVideoModel());
            const auto references = settings.value("referenceImages").toList();
            if (settings.isEmpty() || references.size() != 1) {
                requestError_ = canvas_->inputError().isEmpty()
                                    ? tr("Use one image or one painted canvas as the video's first frame.")
                                    : canvas_->inputError();
                return {};
            }
            firstFrame = references.front().toString();
        }
        if (desktop && desktop->negativePrompt() != desktop->appliedNegativePrompt()) {
            const QVariantList conditions =
                firstFrame.isEmpty() ? QVariantList{}
                                     : QVariantList{QVariantMap{{"frame", 0}, {"image", firstFrame}, {"strength", 1}}};
            id = generation_->enqueueVideoRecipe(
                {{"prompt", prompt().trimmed()},
                 {"negativePrompt", desktop->negativePrompt()},
                 {"width", desktop->outputSize().value("width")},
                 {"height", desktop->outputSize().value("height")},
                 {"outputCount", generationCount()},
                 {"seed", seed},
                 {"duration", videoDuration()},
                 {"fps", videoFps()},
                 {"shots", QVariantList{QVariantMap{{"frames", videoDuration() * videoFps()},
                                                    {"prompt", prompt().trimmed()},
                                                    {"conditions", conditions}}}}});
        } else
            id = generation_->enqueueVideo(prompt().trimmed(), aspectRatio(), generationCount(), QUrl(firstFrame),
                                           videoDuration(), videoFps(), seed);
    } else if (inputs || (desktop && desktop->usesExplicitParameters())) {
        const auto settings = desktop ? desktop->parameters(generation_->selectedModel(), true)
                                      : generationParameters(generation_->selectedModel());
        if (settings.isEmpty()) {
            requestError_ = canvas_ ? canvas_->inputError() : tr("Canvas input is unavailable.");
            return {};
        }
        id = generation_->enqueueHomeCanvas(settings, aspectRatio(), desktop && desktop->usesExplicitParameters());
    } else
        id = generation_->enqueue(prompt().trimmed(), aspectRatio(), generationCount(), seed);
    if (id.isEmpty())
        requestError_ = generation_->errorString();
    return id;
}

bool GenerationComposerModel::applySeedText(const QString &input) {
    const auto parsed=dreamscapes::vm::parseNumber(dreamscapes::vm::Value(dreamscapes::vm::Object{{"min",dreamscapes::vm::Value(0)},{"max",dreamscapes::vm::Value(4294967295.0-generationCount()+1)},{"integer",dreamscapes::vm::Value(true)}}),input.toStdString(),true);
    if(!parsed.valid){setErrorText(tr("Enter an integer seed with room for the batch, or choose Random."));return false;}
    setSeed(dreamscapes::vm::number(parsed.value));setErrorText({});return true;
}
