#include "EditorProject.h"
#include "EditorProjectCodec.h"
#include "EditorImageImport.h"
#include "EditorDocumentOpenRequest.h"
#include "EditorFrameExporter.h"
#include <iiFileProvider.h>
#include <iiSharedCanvas/Media/MediaIo.h>
#include <QFileInfo>
#include <QSet>
#include <QQmlEngine>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using namespace iiSharedCanvas;
QString pathFor(const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() ? url.toString() : QString{}; }
dreamscapes::ImageImportRequest importRequest(EditorCanvas *base,const QVariantList &sources) {
    dreamscapes::ImageImportRequest request;
    for(const auto &source:sources) {
        const auto path=pathFor(source.toUrl());
        if(path.isEmpty())throw std::runtime_error("Choose local images.");
        request.sources.emplace_back(path.toStdU16String());
    }
    if(base) {
        if(base->liveStrokeActive())throw std::runtime_error("Finish the current stroke before attaching images.");
        if(base->infiniteCanvas())throw std::runtime_error("Attach artboards to a finite project.");
        const auto *document=base->document();
        for(const auto &board:document->artboards)request.artboardVisibility.push_back(board.visible);
        for(const auto &layer:document->layers)request.layerVisibility.push_back(layerProperties(layer).visible);
        request.baseName=base->documentName().toStdString();
        request.base=base->editingFrameSnapshot().document;
    }
    return request;
}
}
EditorProject::EditorProject(QObject *parent) : EditorProject(EditorPagePreviews::render,parent) {}
EditorProject::EditorProject(EditorPagePreviews::Operation previewOperation,QObject *parent)
    :EditorProject(std::move(previewOperation),dreamscapes::prepareDocumentOpen,parent) {}
