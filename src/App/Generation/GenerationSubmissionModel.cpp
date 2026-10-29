#include "GenerationSubmissionModel.h"
#include "AdvancedImageParameters.h"
#include "GenerationController.h"
#include "VideoDraftModel.h"
QObject *GenerationSubmissionModel::generation() const {
    return generation_;
}
void GenerationSubmissionModel::setGeneration(QObject *value) {
    auto *next = qobject_cast<GenerationController *>(value);
    if (next == generation_)
        return;
    if (generation_)
        disconnect(generation_, nullptr, this, nullptr);
    generation_ = next;
    if (generation_)
        connect(generation_, &GenerationController::submissionQueued, this, [this](const QStringList &ids) {
            if (submitting_)
                setJobIds(ids);
        });
    emit bindingsChanged();
}
QObject *GenerationSubmissionModel::imageParameters() const {
    return image_;
}
void GenerationSubmissionModel::setImageParameters(QObject *value) {
    auto *next = qobject_cast<AdvancedImageParameters *>(value);
    if (image_ == next) return;
    if (image_) disconnect(image_, nullptr, this, nullptr);
    image_ = next;
    if (image_) connect(image_, &AdvancedImageParameters::invalidInputsChanged,
        this, &GenerationSubmissionModel::clearCorrectedInputError);
    const QPointer<GenerationSubmissionModel> owner(this);
    emit bindingsChanged();
    if (owner) clearCorrectedInputError();
}
QObject *GenerationSubmissionModel::videoParameters() const {
    return video_;
}
void GenerationSubmissionModel::setVideoParameters(QObject *value) {
    auto *next = qobject_cast<VideoDraftModel *>(value);
    if (video_ == next) return;
    if (video_) disconnect(video_, nullptr, this, nullptr);
    video_ = next;
    if (video_) connect(video_, &VideoDraftModel::invalidInputsChanged,
        this, &GenerationSubmissionModel::clearCorrectedInputError);
    const QPointer<GenerationSubmissionModel> owner(this);
    emit bindingsChanged();
    if (owner) clearCorrectedInputError();
}
void GenerationSubmissionModel::clearCorrectedInputError() {
    if (!invalid_) return;
    const bool valid = mediaType_ == "Video" ? video_ && !video_->hasInvalidInputs()
                                            : image_ && !image_->hasInvalidInputs();
    if (!valid) return;
    invalid_ = false;
    error_.clear();
    emit changed();
}
bool GenerationSubmissionModel::submit() {
    if (submitting_)
        return false;
    error_.clear();
    const bool video = mediaType_ == "Video";
    invalid_ = video ? (!video_ || video_->hasInvalidInputs()) : (!image_ || image_->hasInvalidInputs());
    if (invalid_) {
        error_ = video ? tr("Correct invalid video parameter values.")
                       : tr("Correct invalid parameter input before generating.");
        emit changed();
        return false;
    }
    if (!generation_) {
        error_ = tr("The generation runtime is not attached.");
        emit changed();
        return false;
    }
    submitting_ = true;
    emit changed();
    const auto id = video ? generation_->enqueueVideoRecipe(video_->parameters())
                          : generation_->enqueueAdvanced(image_->parameters());
    submitting_ = false;
    if (id.isEmpty())
        error_ = generation_->errorString();
    else {
        result_.clear();
        source_.clear();
        emit selectionResetRequested();
    }
    emit changed();
    return !id.isEmpty();
}
void GenerationSubmissionModel::cancelBatch() {
    if (generation_)
        generation_->cancelBatch(ids_);
}
