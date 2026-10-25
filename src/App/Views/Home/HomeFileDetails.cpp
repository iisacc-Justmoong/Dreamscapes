#include "HomeFileDetails.h"
#include <iiSharedCanvas/File/DocumentFile.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QPointer>
#include <QThreadPool>
#include <QUrl>

void HomeFileDetails::setFile(const QVariantMap &file)
{
    if (m_file == file) return;
    m_file = file;
    m_metadata.clear();
    const auto request = ++*m_request;
    auto source = file.value("source").toUrl();
    if (source.isEmpty()) source = QUrl::fromLocalFile(file.value("path").toString());
    m_loading = source.isLocalFile() && (!file.contains("available") || file.value("available").toBool());
    emit fileChanged();
    emit metadataChanged();
    if (!m_loading) return;
    const auto path = source.toLocalFile();
    const auto generation = m_request;
    const QPointer<HomeFileDetails> guard(this);
    QThreadPool::globalInstance()->start([path, generation, request, guard] {
        if (*generation != request) return;
        QVariantMap result;
        const QFileInfo info(path);
        if (info.isFile() && info.suffix().compare("iisc", Qt::CaseInsensitive) == 0) {
            // Bound inspection memory; large artwork remains previewable and
            // openable even when detailed metadata cannot safely be decoded.
            if (info.size() <= 64 * 1024 * 1024) {
                iiSharedCanvas::SerializationLimits limits;
                limits.maximumContainerBytes = 64 * 1024 * 1024;
                limits.maximumTotalRasterPixels = 16 * 1024 * 1024;
                iiSharedCanvas::Document document;
                if (iiSharedCanvas::DocumentFile::readSnapshot(path.toUtf8().toStdString(), document, limits).ok()) {
                    result = {{"width", document.extent.width}, {"height", document.extent.height},
                        {"layerCount", int(document.layers.size())},
                        {"pageCount", int(document.artboards.empty() ? 1 : document.artboards.size())},
                        {"frameCount", document.timeline.frameCount}, {"colorSpace", "ARGB 8-bit"}};
                }
            }
        } else if (info.isFile()) {
            QImageReader reader(path);
            const auto size = reader.size(); // Header only, no full raster decode.
            if (size.isValid()) result = {{"width", size.width()}, {"height", size.height()}};
        }
        if (*generation != request) return;
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, generation, request, result] {
            if (!guard || *generation != request) return;
            guard->m_metadata = result;
            guard->m_loading = false;
            emit guard->metadataChanged();
        }, Qt::QueuedConnection);
    });
}
