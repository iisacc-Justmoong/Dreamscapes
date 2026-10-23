#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <QMouseEvent>
#include <QCursor>
#include <QPainter>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QKeyEvent>
#include <QTransform>
using namespace iiSharedCanvas;
using namespace editorTools;
void EditorCanvas::configureTool(const QString &tool, const QVariantMap &values) {
    if (m_tool == tool && m_toolValues == values && m_settings.value(tool) == values
        && ((tool != "brush" && tool != "eraser") || liveStrokeActive())) return;
    if(tool=="background" && m_tool==tool && m_toolValues==values && m_settings.value(tool)==values) return;
    if(m_tool=="background" && (tool!=m_tool || m_toolValues!=values)) cancelBackgroundMaskGesture();
    // Deferred presentation updates may repeat an already accepted Elements
    // draft while the pointer is creating a shape. Preserve that gesture.
    if (tool == "elements" && m_tool == tool && m_toolValues == values && m_settings.value(tool) == values) return;
    if (tool == "elements" && m_tool == tool && !m_elementTransformHandle.isEmpty()) {
        const auto selected = selectedElementSettings();
        bool matchesSelection = !selected.isEmpty();
        for (auto it = selected.cbegin(); it != selected.cend(); ++it)
            matchesSelection &= values.value(it.key()) == it.value();
        if (matchesSelection) return;
    }
    cancelElementTransform();
    cancelInteractionGesture();
    if (m_tool != tool || (m_colorRead == ColorRead::Neutral && tool == "color" && m_settings.value(tool) != values)) { retireColorRead(); m_colorReadError.clear(); }
    if (liveStrokeActive()) { cancelStroke(); m_historySuspended = false; m_strokePixels = {}; }
    if (m_tool != tool) { if (m_gestureActive) { m_gestureActive = false; m_historySuspended = false; recordHistory(); } invalidateToolPreview(); }
    m_creatingElement = false; m_tool = tool; m_toolValues = values; m_settings[tool] = values;
    clearBrushEngineState(); setToolMode(tool == "brush" || (tool == "eraser" && values.value("selector", "Pixel").toString() == "Pixel") ? tool : QStringLiteral("pan"));
    if (tool == "brush") {
        setBrushSize(number(values, 4, 8) / (toggle(values, 6) ? zoom() : 1));
        setBrushHardness(number(values, 5, 100) / 100); setBrushOpacity(number(values, 8, 100) / 100); setBrushFlow(number(values, 9, 100) / 100);
        setBrushSpacingRatio(number(values, 16, 15) / 100);
        setStabilizerStrength(std::clamp(number(values, 17) / 100, 0.0, 1.0));
        setPressureToOpacityEnabled(toggle(values, 13, true));
        BrushState state; state.rasterizer.blendMode = blend(text(values, 11));
        const auto shape = text(values, 7, "Round"); state.rasterizer.shape.kind = shape == "Square" ? BrushTipShape::Square : shape == "Diamond" ? BrushTipShape::Diamond : BrushTipShape::Round;
        state.dynamics.pressureToSizeEnabled = true; state.dynamics.pressureToSize = number(values, 12, 100) / 100;
        state.dynamics.pressureToOpacity = number(values, 13, 100) / 100;
        state.dynamics.pressureToFlow = 1;
        if (toggle(values, 15) && toggle(values, 12)) { state.dynamics.sizeResponse.enabled = true; state.dynamics.sizeResponse.pressure = {true, 1, 0.5, 0.05}; state.dynamics.pressureToSize = 0; }
        state.rasterizer.color.enabled = number(values, 22) > 0; state.rasterizer.color.hueJitter = number(values, 22) / 100 * 0.15; state.rasterizer.color.saturationJitter = number(values, 22) / 100 * 0.1;
        state.material.simulation.enabled = number(values, 21) > 0; state.material.simulation.model = BrushSimulationModel::WetPaint; state.material.simulation.wetness = number(values, 21) / 100;
        state.material.simulation.mixStrength = state.material.simulation.wetness / 2; state.material.simulation.pickup = state.material.simulation.wetness / 2;
        if (text(values, 1) == "Texture") { state.material.texture.enabled = true; state.material.texture.width = state.material.texture.height = 8; state.material.texture.alpha.resize(64); for (int i = 0; i < 64; ++i) state.material.texture.alpha[i] = (i * 37 % 128) + 128; }
        setBrushEngineState(state);
    } else if (tool == "audio-track" && m_audioSource.samples.empty() && documentReady() && !document()->audioAssets.empty()) {
        m_audioSource = document()->audioAssets.back();
        for (const auto &track : document()->audioTracks) for (const auto &clip : track.clips) if (clip.assetId == m_audioSource.id) m_audioTrack = QString::fromStdString(track.id);
    } else if (tool == "eraser") {
        if (values.value("selector", "Pixel").toString() == "Pixel" && !restoreSourceMatches()) {
            if (const auto *pixels = selectedRasterPixels()) {
                m_restoreLayer = selectedLayerId(); m_restoreAsset = selectedRasterAssetId();
                m_restoreDocumentSequence = m_graphicDocumentSequence; m_restorePixels = *pixels;
            }
        }
        setBrushSize(number(values, 0, 8)); setBrushHardness(number(values, 1, 100) / 100); setBrushOpacity(number(values, 2, 100) / 100); setBrushFlow(1); setStabilizerStrength(0);
    }
    emit toolStateChanged();
    notifyInteractionFeedback();
}
bool EditorCanvas::ensurePaintLayer() {
    if (m_pixelLocks.contains(selectedLayerId())) return fail(tr("Unlock the selected layer's pixels before painting."));
    if (rasterLayerSelected()) return true;
    if (eraser()) return fail(tr("Select a bitmap layer to erase its pixels."));
    return addPaintLayer();
}
bool EditorCanvas::beginStrokeAt(const QPointF &position, qreal pressure) {
    if (!ensurePaintLayer()) return false;
    if (m_tool == "brush") setBrushSize(number(m_toolValues, 4, 8) / (toggle(m_toolValues, 6) ? zoom() : 1));
    m_historySuspended = true;
    m_strokeAuthorship = document()->authorship;
    const auto *pixels = selectedRasterPixels(); m_strokePixels = pixels ? *pixels : RasterLayer{};
    m_brushOrigin = m_brushFiltered = m_brushPreviousInput = position;
    m_brushClock.start(); m_brushLastInput = 0;
    if (!restoreSourceMatches() && toggle(m_settings.value("eraser"), 3)) {
        m_restorePixels = m_strokePixels; m_restoreLayer = selectedLayerId(); m_restoreAsset = selectedRasterAssetId();
        m_restoreDocumentSequence = m_graphicDocumentSequence;
    }
    const bool begun = CanvasItem::beginStrokeAt(position + (activeArtboard() ? editingOrigin() : QPointF{}), pressure);
    if (!begun) m_historySuspended = false;
    return begun;
}
QPointF EditorCanvas::brushInputPosition(const QPointF &input, bool finish) {
    if (m_tool != "brush") return input;
    const auto v = m_toolValues;
    QPointF position = input;
    if (toggle(v, 26)) {
        const auto delta = input - m_brushOrigin; const double length = std::hypot(delta.x(), delta.y());
        const double angle = std::round(std::atan2(delta.y(), delta.x()) / (std::numbers::pi / 4)) * (std::numbers::pi / 4);
        position = m_brushOrigin + QPointF(std::cos(angle), std::sin(angle)) * length;
    }
    if (finish) return toggle(v, 19, true) ? position : m_brushFiltered;
    const qint64 now = m_brushClock.elapsed(); const double elapsed = std::max<qint64>(1, now - m_brushLastInput);
    const double strength = number(v, 24) / 100, delay = number(v, 25), streamline = number(v, 18) / 100;
    const auto mode = text(v, 23, "Rope");
    QPointF next = position;
    if (strength > 0) {
        if (mode == "Rope") {
            const auto delta = position - m_brushFiltered; const double length = std::hypot(delta.x(), delta.y());
            const double radius = std::max(1.0, brushSize() * strength / 2) + delay / 20;
            next = length > radius ? position - delta * (radius / length) : m_brushFiltered;
        } else if (mode == "Pulled") {
            const double response = 1 - std::exp(-elapsed / std::max(1.0, strength * 250 + delay));
            next = m_brushFiltered + (position - m_brushFiltered) * response;
        } else if (mode == "Predictive") {
            auto lead = (position - m_brushPreviousInput) * std::min(2.0, delay / elapsed) * strength;
            const double length = std::hypot(lead.x(), lead.y()), limit = brushSize() * 2;
            if (length > limit && length > 0) lead *= limit / length;
            next = position + lead;
        }
    }
    m_brushFiltered += (next - m_brushFiltered) * (1 - std::clamp(streamline, 0.0, 1.0) * 0.95);
    m_brushPreviousInput = position; m_brushLastInput = now; return m_brushFiltered;
}
bool EditorCanvas::continueStrokeAt(const QPointF &position, qreal pressure) {
    if (!liveStrokeActive()) return false;
    return CanvasItem::continueStrokeAt(brushInputPosition(position, false) + (activeArtboard() ? editingOrigin() : QPointF{}), pressure);
}
bool EditorCanvas::endStrokeAt(const QPointF &position, qreal pressure) {
    if (!CanvasItem::endStrokeAt(brushInputPosition(position, true) + (activeArtboard() ? editingOrigin() : QPointF{}), pressure)) { m_historySuspended = false; return false; }
    if (selectedRasterPixels() && m_strokePixels.pixels.size() == selectedRasterPixels()->pixels.size() && (!m_selection.alpha.empty() || alphaProtected(selectedLayerId()))) {
        auto result = blendRaster(m_strokePixels, *selectedRasterPixels(), layerMask());
        if (result.ok()) {
            if (m_tool == "brush" && alphaProtected(selectedLayerId())) for (std::size_t i = 0; i < result.pixels.pixels.size(); ++i) result.pixels.pixels[i] = qAlpha(m_strokePixels.pixels[i]) == 0 ? m_strokePixels.pixels[i] : (result.pixels.pixels[i] & 0xffffff) | (m_strokePixels.pixels[i] & 0xff000000);
            replaceSelectedPixels(result.pixels);
        }
    }
    if (m_tool == "eraser" && toggle(m_toolValues, 13) && restoreSourceMatches()) {
        QImage overlay(m_restorePixels.width, m_restorePixels.height, QImage::Format_ARGB32); overlay.fill(Qt::transparent); const auto *current = selectedRasterPixels();
        if (current && current->pixels.size() == m_restorePixels.pixels.size()) for (int y = 0; y < overlay.height(); ++y) for (int x = 0; x < overlay.width(); ++x) { const auto i = y * overlay.width() + x; if (qAlpha(m_restorePixels.pixels[i]) > qAlpha(current->pixels[i])) overlay.setPixel(x, y, 0x778b7cff); }
        QImage mapped(editingWidth(), editingHeight(), QImage::Format_ARGB32); mapped.fill(Qt::transparent);
        if (const auto *layer = findLayer(*document(), selectedLayerId().toStdString())) {
            const auto &t = layerProperties(*layer).transform; QPainter p(&mapped);
            p.setTransform(QTransform(t.m11, t.m12, t.m21, t.m22, t.translationX, t.translationY)); p.drawImage(0, 0, overlay);
        }
        m_eraserOverlay = dataImage(mapped); emit toolStateChanged();
    }
    m_strokePixels = {}; m_strokeAuthorship.reset(); m_historySuspended = false;
    if (!synchronizeDocument(*document())) return false;
    recordHistory(); invalidateToolPreview(); return true;
}
bool EditorCanvas::resetTool(const QString &tool, const QVariantMap &defaults) {
    if ((tool == "color" || tool == "effects" || tool == "auto-enhance") && toolPreviewMatches(tool)) {
        if (!replaceSelectedPixels(*m_previewPixels)) return false;
    }
    invalidateToolPreview(); configureTool(tool, defaults);
    if (tool == "layers" && !selectedLayerId().isEmpty()) {
        const auto id = selectedLayerId(); m_pixelLocks.remove(id); m_positionLocks.remove(id); m_settings.remove("layer-alpha:" + id);
        const bool restored = commit([&](Document &d) {
            auto *layer = findLayer(d, id.toStdString()); if (!layer) return false;
            auto &properties = layerProperties(*layer); properties.opacity = std::clamp(number(defaults, 10, 100) / 100, 0.0, 1.0);
            properties.blendMode = blend(text(defaults, 20, "Normal")); return true;
        });
        emit toolStateChanged(); emit layerStateChanged(); return restored;
    }
    if (tool == "background" && !backgroundMaskActive()) return updateBackground();
    if (tool == "audio-track" && !m_audioSource.samples.empty()) return updateAudio(defaults);
    if (tool == "elements" && m_elementBounds.contains(selectedLayerId())) return updateElement();
    if (tool == "text" && m_textOrigins.contains(selectedLayerId())) return updateText();
    return true;
}
bool EditorCanvas::applyToolField(const QString &tool, const QString &field, const QVariant &value) {
    if (m_colorRead == ColorRead::Neutral && tool == "color") retireColorRead();
    auto values = m_settings.value(tool);
    if (field == "graphicParameters" || field == "graphicDrafts" || field == "graphicOperation"
        || ((tool == "camera-photo" || tool == "background") && !field.startsWith("field-"))) {
        values[field] = value;
        m_settings[tool] = values;
        if (m_tool == tool) m_toolValues = values;
        emit toolStateChanged();
        return true;
    }
    const auto capability = toolControlState(tool, field, values);
    if (!capability.value("enabled").toBool()) return fail(capability.value("reason").toString());
    if (tool == "canvas" && (field == "field-1" || field == "field-17" || field == "field-18")) {
        const auto input = value.toString().trimmed();
        const QRegularExpression expression(field == "field-1" ? "^([0-9]+(?:\\.[0-9]+)?)\\s*(?:ppi)?$" : field == "field-17" ? "^([0-9]+(?:\\.[0-9]+)?)\\s*(?:mm|px)$" : "^([0-9]+(?:\\.[0-9]+)?)\\s*(?:mm|px|%)$");
        const auto match = expression.match(input);
        if (!match.hasMatch() || (field == "field-1" && (match.captured(1).toDouble() < 1 || match.captured(1).toDouble() > 9600)) || (field != "field-1" && match.captured(1).toDouble() > 10000) || (input.endsWith('%') && match.captured(1).toDouble() > 50))
            return fail(tr("Enter a valid resolution, bleed or safe inset with its unit."));
        if (field == "field-1") { m_specification["ppi"] = match.captured(1).toDouble(); emit specificationChanged(); }
    }
    values[field] = value; m_settings[tool] = values;
    if (m_tool == tool) m_toolValues = values;
    m_lastEditedField = field;
    if (!documentReady()) return fail(tr("Open a canvas first."));
    clearError();
    if (tool == "elements" && !m_elementBounds.contains(selectedLayerId()) && m_selectedElements.contains(selectedLayerId())) {
        if (field == "field-0") {
            const bool accepted = resizeSelectedElement(value);
            restoreSelectedElementSettings(); emit elementSelectionChanged(); return accepted;
        }
        if (field == "field-14") return setLayerOpacity(selectedLayerId(), value.toDouble() / 100);
        if (field == "field-15") return setLayerBlend(selectedLayerId(), value.toString());
        if (field == "selector" || field == "field-3") return true;
        restoreSelectedElementSettings(); emit elementSelectionChanged();
        return fail(tr("This imported vector has no editable shape-generation parameters."));
    }
    if (tool == "elements" && m_elementBounds.contains(selectedLayerId())) {
        if (field == "field-14") return setLayerOpacity(selectedLayerId(), value.toDouble() / 100);
        if (field == "field-15") return setLayerBlend(selectedLayerId(), value.toString());
        return updateElement();
    }
    if (tool == "text" && m_textOrigins.contains(selectedLayerId())) return updateText();
    if (tool == "color" && field == "field-18") { emit toolStateChanged(); return true; }
    if (tool == "color" || tool == "effects") return applyPixelTool(tool, field, value);
    if(tool=="background" && (field.startsWith("mask") || field=="backgroundView")) {
        if(field=="maskTarget" && value.toString()!="Selected canvas layer") return selectLayer(value.toString());
        if(field=="maskOverlayColor" && !QColor(value.toString()).isValid()) return fail(tr("Choose a valid overlay color."));
        emit toolStateChanged(); return true;
    }
    if (tool == "background" && (field == "field-0" || field == "field-1" || field == "field-2")) return updateBackground();
    if (tool == "audio-track" && !m_audioSource.samples.empty() && field != "field-0" && field != "field-4") return updateAudio(values);
    if (tool == "layers" && !selectedLayerId().isEmpty()) {
        if (field == "field-9") { m_settings["layer-alpha:" + selectedLayerId()] = {{"enabled", value.toBool()}}; emit toolStateChanged(); emit layerStateChanged(); }
        if (field == "field-13") return combineVectorPaths(value.toString());
        if (field == "field-10") return setLayerOpacity(selectedLayerId(), value.toDouble() / 100);
        if (field == "field-20") return setLayerBlend(selectedLayerId(), value.toString());
        if (field == "field-19") return setLayerName(selectedLayerId(), value.toString());
        if (field == "field-8") { m_pixelLocks.remove(selectedLayerId()); m_positionLocks.remove(selectedLayerId()); if (value.toString() == "Pixels" || value.toString() == "All") m_pixelLocks.insert(selectedLayerId()); if (value.toString() == "Position" || value.toString() == "All") m_positionLocks.insert(selectedLayerId()); emit toolStateChanged(); emit layerStateChanged(); }
    }
    if (tool == "canvas" && field == "field-0") {
        const auto size = value.toList(); if (size.size() != 2) return fail(tr("Enter the canvas width and height."));
        const int w = size[0].toInt(), h = size[1].toInt(); if (w < 1 || h < 1 || w > 65536 || h > 65536) return fail(tr("Canvas dimensions must be from 1 to 65536 pixels."));
        const double sx = double(w) / editingWidth(), sy = double(h) / editingHeight();
        const auto anchor = text(values, 3, "Center"); const double alignment = anchor == "Top-left" ? 0 : anchor == "Bottom-right" ? 1 : 0.5;
        const double dx = (w - editingWidth()) * alignment, dy = (h - editingHeight()) * alignment;
        if (!commit([&](Document &d) { resizeEditingCanvas(d, {w, h}); for (auto &layer : d.layers) { if (!belongsToActiveArtboard(layer)) continue; auto &p = layerProperties(layer); if (toggle(values, 2)) { p.transform.m11 *= sx; p.transform.m21 *= sx; p.transform.translationX *= sx; p.transform.m12 *= sy; p.transform.m22 *= sy; p.transform.translationY *= sy; } else { p.transform.translationX += dx; p.transform.translationY += dy; } } return true; })) return false;
        m_specification["pixelWidth"] = w; m_specification["pixelHeight"] = h; clearAreaSelection(); emit specificationChanged(); fitToView();
    }
    emit toolStateChanged(); return true;
}
bool EditorCanvas::event(QEvent *event) {
    switch(event->type()) {
    case QEvent::ShortcutOverride: case QEvent::KeyPress:
    case QEvent::FocusOut: case QEvent::WindowDeactivate:
    case QEvent::TouchBegin: case QEvent::TouchUpdate: case QEvent::TouchEnd: case QEvent::TouchCancel:
    case QEvent::TabletPress: case QEvent::TabletMove: case QEvent::TabletRelease:
        break;
    default:
        return CanvasItem::event(event);
    }
    if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape
        && (m_gestureActive || m_creatingElement || m_maskGestureActive || !m_maskPolygon.empty()
            || liveStrokeActive() || !m_elementTransformHandle.isEmpty() || m_layerColorPicking || m_pickNeutral || m_colorRead != ColorRead::None)) {
        // A live canvas gesture owns Escape before the editor's panel shortcut.
        event->accept(); return true;
    }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape
        && (m_layerColorPicking || m_pickNeutral || m_colorRead != ColorRead::None)) {
        retireColorRead(); emit toolStateChanged(); event->accept(); return true;
    }
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape
        && (m_gestureActive || m_creatingElement || m_maskGestureActive || !m_maskPolygon.empty() || liveStrokeActive())) {
        cancelInteractionGesture(); event->accept(); return true;
    }
    if (event->type() == QEvent::KeyPress && elementKeyEvent(static_cast<QKeyEvent *>(event))) { event->accept(); return true; }
    if ((event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate) && !m_elementTransformHandle.isEmpty()) cancelElementTransform();
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::WindowDeactivate) {
        cancelInteractionGesture(); m_feedbackPointerInside = false; notifyInteractionFeedback();
    }
    if(backgroundMaskActive()) {
        if(event->type()==QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape) {cancelBackgroundMaskGesture(); event->accept(); return true;}
        if(event->type()==QEvent::TouchBegin || event->type()==QEvent::TouchUpdate || event->type()==QEvent::TouchEnd || event->type()==QEvent::TouchCancel) {
            auto *touch=static_cast<QTouchEvent *>(event);
            if(event->type()==QEvent::TouchCancel || touch->points().size()!=1) cancelBackgroundMaskGesture();
            else {
                const auto &point=touch->points().first();
                updatePointerFeedback(point.position());
                if(event->type()==QEvent::TouchBegin && !selectArtboardAt(point.position())) {event->accept();return true;}
                m_maskInputDevice=PointerDeviceKind::Touch;
                const auto p=editingPosition(point.position());const auto pressure=point.pressure()>0?point.pressure():1.;
                if(event->type()==QEvent::TouchBegin) beginBackgroundMaskGesture(p,pressure);
                else if(event->type()==QEvent::TouchEnd) endBackgroundMaskGesture(p,pressure);
                else continueBackgroundMaskGesture(p,pressure);
                event->accept();return true;
            }
        }
        if(event->type()==QEvent::TabletPress || event->type()==QEvent::TabletMove || event->type()==QEvent::TabletRelease) {
            auto *tablet=static_cast<QTabletEvent *>(event);
            updatePointerFeedback(tablet->position());
            if(event->type()==QEvent::TabletPress && !selectArtboardAt(tablet->position())) {event->accept();return true;}
            m_maskInputDevice=PointerDeviceKind::Tablet;
            const auto p=editingPosition(tablet->position());
            if(event->type()==QEvent::TabletPress) beginBackgroundMaskGesture(p,tablet->pressure());
            else if(event->type()==QEvent::TabletMove) continueBackgroundMaskGesture(p,tablet->pressure());
            else endBackgroundMaskGesture(p,tablet->pressure());
            event->accept();return true;
        }
    }
    if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove || event->type() == QEvent::TabletRelease) {
        auto *tablet = static_cast<QTabletEvent *>(event);
        updatePointerFeedback(tablet->position());
        if (m_tool == "brush" || (m_tool == "eraser" && m_toolValues.value("selector", "Pixel").toString() == "Pixel")) {
            if (event->type() == QEvent::TabletPress && !selectArtboardAt(tablet->position())) { event->accept(); return true; }
            const auto position = editingPosition(tablet->position());
            if (event->type() == QEvent::TabletPress) beginStrokeAt(position, tablet->pressure());
            else if (event->type() == QEvent::TabletMove) continueStrokeAt(position, tablet->pressure()); else endStrokeAt(position, tablet->pressure());
            event->accept(); return true;
        }
    }
    return CanvasItem::event(event);
}
void EditorCanvas::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_navigatingCanvas = true; updatePointerFeedback(event->position());
        CanvasItem::mousePressEvent(event); setCursor(Qt::ClosedHandCursor); return;
    }
    if (event->button() != Qt::LeftButton || !documentReady()) { CanvasItem::mousePressEvent(event); return; }
    forceActiveFocus();
    m_primaryPointerDown = true;
    updatePointerFeedback(event->position());
    const bool readingColor = m_layerColorPicking || m_pickNeutral || m_colorRead != ColorRead::None;
    if (!readingColor && m_tool == "elements" && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
        const auto handle = elementHandleAt(event->position());
        if (!handle.isEmpty()) { beginElementTransform(handle, event->position(), event->modifiers()); event->accept(); return; }
    }
    // Face Refine remains inspectable without drawing a previous repair mode.
    if (!readingColor && m_tool == "retouch" && m_toolValues.value("selector").toString() == "Face Refine") { event->accept(); return; }
    if (!selectArtboardAt(event->position())) { event->accept(); return; }
    const auto p = editingPosition(event->position());
    if (m_layerColorPicking || m_colorRead == ColorRead::Layer) { sampleLayerColor(p); event->accept(); return; }
    if (m_pickNeutral || m_colorRead == ColorRead::Neutral) {
        requestColorRead(p, ColorRead::Neutral);
        event->accept(); return;
    }
    if(backgroundMaskActive()) {m_maskInputDevice=PointerDeviceKind::Mouse;beginBackgroundMaskGesture(p); event->accept(); return;}
    if (m_pickCloneSource) { m_cloneSource = p; m_cloneReady = true; m_pickCloneSource = false; emit toolStateChanged(); event->accept(); return; }
    if (m_tool == "text") { createText(p); event->accept(); return; }
    if (m_tool == "fill") { fillAt(p); event->accept(); return; }
    if (m_tool == "elements") {
        const auto hit = event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier) ? QString{} : elementAt(event->position());
        if (!hit.isEmpty()) {
            if (event->modifiers() & Qt::ShiftModifier) setElementSelected(hit, !m_selectedElements.contains(hit), true);
            else {
                if (!m_selectedElements.contains(hit) || selectedLayerId() != hit) selectElement(hit);
                beginElementTransform("move", event->position(), event->modifiers());
            }
        } else {
            clearElementSelection(); resetAlignmentGesture(); m_elementStart = m_elementCurrent = snapAlignment(p + editingOrigin()) - editingOrigin(); m_creatingElement = true; emit elementTransformChanged(); notifyInteractionFeedback();
        }
        event->accept(); return;
    }
    if (m_tool == "select" && m_toolValues.value("selector").toString() == "Magic Wand") {
        auto sampled = *document(); if (!toggle(m_toolValues, 11, true)) for (auto &layer : sampled.layers) layerProperties(layer).visible = QString::fromStdString(layerProperties(layer).id) == selectedLayerId();
        const auto rendered = renderEditingFrame(sampled); if (rendered.ok()) { auto mask = floodMask(rendered.pixels, {int(p.x()), int(p.y())}, number(m_toolValues, 9, 32) / 255, toggle(m_toolValues, 10, true)); if (mask.ok()) { auto next = std::move(mask.mask); const auto f = featherMask(next, number(m_toolValues, 12)); if (f.ok()) next = f.mask; const auto operation = text(m_toolValues, 0, "New"); if (!m_selection.alpha.empty() && operation != "New") { auto merged = combineMasks(m_selection, next, operation == "Subtract" ? MaskOperation::Subtract : operation == "Intersect" ? MaskOperation::Intersect : MaskOperation::Add); if (merged.ok()) next = merged.mask; } m_selection = std::move(next); updateSelectionOverlay(); } }
        event->accept(); return;
    }
    if (m_tool == "select" && m_toolValues.value("selector").toString() == "Object") {
        clearSelection(); for (std::size_t i = document()->layers.size(); i > 0; --i) {
            if (!belongsToActiveArtboard(document()->layers[i - 1])) continue;
            const auto hit = renderFrameLayerTiles(*document(), frame(), i - 1, {{{{int(p.x() + editingOrigin().x()), int(p.y() + editingOrigin().y())}, {1, 1}}, {1, 1}}});
            if (hit.ok() && hit.visible && !hit.tiles.empty() && !hit.tiles.front().pixels.pixels.empty() && qAlpha(hit.tiles.front().pixels.pixels.front())) { selectLayer(QString::fromStdString(layerProperties(document()->layers[i - 1]).id)); break; }
        }
        event->accept(); return;
    }
    if (m_tool == "select" || m_tool == "masking" || m_tool == "retouch" || (m_tool == "eraser" && m_toolValues.value("selector").toString() != "Pixel")) {
        m_gesture = {p}; m_retouchStart = p; m_gestureActive = true;
        if (m_tool == "retouch" && (m_toolValues.value("selector").toString() == "Heal" || m_toolValues.value("selector").toString() == "Clone")) { m_historySuspended = true; retouchAt(p); }
        notifyInteractionFeedback();
        event->accept(); return;
    }
    if (m_tool == "brush" || m_tool == "eraser") { beginStrokeAt(p); event->accept(); return; }
    CanvasItem::mousePressEvent(event);
}
void EditorCanvas::mouseMoveEvent(QMouseEvent *event) {
    updatePointerFeedback(event->position());
    if (m_navigatingCanvas) { CanvasItem::mouseMoveEvent(event); return; }
    if (!m_elementTransformHandle.isEmpty()) { updateElementTransform(event->position(), event->modifiers()); event->accept(); return; }
    const auto p = editingPosition(event->position());
    if(backgroundMaskActive() && m_maskGestureActive) {continueBackgroundMaskGesture(p); event->accept(); return;}
    if (m_gestureActive) { m_gesture.append(p); if (m_tool == "retouch" && (m_toolValues.value("selector").toString() == "Heal" || m_toolValues.value("selector").toString() == "Clone")) retouchAt(p); notifyInteractionFeedback(); event->accept(); return; }
    if (m_creatingElement) { m_elementCurrent = snapAlignment(p + editingOrigin()) - editingOrigin(); emit elementTransformChanged(); notifyInteractionFeedback(); event->accept(); return; }
    if (liveStrokeActive()) { continueStrokeAt(p); event->accept(); return; }
    CanvasItem::mouseMoveEvent(event);
}
void EditorCanvas::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_navigatingCanvas = false; CanvasItem::mouseReleaseEvent(event);
        updatePointerFeedback(event->position()); return;
    }
    if (event->button() == Qt::LeftButton) m_primaryPointerDown = false;
    updatePointerFeedback(event->position());
    if (!m_elementTransformHandle.isEmpty() && event->button() == Qt::LeftButton) { endElementTransform(event->position(), event->modifiers()); updateElementCursor(event->position()); event->accept(); return; }
    const auto p = editingPosition(event->position());
    if(backgroundMaskActive() && event->button()==Qt::LeftButton) {endBackgroundMaskGesture(p); event->accept(); return;}
    if (m_gestureActive && event->button() == Qt::LeftButton) {
        m_gesture.append(p); m_gestureActive = false; QVariantList points; for (const auto &point : m_gesture) points.append(point);
        if (m_tool == "select" || m_tool == "masking") selectArea(points);
        else if (m_tool == "retouch" && m_toolValues.value("selector").toString() != "Heal" && m_toolValues.value("selector").toString() != "Clone") { const auto oldTool = m_tool; const auto oldValues = m_toolValues; m_tool = "masking"; m_toolValues = {{"selector", "Brush"}, {"field-0", number(oldValues, 0, 84)}}; selectArea(points); m_tool = oldTool; m_toolValues = oldValues; }
        else if (m_tool == "eraser") { const auto oldValues = m_toolValues; m_tool = "masking"; m_toolValues = {{"selector", "Brush"}, {"field-0", number(oldValues, 0, 84)}, {"field-1", oldValues.value("selector").toString() == "Restore / Protect" ? number(oldValues, 12) : 0}}; selectArea(points); m_tool = "eraser"; m_toolValues = oldValues;
            if (oldValues.value("selector").toString() == "Restore / Protect") {
                if (text(oldValues, 11, "Restore") == "Protect") { for (auto &alpha : m_selection.alpha) alpha = 255 - alpha; updateSelectionOverlay(); }
                else if (const auto *pixels = restoreSourceMatches() ? selectedRasterPixels() : nullptr; pixels && m_restorePixels.pixels.size() == pixels->pixels.size()) { const auto restored = blendRaster(*pixels, m_restorePixels, layerMask()); if (restored.ok()) replaceSelectedPixels(restored.pixels); clearAreaSelection(); }
                else fail(tr("Enable history recording before erasing this layer to restore its pixels."));
            }
        }
        if (m_tool == "retouch" && m_historySuspended) { m_historySuspended = false; recordHistory(); }
        m_gesture.clear(); notifyInteractionFeedback();
        event->accept(); return;
    }
    if (m_creatingElement && event->button() == Qt::LeftButton) { m_elementCurrent = snapAlignment(p + editingOrigin()) - editingOrigin(); const auto bounds = elementCreationBounds(); m_creatingElement = false; if (bounds.width() >= 1 && bounds.height() >= 1) createElement(bounds); resetAlignmentGesture(); emit elementTransformChanged(); notifyInteractionFeedback(); event->accept(); return; }
    if (liveStrokeActive()) { endStrokeAt(p); event->accept(); return; }
    CanvasItem::mouseReleaseEvent(event);
}
void EditorCanvas::mouseUngrabEvent() {
    if (m_primaryPointerDown || m_gestureActive || m_creatingElement || m_maskGestureActive
        || liveStrokeActive() || !m_elementTransformHandle.isEmpty()) {
        cancelElementTransform(); cancelInteractionGesture();
    }
    CanvasItem::mouseUngrabEvent();
    m_navigatingCanvas = false; notifyInteractionFeedback();
}
