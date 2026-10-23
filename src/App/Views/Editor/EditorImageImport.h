#pragma once
#include <iiSharedCanvas/Document/Document.h>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dreamscapes {
// Detached original-source work. No canvas, QObject, file binding or UI callback
// is retained here. Native storage and codecs remain owned by the existing SDK.
struct ImageImportRequest {
    std::vector<std::filesystem::path> sources;
    std::shared_ptr<const iiSharedCanvas::Document> base;
    std::vector<bool> artboardVisibility,layerVisibility;
    std::string baseName;
    std::filesystem::path storageRoot;
    std::shared_ptr<std::atomic_bool> cancelled;
};
struct ImageImportResult {
    iiSharedCanvas::Document document;
    std::vector<std::size_t> pageSources;
    int firstNew=0;
    std::string error;
    bool cancelled=false;
    bool ok() const {return error.empty()&&!cancelled;}
};
using ImageImportOperation=std::function<ImageImportResult(const ImageImportRequest &)>;
ImageImportResult prepareImageImport(const ImageImportRequest &request);
void appendImportedArtboards(iiSharedCanvas::Document &target,iiSharedCanvas::Document source,
    const std::string &name,int number,std::int64_t &x);
}
