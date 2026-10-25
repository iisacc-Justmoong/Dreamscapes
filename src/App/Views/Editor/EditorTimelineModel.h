#pragma once
#include "EditorCanvas.h"
#include <QCache>
#include <QPointer>
#include <QtQml/qqmlregistration.h>
#include <utility>
#include <vector>
#include <QElapsedTimer>
#include <QTimer>

class EditorTimelineThumbnails;
class EditorAudioPlayback;
// Projection and commands for the open native document. No second layer list
// or timing recipe is persisted by this model.
class EditorTimelineModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *canvas READ canvas WRITE setCanvas NOTIFY changed)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectionChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY projectionChanged)
    Q_PROPERTY(QVariantList keys READ keys NOTIFY projectionChanged)
    Q_PROPERTY(QVariantList trackLayout READ trackLayout NOTIFY layoutChanged)
    Q_PROPERTY(int laneHeight READ laneHeight NOTIFY layoutChanged)
    Q_PROPERTY(int rulerHeight READ rulerHeight WRITE setRulerHeight NOTIFY layoutChanged)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY projectionChanged)
    Q_PROPERTY(int keptCount READ keptCount NOTIFY projectionChanged)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY projectionChanged)
    Q_PROPERTY(double fps READ fps NOTIFY projectionChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY projectionChanged)
    Q_PROPERTY(int playhead READ playhead WRITE seek NOTIFY playheadChanged)
    Q_PROPERTY(QString timecode READ timecode NOTIFY playheadChanged)
    Q_PROPERTY(bool scrubbing READ scrubbing NOTIFY scrubbingChanged)
    Q_PROPERTY(QVariantMap dragPreview READ dragPreview NOTIFY dragPreviewChanged)
    Q_PROPERTY(QVariantMap keyDragPreview READ keyDragPreview NOTIFY dragPreviewChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY dragPreviewChanged)
    Q_PROPERTY(qulonglong thumbnailRevision READ thumbnailRevision NOTIFY thumbnailsChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY changed)
    Q_PROPERTY(QVariantMap selectedClip READ selectedClip NOTIFY changed)
    Q_PROPERTY(QVariantMap selectedKey READ selectedKey NOTIFY changed)
    Q_PROPERTY(QString selectedTrackId READ selectedTrackId NOTIFY changed)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY changed)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
    Q_PROPERTY(bool snap READ snap WRITE setSnap NOTIFY changed)
    Q_PROPERTY(bool linked READ linked WRITE setLinked NOTIFY changed)
    Q_PROPERTY(int markIn MEMBER m_markIn NOTIFY changed)
    Q_PROPERTY(int markOut MEMBER m_markOut NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playbackChanged)
    Q_PROPERTY(bool preparing READ preparing NOTIFY playbackChanged)
public:
    explicit EditorTimelineModel(QObject *parent=nullptr);
    EditorTimelineModel(EditorAudioPlayback *playback,QObject *parent);
    ~EditorTimelineModel() override;
    EditorCanvas *canvas() const { return m_canvas; }
    void setCanvas(EditorCanvas *canvas);
    QVariantList tracks() const { return m_tracks; }
    QVariantList clips() const { return m_clips; }
    QVariantList keys() const { return m_keys; }
    QVariantList trackLayout() const { return m_trackLayout; }
    int laneHeight() const { return m_laneHeight; }
    int rulerHeight() const { return m_rulerHeight; }
    void setRulerHeight(int value);
    int layerCount() const { return m_layerCount; }
    int keptCount() const { return m_keptCount; }
    bool hasAudio() const { return m_hasAudio; }
    double fps() const;
    int totalFrames() const;
    int playhead() const;
    QString timecode() const;
    bool scrubbing() const {return m_scrubbing;}
    QVariantMap dragPreview() const { return m_dragPreview; }
    QVariantMap keyDragPreview() const { return m_keyDragPreview; }
    bool dragging() const { return !m_dragId.isEmpty(); }
    qulonglong thumbnailRevision() const { return m_thumbnailRevision; }
    QStringList selectedIds() const { return m_selected; }
    QVariantMap selectedClip() const;
    QVariantMap selectedKey() const;
    QString selectedTrackId() const;
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QString tool() const { return m_tool; }
    void setTool(const QString &tool);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    bool snap() const { return m_snap; }
    void setSnap(bool value);
    bool linked() const { return m_linked; }
    void setLinked(bool value);
    bool canUndo() const;
    bool canRedo() const;
    QString error() const { return m_error; }
    bool playing() const {return m_playing;}
    bool preparing() const {return m_playing&&!m_playbackClockStarted;}
    void setPlaying(bool value);
    Q_INVOKABLE void updatePlaybackPosition();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void seek(int frame);
    Q_INVOKABLE bool beginScrub(int frame);
    Q_INVOKABLE void updateScrub(int frame);
    Q_INVOKABLE void finishScrub(int frame);
    Q_INVOKABLE void cancelScrub();
    Q_INVOKABLE void jumpCut(int direction);
    Q_INVOKABLE bool selectClip(const QString &id,bool multiple=false);
    Q_INVOKABLE bool selectTrack(const QString &id);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool selectKey(const QString &id);
    Q_INVOKABLE QUrl thumbnail(const QString &layerId,int frame) const;
    Q_INVOKABLE QUrl requestThumbnail(const QString &layerId,int frame);
    Q_INVOKABLE void cancelThumbnails();
    Q_INVOKABLE bool setRange(const QString &id,int in,int out);
    Q_INVOKABLE bool trim(const QString &id,const QString &edge,int delta);
    Q_INVOKABLE bool moveClip(const QString &id,int delta);
    Q_INVOKABLE bool toolAvailable(const QString &tool) const;
    Q_INVOKABLE bool dragClip(const QString &id,int delta,const QString &edge=QString(),int snapTolerance=0);
    Q_INVOKABLE bool beginDrag(const QString &id,const QString &edge,double pointerX,double pixelsPerFrame,int snapTolerance=0);
    Q_INVOKABLE bool beginKeyDrag(const QString &id,double pointerX,double pixelsPerFrame,int snapTolerance=0);
    Q_INVOKABLE bool updateDrag(double pointerX);
    Q_INVOKABLE bool finishDrag();
    Q_INVOKABLE void cancelDrag();
    Q_INVOKABLE void markRange(int first,int last);
    Q_INVOKABLE void selectMarkedRange();
    Q_INVOKABLE bool setSourceFrame(const QString &id,int frame);
    Q_INVOKABLE bool setClipVisible(const QString &id,bool visible);
    Q_INVOKABLE bool setTrackMuted(const QString &id,bool muted);
    Q_INVOKABLE bool setTrackLocked(const QString &id,bool locked);
    Q_INVOKABLE bool setTrackSolo(const QString &id);
    Q_INVOKABLE bool addLayer(const QString &kind,const QString &name=QString());
    Q_INVOKABLE bool addAudioTrack();
    Q_INVOKABLE bool renameTrack(const QString &id,const QString &name);
    Q_INVOKABLE bool moveTrack(const QString &id,int direction);
    Q_INVOKABLE bool removeTrack(const QString &id);
    Q_INVOKABLE bool split();
    Q_INVOKABLE bool duplicateSelected();
    Q_INVOKABLE bool removeSelected();
    Q_INVOKABLE bool rippleDelete();
    Q_INVOKABLE bool moveKey(const QString &id,int frame);
    Q_INVOKABLE bool setKeyOpacity(const QString &id,double value);
    Q_INVOKABLE bool removeKey(const QString &id);
    Q_INVOKABLE bool importFrameSource(const QString &id,int frame,const QUrl &source);
    Q_INVOKABLE bool setFrameRate(int numerator,int denominator=1);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
