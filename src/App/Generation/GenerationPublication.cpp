#include "GenerationPublication.h"
#include "AdvancedImageOutput.h"
#include "VideoGeneration.h"
#include <iiFileProvider.h>
#include <iiFileProviderNative.h>
#include <iiFileProviderStorage.h>
#include <SharedStorage.h>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <vector>

namespace dreamscapes {
namespace {
struct PublishedFile { QString name, path; qint64 bytes; QDateTime modified; };
struct PublicationRollback {
    const iiSocietyContainer::SharedStorage &storage;
    std::vector<PublishedFile> files;
    bool accepted = false;
    ~PublicationRollback() {
        if (accepted) return;
        for (const auto &file : files) {
            try {
                const auto path = storage.filePath(iiSocietyContainer::StoreSection::GenerationHistory, file.name);
                const QFileInfo current(path);
                if (path == file.path && current.isFile() && !current.isSymLink()
                    && current.size() == file.bytes && current.lastModified() == file.modified)
                    iiFileProvider::File::remove(path);
            } catch (...) { }
        }
    }
};
struct FinishControl {
    std::shared_ptr<PublicationControl> control;
    ~FinishControl() { control->finish(); }
};
}
GenerationPublicationResult publishGenerationResult(GenerationPublicationInput input) {
    GenerationPublicationResult result;
    if (!input.control) input.control = std::make_shared<PublicationControl>();
    const FinishControl finished{input.control};
    const auto cancelled = [&] {
        if (!input.control->cancelled()) return false;
        result.cancelled = true; return true;
    };
    if (cancelled()) return result;
    try {
        auto storage = iiSocietyContainer::SharedStorage::open(input.root, &result.error);
        if (!storage || storage->drive().identifier() != input.containerId || storage->drive().rootPath() != input.root) {
            if (result.error.isEmpty()) result.error = QStringLiteral("The selected generation drive changed.");
            return result;
        }
        if (input.origin != PublicationOrigin::Remote
            && storage->resolveModel(input.job.value("model").toObject(), &result.error).isEmpty()) return result;
        const QFileInfo output(input.output);
        if (input.output != iiFileProvider::Directory(input.directory).filePath("output") || !output.isDir()
            || output.isSymLink() || output.canonicalFilePath() != input.output) {
            result.error = QStringLiteral("The temporary generation output was redirected."); return result;
        }
        const QSize size(input.job.value("width").toInt(), input.job.value("height").toInt());
        QStringList sources, names;
        const bool video = input.job.value("mediaType") == "Video";
        auto generation = input.job.value("generation").toObject();
        if (input.origin == PublicationOrigin::Native) {
            const auto &native = input.native;
            if (!native || native->width != size.width() || native->height != size.height()
                || native->rgb.size() != std::size_t(native->width) * native->height * 3) {
                result.error = QStringLiteral("The local engine returned an incomplete image."); return result;
            }
            const QImage image(native->rgb.data(), native->width, native->height, native->width * 3, QImage::Format_RGB888);
            const auto path = iiFileProvider::Directory(input.output).filePath("image.png");
            if (!iiFileProvider::File::saveImage(image, path, "PNG")) {
                result.error = QStringLiteral("Cannot save the generated image."); return result;
            }
            sources.append(path);
        } else if (input.origin == PublicationOrigin::Remote) {
            const auto path = iiFileProvider::Directory(input.directory).filePath("remote.png");
            iiFileProvider::SaveFile image(path);
            if (!image.open(QIODevice::WriteOnly) || image.write(input.remotePng) != input.remotePng.size() || !image.commit()) {
                result.error = QStringLiteral("Cannot save the image received from Society."); return result;
            }
            sources.append(path);
        } else {
            const auto recordPath = iiFileProvider::Directory(input.output).filePath(video ? "video.json" : "generation.json");
            const QFileInfo information(recordPath); iiFileProvider::FileDevice record(recordPath);
            if (!information.isFile() || information.isSymLink() || information.canonicalFilePath() != recordPath
                || information.size() > 16 * 1024 * 1024 || !record.open(QIODevice::ReadOnly)) {
                result.error = QStringLiteral("The engine exited without a readable generation record."); return result;
            }
            generation = QJsonDocument::fromJson(record.readAll()).object();
            if (generation.isEmpty()) { result.error = QStringLiteral("The engine returned an invalid generation record."); return result; }
            if (video) {
                const auto path = iiFileProvider::Directory(input.output).filePath("video.mp4");
                const auto parameters = input.job.value("videoParameters").toObject();
                if (!validateVideoOutput(path, generation, size, parameters.value("frames").toInt(),
                        parameters.value("fps").toInt(), &result.error)) return result;
                sources = {path, iiFileProvider::Directory(input.output).filePath("video-frames/frame-000000.png")};
                names = {input.job.value("id").toString() + "-0001.mp4", input.job.value("id").toString() + "-poster.png"};
            } else {
                for (const auto &file : iiFileProvider::Directory(input.output).entryInfoList(iiFileProvider::Directory::Files, iiFileProvider::Directory::Name))
                    if (QStringList{"png", "jpg", "jpeg", "webp"}.contains(file.suffix().toLower())) sources.append(file.filePath());
            }
        }
        if (sources.isEmpty()) { result.error = QStringLiteral("The engine exited without a readable generated image."); return result; }
        if (cancelled()) return result;
        // Validate/encode the complete batch before making any public file visible.
        for (int index = 0; index < sources.size(); ++index) {
            const auto source = sources[index]; const QFileInfo file(source);
            if (!file.isFile() || file.isSymLink() || file.canonicalFilePath() != source) {
                result.error = QStringLiteral("The engine returned an unreadable result."); return result;
            }
            if (!video || index == 1) {
                QImageReader reader(source);
                if (reader.size() != size || reader.read().isNull()) {
                    result.error = QStringLiteral("The engine returned an unreadable or incorrectly sized image."); return result;
                }
            }
            if (!video) {
                names.append(input.job.value("id").toString() + QString("-%1.png").arg(index + 1, 4, 10, QLatin1Char('0')));
                const auto advanced = input.job.value("advancedParameters").toObject();
                if (!advanced.isEmpty() || QImageReader(source).format().toLower() != "png") {
                    const auto path = iiFileProvider::Directory(input.directory).filePath(QString("Prepared image-%1.png").arg(index));
                    iiFileProvider::SaveFile prepared(path); bool saved = prepared.open(QIODevice::WriteOnly);
                    if (saved) saved = !advanced.isEmpty()
                        ? writeAdvancedImage(source, &prepared, advanced, input.job.value("seed").toInteger(), &result.error)
                        : QImageReader(source).read().save(&prepared, "PNG");
                    if (!saved || !prepared.commit()) {
                        if (result.error.isEmpty()) result.error = QStringLiteral("Cannot prepare the generated PNG.");
                        return result;
                    }
                    sources[index] = path;
                }
            }
            if (cancelled()) return result;
        }
        for (const auto &name : names) {
            const auto path = storage->filePath(iiSocietyContainer::StoreSection::GenerationHistory, name, &result.error);
            if (path.isEmpty()) return result;
            if (QFileInfo::exists(path)) { result.error = QStringLiteral("A generation result with this name already exists. Existing files were preserved."); return result; }
        }
        if (input.origin != PublicationOrigin::Remote
            && storage->resolveModel(input.job.value("model").toObject(), &result.error).isEmpty()) return result;
        if (!input.control->beginCommit()) { result.cancelled = true; return result; }
        PublicationRollback rollback{*storage, {}};
        for (int index = 0; index < names.size(); ++index) {
            QString destination;
            if (names[index].endsWith(".png")) destination = storage->publishGeneratedImage(sources[index], names[index], &result.error);
            else {
                const auto runtime = storage->ensureRuntimeDirectory("generation-history", &result.error);
                if (runtime.isEmpty()) return result;
                iiFileProvider::StagedFile staged(iiFileProvider::Directory(runtime).filePath("publication.mp4"));
                if (!iiFileProvider::native::copyFile(std::filesystem::path(sources[index].toStdU16String()),
                        std::filesystem::path(staged.path().toStdU16String()), std::filesystem::copy_options::overwrite_existing)) {
                    result.error = QStringLiteral("Cannot stage the generated video."); return result;
                }
                destination = storage->filePath(iiSocietyContainer::StoreSection::GenerationHistory, names[index], &result.error);
                if (destination.isEmpty()) return result;
                const auto published = iiFileProvider::File::publish(std::filesystem::path(staged.path().toStdU16String()),
                    std::filesystem::path(destination.toStdU16String()), false);
                if (!published.succeeded) { result.error = QString::fromStdString(published.message); return result; }
            }
            if (destination.isEmpty()) {
                if (result.error.isEmpty()) result.error = QStringLiteral("Cannot save the generated result in Society History.");
                return result;
            }
            const QFileInfo information(destination);
            rollback.files.push_back({names[index], destination, information.size(), information.lastModified()});
        }
        // One native mirror scan per batch. Its immutable values can be adopted
        // by the GUI owner without rereading payloads or traversing directories.
        iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
        if (!files.open(input.root) || files.containerId() != input.containerId || files.rootPath() != input.root) {
            result.error = files.errorString();
            if (result.error.isEmpty()) result.error = QStringLiteral("The selected generation drive changed.");
            return result;
        }
        result.storage = files.preparedStorage();
        result.values = {{"generation", generation}, {"output", "Generation History"}};
        if (video) result.values.insert("video", "Generation History/" + names[0]);
        if (video) {
            result.values.insert("videos", QJsonArray{result.values.value("video")});
            result.values.insert("poster", "Generation History/" + names[1]);
        } else {
            QJsonArray images; for (const auto &name : names) images.append("Generation History/" + name);
            result.values.insert("images", images); result.values.insert("image", images.first());
        }
        rollback.accepted = true;
    } catch (const std::exception &error) { result.error = QString::fromUtf8(error.what()); }
    catch (...) { result.error = QStringLiteral("The generated result could not be saved."); }
    return result;
}
}
