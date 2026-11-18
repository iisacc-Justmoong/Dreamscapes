#include "NativeDocumentSession.h"
#include <Document/DocumentEditor.h>
#include <Validation/Validation.h>
#include <algorithm>
#include <cmath>
#include <type_traits>
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
ii::core::Result<std::string> DocumentSession::insertVectorDrawing(std::string name,
    std::vector<iiSharedCanvas::VectorPath> paths) {
    using namespace iiSharedCanvas;using ii::core::ErrorCode;
    if (auto valid=writable();!valid) return std::unexpected(valid.error());
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (name.empty() || name.size()>256 || paths.empty() || paths.size()>128)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid vector drawing");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another layer operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() || visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &flag;explicit Guard(bool &value):flag(value){flag=true;}~Guard(){flag=false;}} guard(editingStructure_);
    try {
        // Keep each asset local to its own bounds. This preserves shapes drawn
        // at negative/infinite coordinates and avoids canvas-sized filter rasters.
        double left=std::numeric_limits<double>::infinity(),top=left,right=-left,bottom=-left,padding=1;
        auto points=[](auto &command,auto &&visit) {
            using T=std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T,MoveTo> || std::is_same_v<T,LineTo>) visit(command.point);
            else if constexpr (std::is_same_v<T,QuadraticTo>) {visit(command.control);visit(command.end);}
            else if constexpr (std::is_same_v<T,CubicTo>) {visit(command.control1);visit(command.control2);visit(command.end);}
        };
        bool finite=true;
        for (auto &path:paths) {
            if (path.stroke) padding=std::max(padding,path.stroke->width*.5+1);
            for (auto &command:path.commands) std::visit([&](auto &item){points(item,[&](const Point &point) {
                finite=finite && std::isfinite(point.x) && std::isfinite(point.y);
                left=std::min(left,point.x);top=std::min(top,point.y);right=std::max(right,point.x);bottom=std::max(bottom,point.y);
            });},command);
        }
        if (!finite || !std::isfinite(left) || !std::isfinite(padding) || padding<0)
            return ii::core::fail(ErrorCode::invalid_argument,"Vector coordinates must be finite");
        left=std::floor(left-padding);top=std::floor(top-padding);
        right=std::ceil(right+padding);bottom=std::ceil(bottom+padding);
        const auto width=right-left,height=bottom-top;
        if (!std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 ||
            width>std::numeric_limits<std::int32_t>::max() || height>std::numeric_limits<std::int32_t>::max())
            return ii::core::fail(ErrorCode::out_of_range,"Vector bounds are too large");
        for (auto &path:paths) for (auto &command:path.commands)
            std::visit([&](auto &item){points(item,[&](Point &point){point.x-=left;point.y-=top;});},command);
        const auto generation=documentGeneration.get(),priorRevision=revision.get();
        const bool hadSelection=hasCurrentLayerSelection();
        const auto priorSelection=hadSelection?selectedLayer.get():std::string{};
        auto candidate=std::make_shared<Document>(*document_);
        auto unique=[](std::string_view prefix,auto exists) {
            for (std::uint64_t n=1;n<std::numeric_limits<std::uint64_t>::max();++n) {
                auto value=std::string(prefix)+std::to_string(n);if (!exists(value)) return value;
            }
            throw std::overflow_error("Document identity is exhausted");
        };
        const auto id=unique("layer.shape.",[&](const auto &value){return findLayer(*candidate,value) ||
            std::ranges::any_of(candidate->audioTracks,[&](const auto &entry){return entry.id==value;});});
        const auto asset=unique("asset.shape.",[&](const auto &value){return findAsset(*candidate,value) ||
            std::ranges::any_of(candidate->audioAssets,[&](const auto &entry){return entry.id==value;});});
        const auto base=name;int suffix=1;
        while (std::ranges::any_of(candidate->layers,[&](const auto &entry){return layerProperties(entry).name==name;}))
            name=base+" "+std::to_string(++suffix);
        const auto *selected=findLayer(*candidate,priorSelection);
        const auto position=selected?static_cast<std::size_t>(selected-candidate->layers.data())+1:candidate->layers.size();
        DocumentEditor editor(*candidate);
        auto inserted=editor.insertVectorAsset(asset,{static_cast<std::int32_t>(width),static_cast<std::int32_t>(height)},std::move(paths));
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        LayerProperties properties;properties.id=id;properties.name=name;
        properties.transform.translationX=left;properties.transform.translationY=top;
        inserted=editor.insertStaticLayer(std::move(properties),LayerRepresentation::Vector,asset,position);
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        const auto checked=validate(*candidate);
        if (!checked.ok()) return ii::core::fail(ErrorCode::invalid_argument,checked.issues.front().message);
        if (generation!=documentGeneration.get() || priorRevision!=revision.get())
            return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the drawing");
        auto rows=layers.get();auto row=std::make_shared<DocumentLayerRow>();row->id=id;row->owner=shared_from_this();
        (void)row->name.set(name);(void)row->visible.set(true);
        rows.insert(rows.begin()+static_cast<std::ptrdiff_t>(position),std::move(row));
        document_=std::move(candidate);(void)layers.set(std::move(rows));
        if (generation!=documentGeneration.get()) return ii::core::fail(ErrorCode::cancelled,"The active canvas changed");
        if (auto edited=changed(generation);!edited) return std::unexpected(edited.error());
        if (generation!=documentGeneration.get()) return ii::core::fail(ErrorCode::cancelled,"The active canvas changed");
        if ((!hadSelection && !hasCurrentLayerSelection()) ||
            (hadSelection && hasCurrentLayerSelection() && selectedLayer.get()==priorSelection))
            if (auto selectedResult=selectLayer(id);!selectedResult)
            return std::unexpected(selectedResult.error());
        if (generation!=documentGeneration.get()) return ii::core::fail(ErrorCode::cancelled,"The active canvas changed");
        return id;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot create vector drawing",std::current_exception());}
}
}
