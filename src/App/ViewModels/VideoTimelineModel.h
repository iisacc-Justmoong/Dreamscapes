#pragma once
#include "Native/VideoTimeline.h"
#include <QObject>
#include <QVariant>
namespace dreamscapes {
class VideoTimelineModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(double fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(int minimumFrames READ minimumFrames WRITE setMinimumFrames NOTIFY minimumFramesChanged)
    Q_PROPERTY(int playhead READ playhead WRITE setPlayhead NOTIFY playheadChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(bool snap READ snap WRITE setSnap NOTIFY snapChanged)
    Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY linkedChanged)
    Q_PROPERTY(bool multiSelect READ multiSelect WRITE setMultiSelect NOTIFY multiSelectChanged)
    Q_PROPERTY(int markIn READ markIn WRITE setMarkIn NOTIFY markInChanged)
    Q_PROPERTY(int markOut READ markOut WRITE setMarkOut NOTIFY markOutChanged)
    Q_PROPERTY(int compositionFrames READ compositionFrames WRITE setCompositionFrames NOTIFY compositionFramesChanged)
    Q_PROPERTY(QVariantList trackTypes READ trackTypes NOTIFY trackTypesChanged)
    Q_PROPERTY(QVariantList tracks READ tracks WRITE setTracks NOTIFY tracksChanged)
    Q_PROPERTY(QString selectedTrackId READ selectedTrackId WRITE setSelectedTrackId NOTIFY selectedTrackIdChanged)
    Q_PROPERTY(QString error READ error WRITE setError NOTIFY errorChanged)
    Q_PROPERTY(QVariantMap selectedTrack READ selectedTrack NOTIFY selectedTrackChanged)
    Q_PROPERTY(QVariantList shots READ shots WRITE setShots NOTIFY shotsChanged)
    Q_PROPERTY(QVariantList selectedIds READ selectedIds WRITE setSelectedIds NOTIFY selectedIdsChanged)
    Q_PROPERTY(QVariantList keys READ keys WRITE setKeys NOTIFY keysChanged)
    Q_PROPERTY(QString selectedKeyId READ selectedKeyId WRITE setSelectedKeyId NOTIFY selectedKeyIdChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY totalFramesChanged)
    Q_PROPERTY(int keptFrames READ keptFrames NOTIFY keptFramesChanged)
    Q_PROPERTY(int outputFrames READ outputFrames NOTIFY outputFramesChanged)
    Q_PROPERTY(QVariantMap selectedShot READ selectedShot NOTIFY selectedShotChanged)
    Q_PROPERTY(QVariantMap selectedKey READ selectedKey NOTIFY selectedKeyChanged)
    Q_PROPERTY(int selectedStart READ selectedStart NOTIFY selectedStartChanged)
    Q_PROPERTY(int selectedEnd READ selectedEnd NOTIFY selectedEndChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY canRedoChanged)
    Q_PROPERTY(QString clipboardKey READ clipboardKey NOTIFY clipboardKeyChanged)
    Q_PROPERTY(int keptShotCount READ keptShotCount NOTIFY keptFramesChanged)
    Q_PROPERTY(QString referenceSource READ referenceSource NOTIFY referenceSourceChanged)
    Q_PROPERTY(int nextId READ nextId NOTIFY nextIdChanged)
  public:
    explicit VideoTimelineModel(QObject *parent = nullptr);
    double fps() const {
        return state_.value("fps").toDouble();
    }
    void setFps(double value);
    int minimumFrames() const {
        return state_.value("minimumFrames").toInt();
    }
    void setMinimumFrames(int value);
    int playhead() const {
        return state_.value("playhead").toInt();
    }
    void setPlayhead(int value);
    QString mode() const {
        return state_.value("mode").toString();
    }
    void setMode(QString value);
    QString tool() const {
        return state_.value("tool").toString();
    }
    void setTool(QString value);
    QString filter() const {
        return state_.value("filter").toString();
    }
    void setFilter(QString value);
    bool snap() const {
        return state_.value("snap").toBool();
    }
    void setSnap(bool value);
    bool linked() const {
        return state_.value("linked").toBool();
    }
    void setLinked(bool value);
    bool multiSelect() const {
        return state_.value("multiSelect").toBool();
    }
    void setMultiSelect(bool value);
    int markIn() const {
        return state_.value("markIn").toInt();
    }
    void setMarkIn(int value);
    int markOut() const {
        return state_.value("markOut").toInt();
    }
    void setMarkOut(int value);
    int compositionFrames() const {
        return state_.value("compositionFrames").toInt();
    }
    void setCompositionFrames(int value);
    QVariantList trackTypes() const {
        return state_.value("trackTypes").toList();
    }
    QVariantList tracks() const {
        return state_.value("tracks").toList();
    }
    void setTracks(QVariantList value);
    QString selectedTrackId() const {
        return state_.value("selectedTrackId").toString();
    }
    void setSelectedTrackId(QString value);
    QString error() const {
        return state_.value("error").toString();
    }
    void setError(QString value);
    QVariantMap selectedTrack() const {
        return state_.value("selectedTrack").toMap();
    }
    QVariantList shots() const {
        return state_.value("shots").toList();
    }
    void setShots(QVariantList value);
    QVariantList selectedIds() const {
        return state_.value("selectedIds").toList();
    }
    void setSelectedIds(QVariantList value);
    QVariantList keys() const {
        return state_.value("keys").toList();
    }
    void setKeys(QVariantList value);
    QString selectedKeyId() const {
        return state_.value("selectedKeyId").toString();
    }
    void setSelectedKeyId(QString value);
    int totalFrames() const {
        return state_.value("totalFrames").toInt();
    }
    int keptFrames() const {
        return state_.value("keptFrames").toInt();
    }
    int outputFrames() const {
        return state_.value("outputFrames").toInt();
    }
    QVariantMap selectedShot() const {
        return state_.value("selectedShot").toMap();
    }
    QVariantMap selectedKey() const {
        return state_.value("selectedKey").toMap();
    }
    int selectedStart() const {
        return state_.value("selectedStart").toInt();
    }
    int selectedEnd() const {
        return state_.value("selectedEnd").toInt();
    }
    bool canUndo() const {
        return state_.value("canUndo").toBool();
    }
    bool canRedo() const {
        return state_.value("canRedo").toBool();
    }
    QString clipboardKey() const {
        return state_.value("clipboardKey").toString();
    }
    QString referenceSource() const {
        return state_.value("referenceSource").toString();
    }
    int nextId() const {
        return state_.value("nextId").toInt();
    }
    std::size_t undoDepth() const {
        return model_.undoDepth();
    }
    std::size_t redoDepth() const {
        return model_.redoDepth();
    }
    Q_INVOKABLE bool selectTrack(const QString &id);
    Q_INVOKABLE QString addTrack(const QString &type, const QString &name = {}, int position = -1);
    Q_INVOKABLE bool renameTrack(const QString &id, const QString &name);
    Q_INVOKABLE bool moveTrack(const QString &id, int position);
    Q_INVOKABLE bool removeTrack(const QString &id);
    Q_INVOKABLE bool assignShotTrack(const QString &id, const QString &trackId);
    Q_INVOKABLE void checkpoint();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void seek(double frame);
    Q_INVOKABLE void selectShot(const QString &id, bool extend = false);
    Q_INVOKABLE void jumpCut(int direction);
    Q_INVOKABLE bool changeShot(const QString &id, const QString &field, const QVariant &value);
    Q_INVOKABLE void toggleExclude();
    Q_INVOKABLE bool split();
    Q_INVOKABLE bool addShot();
    Q_INVOKABLE bool addShotToTrack(const QString &trackId);
    Q_INVOKABLE void duplicateShot();
    Q_INVOKABLE bool rippleDelete();
    Q_INVOKABLE bool resizeComposition(double frames);
    Q_INVOKABLE bool resizeComposition(double frames, double frameRate);
    Q_INVOKABLE bool slipKeys(const QString &id, int delta);
    Q_INVOKABLE bool trim(const QString &id, const QString &edge, double delta, const QString &editTool);
    Q_INVOKABLE bool nudgeShot(int delta);
    Q_INVOKABLE bool addKey(const QString &source, double frame);
    Q_INVOKABLE bool addKeyToTrack(const QString &source, double frame, const QString &trackId);
    Q_INVOKABLE bool editKey(const QString &field, const QVariant &value);
    Q_INVOKABLE void deleteKey();
    Q_INVOKABLE void copyKey();
    Q_INVOKABLE void pasteKey();
    Q_INVOKABLE void reset(double frames, double fps);
    Q_INVOKABLE QVariantMap snapshot() const;
    Q_INVOKABLE void restore(const QVariant &);
    Q_INVOKABLE QVariantMap track(const QString &) const;
    Q_INVOKABLE QVariantList tracksOfType(const QString &) const;
    Q_INVOKABLE int trackPosition(const QString &) const;
    Q_INVOKABLE QString destinationTrack(const QString &, const QString &) const;
    Q_INVOKABLE int startOf(const QString &) const;
    Q_INVOKABLE int snappedFrame(double) const;
    int keptShotCount() const {
        return state_.value("keptShotCount").toInt();
    }
    Q_INVOKABLE void selectShotRange(const QString &id);
    Q_INVOKABLE void selectAllShots(bool selected = true);
    Q_INVOKABLE bool setRangeText(const QString &field, const QString &text);
    Q_INVOKABLE int shotIndex(const QString &id) const;
    Q_INVOKABLE QString shotReference(const QString &id) const;
    Q_INVOKABLE QVariantList trackShots(const QString &id) const;
    Q_INVOKABLE QVariantList trackKeys(const QString &id, bool ordered = false) const;
    Q_INVOKABLE QVariantList recipeShots(const QString &) const;
  signals:
    void resetCompleted();
    void nextIdChanged();
    void compositionRestored();
    void fpsChanged();
    void minimumFramesChanged();
    void playheadChanged();
    void modeChanged();
    void toolChanged();
    void filterChanged();
    void snapChanged();
    void linkedChanged();
    void multiSelectChanged();
    void markInChanged();
    void markOutChanged();
    void compositionFramesChanged();
    void trackTypesChanged();
    void tracksChanged();
    void selectedTrackIdChanged();
    void errorChanged();
    void selectedTrackChanged();
    void shotsChanged();
    void selectedIdsChanged();
    void keysChanged();
    void selectedKeyIdChanged();
    void totalFramesChanged();
    void keptFramesChanged();
    void outputFramesChanged();
    void selectedShotChanged();
    void selectedKeyChanged();
    void selectedStartChanged();
    void selectedEndChanged();
    void canUndoChanged();
    void canRedoChanged();
    void clipboardKeyChanged();
    void referenceSourceChanged();

  private:
    QVariant send(vm::TimelineAction, std::initializer_list<QVariant>);
    void refresh();
    vm::VideoTimeline model_;
    QVariantMap state_;
};
} // namespace dreamscapes
