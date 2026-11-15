#include "NativeDocumentSession.h"
#include <Document/DocumentEditor.h>
#include <Validation/Validation.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
ii::core::Result<void> DocumentSession::setVectorSolidFill(std::string_view requested,std::uint32_t rgb,
                                                          std::optional<std::uint8_t> alpha) {
    using namespace iiSharedCanvas;
    using ii::core::ErrorCode;
    if (auto valid=writable();!valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (rgb>0xffffffU) return ii::core::fail(ErrorCode::invalid_argument,"Fill color must be RGB");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    struct Guard {bool &flag;explicit Guard(bool &value):flag(value){flag=true;}~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const std::string layerId(requested);
        const auto *layer=findLayer(*document_,layerId);
        const auto *vector=layer?std::get_if<StaticVectorLayer>(layer):nullptr;
        if (!vector || !vector->properties.motion.empty())
            return ii::core::fail(ErrorCode::unsupported,"Select a static vector layer without motion");
        const auto assetId=vector->content.assetId;
        const auto *asset=findVectorAsset(*document_,assetId);
        if (!asset || asset->paths.empty() || std::ranges::any_of(asset->paths,[](const auto &path){return !path.fill;}))
            return ii::core::fail(ErrorCode::unsupported,"The selected vector does not have solid fills on every path");
        auto paths=asset->paths;bool modified=false;
        for (auto &path:paths) {
            const auto old=path.fill->argb;
            const auto next=(alpha?std::uint32_t(*alpha)<<24:old&0xff000000U)|rgb;
            modified|=next!=old;path.fill->argb=next;
        }
        if (!modified) return {};
        if (revision.get()==std::numeric_limits<std::uint64_t>::max() || visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
            return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
        const auto viewport=asset->viewport;
        const auto references=assetReferences(*document_,assetId);
        const auto layerIndex=static_cast<std::size_t>(layer-document_->layers.data());
        const bool exclusive=references.size()==1 && references[0].layerIndex==layerIndex &&
            references[0].kind==AssetReferenceKind::Content && !references[0].frameIndex;
        const auto generation=documentGeneration.get();
        if (exclusive) {
            if (auto detached=detachForEdit();!detached) return detached;
            auto *owned=findVectorAsset(*document_,assetId);
            if (!owned) return ii::core::fail(ErrorCode::failed,"Vector asset is unavailable");
            auto priorPaths=owned->paths;const auto priorViewport=owned->viewport;
            try {
                DocumentEditor editor(*document_);
                const auto updated=editor.replaceVectorData(assetId,viewport,std::move(paths));
                if (!updated.ok()) return ii::core::fail(ErrorCode::invalid_argument,updated.message);
            } catch (...) {
                owned->paths=std::move(priorPaths);owned->viewport=priorViewport;
                throw;
            }
            return changed(generation);
        }
        auto candidate=std::make_shared<Document>(*document_);
        DocumentEditor editor(*candidate);
        std::string replacement;
        for (std::uint64_t number=1;number<std::numeric_limits<std::uint64_t>::max();++number) {
            auto value="asset.fill."+std::to_string(number);
            if (!findAsset(*candidate,value) && !findAudioAsset(*candidate,value)) {replacement=std::move(value);break;}
        }
        if (replacement.empty()) return ii::core::fail(ErrorCode::out_of_range,"Vector asset identity is exhausted");
        const auto inserted=editor.insertVectorAsset(replacement,viewport,std::move(paths));
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        const auto updated=editor.setStaticSource(layerId,replacement);
        if (!updated.ok()) return ii::core::fail(ErrorCode::invalid_argument,updated.message);
        const auto verified=validate(*candidate);
        if (!verified.ok()) return ii::core::fail(ErrorCode::invalid_argument,verified.issues.front().message);
        document_=std::move(candidate);
        return changed(generation);
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot change the vector fill",std::current_exception());}
}
}
