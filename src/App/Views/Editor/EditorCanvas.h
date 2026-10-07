#pragma once
#include <iiSharedCanvas/QtAdapter/CanvasItem.h>
#include <QVariantMap>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <iiSharedCanvas/Bitmap/BitmapProcessing.h>
#include <QSet>
#include <QPainterPath>
#include <QImage>
#include <QElapsedTimer>
class EditorMedia;

class EditorCanvas : public iiSharedCanvas::CanvasItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap specification READ specification NOTIFY specificationChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY documentInfoChanged)
    Q_PROPERTY(QVariantMap selectedLayer READ selectedLayer NOTIFY documentInfoChanged)
    Q_PROPERTY(QString documentName READ documentName NOTIFY documentInfoChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString selectedLayerId READ selectedLayerId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap layerPanelInfo READ layerPanelInfo NOTIFY documentInfoChanged)
    Q_PROPERTY(QVariantMap layerColorSample READ layerColorSample NOTIFY toolStateChanged)
    Q_PROPERTY(QUrl selectionOverlay READ selectionOverlay NOTIFY toolStateChanged)
    Q_PROPERTY(QString toolHint READ toolHint NOTIFY toolStateChanged)
    Q_PROPERTY(QVariantMap toolState READ toolState NOTIFY toolStateChanged)
    Q_PROPERTY(QString generationSeed READ generationSeed NOTIFY documentInfoChanged)
    Q_PROPERTY(bool previewMirrorX READ previewMirrorX NOTIFY toolStateChanged)
    Q_PROPERTY(bool previewMirrorY READ previewMirrorY NOTIFY toolStateChanged)
    Q_PROPERTY(QObject *media READ media CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoRedoChanged)
public:
    explicit EditorCanvas(QQuickItem *parent = nullptr);
    ~EditorCanvas() override;
    // CanvasItem intentionally blocks mutable document access for working files.
    // All consumer reads use its const view; writes go through SDK transactions.
    const iiSharedCanvas::Document *document() const noexcept { return CanvasItem::document(); }
    QVariantMap specification() const { return m_specification; }
    QVariantList layers() const;
    QVariantMap selectedLayer() const;
    QString documentName() const;
    QString error() const;
    QString selectedLayerId() const;
    bool loadProjectPage(iiSharedCanvas::Document document, const QString &name) {
        return adopt(std::move(document), {{"name", name}});
    }
    Q_INVOKABLE bool selectLayer(const QString &id);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool createCanvas(const QVariantMap &specification);
    Q_INVOKABLE bool openDocumentSource(const QUrl &source, bool asCopy = false);
    Q_INVOKABLE bool openImages(const QVariantList &sources);
    Q_INVOKABLE QUrl localFileSource(const QString &path) const { return QUrl::fromLocalFile(path); }
    Q_INVOKABLE QUrl localDirectory(const QString &path) const;
    Q_INVOKABLE bool saveDocumentAs(const QUrl &destination);
    Q_INVOKABLE bool saveDocument();
    Q_INVOKABLE bool placeImage(const QUrl &source, const QString &placement = QStringLiteral("Fit"));
    Q_INVOKABLE bool addPaintLayer();
    Q_INVOKABLE bool setLayerVisible(const QString &id, bool visible);
    Q_INVOKABLE bool setLayerOpacity(const QString &id, double opacity);
    Q_INVOKABLE bool setLayerName(const QString &id, const QString &name);
    Q_INVOKABLE bool setLayerBlend(const QString &id, const QString &blend);
    Q_INVOKABLE bool setLayerTransform(const QString &id, const QVariantMap &transform);
    Q_INVOKABLE bool removeLayer(const QString &id);
    Q_INVOKABLE QVariantList layerHierarchy() const;
    QVariantMap layerPanelInfo() const;
    Q_INVOKABLE QVariantMap layerHistogram(bool composite = true, const QString &channel = QStringLiteral("RGB")) const;
    Q_INVOKABLE bool duplicateSelectedLayer();
    Q_INVOKABLE bool groupSelectedLayer();
    Q_INVOKABLE bool setLayerGroupVisible(const QString &id, bool visible);
    Q_INVOKABLE bool mergeSelectedDown();
    Q_INVOKABLE bool beginLayerColorSample();
    Q_INVOKABLE bool sampleLayerColor(const QPointF &position);
    Q_INVOKABLE void cancelLayerColorSample();
    QVariantMap layerColorSample() const;
    Q_INVOKABLE bool copyLayerPanelDetails();
    Q_INVOKABLE bool copyLayerSampleColor();
    Q_INVOKABLE bool revealLayerSource();
    Q_INVOKABLE void setLayerHistogramClipping(bool shadows, bool highlights);
    Q_INVOKABLE void configureTool(const QString &tool, const QVariantMap &values);
    Q_INVOKABLE bool applyToolField(const QString &tool, const QString &field, const QVariant &value);
    Q_INVOKABLE bool createElement(const QRectF &bounds);
    Q_INVOKABLE bool createText(const QPointF &position);
    Q_INVOKABLE bool selectArea(const QVariantList &points);
    Q_INVOKABLE bool fillAt(const QPointF &position);
    Q_INVOKABLE bool executeToolAction(const QString &tool, const QString &field, const QVariantMap &values);
    Q_INVOKABLE bool importToolSource(const QUrl &, const QString &tool, const QVariantMap &values);
    Q_INVOKABLE QVariantMap toolPreview(const QString &tool, const QString &field, const QVariantMap &values) const;
    Q_INVOKABLE QVariantMap toolControlState(const QString &tool, const QString &field, const QVariantMap &values) const;
    Q_INVOKABLE QVariantMap generationParameters(const QString &operation, const QVariantMap &values);
    Q_INVOKABLE bool placeGenerated(const QUrl &source, const QString &operation);
    Q_INVOKABLE void clearAreaSelection();
    Q_INVOKABLE bool rasterizeSelected();
    Q_INVOKABLE bool insertPixels(const QImage &, const QString &name, bool background = false);
    QUrl selectionOverlay() const;
    QString toolHint() const;
    QVariantMap toolState() const;
    QString generationSeed() const;
    bool previewMirrorX() const { return m_previewMirrorX; }
    bool previewMirrorY() const { return m_previewMirrorY; }
    QObject *media() const;
    bool canUndo() const { return m_historyIndex > 0; }
    bool canRedo() const { return m_historyIndex + 1 < int(m_history.size()); }
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QVariantMap toolValues(const QString &tool) const { return m_settings.value(tool); }
    Q_INVOKABLE bool resetTool(const QString &tool, const QVariantMap &defaults);
    Q_INVOKABLE bool playAudio();
    Q_INVOKABLE bool playTimelineAudio();
    Q_INVOKABLE bool moveVectorPath(int index, qreal x, qreal y);
    Q_INVOKABLE bool exportImage(const QUrl &, const QString &format);
    Q_INVOKABLE bool endStrokeAt(const QPointF &position, qreal pressure = 1.0);
    Q_INVOKABLE bool beginStrokeAt(const QPointF &position, qreal pressure = 1.0);
    Q_INVOKABLE bool continueStrokeAt(const QPointF &position, qreal pressure = 1.0);
