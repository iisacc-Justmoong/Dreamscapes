#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <memory>

// Presentation only: no document, file, storage or history owner is reachable.
class EditorTimelineViewport : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int frameCount READ frameCount WRITE setFrameCount NOTIFY geometryChanged)
    Q_PROPERTY(double frameRate READ frameRate WRITE setFrameRate NOTIFY geometryChanged)
    Q_PROPERTY(double viewportWidth READ viewportWidth WRITE setViewportWidth NOTIFY geometryChanged)
    Q_PROPERTY(double viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY geometryChanged)
    Q_PROPERTY(double verticalOffset READ verticalOffset WRITE setVerticalOffset NOTIFY geometryChanged)
    Q_PROPERTY(int rulerHeight READ rulerHeight WRITE setRulerHeight NOTIFY geometryChanged)
    Q_PROPERTY(double contentX READ contentX WRITE setContentX NOTIFY geometryChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY geometryChanged)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY geometryChanged)
    Q_PROPERTY(double pixelsPerFrame READ pixelsPerFrame NOTIFY geometryChanged)
    Q_PROPERTY(int viewFrameCount READ viewFrameCount NOTIFY geometryChanged)
    Q_PROPERTY(int firstVisibleFrame READ firstVisibleFrame NOTIFY geometryChanged)
    Q_PROPERTY(int lastVisibleFrame READ lastVisibleFrame NOTIFY geometryChanged)
    Q_PROPERTY(int majorStep READ majorStep NOTIFY geometryChanged)
    Q_PROPERTY(int minorStep READ minorStep NOTIFY geometryChanged)
    Q_PROPERTY(int snapTolerance READ snapTolerance NOTIFY geometryChanged)
    Q_PROPERTY(QVariantList majorTicks READ majorTicks NOTIFY rulerChanged)
    Q_PROPERTY(QVariantList minorTicks READ minorTicks NOTIFY rulerChanged)
    Q_PROPERTY(QVariantList trackLayout READ trackLayout WRITE setTrackLayout NOTIFY trackLayoutChanged)
    Q_PROPERTY(QVariantMap dragPreview READ dragPreview WRITE setDragPreview NOTIFY dragPreviewChanged)
    Q_PROPERTY(QVariantMap keyDragPreview READ keyDragPreview WRITE setKeyDragPreview NOTIFY dragPreviewChanged)
    Q_PROPERTY(QVariantList visibleTracks READ visibleTracks NOTIFY visibleTracksChanged)
public:
    explicit EditorTimelineViewport(QObject *parent = nullptr);
    ~EditorTimelineViewport() override;
    int frameCount() const;
    double frameRate() const;
    double viewportWidth() const;
    double viewportHeight() const;
    double verticalOffset() const;
    int rulerHeight() const;
    double contentX() const;
    double zoom() const;
    double contentWidth() const;
    double pixelsPerFrame() const;
    int viewFrameCount() const;
    int firstVisibleFrame() const;
    int lastVisibleFrame() const;
    int majorStep() const;
    int minorStep() const;
    int snapTolerance() const;
    QVariantList majorTicks() const;
    QVariantList minorTicks() const;
    QVariantList trackLayout() const;
    QVariantMap dragPreview() const;
    QVariantMap keyDragPreview() const;
    QVariantList visibleTracks() const;
    void setFrameCount(int value);
    void setFrameRate(double value);
    void setViewportWidth(double value);
    void setViewportHeight(double value);
    void setVerticalOffset(double value);
    void setRulerHeight(int value);
    void setContentX(double value);
    void setZoom(double value);
    void setTrackLayout(const QVariantList &value);
    void setDragPreview(const QVariantMap &value);
    void setKeyDragPreview(const QVariantMap &value);
    Q_INVOKABLE int frameAt(double contentPosition) const;
    Q_INVOKABLE QString frameLabel(int frame) const;
    Q_INVOKABLE double boundedScroll(double value) const;
    // Return the new scroll offset after changing zoom. The Flickable owns scroll.
    Q_INVOKABLE double zoomAt(double factor, double viewportAnchor);
    Q_INVOKABLE double edgeScroll(double viewportPointer, double elapsedMs) const;
    Q_INVOKABLE QVariantMap hitTest(double contentPosition, double y) const;
    Q_INVOKABLE QVariantList curveKeys(const QString &trackId) const;
signals:
    void geometryChanged();
    void rulerChanged();
    void trackLayoutChanged();
    void dragPreviewChanged();
    void visibleTracksChanged();
private:
    void updatePresentation();
    class Private;
    std::unique_ptr<Private> d;
};
