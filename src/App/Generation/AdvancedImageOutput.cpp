#include "AdvancedImageOutput.h"
#include <Generation/ImageWatermark.hpp>
#include <QColorSpace>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <algorithm>

namespace dreamscapes {
bool writeAdvancedImage(const QString &source, QIODevice *destination, const QJsonObject &parameters,
    qint64 resolvedSeed, QString *error)
{
    QImageReader reader(source);
    auto image = reader.read();
    if (image.isNull()) { *error = reader.errorString(); return false; }
    // The native RGB output is sRGB when no explicit profile is attached.
    if (!image.colorSpace().isValid()) image.setColorSpace(QColorSpace::SRgb);
    const auto target = parameters.value("colorProfile").toString() == "Display P3"
        ? QColorSpace(QColorSpace::DisplayP3) : QColorSpace(QColorSpace::SRgb);
    image.convertToColorSpace(target);
    if (parameters.value("watermark").toBool()) {
        // QImage remains the existing codec/color-management boundary. Geometry,
        // resampling and compositing execute in the Qt-free SDK.
        QImage mark(QStringLiteral(":/generation/watermark/Dreamscapes.png"));
        if (mark.isNull()) { *error = QStringLiteral("The Dreamscapes watermark asset is unavailable."); return false; }
        if (!mark.colorSpace().isValid()) mark.setColorSpace(QColorSpace::SRgb);
        mark.convertToColorSpace(target);
        mark = mark.convertToFormat(QImage::Format_RGBA8888);
        image = image.convertToFormat(QImage::Format_RGBA8888);
        const int shorter = std::min(image.width(), image.height());
        const int side = std::clamp(shorter / 16, 1, 128);
        const int margin = std::min(std::max(1, shorter / 64), (shorter - side) / 2);
        std::string nativeError;
        if (!iiLocalDiffusion::compositeImageWatermark(
                {{image.bits(), size_t(image.sizeInBytes())}, image.width(), image.height(), size_t(image.bytesPerLine())},
                {{mark.constBits(), size_t(mark.sizeInBytes())}, mark.width(), mark.height(), size_t(mark.bytesPerLine())},
                {image.width() - side - margin, image.height() - side - margin, side, side, .55}, &nativeError)) {
            *error = QString::fromStdString(nativeError); return false;
        }
    }
    if (parameters.value("preserveMetadata").toBool()) {
        auto recipe = parameters; recipe["resolvedSeed"] = resolvedSeed;
        image.setText("Dreamscapes.Parameters", QString::fromUtf8(QJsonDocument(recipe).toJson(QJsonDocument::Compact)));
    } else {
        // A pixel-only wrapper avoids copying text/EXIF metadata. Keep the
        // selected ICC profile, which is required to interpret the pixel values.
        QImage pixels(image.constBits(), image.width(), image.height(), image.bytesPerLine(), image.format());
        auto stripped = pixels.copy(); stripped.setColorSpace(target); image = std::move(stripped);
    }
    QImageWriter writer(destination, QFileInfo(source).suffix().toLatin1());
    if (!writer.write(image)) { *error = writer.errorString(); return false; }
    return true;
}
}
