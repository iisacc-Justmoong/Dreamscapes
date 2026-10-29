#include "VideoDraftModel.h"
#include "../ViewModels/ViewData.h"
#include "GenerationController.h"
#include <limits>

using namespace dreamscapes;
using namespace dreamscapes::vm;
VideoDraftModel::VideoDraftModel(QObject *parent)
    : QObject(parent),
      draft_([this](int frames, double fps) { return !timeline_ || timeline_->resizeComposition(frames, fps); }) {}
QObject *VideoDraftModel::generation() const {
    return generation_;
}
void VideoDraftModel::setGeneration(QObject *generation) {
    auto *next = qobject_cast<GenerationController *>(generation);
    if (generation_ == next)
        return;
    const bool initialize = !generation_;
    generation_ = next;
    if (initialize)
        setValues(defaults());
    emit generationChanged();
}
void VideoDraftModel::setTimeline(VideoTimelineModel *timeline) {
    if (timeline_ == timeline)
        return;
    if (timeline_)
        disconnect(timeline_, nullptr, this, nullptr);
    timeline_ = timeline;
    if (timeline_)
        connect(timeline_, &VideoTimelineModel::compositionRestored, this, [this] {
            const auto old = values(), invalid = invalidInputs();
            draft_.restoreComposition(timeline_->compositionFrames(), timeline_->fps());
            notify(old, invalid);
        });
    emit timelineChanged();
}
QVariantMap VideoDraftModel::values() const {
    return viewValue(draft_.values()).toMap();
}
void VideoDraftModel::setValues(const QVariantMap &values) {
    const auto old = this->values(), invalid = invalidInputs();
    draft_.replace(nativeValue(values));
    notify(old, invalid);
}
QVariantMap VideoDraftModel::invalidInputs() const {
    return viewValue(draft_.invalidInputs()).toMap();
}
bool VideoDraftModel::hasInvalidInputs() const {
    return draft_.invalid();
}
bool VideoDraftModel::cameraSelectionValid() const {
    return videoCameraValid(draft_.values());
}
QString VideoDraftModel::aspectRatio() const {
    return QString::fromStdString(
        vm::aspectRatio(number(get(draft_.values(), "width")), number(get(draft_.values(), "height"))));
}
QVariantMap VideoDraftModel::defaults() const {
    return viewValue(videoDefaults(generation_ ? nativeValue(generation_->defaultGenerationPrompts().value("Video"))
                                               : Value()))
        .toMap();
}
void VideoDraftModel::notify(const QVariantMap &oldValues, const QVariantMap &oldInvalid) {
    if (oldValues != values())
        emit valuesChanged();
    // Camera conflicts participate in the submission guard even without a field error.
    if (oldInvalid != invalidInputs() || oldValues != values())
        emit invalidInputsChanged();
}
bool VideoDraftModel::edit(const QString &key, const QVariant &value) {
    const auto old = values(), invalid = invalidInputs();
    const auto result = draft_.edits().send({key.toStdString(), nativeValue(value)});
    notify(old, invalid);
    return result && *result;
}
QVariantMap VideoDraftModel::parameters() const {
    auto result = values();
    QStringList camera;
    for (auto key : {"camera1", "camera2", "camera3"})
        if (result.value(key).toString() != "none")
            camera.push_back(result.value(key).toString());
    result["model"] = generation_ ? generation_->selectedVideoModel() : QString();
    result["seed"] = result.value("randomSeed").toBool() ? -1 : result.value("seed");
    result["camera"] = camera.isEmpty() ? QStringList{"none"} : camera;
    result["shots"] = timeline_ ? timeline_->recipeShots(result.value("prompt").toString()) : QVariantList{};
    return result;
}
bool VideoDraftModel::savePreset(const QString &name) {
    auto parameters = values();
    parameters["model"] = generation_ ? generation_->selectedVideoModel() : QString();
    if (hasInvalidInputs() || !generation_ || !generation_->saveVideoPreset(name, parameters)) {
        presetFeedback_ =
            generation_ ? generation_->videoPresetError() : QStringLiteral("Generation service is unavailable.");
        emit presetChanged();
        return false;
    }
    selectedPreset_ = name.trimmed();
    presetFeedback_ = tr("Preset saved");
    emit presetChanged();
    return true;
}
bool VideoDraftModel::loadPreset(const QString &name) {
    auto next = defaults();
    QVariantMap saved;
    if (name != tr("Default")) {
        if (!generation_)
            return false;
        saved = generation_->videoPreset(name);
        if (saved.isEmpty()) {
            presetFeedback_ = generation_->videoPresetError();
            emit presetChanged();
            return false;
        }
        for (auto it = saved.begin(); it != saved.end(); ++it)
            next[it.key()] = it.value();
    }
    if (timeline_ && !timeline_->resizeComposition(next.value("duration").toInt() * next.value("fps").toInt(),
                                                   next.value("fps").toDouble())) {
        presetFeedback_ = timeline_->error();
        emit presetChanged();
        return false;
    }
    setValues(next);
    if (generation_ && !saved.value("model").toString().isEmpty())
        for (const auto &entry : generation_->videoModels())
            if (entry.toMap().value("id") == saved.value("model"))
                generation_->setSelectedVideoModel(saved.value("model").toString());
    selectedPreset_ = name;
    presetFeedback_.clear();
    emit presetChanged();
    return true;
}
void VideoDraftModel::resetDraft() {
    setValues(defaults());
    if (timeline_)
        timeline_->reset(values().value("duration").toInt() * values().value("fps").toInt(),
                         values().value("fps").toDouble());
    selectedPreset_ = tr("Default");
    presetFeedback_.clear();
    emit presetChanged();
    emit viewResetRequested();
}
bool VideoDraftModel::addImageKey(const QUrl &source, int frame, const QString &trackId) {
    if (!generation_ || !timeline_ || (!trackId.isEmpty() && timeline_->track(trackId).value("type") != "Contents"))
        return false;
    const auto document = generation_->imageDocument(source);
    return !document.isEmpty() && timeline_->addKeyToTrack(document.toString(), frame, trackId);
}

bool VideoDraftModel::editNumber(const QString &key, const QString &input) {
    const auto unit = key == "width" || key == "height" ? "px" : key == "duration" ? "s" : "";
    const auto parsed = dreamscapes::vm::parseNumber(
        dreamscapes::vm::Value(dreamscapes::vm::Object{{"unit", dreamscapes::vm::Value(unit)}}), input.toStdString(),
        true);
    return edit(key, parsed.valid ? dreamscapes::viewValue(parsed.value)
                                  : QVariant(std::numeric_limits<double>::quiet_NaN()));
}
