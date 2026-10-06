#include "EditorProject.h"
#include "EditorProjectCodec.h"
#include <iiFileProvider.h>
#include <iiSharedCanvas/Media/MediaIo.h>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QQmlEngine>

namespace {
QString pathFor(const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() ? url.toString() : QString{}; }
}
EditorProject::EditorProject(QObject *parent) : QObject(parent) {
    std::vector<std::unique_ptr<EditorCanvas>> pages;
    pages.push_back(std::make_unique<EditorCanvas>());
    replace(std::move(pages));
}
void EditorProject::attachCanvas(QQuickItem *surface) {
    if (!surface) return;
    auto *canvas = currentCanvas();
    const bool resized = canvas->size() != surface->size();
    // Mount the full document item in its real window, so the SDK chooses
    // texture resolution from the destination display rather than a preview.
    canvas->setSmoothRendering(true);
    canvas->setParentItem(surface);
    canvas->setSize(surface->size());
    if (resized && canvas->documentReady()) canvas->fitToView();
}
EditorProject::~EditorProject() {
    // CanvasItem::unbind() emits revision signals during its destructor.
    // They must not call project getters while the page vector is being destroyed.
    for (const auto &canvas : m_pages) disconnect(canvas.get(), nullptr, this, nullptr);
}
QString EditorProject::filePath() const { return m_path.isEmpty() ? currentCanvas()->filePath() : m_path; }
QString EditorProject::documentName() const {
    return !m_path.isEmpty() ? QFileInfo(m_path).fileName()
        : canvasCount() > 1 ? tr("Image Project (%1 canvases)").arg(canvasCount()) : currentCanvas()->documentName();
}
bool EditorProject::fail(const QString &message) { m_error = message; emit errorChanged(); return false; }
void EditorProject::replace(std::vector<std::unique_ptr<EditorCanvas>> pages, int index, const QString &path) {
    if (!m_pages.empty()) { currentCanvas()->setVisible(false); currentCanvas()->setParentItem(nullptr); }
    for (const auto &canvas : m_pages) disconnect(canvas.get(), nullptr, this, nullptr);
    auto previous = std::move(m_pages); // Keep the old QObject alive until QML retargets its connections.
    m_pages = std::move(pages); m_index = index; m_path = path; m_error.clear();
    m_modified = path.isEmpty() && currentCanvas()->filePath().isEmpty();
    for (int i = 0; i < canvasCount(); ++i) {
        auto *canvas = m_pages[i].get();
        canvas->setParent(this);
        QQmlEngine::setObjectOwnership(canvas, QQmlEngine::CppOwnership);
        canvas->setObjectName(i == m_index ? "editorBlankCanvas" : QString("editorCanvasPage%1").arg(i));
        canvas->setVisible(i == m_index);
        connect(canvas, &EditorCanvas::errorChanged, this, &EditorProject::errorChanged);
        connect(canvas, &EditorCanvas::documentInfoChanged, this, &EditorProject::projectChanged);
        connect(canvas, &EditorCanvas::revisionChanged, this, [this] {
            m_modified = isProject() || currentCanvas()->filePath().isEmpty(); emit projectChanged();
        });
    }
    emit currentCanvasChanged(); emit projectChanged(); emit errorChanged();
}
bool EditorProject::setCurrentIndex(int index) {
    if (index < 0 || index >= canvasCount()) return fail(tr("Choose an existing canvas."));
    if (index == m_index) return true;
    if (currentCanvas()->liveStrokeActive()) return fail(tr("Finish the current stroke before changing canvases."));
    currentCanvas()->setVisible(false); currentCanvas()->setParentItem(nullptr);
    currentCanvas()->setObjectName(QString("editorCanvasPage%1").arg(m_index));
    m_index = index;
    currentCanvas()->setObjectName("editorBlankCanvas"); currentCanvas()->setVisible(true);
    m_error.clear(); emit currentCanvasChanged(); emit projectChanged(); emit errorChanged(); return true;
}
bool EditorProject::createCanvas(const QVariantMap &specification) {
    auto canvas = std::make_unique<EditorCanvas>();
    if (!canvas->createCanvas(specification)) return fail(canvas->error());
    std::vector<std::unique_ptr<EditorCanvas>> pages; pages.push_back(std::move(canvas));
    replace(std::move(pages)); return true;
}
bool EditorProject::openImages(const QVariantList &sources) {
    if (sources.isEmpty() || sources.size() > dreamscapes::MaximumProjectPages) return fail(tr("Select between 1 and 1000 images."));
    std::vector<std::unique_ptr<EditorCanvas>> pages;
    QSet<QString> paths;
    std::uint64_t pixels = 0;
    for (const auto &source : sources) {
        const auto path = pathFor(source.toUrl());
        if (path.isEmpty()) return fail(tr("Choose local images."));
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!canonical.isEmpty() && paths.contains(canonical)) continue;
        auto canvas = std::make_unique<EditorCanvas>();
        if (!canvas->openImages({source})) return fail(canvas->error());
        pixels += std::uint64_t(canvas->canvasWidth()) * canvas->canvasHeight();
        if (pixels > iiSharedCanvas::MediaLimits{}.maxPixelsPerFrame) return fail(tr("Selected images exceed the project import limit."));
        paths.insert(canonical); pages.push_back(std::move(canvas));
    }
    replace(std::move(pages)); return true;
}
bool EditorProject::openDocumentSource(const QUrl &source, bool asCopy) {
    const auto path = pathFor(source);
    if (path.isEmpty()) return fail(tr("Choose a local canvas or project."));
    if (QFileInfo(path).suffix().compare("iiscp", Qt::CaseInsensitive) == 0) {
        try {
            const auto bytes = iiFileProvider::File::read(path, iiSharedCanvas::SerializationLimits{}.maximumContainerBytes);
            auto decoded = dreamscapes::decodeProject({reinterpret_cast<const std::uint8_t *>(bytes.constData()), std::size_t(bytes.size())});
            if (!decoded.error.empty()) return fail(QString::fromStdString(decoded.error));
            std::vector<std::unique_ptr<EditorCanvas>> pages;
            for (auto &page : decoded.pages) {
                auto canvas = std::make_unique<EditorCanvas>();
                if (!canvas->loadProjectPage(std::move(page.document), QString::fromStdString(page.name))) return fail(canvas->error());
                pages.push_back(std::move(canvas));
            }
            replace(std::move(pages), decoded.active, asCopy ? QString{} : path); return true;
        } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
    }
    auto canvas = std::make_unique<EditorCanvas>();
    if (!canvas->openDocumentSource(source, asCopy)) return fail(canvas->error());
    std::vector<std::unique_ptr<EditorCanvas>> pages; pages.push_back(std::move(canvas));
    replace(std::move(pages)); return true;
}
bool EditorProject::saveDocumentAs(const QUrl &destination) {
    const auto path = pathFor(destination);
    if (path.isEmpty()) return fail(tr("Choose a local project path."));
    if (currentCanvas()->liveStrokeActive()) return fail(tr("Finish the stroke before saving."));
    if (QFileInfo(path).suffix().compare("iiscp", Qt::CaseInsensitive) != 0) {
        if (isProject()) return fail(tr("Save all canvases as an .iiscp project."));
        if (!currentCanvas()->saveDocumentAs(destination)) return fail(currentCanvas()->error());
    } else {
        std::vector<dreamscapes::ProjectPageView> pages;
        for (const auto &canvas : m_pages) pages.push_back({canvas->documentName().toStdString(), canvas->document()});
        const auto encoded = dreamscapes::encodeProject(pages, m_index);
        if (!encoded.error.empty()) return fail(QString::fromStdString(encoded.error));
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(reinterpret_cast<const char *>(encoded.bytes.data()), encoded.bytes.size()) != qint64(encoded.bytes.size()) || !file.commit())
            return fail(file.errorString());
        m_path = path;
    }
    m_modified = false; m_error.clear(); emit projectChanged(); emit errorChanged(); return true;
}
bool EditorProject::saveDocument() {
    if (filePath().isEmpty()) return fail(tr("Choose a project path with Save As."));
    return saveDocumentAs(QUrl::fromLocalFile(filePath()));
}
