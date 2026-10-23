#include "App/Generation/ImageCanvas.h"
#include <SharedStorage.h>
#include <FileOperations.h>
#include <iiSocietyHelper.h>
#include <File.h>
#include <iiFileProviderDevice.h>
#include <iiSharedCanvas/Bitmap/BitmapCodec.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTextStream>
#include <filesystem>
#include <stdexcept>

// One-time repair for versions that published generated fill canvases instead
// of PNG originals. Never remove canvases or replace an existing PNG.
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    const bool moveCanvases = args.size() == 3 && args[1] == "--move-canvases";
    const bool apply = moveCanvases || (args.size() == 3 && args[1] == "--apply");
    if (args.size() != (apply ? 3 : 2)) {
        QTextStream(stderr) << "Usage: DreamscapesHistoryRepair [--apply|--move-canvases] <absolute-Society-package>\n"; return 2;
    }
    const auto root = args.last();
    QString error;
    const auto storage = iiSocietyContainer::SharedStorage::open(root, &error);
    iiSocietyHelper::FileSystem files;
    if (!storage || !files.open(root)) {
        QTextStream(stderr) << (storage ? files.errorString() : error) << '\n'; return 2;
    }
    const auto historyPath = files.path("generation-history");
    if (historyPath.isEmpty()) { QTextStream(stderr) << files.errorString() << '\n'; return 2; }
    const QDir history(historyPath);
    const QRegularExpression generated("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}-[0-9]{4}\\.iisc$",
        QRegularExpression::CaseInsensitiveOption);
    QJsonArray rows;
    int restored = 0, moved = 0, failures = 0;
    for (const auto &entry : history.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name)) {
        if (!generated.match(entry.fileName()).hasMatch() || entry.canonicalFilePath() != entry.absoluteFilePath()) continue;
        const auto name = entry.completeBaseName() + ".png";
        const auto destination = files.path("generation-history", name);
        QJsonObject row{{"source", entry.absoluteFilePath()}, {"png", destination}};
        if (destination.isEmpty()) {
            row["status"] = "error"; row["error"] = files.errorString(); ++failures; rows.append(row); continue;
        }
        if (moveCanvases) {
            try {
                if (!QFileInfo(destination).isFile() || QFileInfo(destination).isSymLink()
                    || !QImageReader(destination).canRead()) throw std::runtime_error("Restore the PNG before moving its original canvas.");
                const auto original = iiFileProvider::File::read(entry.absoluteFilePath(), iiSharedCanvas::MediaLimits{}.maxOutputBytes);
                iiSocietyContainer::FileOperations operations(storage->drive());
                const auto result = operations.perform(iiSocietyContainer::FileOperations::Action::Move,
                    entry.absoluteFilePath(), files.path("files"));
                if (!result) throw std::runtime_error(result.error.toStdString());
                if (iiFileProvider::File::read(result.path, original.size()) != original)
                    throw std::runtime_error("The moved canvas bytes differ.");
                row["status"] = "moved to Files"; row["canvas"] = result.path; row["sourcePreserved"] = true;
                row["sourceSha256"] = QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex());
                ++moved;
            } catch (const std::exception &exception) {
                row["status"] = "error"; row["error"] = QString::fromUtf8(exception.what()); ++failures;
            }
            rows.append(row); continue;
        }
        if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()) {
            row["status"] = "existing PNG preserved"; rows.append(row); continue;
        }
        if (!apply) { row["status"] = "candidate"; rows.append(row); continue; }
        try {
            const auto original = iiFileProvider::File::read(entry.absoluteFilePath(), iiSharedCanvas::MediaLimits{}.maxOutputBytes);
            const auto document = dreamscapes::readImageCanvas(entry.absoluteFilePath());
            if (!document.ok()) throw std::runtime_error(document.result.message);
            const auto &native = document.document;
            if (native.layers.size() != 1 || native.assets.size() != 1
                || !std::holds_alternative<iiSharedCanvas::StaticBitmapLayer>(native.layers[0])
                || !std::holds_alternative<iiSharedCanvas::RasterAsset>(native.assets[0])
                || iiSharedCanvas::layerProperties(native.layers[0]).id != "image.fill.layer")
                throw std::runtime_error("This document is not an original generated fill canvas.");
            const auto runtime = storage->ensureRuntimeDirectory("history-repair", &error);
            const auto preview = runtime.isEmpty() ? QString{} : dreamscapes::imageCanvasPreview(entry.absoluteFilePath(), runtime, &error);
            if (preview.isEmpty()) throw std::runtime_error(error.toStdString());
            const QImage png(preview);
            const auto &pixels = std::get<iiSharedCanvas::RasterAsset>(native.assets[0]).pixels;
            if (png.size() != QSize(pixels.width, pixels.height)) throw std::runtime_error("The restored dimensions differ.");
            for (int y = 0; y < png.height(); ++y)
                for (int x = 0; x < png.width(); ++x)
                    if (png.pixel(x, y) != pixels.pixels[std::size_t(y) * png.width() + x])
                        throw std::runtime_error("The document has been edited; its original pixels were preserved.");
            if (iiFileProvider::File::read(entry.absoluteFilePath(), original.size()) != original)
                throw std::runtime_error("The source changed during repair.");
            iiSharedCanvas::BitmapExportOptions options;
            options.format = "png"; options.extendedCodecs = false;
            if (native.stableDiffusionMetadata)
                for (const auto &entry : native.stableDiffusionMetadata->extraParameters)
                    options.text.push_back({entry.key, entry.value});
            const auto encoded = iiSharedCanvas::encodeBitmap(pixels, options);
            if (!encoded.ok()) throw std::runtime_error(encoded.result.message);
            iiFileProvider::TemporaryDirectory restoredDirectory(QDir(runtime).filePath("png-XXXXXX"));
            if (!restoredDirectory.isValid()) throw std::runtime_error("Cannot create a private PNG repair directory.");
            const auto restoredPng = restoredDirectory.filePath("original.png");
            iiFileProvider::File::create(restoredPng, QByteArray(reinterpret_cast<const char *>(encoded.bytes.data()), qsizetype(encoded.bytes.size())));
            if (files.publishGeneratedImage(restoredPng, name) != destination) throw std::runtime_error(files.errorString().toStdString());
            // Keep chronological History order rather than assigning repair time.
            std::filesystem::last_write_time(std::filesystem::path(destination.toStdU16String()),
                std::filesystem::last_write_time(std::filesystem::path(entry.absoluteFilePath().toStdU16String())));
            row["status"] = "restored"; row["width"] = png.width(); row["height"] = png.height();
            row["pixelsMatch"] = true; row["sourcePreserved"] = true;
            row["sourceSha256"] = QString::fromLatin1(QCryptographicHash::hash(original, QCryptographicHash::Sha256).toHex());
            ++restored;
        } catch (const std::exception &exception) {
            row["status"] = "error"; row["error"] = QString::fromUtf8(exception.what()); ++failures;
        }
        rows.append(row);
    }
    QTextStream(stdout) << QJsonDocument(QJsonObject{{"container", files.rootPath()}, {"apply", apply},
        {"restored", restored}, {"moved", moved}, {"errors", failures}, {"entries", rows}}).toJson();
    return failures ? 1 : 0;
}
