#pragma once
#include <iiSharedCanvas/QtAdapter/CanvasItem.h>
#include <QVariantMap>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <iiSharedCanvas/Bitmap/BitmapProcessing.h>
#include <QSet>
#include <iiSocietyHelper.h>
#include <QPainterPath>
#include <QImage>
#include <QElapsedTimer>
#include <QTransform>
#include <optional>
#include <Application/MaskPaintSession.h>
class EditorMedia;
class EditorCameraPhoto;
class QHoverEvent;
class QKeyEvent;
class EditorCanvasFeedback;

class EditorCanvas : public iiSharedCanvas::CanvasItem {
    friend class EditorTimelineModel;
    friend class EditorCanvasFeedback;
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantMap specification READ specification NOTIFY specificationChanged)
    Q_PROPERTY(QVariantList layers READ layers NOTIFY documentInfoChanged)
    Q_PROPERTY(QVariantMap selectedLayer READ selectedLayer NOTIFY documentInfoChanged)
    Q_PROPERTY(QString documentName READ documentName NOTIFY documentInfoChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentInfoChanged)
    Q_PROPERTY(QVariantMap fileAlias READ fileAlias NOTIFY documentInfoChanged)
    Q_PROPERTY(QVariantMap fileInformation READ fileInformation NOTIFY fileInformationChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString selectedLayerId READ selectedLayerId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap layerPanelInfo READ layerPanelInfo NOTIFY documentInfoChanged)
    Q_PROPERTY(QVariantMap layerColorSample READ layerColorSample NOTIFY toolStateChanged)
    Q_PROPERTY(QVariantMap maskPanelInfo READ maskPanelInfo NOTIFY maskPanelChanged)
    Q_PROPERTY(QUrl selectionOverlay READ selectionOverlay NOTIFY selectionOverlayChanged)
    Q_PROPERTY(QString toolHint READ toolHint NOTIFY toolStateChanged)
    Q_PROPERTY(QVariantMap toolState READ toolState NOTIFY toolStateChanged)
    Q_PROPERTY(QString generationSeed READ generationSeed NOTIFY documentInfoChanged)
    Q_PROPERTY(bool previewMirrorX READ previewMirrorX NOTIFY toolStateChanged)
    Q_PROPERTY(bool previewMirrorY READ previewMirrorY NOTIFY toolStateChanged)
    Q_PROPERTY(QObject *media READ media CONSTANT)
    Q_PROPERTY(QObject *photoSources READ photoSources CONSTANT)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoRedoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoRedoChanged)
    Q_PROPERTY(int activeArtboardIndex READ activeArtboardIndex NOTIFY activeArtboardChanged)
    Q_PROPERTY(int editingWidth READ editingWidth NOTIFY documentInfoChanged)
    Q_PROPERTY(int editingHeight READ editingHeight NOTIFY documentInfoChanged)
    Q_PROPERTY(QPointF editingOrigin READ editingOrigin NOTIFY documentInfoChanged)
    Q_PROPERTY(bool pagePresentation READ pagePresentation WRITE setPagePresentation NOTIFY pagePresentationChanged)
    Q_PROPERTY(QVariantList elementCandidates READ elementCandidates NOTIFY elementSelectionChanged)
    Q_PROPERTY(int elementSelectionCount READ elementSelectionCount NOTIFY elementSelectionChanged)
    Q_PROPERTY(QVariantMap selectedElementSettings READ selectedElementSettings NOTIFY elementSelectionChanged)
    Q_PROPERTY(QVariantMap elementTransform READ elementTransform NOTIFY elementTransformChanged)
    Q_PROPERTY(QVariantMap interactionFeedback READ interactionFeedback NOTIFY interactionFeedbackChanged)