EditorProject::EditorProject(EditorPagePreviews::Operation previewOperation,dreamscapes::DocumentOpenOperation openOperation,QObject *parent)
    : QObject(parent),m_frameExporter(std::make_shared<EditorFrameExporter>()),
      m_pagePreviews(std::make_unique<EditorPagePreviews>(std::move(previewOperation))) {
    connect(this, &EditorProject::errorChanged, this, &EditorProject::fileInformationChanged);
    connect(m_pagePreviews.get(), &EditorPagePreviews::ready, this, [this](int index) {
        if(m_canvas&&m_canvas->liveStrokeActive())m_previewNotificationPending=true;
        else emit pagePreviewReady(index);
    });
    replace(std::make_unique<EditorCanvas>(m_frameExporter,nullptr));
    m_documentOpen=std::make_unique<EditorDocumentOpenRequest>(std::move(openOperation),nullptr);m_documentOpen->setProject(this);
    connect(m_documentOpen.get(),&EditorDocumentOpenRequest::changed,this,[this] {
        const QPointer<EditorProject> guard(this);const auto epoch=m_documentOpen->generation();
        if(opening()&&!m_error.isEmpty()){m_error.clear();emit errorChanged();}
        if(guard&&m_documentOpen->generation()==epoch)emit documentOpenChanged();
    });
    connect(m_documentOpen.get(),&EditorDocumentOpenRequest::ready,this,&EditorProject::documentOpened);
    connect(m_documentOpen.get(),&EditorDocumentOpenRequest::cancelled,this,&EditorProject::documentOpenCancelled);
    connect(m_documentOpen.get(),&EditorDocumentOpenRequest::rejected,this,[this](const QString &message){fail(message);});
    connect(m_documentOpen.get(),&EditorDocumentOpenRequest::failed,this,[this](const QString &message) {
        const QPointer<EditorProject> guard(this);const auto epoch=m_documentOpen->generation();fail(message);
        if(guard&&m_documentOpen->generation()==epoch&&!opening())emit documentOpenFailed(message);
    });
}
EditorProject::~EditorProject() { m_documentOpen.reset();disconnect(m_canvas.get(), nullptr, this, nullptr); }
int EditorProject::canvasCount() const { return m_canvas->documentReady() ? std::max(1, int(m_canvas->document()->artboards.size())) : 1; }
QVariantList EditorProject::artboards() const {
    QVariantList result;
    if (!m_canvas->documentReady()) return result;
    for (const auto &board : m_canvas->document()->artboards) result.append(QVariantMap{
        {"id", QString::fromStdString(board.id)}, {"name", QString::fromStdString(board.name)},
        {"x", board.region.origin.x}, {"y", board.region.origin.y},
        {"width", board.region.extent.width}, {"height", board.region.extent.height},
        {"selected", int(result.size()) == currentIndex()}});
    return result;
}
void EditorProject::invalidatePagePreviews(bool force) {
    // A live stroke changes pixels, but its presentation notification waits for
    // completion. Direct requests remain keyed by the current native revision.
    if (m_canvas->liveStrokeActive()) { m_previewNotificationPending = true; return; }
    const auto revision = m_canvas->revision();
    const auto frame = m_canvas->frame();
    const bool ready = m_canvas->documentReady();
    if (force || !m_previewStateValid || revision != m_lastPreviewCanvasRevision
        || ready != m_lastPreviewDocumentReady) {
        m_pagePreviews->invalidate();m_pageSnapshot.reset();m_pageVisibility.reset();
        m_lastPreviewCanvasRevision = revision;
        m_lastPreviewDocumentReady = ready;
        m_previewStateValid = true;
        m_previewNotificationPending = true;
        m_timeDependentPreviews.assign(canvasCount(), false);
        if (ready) {
            const auto &document = *m_canvas->document();
            QHash<QString, int> pages;
            for (int i = 0; i < int(document.artboards.size()); ++i)
                pages.insert(QString::fromStdString(document.artboards[i].id), i);
            for (const auto &layer : document.layers) {
                const auto &p = iiSharedCanvas::layerProperties(layer);
                if (!p.visible) continue;
                const bool timed = iiSharedCanvas::layerTiming(layer) == iiSharedCanvas::LayerTiming::Dynamic
                    || iiSharedCanvas::contentKind(layer) == iiSharedCanvas::ContentKind::Video
                    || !p.motion.empty() || (p.frameRange && (p.frameRange->firstFrame != 0
                        || p.frameRange->lastFrame + 1 != document.timeline.frameCount));
                if (!timed) continue;
                if (document.artboards.empty()) m_timeDependentPreviews[0] = true;
                else if (p.artboardId) {
                    const auto page = pages.constFind(QString::fromStdString(*p.artboardId));
                    if (page != pages.cend()) m_timeDependentPreviews[*page] = true;
                }
            }
        }
    }
    if (frame != m_lastPreviewFrame && std::ranges::any_of(m_timeDependentPreviews, [](bool timed) { return timed; }))
        m_previewNotificationPending = true;
    m_lastPreviewFrame = frame;
    // Direct preview requests still use the current native revision. Only
    // presentation bindings wait until the live stroke ends or is cancelled.
    if (!m_previewNotificationPending || m_canvas->liveStrokeActive()) return;
    m_previewNotificationPending = false;
    ++m_pagePreviewRevision;
    emit pagePreviewsChanged();
}
QUrl EditorProject::pagePreviewSource(int index, int maxWidth, int maxHeight) {
    if (!m_canvas->documentReady() || m_canvas->infiniteCanvas() || index < 0 || index >= canvasCount()) return {};
    const auto *document = m_canvas->document();
    const auto &boards = document->artboards;
    const auto extent = boards.empty() ? documentViewRegion(*document).extent : boards[index].region.extent;
    if (extent.width <= 0 || extent.height <= 0) return {};
    const auto scale = std::min(double(std::clamp(maxWidth, 1, 1024)) / extent.width,
                                double(std::clamp(maxHeight, 1, 1024)) / extent.height);
    const CanvasExtent output{std::max(1, int(std::floor(extent.width * scale))),
                              std::max(1, int(std::floor(extent.height * scale)))};
    const auto sampleFrame = index < int(m_timeDependentPreviews.size()) && !m_timeDependentPreviews[index]
        ? iiSharedCanvas::FrameIndex{0} : m_canvas->frame();
    const auto key = QString::number(index) + ':' + QString::number(m_canvas->revision()) + ':'
        + QString::number(sampleFrame) + ':' + QString::number(output.width) + 'x' + QString::number(output.height);
    try {
        if(!m_pageSnapshot||m_pageSnapshotRevision!=m_canvas->revision()) {
            m_pagePreviews->invalidate();
            auto visibility=std::make_shared<EditorPagePreviews::Visibility>();
            visibility->artboards.reserve(document->artboards.size());visibility->layers.reserve(document->layers.size());
            for(const auto &board:document->artboards)visibility->artboards.push_back(board.visible);
            for(const auto &layer:document->layers)visibility->layers.push_back(layerProperties(layer).visible);
            m_pageVisibility=std::move(visibility);
            m_pageSnapshot=m_canvas->editingFrameSnapshot().document;
            m_pageSnapshotRevision=m_canvas->revision();
        }
        return m_pagePreviews->source(key,{m_pageSnapshot,sampleFrame,
            boards.empty()?std::string{}:boards[index].id,output,index,m_pageVisibility});
    } catch (const std::exception &) {
        // Optional presentation must not change the document's editing error.
        return {};
    }
}
void EditorProject::attachCanvas(QQuickItem *surface) {
    if (!surface) return;
    const bool resized = m_canvas->size() != surface->size();
    m_canvas->setSmoothRendering(true); m_canvas->setParentItem(surface); m_canvas->setSize(surface->size());
    if (resized && m_canvas->documentReady()) m_canvas->fitToView();
}
QString EditorProject::filePath() const { return m_canvas->filePath(); }
QString EditorProject::documentName() const { return m_canvas->documentName(); }
bool EditorProject::fail(const QString &message) { m_error = message; emit errorChanged(); return false; }
void EditorProject::replace(std::unique_ptr<EditorCanvas> canvas, int index) {
    if (m_canvas) { m_canvas->setVisible(false); m_canvas->setParentItem(nullptr); disconnect(m_canvas.get(), nullptr, this, nullptr); }
    auto previous = std::move(m_canvas);
    m_canvas = std::move(canvas); m_error.clear();
    invalidatePagePreviews(true);
    m_canvas->setParent(this); QQmlEngine::setObjectOwnership(m_canvas.get(), QQmlEngine::CppOwnership);
    m_canvas->setObjectName("editorBlankCanvas"); m_canvas->setVisible(true);
    if (m_canvas->documentReady() && !m_canvas->document()->artboards.empty()) m_canvas->selectArtboard(index, false);
    m_modified = m_canvas->filePath().isEmpty() || !m_canvas->documentSynchronized();
    connect(m_canvas.get(), &EditorCanvas::errorChanged, this, &EditorProject::errorChanged);
    const auto invalidatePreviews = [this] { invalidatePagePreviews(); };
    connect(m_canvas.get(), &EditorCanvas::revisionChanged, this, invalidatePreviews);
    connect(m_canvas.get(), &EditorCanvas::frameChanged, this, invalidatePreviews);
    connect(m_canvas.get(), &EditorCanvas::documentChanged, this, invalidatePreviews);
    connect(m_canvas.get(), &EditorCanvas::liveStrokeActiveChanged, this, invalidatePreviews);
    connect(m_canvas.get(), &EditorCanvas::documentInfoChanged, this, &EditorProject::projectChanged);
    connect(m_canvas.get(), &EditorCanvas::documentInfoChanged, this, &EditorProject::navigationChanged);
    connect(m_canvas.get(), &EditorCanvas::fileInformationChanged, this, &EditorProject::fileInformationChanged);
    connect(m_canvas.get(), &EditorCanvas::fileStateChanged, this, [this] {
        m_modified = m_canvas->filePath().isEmpty() || !m_canvas->documentSynchronized(); emit projectChanged();
    });
    connect(m_canvas.get(), &EditorCanvas::activeArtboardChanged, this, &EditorProject::navigationChanged);
    connect(m_canvas.get(), &EditorCanvas::revisionChanged, this, [this] { m_modified = m_canvas->filePath().isEmpty() || !m_canvas->documentSynchronized(); emit projectChanged(); });
    emit currentCanvasChanged(); emit projectChanged(); emit navigationChanged(); emit fileInformationChanged(); emit errorChanged();
}
bool EditorProject::setCurrentIndex(int index) {
    if (index < 0 || index >= canvasCount()) return fail(tr("Choose an existing artboard."));
    if (!m_canvas->selectArtboard(index)) return fail(m_canvas->error());
    m_error.clear(); emit errorChanged(); return true;
}
bool EditorProject::createCanvas(const QVariantMap &specification) {
    auto canvas = std::make_unique<EditorCanvas>(m_frameExporter,nullptr);
    if (!canvas->createCanvas(specification)) return fail(canvas->error());
    replace(std::move(canvas)); return true;
}
bool EditorProject::openImages(const QVariantList &sources) {
    try {
        return adoptImageImport(dreamscapes::prepareImageImport(imageImportRequest(sources,false)),false);
    } catch(const std::exception &error){return fail(QString::fromUtf8(error.what()));}
}
bool EditorProject::attachImages(const QVariantList &sources) {
    if(!m_canvas->documentReady())return openImages(sources);
    try {
        return adoptImageImport(dreamscapes::prepareImageImport(imageImportRequest(sources,true)),true);
    } catch(const std::exception &error){return fail(QString::fromUtf8(error.what()));}
}
dreamscapes::ImageImportRequest EditorProject::imageImportRequest(const QVariantList &sources,bool attached) const {
    return importRequest(attached&&m_canvas->documentReady()?m_canvas.get():nullptr,sources);
}
bool EditorProject::adoptImageImport(dreamscapes::ImageImportResult imported,bool attached) {
    if(!imported.ok())return fail(QString::fromStdString(imported.error));
    if(attached&&m_canvas->documentReady()) {
        const int firstNew=imported.firstNew;
        if(!m_canvas->applyArtboardImport(std::move(imported.document)))return fail(m_canvas->error());
        m_canvas->selectArtboard(firstNew,false);m_canvas->fitToView();return setCurrentIndex(firstNew);
    }
    auto canvas=std::make_unique<EditorCanvas>(m_frameExporter,nullptr);
    if(!canvas->loadProjectPage(std::move(imported.document),tr("Image Project")))return fail(canvas->error());
    replace(std::move(canvas));return true;
}
bool EditorProject::openDocumentSource(const QUrl &source, bool asCopy) {
    return m_documentOpen->start(source,asCopy);
}
bool EditorProject::opening() const {return m_documentOpen&&m_documentOpen->active();}
iiSocietyContainer::Container *EditorProject::container() const {return m_documentOpen?m_documentOpen->container():nullptr;}
void EditorProject::setContainer(iiSocietyContainer::Container *value) {
    if(container()==value)return;
    const QPointer<EditorProject> guard(this);m_documentOpen->setContainer(value);if(guard)emit documentOpenChanged();
}
void EditorProject::cancelDocumentOpen() {m_documentOpen->cancel();}
bool EditorProject::adoptDocumentOpen(dreamscapes::DocumentOpenResult result) {
    if(!result.ok())return fail(QString::fromStdString(result.error));
    const auto active=result.activeArtboard;
    auto canvas = std::make_unique<EditorCanvas>(m_frameExporter,nullptr);
    if(!canvas->adoptDocumentOpen(std::move(result)))return fail(canvas->error());
    replace(std::move(canvas),active);return true;
}
bool EditorProject::saveDocumentAs(const QUrl &destination) {
    if (!m_canvas->saveDocumentAs(destination)) return fail(m_canvas->error());
    m_modified = false; m_error.clear(); emit projectChanged(); emit errorChanged(); return true;
}
bool EditorProject::saveDocument() {
    if (filePath().isEmpty()) return fail(tr("Choose an .iisc path with Save As."));
    return saveDocumentAs(QUrl::fromLocalFile(filePath()));
}
