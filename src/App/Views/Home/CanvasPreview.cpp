#include "CanvasPreview.h"

#include <iiSharedCanvas>
#include <QFileInfo>
#include <QImage>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QUrlQuery>

#include <algorithm>

namespace {
constexpr auto ProviderName = "iisc-preview";

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

void dreamscapes::installCanvasPreviewProvider(QQmlEngine &engine)
{
    if (!engine.imageProvider(ProviderName)) engine.addImageProvider(ProviderName, new CanvasPreviewImageProvider);
}

QUrl CanvasPreview::sourceForFile(const QVariantMap &file)
{
    if (auto *engine = qmlEngine(this)) dreamscapes::installCanvasPreviewProvider(*engine);
    return dreamscapes::canvasPreviewSource(file);
}

QUrl CanvasPreview::sourceForUrl(const QUrl &source)
{
    if (!source.isLocalFile()) return {};
    const auto path = source.toLocalFile();
    const QFileInfo file(path);
    if (!file.isAbsolute()) return {};
    if (file.suffix().compare("iisc", Qt::CaseInsensitive) == 0)
        return sourceForFile({{"source", source}});
    auto result = source;
    QUrlQuery query(result);
    query.removeAllQueryItems("_dreamscapes_source");
    query.addQueryItem("_dreamscapes_source", dreamscapes::canvasFileVersion(path));
    result.setQuery(query);
    return result;
}
