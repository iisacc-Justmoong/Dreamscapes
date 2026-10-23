#include "EditorClippingOverlay.h"
#include "EditorCanvas.h"
#include <iiSharedCanvas/Render/FrameRenderer.h>
#include <QPainter>
#include <QQuickWindow>
#include <QTimer>
#include <QtConcurrentRun>
#include <algorithm>
#include <exception>

EditorClippingOverlay::EditorClippingOverlay(QQuickItem *parent)
    : EditorClippingOverlay(render, parent) {}
EditorClippingOverlay::EditorClippingOverlay(RenderOperation operation, QQuickItem *parent)
    : QQuickPaintedItem(parent), m_operation(std::move(operation)) {
    setAcceptedMouseButtons(Qt::NoButton);
    setClip(true); setFillColor(Qt::transparent);
    connect(&m_completion, &QFutureWatcher<Result>::finished, this, &EditorClippingOverlay::finish);
    connect(this, &QQuickItem::visibleChanged, this, &EditorClippingOverlay::refreshIntent);
    connect(this, &QQuickItem::enabledChanged, this, &EditorClippingOverlay::refreshIntent);
    const auto observeWindow = [this](QQuickWindow *window) {
        if (m_observedWindow) disconnect(m_observedWindow, nullptr, this, nullptr);
        m_observedWindow = window;
        if (window) connect(window, &QWindow::visibilityChanged, this, [this] { refreshIntent(); });
        refreshIntent();
    };
    connect(this, &QQuickItem::windowChanged, this, observeWindow);
    observeWindow(window());
}
EditorClippingOverlay::~EditorClippingOverlay() {
    // QQuickItem base teardown can change visibility/window after our members
    // have been destroyed. Stop those callbacks while their state still exists.
    disconnect(this, nullptr, this, nullptr);
    if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
    if (m_observedWindow) disconnect(m_observedWindow, nullptr, this, nullptr);
    disconnect(&m_completion, nullptr, this, nullptr);
}
EditorCanvas *EditorClippingOverlay::canvas() const { return m_canvas; }
void EditorClippingOverlay::setCanvas(EditorCanvas *canvas) {
    if (m_canvas == canvas) return;
    if (m_canvas) disconnect(m_canvas, nullptr, this, nullptr);
    m_canvas = canvas; ++m_documentEpoch;
    if (canvas) {
        const auto refresh = &EditorClippingOverlay::refreshIntent;
        connect(canvas, &EditorCanvas::toolStateChanged, this, refresh);
        connect(canvas, &EditorCanvas::revisionChanged, this, refresh);
        connect(canvas, &EditorCanvas::frameChanged, this, refresh);
        connect(canvas, &EditorCanvas::activeArtboardChanged, this, refresh);
        connect(canvas, &QQuickItem::visibleChanged, this, refresh);
        connect(canvas, &EditorCanvas::documentChanged, this, [this] { ++m_documentEpoch; refreshIntent(); });
        connect(canvas, &EditorCanvas::viewportChanged, this, [this] { update(); });
        connect(canvas, &EditorCanvas::editingGeometryChanged, this, [this] { update(); });
        connect(canvas, &QObject::destroyed, this, [this] {
            ++m_documentEpoch;
            const QPointer<EditorClippingOverlay> owner(this);
            refreshIntent();
            if (owner) emit canvasChanged();
        });
    }
    const QPointer<EditorClippingOverlay> owner(this);
    refreshIntent();
    if (owner) emit canvasChanged();
}
QString EditorClippingOverlay::contextKey() const {
    if (!m_canvas || !isVisible() || !isEnabled() || !m_canvas->isVisible() || !window()
        || !window()->isVisible() || window()->visibility() == QWindow::Minimized || !m_canvas->documentReady()) return {};
    const auto settings = m_canvas->clippingState();
    if (!settings.value("enabled").toBool()) return {};
    return QString("%1:%2:%3:%4:%5:%6:%7:%8")
        .arg(m_documentEpoch).arg(m_canvas->revision()).arg(m_canvas->frame()).arg(m_canvas->activeArtboardIndex())
        .arg(settings.value("white").toInt()).arg(settings.value("black").toInt())
        .arg(settings.value("highlights").toBool()).arg(settings.value("shadows").toBool());
}
void EditorClippingOverlay::refreshIntent() {
    const auto key = contextKey();
    if (key == m_key) return;
    m_key = key; ++m_generation; m_pending.reset();
    m_image = {}; m_error.clear(); m_busy = !key.isEmpty();
    update();
    // Coalesce all producer changes in this GUI event before taking a snapshot.
    if (m_busy && !m_prepareScheduled) {
        m_prepareScheduled = true;
        QTimer::singleShot(0, this, &EditorClippingOverlay::prepare);
    }
    emit stateChanged();
}
void EditorClippingOverlay::prepare() {
    m_prepareScheduled = false;
    if (contextKey() != m_key) { refreshIntent(); return; }
    if (m_key.isEmpty() || !m_canvas) return;
    try {
        const auto settings = m_canvas->clippingState();
        Work work{{m_canvas->editingFrameSnapshot(), settings.value("white").toInt(), settings.value("black").toInt(),
                   settings.value("highlights").toBool(), settings.value("shadows").toBool()}, m_generation};
        if (m_running) m_pending = std::move(work);
        else start(std::move(work));
    } catch (const std::exception &error) {
        m_error = QString::fromUtf8(error.what()); m_busy = false;
        const auto message = m_error; const auto generation = m_generation;
        const QPointer<EditorClippingOverlay> owner(this);
        emit stateChanged();
        if (owner && m_generation == generation) emit previewFailed(message);
    }
}
void EditorClippingOverlay::start(Work work) {
    m_running = true; m_runningGeneration = work.generation;
    m_completion.setFuture(QtConcurrent::run([request = std::move(work.request), operation = m_operation] {
        try { return operation(request); }
        catch (const std::exception &error) { return Result{{}, QString::fromUtf8(error.what())}; }
        catch (...) { return Result{{}, tr("Could not build the clipping preview.")}; }
    }));
}
void EditorClippingOverlay::finish() {
    auto result = m_completion.result();
    m_running = false;
    if (contextKey() != m_key) { refreshIntent(); return; }
    if (m_pending) {
        auto next = std::move(*m_pending); m_pending.reset();
        if (next.generation == m_generation && !m_key.isEmpty()) { start(std::move(next)); return; }
    }
    if (m_runningGeneration != m_generation || m_key.isEmpty()) return;
    m_image = std::move(result.image); m_error = std::move(result.error); m_busy = false;
    const auto error = m_error; const auto generation = m_generation;
    update();
    const QPointer<EditorClippingOverlay> owner(this);
    emit stateChanged();
    if (owner && m_generation == generation && !error.isEmpty()) emit previewFailed(error);
}
EditorClippingOverlay::Result EditorClippingOverlay::render(const Request &request) {
    if (!request.snapshot.document) return {{}, tr("Open a canvas before inspecting clipping.")};
    if (!request.highlights && !request.shadows) return {};
    const auto &snapshot = request.snapshot;
    const auto rendered = snapshot.artboardId.empty()
        ? iiSharedCanvas::renderFrame(*snapshot.document, snapshot.frame)
        : iiSharedCanvas::renderArtboard(*snapshot.document, snapshot.frame, snapshot.artboardId);
    if (!rendered.ok()) return {{}, QString::fromStdString(rendered.message)};
    QImage image(rendered.pixels.width, rendered.pixels.height, QImage::Format_ARGB32);
    if (image.isNull()) return {{}, tr("The clipping preview could not be allocated.")};
    image.fill(Qt::transparent); bool clipped = false;
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const auto pixel = rendered.pixels.pixels[std::size_t(y) * image.width() + x];
            if (!qAlpha(pixel)) continue;
            if (request.highlights && std::max({qRed(pixel), qGreen(pixel), qBlue(pixel)}) >= request.white)
                row[x] = 0x88ff453au;
            else if (request.shadows && std::min({qRed(pixel), qGreen(pixel), qBlue(pixel)}) <= request.black)
                row[x] = 0x880a84ffu;
            clipped |= row[x] != 0;
        }
    }
    return {clipped ? image : QImage{}, {}};
}
void EditorClippingOverlay::paint(QPainter *painter) {
    if (!m_canvas || m_image.isNull()) return;
    const auto origin = m_canvas->editingOrigin();
    const auto zoom = m_canvas->zoom();
    const QRectF target(m_canvas->panX() + (origin.x() - m_canvas->canvasOriginX()) * zoom,
                        m_canvas->panY() + (origin.y() - m_canvas->canvasOriginY()) * zoom,
                        m_canvas->editingWidth() * zoom, m_canvas->editingHeight() * zoom);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->drawImage(target, m_image);
}
