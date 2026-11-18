#pragma once
#include <Document/Document.h>
#include <iiFileProvider>
#include <iiSDKCore>
#include <cstdint>
#include <filesystem>

namespace dreamscapes::native {
struct CanvasFileVersion final {
    std::filesystem::path path;
    iiFileProvider::ObjectSourceIdentity identity;
    std::uint64_t revision=0;
    bool working=false;
};
struct CanvasFileResult final {
    iiSharedCanvas::Document document;
    CanvasFileVersion version;
    std::string warning;
};
// Blocking operations: the application invokes them only on an I/O worker.
// Create is exclusive; save rejects external source replacement or edits.
[[nodiscard]] ii::core::Result<CanvasFileResult> openCanvasFile(const std::filesystem::path &);
[[nodiscard]] bool supportsCanvasInput(const std::filesystem::path &);
[[nodiscard]] bool isCanvasImage(const std::filesystem::path &);
[[nodiscard]] ii::core::Result<CanvasFileResult> importCanvasImage(const std::filesystem::path &);
[[nodiscard]] ii::core::Result<CanvasFileResult> createCanvasFile(const std::filesystem::path &,const iiSharedCanvas::Document &);
[[nodiscard]] ii::core::Result<CanvasFileResult> saveCanvasFile(const CanvasFileVersion &,const iiSharedCanvas::Document &);
}
