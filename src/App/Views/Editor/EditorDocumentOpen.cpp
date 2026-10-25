#include "EditorDocumentOpen.h"
#include "EditorImageImport.h"
#include "EditorProjectCodec.h"
#include <iiFileProviderNative.h>
#include <iiFileProviderPresentation.h>
#include <cctype>
#include <mutex>
#include <stdexcept>

namespace dreamscapes {
namespace {
DocumentSourceStamp stamp(const std::filesystem::path &path) {
    const auto canonical=std::filesystem::canonical(path);
    if(canonical!=path||!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
        throw std::runtime_error("Choose a regular local document without symbolic links.");
    DocumentSourceStamp result;result.canonical=canonical;
    int index=0;
    for(const auto &suffix:{"","-wal","-journal"}) {
        const auto file=std::filesystem::path(path.native()+std::filesystem::path(suffix).native());
        std::error_code ec;const auto status=std::filesystem::symlink_status(file,ec);
        if(ec&&ec!=std::errc::no_such_file_or_directory)throw std::filesystem::filesystem_error("Cannot inspect the document identity",file,ec);
        if(!std::filesystem::exists(status)) {result.tokens[index++]="missing";continue;}
        if(ec||!std::filesystem::is_regular_file(status))throw std::runtime_error("Document sidecars must be regular local files.");
        auto token=iiFileProvider::native::Presentation::cacheChangeToken(file);
        if(token.empty())throw std::runtime_error("The document identity is unavailable.");
        result.tokens[index++]=std::move(token);
    }
    return result;
}
}
bool documentSourceUnchanged(const DocumentOpenResult &result) {
    try {return stamp(result.source)==result.stamp;}catch(...){return false;}
}
DocumentOpenResult prepareDocumentOpen(const DocumentOpenInput &input) {
    DocumentOpenResult result;
    const auto cancelled=[&]{return input.cancelled&&input.cancelled->load();};
    const auto abandon=[&] {DocumentOpenResult empty;empty.cancelled=true;return empty;};
    try {
        if(cancelled())return abandon();
        result.source=std::filesystem::absolute(input.source).lexically_normal();
        {
            iiFileProvider::native::SourceAccess access(result.source);
            std::lock_guard lock(access);
            result.stamp=stamp(result.source);
        }
        // SDK decoders guard their own reads. Do not hold the original's token
        // across managed publication: catalog intake owns a repository lease
        // before reading that same original. The final stamp rejects changes.
        const auto utf8=result.source.filename().u8string();const std::string filename(utf8.begin(),utf8.end());
        const auto dot=filename.find_last_of('.');auto suffix=dot==std::string::npos?std::string{}:filename.substr(dot);
        for(auto &ch:suffix)ch=char(std::tolower(static_cast<unsigned char>(ch)));
        result.name=filename.substr(0,dot);
        if(suffix==".iiscp") {
            const auto bytes=iiFileProvider::native::File::read(result.source,iiSharedCanvas::SerializationLimits{}.maximumContainerBytes);
            auto decoded=decodeProject({reinterpret_cast<const std::uint8_t *>(bytes.data()),bytes.size()});
            if(!decoded.error.empty())throw std::runtime_error(decoded.error);
            std::int64_t x=0;int number=0;
            for(auto &page:decoded.pages) {
                if(cancelled())return abandon();
                if(number==int(decoded.active))result.activeArtboard=int(result.document.artboards.size());
                appendImportedArtboards(result.document,std::move(page.document),page.name,number++,x);
            }
        } else if(suffix==".iisc") {
            const auto path=QString::fromStdU16String(result.source.u16string());
            iiSocietyHelper::FileSystem files(iiSocietyHelper::FileSystem::Initialization::Deferred);
            files.openForSource(path);
            if(cancelled())return abandon();
            auto imported=files.readCanvas(path);
            if(!imported.ok())throw std::runtime_error(imported.result.message);
            if(cancelled())return abandon();
            result.alias=files.aliasForPath(path);
            if(!result.alias.value("available").toBool())throw std::runtime_error("The canvas alias is unavailable.");
            result.document=std::move(imported.document);result.name=result.alias.value("name").toString().toStdString();
            result.bindFile=!input.asCopy;
            if(result.bindFile)result.storage=files.preparedStorage();
        } else {
            ImageImportRequest request;request.sources={result.source};request.storageRoot=input.storageRoot;request.cancelled=input.cancelled;
            auto imported=prepareImageImport(request);
            if(imported.cancelled)return abandon();
            if(!imported.ok())throw std::runtime_error(imported.error);
            result.document=std::move(imported.document);
        }
        if(cancelled())return abandon();
        if(!documentSourceUnchanged(result))throw std::runtime_error("The source document changed while opening. Open it again.");
    } catch(const std::exception &error) {
        if(cancelled())return abandon();
        result=DocumentOpenResult{};result.error=error.what();
    }
    return result;
}
}
