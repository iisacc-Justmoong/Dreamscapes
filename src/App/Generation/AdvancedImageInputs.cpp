#include "AdvancedImageInputs.h"
#include <QFileInfo>
#include <QImageReader>
#include <QJsonValue>
#include <QJsonObject>
#include <QFile>
#include <algorithm>

namespace dreamscapes {
static bool decodeImages(const QJsonArray &sources,
    iiLocalDiffusion::NativeAdvancedControls *controls, QString *error,
    const std::atomic_bool &cancelled,
    const std::shared_ptr<iiLocalDiffusion::NativeExecutionControl> &control, bool mask)
{
    std::vector<iiLocalDiffusion::NativeReferenceImage> images;
    const auto runnable = [&] { return control ? control->waitUntilRunnable(cancelled) : !cancelled.load(); };
    if (sources.size() > 20) { *error = "At most 20 reference images are allowed."; return false; }
    for (const auto &source : sources) {
        if (!runnable()) { *error = "Reference image loading was cancelled."; return false; }
        const QFileInfo info(source.toString());
        if (!info.isFile() || info.canonicalFilePath() != source.toString()) {
            *error = "Reference image is no longer an available canonical local file: " + source.toString(); return false;
        }
        QImageReader reader(info.filePath());
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (size.isEmpty() || qint64(size.width()) * size.height() > 64 * 1024 * 1024) {
            *error = "Reference image has invalid dimensions or exceeds 64 megapixels: " + info.fileName(); return false;
        }
        if (size.width() > 2048 || size.height() > 2048)
            reader.setScaledSize(size.scaled(2048, 2048, Qt::KeepAspectRatio));
        auto image = reader.read();
        if (image.isNull()) { *error = "Cannot decode reference image: " + reader.errorString(); return false; }
        if (image.width() > 2048 || image.height() > 2048)
            image = image.scaled(2048, 2048, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (mask) {
            image = image.convertToFormat(QImage::Format_RGBA8888);
            for (int y = 0; y < image.height(); ++y) {
                auto *line = image.scanLine(y);
                for (int x = 0; x < image.width(); ++x) {
                    auto *pixel = line + x * 4;
                    const int luminance = 77 * pixel[0] + 150 * pixel[1] + 29 * pixel[2];
                    const auto coverage = static_cast<uchar>((luminance * pixel[3] + 32640) / 65280);
                    pixel[0] = pixel[1] = pixel[2] = coverage; pixel[3] = 255;
                }
            }
        }
        image = image.convertToFormat(QImage::Format_RGB888);
        iiLocalDiffusion::NativeReferenceImage result;
        result.width = image.width(); result.height = image.height();
        const auto row = std::size_t(result.width) * 3;
        result.rgb.resize(row * result.height);
        for (int y = 0; y < result.height; ++y)
            std::copy_n(image.constScanLine(y), row, result.rgb.data() + y * row);
        images.push_back(std::move(result));
    }
    if (!runnable()) { *error = "Reference image loading was cancelled."; return false; }
    controls->references = std::move(images);
    return true;
}
bool decodeAdvancedReferences(const QJsonArray &sources,
    iiLocalDiffusion::NativeAdvancedControls *controls, QString *error,
    const std::atomic_bool &cancelled,
    const std::shared_ptr<iiLocalDiffusion::NativeExecutionControl> &control)
{
    return decodeImages(sources, controls, error, cancelled, control, false);
}
bool decodeAdvancedControlImages(const QJsonArray &sources,
    iiLocalDiffusion::NativeAdvancedControls *controls, QString *error,
    const std::atomic_bool &cancelled,
    const std::shared_ptr<iiLocalDiffusion::NativeExecutionControl> &control)
{
    std::vector<iiLocalDiffusion::NativeAdvancedControls::ControlNet> decoded;
    std::vector<iiLocalDiffusion::NativeAdvancedControls::IPAdapter> adapters;
    if (sources.size() > 64) { *error = "At most 64 ControlNet entries are allowed."; return false; }
    for (const auto &source : sources) {
        const auto item = source.toObject();
        if (!item.value("applied").toBool()) continue;
        iiLocalDiffusion::NativeAdvancedControls scratch;
        if (!decodeAdvancedReferences({item.value("imageSource")}, &scratch, error, cancelled, control)) {
            *error = "ControlNet: " + *error; return false;
        }
        auto image = std::move(scratch.references.front());
        iiLocalDiffusion::NativeReferenceImage mask;
        if (item.value("regionalMask").toBool()) {
            if (!decodeImages({item.value("maskSource")}, &scratch, error, cancelled, control, true)) {
                *error = "ControlNet regional mask: " + *error; return false;
            }
            mask = std::move(scratch.references.front());
        }
        const auto process = item.value("process").toString();
        const auto weight = float(item.value("weight").toDouble());
        const bool ipAdapter = item.value("ipAdapter").toBool();
        const bool controlNet = process != "None" && process != "IP-Adapter";
        if (!controlNet && !ipAdapter) { *error = "Applied control has no conditioning enabled."; return false; }
        if (ipAdapter) adapters.push_back({QFile::encodeName(item.value("ipAdapterModel").toString()).toStdString(),
            QFile::encodeName(item.value("ipAdapterVision").toString()).toStdString(), image, weight, mask});
        if (controlNet) decoded.push_back({QFile::encodeName(item.value("model").toString()).toStdString(),
            std::move(image), process.toStdString(), weight, std::move(mask),
            QFile::encodeName(item.value("poseDetector").toString()).toStdString(),
            QFile::encodeName(item.value("poseModel").toString()).toStdString()});
    }
    if (control ? !control->waitUntilRunnable(cancelled) : cancelled.load()) {
        *error = "ControlNet image loading was cancelled."; return false;
    }
    controls->controls = std::move(decoded);
    controls->ipAdapters = std::move(adapters);
    return true;
}
}
