#include "EditorPhotoImport.h"
#include "EditorToolUtils.h"
#include <iiSocietyHelper.h>
#include <iiSharedCanvas.h>
#include <QImageReader>
#include <algorithm>
#include <stdexcept>

namespace dreamscapes {
namespace {
struct Original {
    std::filesystem::path path, canonical;
    std::uintmax_t size;
    std::filesystem::file_time_type modified;
    bool image;
};
bool unchanged(const Original &value) {
    return std::filesystem::is_regular_file(std::filesystem::symlink_status(value.path))
        && std::filesystem::canonical(value.path) == value.canonical
        && std::filesystem::file_size(value.path) == value.size
        && std::filesystem::last_write_time(value.path) == value.modified;
}
std::string utf8Path(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}
}
PhotoImportOutput preparePhotoImport(const PhotoImportInput &input) {
    using namespace iiSharedCanvas;
    PhotoImportOutput out;
    const auto stopped = [&] { return input.cancelled && input.cancelled->load(); };
    try {
        if (input.originals.paths.empty() || input.originals.paths.size() > 1000)
            throw std::runtime_error("Select between 1 and 1000 original photos.");
        std::vector<Original> originals;
        std::uint64_t decodedBytes = 0;
        iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
        for (const auto &path : input.originals.paths) {
            if (stopped()) break;
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
                throw std::runtime_error("Choose regular, available original photo files.");
            const auto extension = QString::fromStdString(path.extension().string()).toLower();
            const bool video = QStringList{".mp4", ".mov", ".mkv", ".webm"}.contains(extension);
            Original original{path, std::filesystem::canonical(path), std::filesystem::file_size(path), std::filesystem::last_write_time(path), !video};
            if (original.size > MediaLimits{}.maxInputBytes) throw std::runtime_error("An original photo exceeds the import size limit.");
            const auto local = QString::fromStdU16String(path.u16string());
            if (video) {
                VideoImportOptions options; options.frameRate = input.frameRate;
                options.limits.maxDecodedBytes = 256ULL * 1024 * 1024; options.limits.maxFrames = 512;
                options.backend.ffmpegPath = DREAMSCAPES_FFMPEG_EXECUTABLE; options.backend.ffprobePath = DREAMSCAPES_FFPROBE_EXECUTABLE;
                options.backend.cancelled = stopped;
                auto imported = importVideoAsset(utf8Path(path), "prepared.video", options);
                if (!imported.ok()) throw std::runtime_error(imported.result.message);
                for (const auto &frame : imported.asset.frames) {
                    const auto bytes = std::uint64_t(frame.width) * frame.height * sizeof(std::uint32_t);
                    if (bytes > MediaLimits{}.maxDecodedBytes - decodedBytes) throw std::runtime_error("Selected media exceed the decoded import limit.");
                    decodedBytes += bytes;
                }
                if (!unchanged(original)) throw std::runtime_error("The original recording changed during preparation.");
                out.photos.push_back({utf8Path(path.filename()), std::move(imported.asset)}); originals.push_back(std::move(original)); continue;
            }
            RasterLayer pixels;
            if (extension == ".iisc") {
                auto native = files.readCanvas(local);
                if (!native.ok()) throw std::runtime_error(native.result.message);
                if (native.document.canvasMode != CanvasMode::Finite) throw std::runtime_error("Place a finite image canvas as a photo.");
                if (std::uint64_t(native.document.extent.width) * native.document.extent.height > MediaLimits{}.maxPixelsPerFrame)
                    throw std::runtime_error("The original canvas exceeds the photo import limit.");
                const auto rendered = renderFrame(native.document, 0);
                if (!rendered.ok()) throw std::runtime_error(rendered.message);
                pixels = rendered.pixels;
            } else {
                QImageReader reader(local);
                const auto size = reader.size();
                if (!size.isValid() || std::uint64_t(size.width()) * size.height() > MediaLimits{}.maxPixelsPerFrame)
                    throw std::runtime_error("Choose an original image within the canvas import limit.");
                const auto image = reader.read();
                if (image.isNull()) throw std::runtime_error(reader.errorString().toStdString());
                pixels = editorTools::raster(image);
            }
            const auto count = std::uint64_t(pixels.width) * pixels.height;
            if (!count || count > MediaLimits{}.maxPixelsPerFrame || count * sizeof(std::uint32_t) > MediaLimits{}.maxDecodedBytes - decodedBytes)
                throw std::runtime_error("Selected photos exceed the decoded image import limit.");
            decodedBytes += count * sizeof(std::uint32_t);
            if (!unchanged(original)) throw std::runtime_error("An original photo changed during preparation. Select it again.");
            out.photos.push_back({utf8Path(path.filename()), std::move(pixels)}); originals.push_back(std::move(original));
        }
        // Validate the complete batch before entering individually durable storage.
        for (const auto &original : originals) if (!unchanged(original)) throw std::runtime_error("An original photo changed during preparation. Select it again.");
        const auto root = QString::fromStdU16String(input.storageRoot.u16string());
        bool opened = false;
        for (const auto &original : originals) {
            if (stopped()) break;
            if (!original.image) continue;
            const auto local = QString::fromStdU16String(original.path.u16string());
            if (root.isEmpty()) opened = files.openForSource(local);
            else if (!opened) opened = files.open(root);
            if (!opened || files.importCanvas(local).value("path").toString().isEmpty()) {
                const auto error = files.errorString().toStdString();
                throw std::runtime_error(error.empty() ? "The managed original could not be saved." : error);
            }
            if (!unchanged(original)) throw std::runtime_error("An original photo changed during import. Select it again.");
        }
        out.cancelled = stopped();
    } catch (const std::exception &error) { out.error = *error.what() ? error.what() : "The original photos could not be prepared."; }
    catch (...) { out.error = "The original photos could not be prepared."; }
    if (!out.error.empty() || out.cancelled) out.photos.clear();
    return out;
}
}
