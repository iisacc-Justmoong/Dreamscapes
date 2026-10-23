#pragma once
#include <iiSharedCanvas/Document/Document.h>
#include <iiSocietyHelper.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace dreamscapes {
struct DocumentOpenInput {
    std::filesystem::path source,storageRoot;
    std::shared_ptr<std::atomic_bool> cancelled;
    bool asCopy=false;
};
struct DocumentSourceStamp {
    std::filesystem::path canonical;
    // The data fork and SQLite WAL/journal affect document content. Shared
    // memory is a reader lock table, not document state.
    std::array<std::string,3> tokens;
    friend bool operator==(const DocumentSourceStamp &,const DocumentSourceStamp &)=default;
};
struct DocumentOpenResult {
    iiSharedCanvas::Document document;
    iiSocietyHelper::PreparedStorage storage;
    QVariantMap alias;
    std::filesystem::path source;
    DocumentSourceStamp stamp;
    std::string name,error;
    int activeArtboard=0;
    bool bindFile=false,cancelled=false;
    bool ok() const {return error.empty()&&!cancelled;}
};
using DocumentOpenOperation=std::function<DocumentOpenResult(const DocumentOpenInput &)>;
DocumentOpenResult prepareDocumentOpen(const DocumentOpenInput &input);
// Bounded native identity metadata only. No payload hash, decode or mirror scan.
bool documentSourceUnchanged(const DocumentOpenResult &result);
}
