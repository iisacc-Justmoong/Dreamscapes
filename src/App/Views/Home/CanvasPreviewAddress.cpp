#include "CanvasPreviewAddress.h"
#include "App/Generation/ImageCanvas.h"
#include <iiFileProviderPresentation.h>
#include <QFileInfo>
#include <exception>
#include <filesystem>

namespace dreamscapes {
QString canvasFileVersion(const QString &path) {
    // Nanosecond-capable timestamps and WAL identity avoid stale same-size edits.
    QString version;
    for (const auto suffix : {QString{}, QStringLiteral("-wal")}) {
        const auto native = std::filesystem::path((path + suffix).toStdU16String());
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(native, error);
        if (error) { version += ":missing"; continue; }
        const auto size = std::filesystem::file_size(native, error);
        version += ':' + QString::number(static_cast<qlonglong>(modified.time_since_epoch().count())) + ':'
            + QString::number(static_cast<qulonglong>(error ? 0 : size));
    }
    try {
        version += ':' + QString::fromStdString(iiFileProvider::native::Presentation::cacheChangeToken(
            std::filesystem::path(path.toStdU16String())));
    } catch (const std::exception &) { version += ":unavailable"; }
    return version;
}
QUrl canvasPreviewSource(const QVariantMap &file) {
    const auto existing = file.value("previewSource").toUrl();
    if (file.contains("available") && !file.value("available").toBool()) return existing;
    auto source = file.value("source").toUrl();
    if (source.isEmpty()) source = QUrl::fromLocalFile(file.value("path").toString());
    if (!source.isLocalFile()) return existing;
    const auto path = source.toLocalFile();
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.suffix().compare("iisc", Qt::CaseInsensitive) != 0) return existing;
    return imageCanvasPreviewSource(path, file.value("version").toString() + canvasFileVersion(path));
}
} // namespace dreamscapes