signals:
    void specificationChanged();
    void documentInfoChanged();
    void errorChanged();
    void toolStateChanged();
    void historyRestored(QVariantMap settings);
protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
private:
    void constrainViewport();
    bool m_constrainingViewport = false;
    bool fail(const QString &message);
    void clearError();
    bool adopt(iiSharedCanvas::Document document, const QVariantMap &specification);
    bool commit(const std::function<bool(iiSharedCanvas::Document &)> &edit);
    bool ensurePaintLayer();
    bool applyPixelTool(const QString &tool, const QString &field, const QVariant &value);
    bool updateElement();
    bool updateText();
    bool updateBackground();
    bool transformCanvas(const QVariantMap &values, bool mirror);
    bool applySelectionMask(const QVariantMap &values);
    bool semanticSelection(const QVariantMap &values, bool background);
    bool retouchAt(const QPointF &position);
    bool repairSelection();
    bool eraseVector(const QVariantMap &values);
    bool combineVectorPaths(const QString &operation);
    QUrl clippingOverlay() const;
    std::optional<iiSharedCanvas::RasterMask> expandedObjectMask(const iiSharedCanvas::RasterMask &mask, double radius) const;
    QPointF brushInputPosition(const QPointF &position, bool finish);
    bool updateAudio(const QVariantMap &values);
    iiSharedCanvas::RasterMask layerMask() const;
    void updateSelectionOverlay();
    void invalidateToolPreview();
    void recordHistory();
    void resetHistory();
    bool restoreHistory(int index);
    QVariantMap settingsFor(const QString &tool) const;
    bool alphaProtected(const QString &id) const { return m_settings.value("layer-alpha:" + id).value("enabled").toBool(); }
    void restoreView(qreal zoom, qreal x, qreal y, quint32 frame, const QString &selection);
    std::vector<iiSharedCanvas::Document> m_history;
    std::vector<QMap<QString, QVariantMap>> m_historySettings;
    int m_historyIndex = -1;
    bool m_historySuspended = false;
    bool m_previewMirrorX = false, m_previewMirrorY = false;
    iiSharedCanvas::Document m_canvas;
    std::unique_ptr<iiSharedCanvas::DocumentFile> m_workingFile;
    QVariantMap m_specification;
    QString m_error;
    QString m_selectedDocumentLayer;
    QString m_tool = QStringLiteral("elements");
    QVariantMap m_toolValues;
    bool m_creatingElement = false;
    QPointF m_elementStart;
    QMap<QString, QVariantMap> m_settings;
    QMap<QString, QRectF> m_elementBounds;
    QMap<QString, QVariantMap> m_elementSettings;
    QMap<QString, QPointF> m_textOrigins;
    QMap<QString, QVariantMap> m_textSettings;
    iiSharedCanvas::RasterMask m_selection;
    QUrl m_selectionOverlay;
    QString m_previewTool, m_previewLayer;
    RasterLayer m_previewPixels;
    QSet<QString> m_touchedFields, m_pixelLocks, m_positionLocks;
    qulonglong m_previewRevision = 0;
    QVector<QPointF> m_gesture;
    bool m_gestureActive = false, m_pickCloneSource = false, m_pickNeutral = false;
    bool m_layerColorPicking = false, m_layerColorValid = false;
    bool m_histogramShadows = false, m_histogramHighlights = false;
    QColor m_layerSampleColor;
    QPointF m_cloneSource, m_retouchStart;
    bool m_cloneReady = false;
    QUrl m_eraserOverlay;
    RasterLayer m_strokePixels;
    RasterLayer m_restorePixels;
    QString m_restoreLayer;
    mutable QString m_clippingKey;
    mutable QUrl m_clippingImage;
    QPointF m_brushOrigin, m_brushFiltered, m_brushPreviousInput;
    QElapsedTimer m_brushClock;
    qint64 m_brushLastInput = 0;
    iiSharedCanvas::AudioAsset m_audioSource;
    QString m_audioTrack;
    QString m_lastEditedField;
    QImage m_patternImage;
    QSize m_generationExtent;
    QPointF m_generationOffset;
    iiSharedCanvas::RasterMask m_generationMask;
    QString m_generationLayer;
    std::unique_ptr<EditorMedia> m_media;
};
