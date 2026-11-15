#include "NativeDocumentSession.h"
#include <Document/DocumentEditor.h>
#include <Validation/Validation.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
namespace {
using namespace iiSharedCanvas;
using ii::core::ErrorCode;
template<class Exists> std::string unique(std::string_view prefix,Exists exists) {
    for (std::uint64_t number=1;number<std::numeric_limits<std::uint64_t>::max();++number) {
        auto value=std::string(prefix)+std::to_string(number);if (!exists(value)) return value;
    }
    throw std::overflow_error("Mask identity is exhausted");
}
std::string assetId(const Document &document) {
    return unique("asset.mask.",[&](const auto &value) {
        return findAsset(document,value) || std::ranges::any_of(document.audioAssets,[&](const auto &item){return item.id==value;});
    });
}
std::string maskId(const LayerAppearance &appearance) {
    return unique("mask.",[&](const auto &value) {
        return std::ranges::any_of(appearance.maskStack,[&](const auto &entry){return entry.id==value;});
    });
}
std::string maskName(const LayerAppearance &appearance) {
    return unique("Mask ",[&](const auto &value) {
        return std::ranges::any_of(appearance.maskStack,[&](const auto &entry){return entry.name==value;});
    });
}
struct Guard final {
    bool &flag;
    explicit Guard(bool &value):flag(value) {flag=true;}
    ~Guard() {flag=false;}
};
}
ii::core::Result<std::string> DocumentSession::createLayerMask(std::string_view requested) {
    using namespace iiSharedCanvas;
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() || visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    const std::string layerId(requested);const auto *source=findLayer(*document_,layerId);
    if (!source || layerRole(*source)!=LayerRole::Artwork)
        return ii::core::fail(ErrorCode::invalid_argument,"Select an artwork layer for the mask");
    Guard guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        auto candidate=std::make_shared<Document>(*document_);
        auto appearance=layerProperties(*findLayer(*candidate,layerId)).appearance;
        const auto id=maskId(appearance),name=maskName(appearance),asset=assetId(*candidate);
        DocumentEditor editor(*candidate);
        // A one-pixel identity mask has white outside coverage; it produces
        // full coverage without allocating a canvas-sized image. Painting is
        // not enabled until a brush implementation expands the mask domain.
        const auto inserted=editor.insertRasterAsset(asset,makeRasterLayer(1,1,0xffffffff));
        if (!inserted.ok()) return ii::core::fail(ErrorCode::failed,inserted.message);
        LayerMaskEntry entry;entry.id=id;entry.name=name;entry.mask.assetId=asset;
        appearance.maskStack.push_back(std::move(entry));
        const auto updated=editor.setLayerAppearance(layerId,std::move(appearance));
        if (!updated.ok()) return ii::core::fail(ErrorCode::invalid_argument,updated.message);
        const auto verified=validate(*candidate);
        if (!verified.ok()) return ii::core::fail(ErrorCode::invalid_argument,verified.issues.front().message);
        document_=std::move(candidate);
        if (auto result=changed(generation);!result) return std::unexpected(result.error());
        return id;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot create the mask",std::current_exception());}
}
ii::core::Result<std::string> DocumentSession::duplicateLayerMask(std::string_view requested,std::string_view requestedMask) {
    using namespace iiSharedCanvas;
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() || visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    const std::string layerId(requested),sourceId(requestedMask);const auto *source=findLayer(*document_,layerId);
    if (!source || layerRole(*source)!=LayerRole::Artwork)
        return ii::core::fail(ErrorCode::invalid_argument,"Select an artwork layer for the mask");
    const auto &stack=layerProperties(*source).appearance.maskStack;
    if (std::ranges::none_of(stack,[&](const auto &entry){return entry.id==sourceId;}))
        return ii::core::fail(ErrorCode::invalid_argument,"Mask is unavailable");
    Guard guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        auto candidate=std::make_shared<Document>(*document_);
        auto appearance=layerProperties(*findLayer(*candidate,layerId)).appearance;
        const auto found=std::ranges::find_if(appearance.maskStack,[&](const auto &entry){return entry.id==sourceId;});
        const auto position=static_cast<std::size_t>(found-appearance.maskStack.begin());
        auto duplicate=*found;const auto id=maskId(appearance),asset=assetId(*candidate);
        const auto *content=findAsset(*candidate,duplicate.mask.assetId);
        if (!content) return ii::core::fail(ErrorCode::failed,"Mask asset is unavailable");
        DocumentEditor editor(*candidate);DocumentEditResult inserted;
        if (const auto *raster=std::get_if<RasterAsset>(content)) inserted=editor.insertRasterAsset(asset,raster->pixels);
        else if (const auto *vector=std::get_if<VectorAsset>(content)) inserted=editor.insertVectorAsset(asset,vector->viewport,vector->paths);
        else return ii::core::fail(ErrorCode::invalid_argument,"Mask asset has an unsupported type");
        if (!inserted.ok()) return ii::core::fail(ErrorCode::failed,inserted.message);
        duplicate.id=id;duplicate.name+=" copy";duplicate.mask.assetId=asset;
        appearance.maskStack.insert(appearance.maskStack.begin()+static_cast<std::ptrdiff_t>(position+1),std::move(duplicate));
        const auto updated=editor.setLayerAppearance(layerId,std::move(appearance));
        if (!updated.ok()) return ii::core::fail(ErrorCode::invalid_argument,updated.message);
        const auto verified=validate(*candidate);
        if (!verified.ok()) return ii::core::fail(ErrorCode::invalid_argument,verified.issues.front().message);
        document_=std::move(candidate);
        if (auto result=changed(generation);!result) return std::unexpected(result.error());
        return id;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot duplicate the mask",std::current_exception());}
}
ii::core::Result<void> DocumentSession::removeLayerMask(std::string_view requested,std::string_view requestedMask) {
    using namespace iiSharedCanvas;
    if (auto valid=writable();!valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    const std::string layerId(requested),id(requestedMask);const auto *layer=findLayer(*document_,layerId);
    if (!layer) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    auto appearance=layerProperties(*layer).appearance;
    const auto found=std::ranges::find_if(appearance.maskStack,[&](const auto &entry){return entry.id==id;});
    if (found==appearance.maskStack.end()) return ii::core::fail(ErrorCode::invalid_argument,"Mask is unavailable");
    appearance.maskStack.erase(found);
    const auto generation=documentGeneration.get();
    Guard guard(editingStructure_);
    if (auto result=detachForEdit();!result) return result;
    DocumentEditor editor(*document_);const auto updated=editor.setLayerAppearance(layerId,std::move(appearance));
    if (!updated.ok()) return ii::core::fail(ErrorCode::invalid_argument,updated.message);
    return updated.changed?changed(generation):ii::core::Result<void>{};
}
ii::core::Result<void> DocumentSession::updateLayerMask(std::string_view requested,std::string_view requestedMask,const MaskPatch &patch) {
    using namespace iiSharedCanvas;
    if (auto valid=writable();!valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if ((patch.density && (!std::isfinite(*patch.density)||*patch.density<0||*patch.density>1)) ||
        (patch.featherRadius && (!std::isfinite(*patch.featherRadius)||*patch.featherRadius<0||*patch.featherRadius>128)) ||
        (patch.combine && static_cast<unsigned>(*patch.combine)>static_cast<unsigned>(MaskCombineMode::Replace)))
        return ii::core::fail(ErrorCode::invalid_argument,"Mask parameters are outside their supported ranges");
    const std::string layerId(requested),id(requestedMask);const auto *layer=findLayer(*document_,layerId);
    if (!layer) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    auto appearance=layerProperties(*layer).appearance;
    const auto found=std::ranges::find_if(appearance.maskStack,[&](const auto &entry){return entry.id==id;});
    if (found==appearance.maskStack.end()) return ii::core::fail(ErrorCode::invalid_argument,"Mask is unavailable");
    if (patch.enabled) found->mask.enabled=*patch.enabled;
    if (patch.inverted) found->mask.inverted=*patch.inverted;
    if (patch.density) found->mask.density=*patch.density;
    if (patch.featherRadius) found->mask.featherRadius=*patch.featherRadius;
    if (patch.combine) found->combine=*patch.combine;
    const auto generation=documentGeneration.get();
    Guard guard(editingStructure_);
    if (auto result=detachForEdit();!result) return result;
    DocumentEditor editor(*document_);const auto updated=editor.setLayerAppearance(layerId,std::move(appearance));
    if (!updated.ok()) return ii::core::fail(ErrorCode::invalid_argument,updated.message);
    return updated.changed?changed(generation):ii::core::Result<void>{};
}
ii::core::Result<void> DocumentSession::setLayerMaskStackEnabled(std::string_view requested,bool value) {
    using namespace iiSharedCanvas;
    if (auto valid=writable();!valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    const std::string layerId(requested);const auto *layer=findLayer(*document_,layerId);
    if (!layer || layerRole(*layer)!=LayerRole::Artwork)
        return ii::core::fail(ErrorCode::invalid_argument,"Select an artwork layer first");
    auto appearance=layerProperties(*layer).appearance;
    if (appearance.maskStackEnabled==value) return {};
    appearance.maskStackEnabled=value;const auto generation=documentGeneration.get();
    Guard guard(editingStructure_);
    if (auto result=detachForEdit();!result) return result;
    DocumentEditor editor(*document_);const auto updated=editor.setLayerAppearance(layerId,std::move(appearance));
    if (!updated.ok()) return ii::core::fail(ErrorCode::invalid_argument,updated.message);
    return updated.changed?changed(generation):ii::core::Result<void>{};
}
}