signals:
    void changed();
    void projectionChanged();
    void layoutChanged();
    void playheadChanged();
    void scrubbingChanged();
    void dragPreviewChanged();
    void thumbnailsChanged();
    void thumbnailReady(const QString &layerId);
    void playbackChanged();
    void playbackFailed(const QString &message);
private:
    void navigateFrame(int frame,bool fromScrub=false);
    void flushScrub();
    bool discardScrub();
    void bindPlayback();
    void endPlayback(bool stopAudio=true,bool notify=true);
    void beginPlaybackClock(qulonglong id);
    void rejectPlayback(const QString &message);
    void invalidateThumbnails();
    void updateLocks();
    void updateLayout();
    int snappedDelta(const QString &id,int delta,const QString &edge,int tolerance) const;
    const iiSharedCanvas::Document *document() const;
    bool mutate(const std::function<bool(iiSharedCanvas::Document &)> &edit);
    bool reject(const QString &error);
    bool locked(const QString &id) const;
    QPointer<EditorCanvas> m_canvas;
    QPointer<EditorAudioPlayback> m_playback,m_overridePlayback;
    QTimer m_playbackTimer;
    QTimer m_scrubTimer;
    int m_scrubInitial=0,m_scrubPending=-1;
    bool m_scrubbing=false,m_advancingScrub=false;
    qulonglong m_scrubSerial=0;
    QElapsedTimer m_playbackClock;
    qulonglong m_playbackId=0,m_playbackSerial=0;
    int m_playbackFirstFrame=0;
    bool m_playing=false,m_playbackClockStarted=false,m_audioClock=false,m_advancingPlayback=false;
    QVariantList m_tracks,m_clips,m_keys;
    QVariantList m_trackLayout;
    int m_laneHeight=126,m_rulerHeight=44,m_layerCount=0,m_keptCount=0;
    bool m_hasAudio=false;
    double m_projectedFps=24;
    int m_projectedFrames=1;
    QString m_dragId,m_dragEdge;
    double m_dragPointer=0,m_dragPixelsPerFrame=1;
    int m_dragDelta=0,m_dragTolerance=0;
    bool m_dragValid=false;
    QVariantMap m_dragPreview;
    QVariantMap m_dragClip;
    QVariantMap m_dragKey,m_keyDragPreview;
    std::vector<int> m_dragOccupiedKeys;
    QVariantList m_dragRows;
    std::vector<std::pair<int,std::size_t>> m_dragSnapAnchors;
    bool m_dragToolAvailable=false;
    QStringList m_selected;
    QString m_selectedKey,m_selectedTrack,m_mode="Layers",m_tool="Select",m_filter="All",m_error,m_solo;
    QMap<QString,bool> m_soloVisibility;
    QSet<QString> m_audioLocks;
    bool m_snap=true,m_linked=true,m_refreshing=false,m_strokeRefreshPending=false;
    int m_markIn=0,m_markOut=1;
    mutable QCache<QString,QUrl> m_thumbnails{128};
    std::unique_ptr<EditorTimelineThumbnails> m_thumbnailWorker;
    std::shared_ptr<const iiSharedCanvas::Document> m_thumbnailSnapshot;
    QHash<QString,QString> m_requestedThumbnails;
    qulonglong m_thumbnailGeneration=0,m_thumbnailRevision=0;
};
