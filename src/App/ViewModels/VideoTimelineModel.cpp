#include "VideoTimelineModel.h"
#include "ViewData.h"
namespace dreamscapes {
VideoTimelineModel::VideoTimelineModel(QObject *parent) : QObject(parent) { refresh(); }
QVariant VideoTimelineModel::send(vm::TimelineAction action, std::initializer_list<QVariant> arguments) {
    vm::TimelineCommand request{action, {}}; for (const auto &v : arguments) request.arguments.push_back(nativeValue(v));
    const auto reply = model_.commands().send(request); refresh();
    if (!reply) return {};
    if (reply->reset) emit resetCompleted();
    if (reply->compositionRestored) emit compositionRestored();
    return viewValue(reply->returned);
}
void VideoTimelineModel::refresh() {
    const auto old = state_; state_ = viewValue(model_.presentation()).toMap();
    if (old.value("fps") != state_.value("fps")) emit fpsChanged();
    if (old.value("minimumFrames") != state_.value("minimumFrames")) emit minimumFramesChanged();
    if (old.value("playhead") != state_.value("playhead")) emit playheadChanged();
    if (old.value("mode") != state_.value("mode")) emit modeChanged();
    if (old.value("tool") != state_.value("tool")) emit toolChanged();
    if (old.value("filter") != state_.value("filter")) emit filterChanged();
    if (old.value("snap") != state_.value("snap")) emit snapChanged();
    if (old.value("linked") != state_.value("linked")) emit linkedChanged();
    if (old.value("multiSelect") != state_.value("multiSelect")) emit multiSelectChanged();
    if (old.value("markIn") != state_.value("markIn")) emit markInChanged();
    if (old.value("markOut") != state_.value("markOut")) emit markOutChanged();
    if (old.value("compositionFrames") != state_.value("compositionFrames")) emit compositionFramesChanged();
    if (old.value("trackTypes") != state_.value("trackTypes")) emit trackTypesChanged();
    if (old.value("tracks") != state_.value("tracks")) emit tracksChanged();
    if (old.value("selectedTrackId") != state_.value("selectedTrackId")) emit selectedTrackIdChanged();
    if (old.value("error") != state_.value("error")) emit errorChanged();
    if (old.value("selectedTrack") != state_.value("selectedTrack")) emit selectedTrackChanged();
    if (old.value("shots") != state_.value("shots")) emit shotsChanged();
    if (old.value("selectedIds") != state_.value("selectedIds")) emit selectedIdsChanged();
    if (old.value("keys") != state_.value("keys")) emit keysChanged();
    if (old.value("selectedKeyId") != state_.value("selectedKeyId")) emit selectedKeyIdChanged();
    if (old.value("totalFrames") != state_.value("totalFrames")) emit totalFramesChanged();
    if (old.value("keptFrames") != state_.value("keptFrames")) emit keptFramesChanged();
    if (old.value("outputFrames") != state_.value("outputFrames")) emit outputFramesChanged();
    if (old.value("selectedShot") != state_.value("selectedShot")) emit selectedShotChanged();
    if (old.value("selectedKey") != state_.value("selectedKey")) emit selectedKeyChanged();
    if (old.value("selectedStart") != state_.value("selectedStart")) emit selectedStartChanged();
    if (old.value("selectedEnd") != state_.value("selectedEnd")) emit selectedEndChanged();
    if (old.value("canUndo") != state_.value("canUndo")) emit canUndoChanged();
    if (old.value("canRedo") != state_.value("canRedo")) emit canRedoChanged();
    if (old.value("clipboardKey") != state_.value("clipboardKey")) emit clipboardKeyChanged();
}
void VideoTimelineModel::setFps(double value) { send(vm::TimelineAction::write, {QStringLiteral("fps"), value}); }
void VideoTimelineModel::setMinimumFrames(int value) { send(vm::TimelineAction::write, {QStringLiteral("minimumFrames"), value}); }
void VideoTimelineModel::setPlayhead(int value) { send(vm::TimelineAction::write, {QStringLiteral("playhead"), value}); }
void VideoTimelineModel::setMode(QString value) { send(vm::TimelineAction::write, {QStringLiteral("mode"), value}); }
void VideoTimelineModel::setTool(QString value) { send(vm::TimelineAction::write, {QStringLiteral("tool"), value}); }
void VideoTimelineModel::setFilter(QString value) { send(vm::TimelineAction::write, {QStringLiteral("filter"), value}); }
void VideoTimelineModel::setSnap(bool value) { send(vm::TimelineAction::write, {QStringLiteral("snap"), value}); }
void VideoTimelineModel::setLinked(bool value) { send(vm::TimelineAction::write, {QStringLiteral("linked"), value}); }
void VideoTimelineModel::setMultiSelect(bool value) { send(vm::TimelineAction::write, {QStringLiteral("multiSelect"), value}); }
void VideoTimelineModel::setMarkIn(int value) { send(vm::TimelineAction::write, {QStringLiteral("markIn"), value}); }
void VideoTimelineModel::setMarkOut(int value) { send(vm::TimelineAction::write, {QStringLiteral("markOut"), value}); }
void VideoTimelineModel::setCompositionFrames(int value) { send(vm::TimelineAction::write, {QStringLiteral("compositionFrames"), value}); }
void VideoTimelineModel::setTracks(QVariantList value) { send(vm::TimelineAction::write, {QStringLiteral("tracks"), value}); }
void VideoTimelineModel::setSelectedTrackId(QString value) { send(vm::TimelineAction::write, {QStringLiteral("selectedTrackId"), value}); }
void VideoTimelineModel::setError(QString value) { send(vm::TimelineAction::write, {QStringLiteral("error"), value}); }
void VideoTimelineModel::setShots(QVariantList value) { send(vm::TimelineAction::write, {QStringLiteral("shots"), value}); }
void VideoTimelineModel::setSelectedIds(QVariantList value) { send(vm::TimelineAction::write, {QStringLiteral("selectedIds"), value}); }
void VideoTimelineModel::setKeys(QVariantList value) { send(vm::TimelineAction::write, {QStringLiteral("keys"), value}); }
void VideoTimelineModel::setSelectedKeyId(QString value) { send(vm::TimelineAction::write, {QStringLiteral("selectedKeyId"), value}); }
bool VideoTimelineModel::selectTrack(const QString &id) { return send(vm::TimelineAction::selectTrack, {id}).toBool(); }
QString VideoTimelineModel::addTrack(const QString &type, const QString &name, int position) { return send(vm::TimelineAction::addTrack, {type, name, position}).toString(); }
bool VideoTimelineModel::renameTrack(const QString &id, const QString &name) { return send(vm::TimelineAction::renameTrack, {id, name}).toBool(); }
bool VideoTimelineModel::moveTrack(const QString &id, int position) { return send(vm::TimelineAction::moveTrack, {id, position}).toBool(); }
bool VideoTimelineModel::removeTrack(const QString &id) { return send(vm::TimelineAction::removeTrack, {id}).toBool(); }
bool VideoTimelineModel::assignShotTrack(const QString &id, const QString &trackId) { return send(vm::TimelineAction::assignShotTrack, {id, trackId}).toBool(); }
void VideoTimelineModel::checkpoint() { send(vm::TimelineAction::checkpoint, {}); }
void VideoTimelineModel::undo() { send(vm::TimelineAction::undo, {}); }
void VideoTimelineModel::redo() { send(vm::TimelineAction::redo, {}); }
void VideoTimelineModel::seek(double frame) { send(vm::TimelineAction::seek, {frame}); }
void VideoTimelineModel::selectShot(const QString &id, bool extend) { send(vm::TimelineAction::selectShot, {id, extend}); }
void VideoTimelineModel::jumpCut(int direction) { send(vm::TimelineAction::jumpCut, {direction}); }
bool VideoTimelineModel::changeShot(const QString &id, const QString &field, const QVariant &value) { return send(vm::TimelineAction::changeShot, {id, field, value}).toBool(); }
void VideoTimelineModel::toggleExclude() { send(vm::TimelineAction::toggleExclude, {}); }
bool VideoTimelineModel::split() { return send(vm::TimelineAction::split, {}).toBool(); }
bool VideoTimelineModel::addShot() { return send(vm::TimelineAction::addShot, {}).toBool(); }
bool VideoTimelineModel::addShotToTrack(const QString &trackId) { return send(vm::TimelineAction::addShot, {trackId}).toBool(); }
void VideoTimelineModel::duplicateShot() { send(vm::TimelineAction::duplicateShot, {}); }
bool VideoTimelineModel::rippleDelete() { return send(vm::TimelineAction::rippleDelete, {}).toBool(); }
bool VideoTimelineModel::resizeComposition(double frames, double frameRate) { return send(vm::TimelineAction::resizeComposition, {frames, frameRate < 0 ? fps() : frameRate}).toBool(); }
bool VideoTimelineModel::slipKeys(const QString &id, int delta) { return send(vm::TimelineAction::slipKeys, {id, delta}).toBool(); }
bool VideoTimelineModel::trim(const QString &id, const QString &edge, double delta, const QString &editTool) { return send(vm::TimelineAction::trim, {id, edge, delta, editTool}).toBool(); }
bool VideoTimelineModel::nudgeShot(int delta) { return send(vm::TimelineAction::nudgeShot, {delta}).toBool(); }
bool VideoTimelineModel::addKey(const QString &source, double frame) { return send(vm::TimelineAction::addKey, {source, frame}).toBool(); }
bool VideoTimelineModel::addKeyToTrack(const QString &source, double frame, const QString &trackId) { return send(vm::TimelineAction::addKey, {source, frame, trackId}).toBool(); }
bool VideoTimelineModel::editKey(const QString &field, const QVariant &value) { return send(vm::TimelineAction::editKey, {field, value}).toBool(); }
void VideoTimelineModel::deleteKey() { send(vm::TimelineAction::deleteKey, {}); }
void VideoTimelineModel::copyKey() { send(vm::TimelineAction::copyKey, {}); }
void VideoTimelineModel::pasteKey() { send(vm::TimelineAction::pasteKey, {}); }
void VideoTimelineModel::reset(double frames, double fps) { send(vm::TimelineAction::reset, {frames, fps}); }
QVariantMap VideoTimelineModel::snapshot() const { return viewValue(model_.snapshot()).toMap(); }
void VideoTimelineModel::restore(const QVariant &value) { send(vm::TimelineAction::restore, {value}); }
QVariantMap VideoTimelineModel::track(const QString &id) const { return viewValue(model_.track(id.toStdString())).toMap(); }
QVariantList VideoTimelineModel::tracksOfType(const QString &type) const { return viewValue(model_.tracksOfType(type.toStdString())).toList(); }
int VideoTimelineModel::trackPosition(const QString &id) const { return model_.trackPosition(id.toStdString()); }
QString VideoTimelineModel::destinationTrack(const QString &type, const QString &id) const { return QString::fromStdString(model_.destinationTrack(type.toStdString(), id.toStdString())); }
int VideoTimelineModel::startOf(const QString &id) const { return model_.startOf(id.toStdString()); }
int VideoTimelineModel::snappedFrame(double frame) const { return model_.snappedFrame(frame); }
QVariantList VideoTimelineModel::recipeShots(const QString &prompt) const { return viewValue(model_.recipeShots(prompt.toStdString())).toList(); }
} // namespace dreamscapes
