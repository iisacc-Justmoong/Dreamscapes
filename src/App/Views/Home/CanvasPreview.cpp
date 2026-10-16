#include "CanvasPreview.h"

#include <iiSharedCanvas/Render/DocumentPreview.h>
#include <QFileInfo>
#include <QImage>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QUrlQuery>

#include <algorithm>
#include <filesystem>

namespace {
constexpr auto ProviderName = "iisc-preview";

QString fileVersion(const QString &path)
{
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
    return version;
}

class CanvasPreviewImageProvider final : public QQuickImageProvider {
public:
    CanvasPreviewImageProvider() : QQuickImageProvider(Image, ForceAsynchronousImageLoading) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override
    {
        if (size) *size = {};
        const auto encoded = id.section('?', 0, 0).toLatin1();
        const auto path = QString::fromUtf8(QByteArray::fromBase64(encoded,
            QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors));
        const QFileInfo file(path);
        if (!file.isAbsolute() || file.suffix().compare("iisc", Qt::CaseInsensitive) != 0) return {};
        const iiSharedCanvas::CanvasExtent maximum{
            std::clamp(requestedSize.width() > 0 ? requestedSize.width() : 512, 1, 2048),
            std::clamp(requestedSize.height() > 0 ? requestedSize.height() : 512, 1, 2048)};
        auto preview = iiSharedCanvas::readIiscPreview(path.toUtf8().toStdString(), {maximum});
        if (!preview.ok()) return {};
        const auto &pixels = preview.pixels;
        // Detach from the SDK-owned pixel buffer before returning to Qt Quick.
        auto image = QImage(reinterpret_cast<const uchar *>(pixels.pixels.data()), pixels.width,
            pixels.height, qsizetype(pixels.width) * 4, QImage::Format_ARGB32).copy();
        if (size) *size = image.size();
        return image;
    }
};
}

QUrl dreamscapes::canvasPreviewSource(const QVariantMap &file)
{
    const auto existing = file.value("previewSource").toUrl();
    if (file.contains("available") && !file.value("available").toBool()) return existing;
    auto source = file.value("source").toUrl();
    if (source.isEmpty()) source = QUrl::fromLocalFile(file.value("path").toString());
    if (!source.isLocalFile()) return existing;
    const auto path = source.toLocalFile();
    const QFileInfo info(path);
    if (!info.isAbsolute() || info.suffix().compare("iisc", Qt::CaseInsensitive) != 0) return existing;
    const auto encoded = path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QUrl result(QStringLiteral("image://iisc-preview/") + QString::fromLatin1(encoded));
    QUrlQuery query;
    query.addQueryItem("revision", file.value("version").toString() + fileVersion(path));
    result.setQuery(query);
    return result;
}

void dreamscapes::installCanvasPreviewProvider(QQmlEngine &engine)
{
    if (!engine.imageProvider(ProviderName)) engine.addImageProvider(ProviderName, new CanvasPreviewImageProvider);
}

QUrl CanvasPreview::sourceForFile(const QVariantMap &file)
{
    if (auto *engine = qmlEngine(this)) dreamscapes::installCanvasPreviewProvider(*engine);
    return dreamscapes::canvasPreviewSource(file);
}
