#pragma once
#include "EditorCanvas.h"
#include <QCache>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

// Projection and commands for the open native document. No second layer list
// or timing recipe is persisted by this model.
class EditorTimelineModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(EditorCanvas *canvas READ canvas WRITE setCanvas NOTIFY changed)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY changed)
    Q_PROPERTY(QVariantList keys READ keys NOTIFY changed)
    Q_PROPERTY(double fps READ fps NOTIFY changed)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY changed)
    Q_PROPERTY(int playhead READ playhead WRITE seek NOTIFY playheadChanged)
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
public:
    explicit EditorTimelineModel(QObject *parent=nullptr);
    EditorCanvas *canvas() const { return m_canvas; }
    void setCanvas(EditorCanvas *canvas);
    QVariantList tracks() const { return m_tracks; }
    QVariantList clips() const { return m_clips; }
    QVariantList keys() const { return m_keys; }
    double fps() const;
    int totalFrames() const;
    int playhead() const;
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
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void seek(int frame);
    Q_INVOKABLE void jumpCut(int direction);
    Q_INVOKABLE bool selectClip(const QString &id,bool multiple=false);
    Q_INVOKABLE bool selectTrack(const QString &id);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool selectKey(const QString &id);
    Q_INVOKABLE QUrl thumbnail(const QString &layerId,int frame) const;
    Q_INVOKABLE bool setRange(const QString &id,int in,int out);
    Q_INVOKABLE bool trim(const QString &id,const QString &edge,int delta);
    Q_INVOKABLE bool moveClip(const QString &id,int delta);
    Q_INVOKABLE bool toolAvailable(const QString &tool) const;
    Q_INVOKABLE bool dragClip(const QString &id,int delta,const QString &edge=QString(),int snapTolerance=0);
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
    void playheadChanged();
private:
    void updateLocks();
    const iiSharedCanvas::Document *document() const;
    bool mutate(const std::function<bool(iiSharedCanvas::Document &)> &edit);
    bool reject(const QString &error);
    bool locked(const QString &id) const;
    QPointer<EditorCanvas> m_canvas;
    QVariantList m_tracks,m_clips,m_keys;
    QStringList m_selected;
    QString m_selectedKey,m_selectedTrack,m_mode="Layers",m_tool="Select",m_filter="All",m_error,m_solo;
    QMap<QString,bool> m_soloVisibility;
    QSet<QString> m_audioLocks;
    bool m_snap=true,m_linked=true,m_refreshing=false;
    int m_markIn=0,m_markOut=1;
    mutable QCache<QString,QUrl> m_thumbnails{128};
};
