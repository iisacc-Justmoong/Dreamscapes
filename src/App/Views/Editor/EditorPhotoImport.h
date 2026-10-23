#pragma once
#include <iiSharedCanvas/Document/Document.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <variant>

namespace dreamscapes {
struct PhotoOriginals {
    std::vector<std::filesystem::path> paths;
    // Opaque ownership of native temporary files, never a QObject or GUI value.
    std::shared_ptr<const void> sourceLease;
};
struct PhotoImportInput {
    PhotoOriginals originals;
    std::filesystem::path storageRoot;
    iiSharedCanvas::FrameRate frameRate;
    std::shared_ptr<std::atomic_bool> cancelled;
};
struct ImportedPhoto {
    std::string name;
    std::variant<RasterLayer, iiSharedCanvas::VideoAsset> media;
};
struct PhotoImportOutput {
    std::vector<ImportedPhoto> photos;
    std::string error;
    bool cancelled = false;
    bool ok() const { return error.empty() && !cancelled && !photos.empty(); }
};
using PhotoImportOperation = std::function<PhotoImportOutput(const PhotoImportInput &)>;
PhotoImportOutput preparePhotoImport(const PhotoImportInput &input);
}