public:
    explicit EditorCanvas(QQuickItem *parent = nullptr);
    ~EditorCanvas() override;
    // CanvasItem intentionally blocks mutable document access for working files.
    // All consumer reads use its const view; accepted writes go through SDK
    // transactions. A transform previews only the owned detached aggregate.
    const iiSharedCanvas::Document *document() const noexcept { return CanvasItem::document(); }
    QVariantMap specification() const { return m_specification; }
    QVariantList layers() const;
    QVariantMap selectedLayer() const;
    QString documentName() const;
    QString filePath() const { return m_documentAlias.value("path").toString(); }
    QVariantMap fileAlias() const { return m_documentAlias; }
    QVariantMap fileInformation() const;
    bool documentSynchronized() const { return m_documentSynchronized; }
    QString error() const;
    QString selectedLayerId() const;
    bool loadProjectPage(iiSharedCanvas::Document document, const QString &name) {
        return adopt(std::move(document), {{"name", name}});
    }
    bool applyArtboardImport(iiSharedCanvas::Document document) {
        return commit([&](iiSharedCanvas::Document &draft) { draft = std::move(document); return true; });
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
    Q_INVOKABLE bool applyGraphicOperation(const QString &tool, const QString &operation, const QVariantMap &parameters);
    Q_INVOKABLE QVariantMap previewGraphicOperation(const QString &tool, const QString &operation, const QVariantMap &parameters);
    Q_INVOKABLE QVariantMap graphicOperationState(const QString &operation) const;
    Q_INVOKABLE bool createElement(const QRectF &bounds);
    QVariantList elementCandidates() const;
    int elementSelectionCount() const;
    QVariantMap selectedElementSettings() const;
    QVariantMap elementTransform() const;
    QVariantMap interactionFeedback() const;
    Q_INVOKABLE bool beginElementTransform(const QString &handle, const QPointF &itemPosition, int modifiers = 0);
    Q_INVOKABLE bool updateElementTransform(const QPointF &itemPosition, int modifiers = 0);
    Q_INVOKABLE bool endElementTransform(const QPointF &itemPosition, int modifiers = 0);
    Q_INVOKABLE void cancelElementTransform();
    Q_INVOKABLE bool selectElement(const QString &id);
    Q_INVOKABLE bool setElementSelected(const QString &id, bool checked, bool multiple = true);
    Q_INVOKABLE bool selectAllElements();
    Q_INVOKABLE bool invertElementSelection();
    Q_INVOKABLE bool clearElementSelection();
    Q_INVOKABLE bool createText(const QPointF &position);
    QVariantMap maskPanelInfo() const;
    Q_INVOKABLE bool selectBackgroundMask(const QString &id);
    Q_INVOKABLE bool createBackgroundMask();
    Q_INVOKABLE bool duplicateBackgroundMask();
    Q_INVOKABLE bool deleteBackgroundMask();
    Q_INVOKABLE bool setBackgroundMaskProperty(const QString &field, const QVariant &value);
    Q_INVOKABLE bool backgroundMaskAction(const QString &action);
    Q_INVOKABLE bool beginBackgroundMaskGesture(const QPointF &point, qreal pressure = 1);
    Q_INVOKABLE bool continueBackgroundMaskGesture(const QPointF &point, qreal pressure = 1);
    Q_INVOKABLE bool endBackgroundMaskGesture(const QPointF &point, qreal pressure = 1);
    Q_INVOKABLE void cancelBackgroundMaskGesture();
    Q_INVOKABLE bool applyBackgroundFill(const QVariantMap &values);
    Q_INVOKABLE QVariantMap previewBackgroundRemoval(const QVariantMap &values);
    Q_INVOKABLE bool applyBackgroundRemoval(const QVariantMap &values);
    Q_INVOKABLE bool captureBackgroundClipboard();
    Q_INVOKABLE bool applyBackgroundReplacement(const QUrl &source, const QVariantMap &values);
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
    QObject *photoSources() const;
    bool canUndo() const { return m_historyIndex > 0; }
    bool canRedo() const { return m_historyIndex + 1 < int(m_history.size()); }
    int activeArtboardIndex() const;
    int editingWidth() const;
    int editingHeight() const;
    QPointF editingOrigin() const;
    bool pagePresentation() const { return m_pagePresentation; }
    void setPagePresentation(bool enabled);
    bool selectArtboard(int index, bool reveal = true);
    Q_INVOKABLE void fitToView();
    Q_INVOKABLE void scrollHorizontally(qreal pixels);
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
    void selectionOverlayChanged();
    void maskPanelChanged();
    void specificationChanged();
    void documentInfoChanged();
    void fileInformationChanged();
    void errorChanged();
    void toolStateChanged();
    void historyRestored(QVariantMap settings);
    void activeArtboardChanged();
    void pagePresentationChanged();
    void elementSelectionChanged();
    void elementTransformChanged();
    void interactionFeedbackChanged();
protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverEnterEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
private:
    void updatePointerFeedback(const QPointF &position, bool inside = true);
    void cancelInteractionGesture();
    void notifyInteractionFeedback();
    QPainterPath selectionGesturePath(const QVector<QPointF> &points, QRectF *geometryBounds = nullptr) const;
    QPainterPath elementCreationPath() const;
    QRectF elementCreationBounds() const;
    QPainterPath gestureFeedbackPath() const;
    QPainterPath brushFeedbackPath() const;
    QTransform feedbackTransform() const;
    void paintInteractionFeedback(QPainter *painter, qreal dashOffset) const;
    void updateSelectionOutline();
    QPointF m_feedbackPointer;
    bool m_feedbackPointerInside = false;
    bool m_primaryPointerDown = false;
    bool m_navigatingCanvas = false;
    std::optional<iiFileProvider::Authorship> m_strokeAuthorship;
    QPainterPath m_selectionOutline;
    void constrainViewport();
    const iiSharedCanvas::Artboard *activeArtboard() const;
    bool belongsToActiveArtboard(const iiSharedCanvas::Layer &layer) const;
    iiSharedCanvas::CanvasExtent editingExtent() const;
    iiSharedCanvas::FrameRenderResult renderEditingFrame(const iiSharedCanvas::Document &document) const;
    QPointF editingPosition(const QPointF &itemPosition) const;
    bool selectArtboardAt(const QPointF &itemPosition);
    void resizeEditingCanvas(iiSharedCanvas::Document &document, iiSharedCanvas::CanvasExtent extent);
    std::string backgroundLayerId() const;
    std::string m_activeArtboard;
    bool m_pagePresentation = false;
    bool m_constrainingViewport = false;
    bool fail(const QString &message);
    void clearError();
    bool adopt(iiSharedCanvas::Document document, const QVariantMap &specification);
    bool commit(const std::function<bool(iiSharedCanvas::Document &)> &edit);
    bool ensurePaintLayer();
    bool applyPixelTool(const QString &tool, const QString &field, const QVariant &value);
    QString m_previewGraphicOperation;
    bool updateElement();
    QRectF elementLocalBounds(const QString &id) const;
    QTransform elementWorldTransform(const QString &id) const;
    QString elementAt(const QPointF &itemPosition) const;
    QString elementHandleAt(const QPointF &itemPosition) const;
    QPointF elementWorldPosition(const QPointF &itemPosition) const;
    QPointF elementItemPosition(const QPointF &worldPosition) const;
    bool resizeSelectedElement(const QVariant &size);
    bool elementKeyEvent(QKeyEvent *event);
    void updateElementCursor(const QPointF &itemPosition);
    void synchronizeElementSelection(bool replaceWithActive);
    void restoreSelectedElementSettings();
    bool applyElementSelection(const QSet<QString> &ids, const QString &active = {});
    bool updateText();
    bool updateBackground();
    bool transformCanvas(const QVariantMap &values, bool mirror);
    bool backgroundMaskActive() const;
    int backgroundMaskIndex() const;
    iiSharedCanvas::RasterMask backgroundMaskPixels() const;
    iiSharedCanvas::RasterMask backgroundMaskToLayer(const iiSharedCanvas::RasterMask &) const;
    bool completeBackgroundMask(iiSharedCanvas::RasterMask, const QString &kind, bool replaceCurrent = false);
    bool completeBackgroundMaskShape(const QVector<QPointF> &points);
    bool detectBackgroundMask(bool sky);
    void refreshBackgroundMaskOverlay();
    QString m_selectedBackgroundMask;
    QString m_maskGestureTarget;
    QString m_maskGestureTool;
    QVector<QPointF> m_maskGesturePoints, m_maskPolygon;
    MaskPaintSession m_maskPainter;
    PointerDeviceKind m_maskInputDevice = PointerDeviceKind::Mouse;
    PointerDeviceKind m_maskStrokeDevice = PointerDeviceKind::Mouse;
    bool m_maskGestureActive = false;
    QUrl m_backgroundMaskOverlay;
    QElapsedTimer m_maskClock;
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
    void updateElementHistoryMetadata();
    void resetHistory();
    bool restoreHistory(int index);
    QVariantMap settingsFor(const QString &tool) const;
    bool alphaProtected(const QString &id) const { return m_settings.value("layer-alpha:" + id).value("enabled").toBool(); }
    void restoreView(qreal zoom, qreal x, qreal y, quint32 frame, const QString &selection);
    std::vector<iiSharedCanvas::Document> m_history;
    std::vector<QMap<QString, QVariantMap>> m_historySettings;
    std::vector<QMap<QString, QRectF>> m_historyElementBounds;
    std::vector<QMap<QString, QVariantMap>> m_historyElementSettings;
    std::vector<QMap<QString, QImage>> m_historyElementPatterns;
    int m_historyIndex = -1;
    bool m_historySuspended = false;
    bool m_previewMirrorX = false, m_previewMirrorY = false;
    iiSharedCanvas::Document m_canvas;
    iiSocietyHelper::FileSystem m_documentStorage;
    QVariantMap m_documentAlias;
    QString m_documentVersion;
    mutable QString m_fileInspectionKey;
    mutable QString m_fileStorageFormat;
    mutable QString m_fileStoredRevision;
    bool m_documentSynchronized = true;
    bool m_synchronizingDocument = false;
    bool synchronizeDocument(const iiSharedCanvas::Document &document);
    iiSharedCanvas::DocumentEditResult editDocument(
        const std::function<iiSharedCanvas::DocumentEditResult(iiSharedCanvas::DocumentEditor &)> &edit);
    QVariantMap m_specification;
    QString m_error;
    QString m_selectedDocumentLayer;
    QString m_tool = QStringLiteral("elements");
    QVariantMap m_toolValues;
    bool m_creatingElement = false;
    QPointF m_elementStart;
    QPointF m_elementCurrent;
    QString m_elementTransformId, m_elementTransformHandle;
    QRectF m_elementTransformBounds;
    QPointF m_elementTransformStart;
    QPointF m_elementTransformPosition;
    QTransform m_elementTransformBase, m_elementTransformSample;
    bool m_elementTransformChanged = false;
    QMap<QString, QVariantMap> m_settings;
    QMap<QString, QRectF> m_elementBounds;
    QMap<QString, QVariantMap> m_elementSettings;
    QMap<QString, QImage> m_elementPatterns;
    // UI selection is transient. Only one of these IDs is CanvasItem's active layer.
    QSet<QString> m_selectedElements;
    bool m_changingElementSelection = false;
    QString m_elementObservedTool = QStringLiteral("elements");
    QMap<QString, QPointF> m_textOrigins;
    QMap<QString, QVariantMap> m_textSettings;
    iiSharedCanvas::RasterMask m_selection;
    iiSharedCanvas::RasterMask m_backgroundRemovalMask;
    QString m_backgroundRemovalTarget;
    qulonglong m_backgroundRemovalRevision = 0;
    QImage m_backgroundClipboard;
    QUrl m_selectionOverlay;
    QString m_previewTool, m_previewLayer;
    RasterLayer m_previewPixels;
    QSet<QString> m_touchedFields, m_pixelLocks, m_positionLocks;
    qulonglong m_previewRevision = 0;
    QVector<QPointF> m_gesture;
    bool m_gestureActive = false, m_pickCloneSource = false, m_pickNeutral = false;
    bool m_layerColorPicking = false, m_layerColorValid = false;
    int m_layerSampleSequence = 0;
    QVariantMap m_graphicMaskOperand;
    QString m_graphicMaskLayer;
    qulonglong m_graphicDocumentSequence = 0, m_graphicMaskDocumentSequence = 0;
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
    std::string m_generationArtboard;
    QVariantMap m_backgroundGenerationValues;
    std::unique_ptr<EditorMedia> m_media;
    std::unique_ptr<EditorCameraPhoto> m_photoSources;
};
