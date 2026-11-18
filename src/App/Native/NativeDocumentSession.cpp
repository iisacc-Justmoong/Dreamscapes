#include "NativeDocumentSession.h"
#include <Document/DocumentEditor.h>
#include <Validation/Validation.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
using ii::core::ErrorCode;

std::shared_ptr<DocumentSession> DocumentSession::create() {
    return std::shared_ptr<DocumentSession>(new DocumentSession);
}
ii::core::Result<void> DocumentSession::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Document session requires its owner thread");
    return {};
}
const iiSharedCanvas::Document *DocumentSession::document() const noexcept {return document_.get();}
bool DocumentSession::hasCurrentLayerSelection() const {
    return document_ && selectedLayerGeneration_==documentGeneration.get() && !selectedLayer.get().empty();
}
std::shared_ptr<const iiSharedCanvas::Document> DocumentSession::snapshot() const {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Document snapshot requires its owner thread");
    return document_;
}
ii::core::Result<void> DocumentSession::detachForEdit(bool visual) {
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    if (visual && visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Canvas visual revision is exhausted");
    if (document_.use_count()!=1) {
        try {document_=std::make_shared<iiSharedCanvas::Document>(*document_);}
        catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot detach the document for editing",std::current_exception());}
    }
    return {};
}
ii::core::Result<void> DocumentSession::attach(iiSharedCanvas::Document document,std::string name,bool isNew) {
    if (auto valid=writable(); !valid) return valid;
    if (name.empty() || !iiSharedCanvas::validate(document).ok())
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid canvas document");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() ||
        visualRevision.get()==std::numeric_limits<std::uint64_t>::max() ||
        documentGeneration.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document identity is exhausted");
    std::vector<std::shared_ptr<DocumentLayerRow>> entries;
    entries.reserve(document.layers.size());
    for (const auto &layer : document.layers) {
        const auto &properties=iiSharedCanvas::layerProperties(layer);
        auto row=std::make_shared<DocumentLayerRow>(); row->id=properties.id; row->owner=shared_from_this();
        (void)row->name.set(properties.name); (void)row->visible.set(properties.visible);
        entries.push_back(std::move(row));
    }
    auto metadata=document.publicationMetadata;
    const auto width=document.extent.width,height=document.extent.height;
    const auto frames=document.timeline.frameCount;
    try {document_=std::make_shared<iiSharedCanvas::Document>(std::move(document));}
    catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot attach canvas document",std::current_exception());}
    selectedLayerGeneration_=0;
    (void)documentGeneration.set(documentGeneration.get()+1);
    (void)title.set(std::move(name)); (void)dimensions.set(std::to_string(width)+" × "+std::to_string(height)+" px");
    (void)frameCount.set(frames); (void)frame.set(0); (void)selectedLayer.set({});
    (void)publicationTitle.set(metadata?metadata->title:std::string{});
    (void)publicationDescription.set(metadata?metadata->description:std::string{});
    (void)layers.set(std::move(entries)); (void)dirty.set(isNew); (void)hasDocument.set(true);
    (void)revision.set(revision.get()+1);
    (void)visualRevision.set(visualRevision.get()+1);
    return {};
}
ii::core::Result<void> DocumentSession::selectLayer(std::string_view id) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_ || (!id.empty() && std::ranges::none_of(layers.get(),[&](const auto &row){return row->id==id;})))
        return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    const bool outer=!reconcilingSelection_;
    if (outer) reconcilingSelection_=true;
    try {
        selectedLayerGeneration_=documentGeneration.get();
        (void)selectedLayer.set(std::string(id));
        selectionPending_=true;
        if (outer) {
            do {
                selectionPending_=false;
                const auto current=selectedLayer.get();
                // Observers can select another layer or replace the document.
                // Keep the old rows alive until this pass is abandoned.
                const auto rows=layers.get();
                for (const auto &row : rows) {
                    (void)row->selected.set(row->id==current);
                    if (selectionPending_ || selectedLayer.get()!=current) {selectionPending_=true;break;}
                }
            } while (selectionPending_);
            reconcilingSelection_=false;
        }
    } catch (...) {if (outer) reconcilingSelection_=false; throw;}
    return {};
}
ii::core::Result<void> DocumentSession::seek(std::int64_t value) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_ || value<0 || std::uint64_t(value)>=document_->timeline.frameCount)
        return ii::core::fail(ErrorCode::out_of_range,"Frame is outside the document timeline");
    (void)frame.set(value); return {};
}
std::string DocumentSession::canvasTransformStatus() const {
    using namespace iiSharedCanvas;
    if (owner_!=std::this_thread::get_id()) return "Canvas transformation requires its owner thread";
    if (!document_) return "Open a canvas first";
    if (document_->canvasMode!=CanvasMode::Finite || !document_->artboards.empty())
        return "Canvas transformation is unavailable for infinite canvases or artboards";
    for (const auto &layer:document_->layers) {
        const auto &properties=layerProperties(layer);
        if (properties.blendMode==RasterBlendMode::Dissolve)
            return "Canvas transformation is unavailable for dissolve layers";
    }
    return {};
}
ii::core::Result<void> DocumentSession::rotateCanvas(double degrees) {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (!std::isfinite(degrees)) return ii::core::fail(ErrorCode::invalid_argument,"Rotation must be finite");
    degrees=std::fmod(degrees,360.0);
    if (degrees==0) return {};
    if (const auto reason=canvasTransformStatus(); !reason.empty()) return ii::core::fail(ErrorCode::unsupported,reason);
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() ||
        visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        const auto angle=degrees*std::numbers::pi/180.0;
        auto snap=[](double value) {
            if (std::abs(value)<1e-12) return 0.0;
            if (std::abs(value-1.0)<1e-12) return 1.0;
            if (std::abs(value+1.0)<1e-12) return -1.0;
            return value;
        };
        const double c=snap(std::cos(angle)),s=snap(std::sin(angle));
        const double w=document_->extent.width,h=document_->extent.height;
        auto bound=[](double value) {
            const auto rounded=std::round(value);
            if (std::abs(value-rounded)<1e-10) value=rounded;
            return std::ceil(value);
        };
        const double newW=bound(std::abs(w*c)+std::abs(h*s));
        const double newH=bound(std::abs(w*s)+std::abs(h*c));
        if (newW<1 || newH<1 || newW>65536 || newH>65536)
            return ii::core::fail(ErrorCode::out_of_range,"Rotated canvas exceeds the supported dimensions");
        const auto width=static_cast<std::int32_t>(newW),height=static_cast<std::int32_t>(newH);
        const double qx=newW/2-c*w/2+s*h/2,qy=newH/2-s*w/2-c*h/2;
        auto candidate=std::make_shared<Document>(*document_);
        DocumentEditor editor(*candidate);
        const auto resized=editor.setCanvasExtent({width,height});
        if (!resized.ok()) return ii::core::fail(ErrorCode::failed,resized.message);
        auto rotate=[&](AffineTransform &t) {
            const auto original=t;
            t.m11=c*original.m11-s*original.m12;t.m21=c*original.m21-s*original.m22;
            t.translationX=c*original.translationX-s*original.translationY+qx;
            t.m12=s*original.m11+c*original.m12;t.m22=s*original.m21+c*original.m22;
            t.translationY=s*original.translationX+c*original.translationY+qy;
        };
        for (auto &layer:candidate->layers) {
            auto &properties=layerProperties(layer);rotate(properties.transform);
            auto rotateMask=[&](LayerMask &mask) {if (!mask.linkedToLayer) rotate(mask.transform);};
            auto &appearance=properties.appearance;
            if (appearance.rasterMask) rotateMask(*appearance.rasterMask);
            if (appearance.vectorMask) rotateMask(*appearance.vectorMask);
            if (appearance.filterMask) rotateMask(*appearance.filterMask);
            for (auto &entry:appearance.maskStack) rotateMask(entry.mask);
        }
        const auto verified=validate(*candidate);
        if (!verified.ok()) return ii::core::fail(ErrorCode::invalid_argument,verified.issues.front().message);
        document_=std::move(candidate);
        (void)dimensions.set(std::to_string(width)+" × "+std::to_string(height)+" px");
        return changed(generation);
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot rotate the canvas",std::current_exception());
    }
}
ii::core::Result<void> DocumentSession::mirrorCanvas(bool horizontal) {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (const auto reason=canvasTransformStatus(); !reason.empty()) return ii::core::fail(ErrorCode::unsupported,reason);
    if (document_->layers.empty()) return {};
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() ||
        visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        auto candidate=std::make_shared<Document>(*document_);
        const double width=candidate->extent.width,height=candidate->extent.height;
        auto reflect=[&](AffineTransform &transform) {
            if (horizontal) {
                transform.m11=-transform.m11;transform.m21=-transform.m21;
                transform.translationX=width-transform.translationX;
            } else {
                transform.m12=-transform.m12;transform.m22=-transform.m22;
                transform.translationY=height-transform.translationY;
            }
        };
        for (auto &layer:candidate->layers) {
            auto &properties=layerProperties(layer);reflect(properties.transform);
            auto reflectMask=[&](LayerMask &mask) {if (!mask.linkedToLayer) reflect(mask.transform);};
            auto &appearance=properties.appearance;
            if (appearance.rasterMask) reflectMask(*appearance.rasterMask);
            if (appearance.vectorMask) reflectMask(*appearance.vectorMask);
            if (appearance.filterMask) reflectMask(*appearance.filterMask);
            for (auto &entry:appearance.maskStack) reflectMask(entry.mask);
        }
        const auto verified=validate(*candidate);
        if (!verified.ok()) return ii::core::fail(ErrorCode::invalid_argument,verified.issues.front().message);
        document_=std::move(candidate);
        return changed(generation);
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot mirror the canvas",std::current_exception());
    }
}
ii::core::Result<void> DocumentSession::resizeCanvasCentered(std::int32_t width,std::int32_t height) {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (width<1 || height<1 || width>65536 || height>65536)
        return ii::core::fail(ErrorCode::out_of_range,"Canvas dimensions must be from 1 to 65536 pixels");
    if (width==document_->extent.width && height==document_->extent.height) return {};
    if (const auto reason=canvasTransformStatus(); !reason.empty()) return ii::core::fail(ErrorCode::unsupported,reason);
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() ||
        visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        const auto dx=(double(width)-document_->extent.width)/2.0,dy=(double(height)-document_->extent.height)/2.0;
        auto candidate=std::make_shared<Document>(*document_);
        DocumentEditor editor(*candidate);
        const auto resized=editor.setCanvasExtent({width,height});
        if (!resized.ok()) return ii::core::fail(ErrorCode::failed,resized.message);
        for (auto &layer:candidate->layers) {
            auto &properties=layerProperties(layer);
            auto &transform=properties.transform;
            transform.translationX+=dx;transform.translationY+=dy;
            auto shiftMask=[&](LayerMask &mask) {
                if (!mask.linkedToLayer) {mask.transform.translationX+=dx;mask.transform.translationY+=dy;}
            };
            auto &appearance=properties.appearance;
            if (appearance.rasterMask) shiftMask(*appearance.rasterMask);
            if (appearance.vectorMask) shiftMask(*appearance.vectorMask);
            if (appearance.filterMask) shiftMask(*appearance.filterMask);
            for (auto &entry:appearance.maskStack) shiftMask(entry.mask);
        }
        const auto verified=validate(*candidate);
        if (!verified.ok()) return ii::core::fail(ErrorCode::invalid_argument,verified.issues.front().message);
        document_=std::move(candidate);
        (void)dimensions.set(std::to_string(width)+" × "+std::to_string(height)+" px");
        return changed(generation);
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot resize the canvas",std::current_exception());
    }
}
ii::core::Result<void> DocumentSession::changed(std::uint64_t generation,bool visual) {
    auto current=[&] {return documentGeneration.get()==generation;};
    if (!current()) return ii::core::fail(ErrorCode::cancelled,"The active document changed during editing");
    (void)dirty.set(true);
    if (!current()) return ii::core::fail(ErrorCode::cancelled,"The active document changed during editing");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    (void)revision.set(revision.get()+1);
    if (!current()) return ii::core::fail(ErrorCode::cancelled,"The active document changed during editing");
    if (visual) {
        if (visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
            return ii::core::fail(ErrorCode::out_of_range,"Canvas visual revision is exhausted");
        (void)visualRevision.set(visualRevision.get()+1);
    }
    return {};
}
ii::core::Result<void> DocumentSession::setLayerVisible(std::string_view id,bool value) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    auto row=std::ranges::find_if(layers.get(),[&](const auto &candidate){return candidate->id==id;});
    if (row==layers.get().end()) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    if ((*row)->visible.get()==value) return {};
    const auto selectedRow=*row;
    const auto generation=documentGeneration.get();
    if (auto result=detachForEdit(); !result) return result;
    iiSharedCanvas::DocumentEditor editor(*document_);
    const auto result=editor.setLayerVisible(std::string(id),value);
    if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
    if (result.changed) {(void)selectedRow->visible.set(value);return changed(generation);}
    return {};
}
ii::core::Result<void> DocumentSession::setLayerOpacity(std::string_view id,double value) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (!std::isfinite(value) || value<0 || value>1)
        return ii::core::fail(ErrorCode::invalid_argument,"Opacity must be between 0 and 100 percent");
    const auto *layer=iiSharedCanvas::findLayer(*document_,std::string(id));
    if (!layer) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    if (iiSharedCanvas::layerProperties(*layer).opacity==value) return {};
    const auto generation=documentGeneration.get();
    if (auto detached=detachForEdit(); !detached) return detached;
    iiSharedCanvas::DocumentEditor editor(*document_);
    auto result=editor.setLayerOpacity(std::string(id),value);
    if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
    if (result.changed) return changed(generation);
    return {};
}
ii::core::Result<void> DocumentSession::setLayerBlend(std::string_view id,RasterBlendMode value) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (value==RasterBlendMode::DestinationOut || static_cast<unsigned>(value)>static_cast<unsigned>(RasterBlendMode::Dissolve))
        return ii::core::fail(ErrorCode::invalid_argument,"Unsupported blend mode");
    const auto *layer=iiSharedCanvas::findLayer(*document_,std::string(id));
    if (!layer) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    if (iiSharedCanvas::layerProperties(*layer).blendMode==value) return {};
    const auto generation=documentGeneration.get();
    if (auto detached=detachForEdit(); !detached) return detached;
    iiSharedCanvas::DocumentEditor editor(*document_);
    auto result=editor.setLayerBlendMode(std::string(id),value);
    if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
    if (result.changed) return changed(generation);
    return {};
}
ii::core::Result<void> DocumentSession::setLayerTransform(std::string_view id,const AffineTransform &value) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (!std::isfinite(value.m11) || !std::isfinite(value.m12) || !std::isfinite(value.m21) ||
        !std::isfinite(value.m22) || !std::isfinite(value.translationX) || !std::isfinite(value.translationY))
        return ii::core::fail(ErrorCode::invalid_argument,"Layer transform must be finite");
    const std::string layerId(id);
    const auto *layer=iiSharedCanvas::findLayer(*document_,layerId);
    if (!layer) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    const auto &old=iiSharedCanvas::layerProperties(*layer).transform;
    if (old.m11==value.m11 && old.m12==value.m12 && old.m21==value.m21 && old.m22==value.m22 &&
        old.translationX==value.translationX && old.translationY==value.translationY) return {};
    const auto generation=documentGeneration.get();
    if (auto detached=detachForEdit(); !detached) return detached;
    iiSharedCanvas::DocumentEditor editor(*document_);
    auto result=editor.setLayerTransform(layerId,value);
    if (!result.ok()) return ii::core::fail(ErrorCode::invalid_argument,result.message);
    return result.changed?changed(generation):ii::core::Result<void>{};
}
ii::core::Result<void> DocumentSession::setLayerRange(std::string_view id,std::int64_t first,std::int64_t end) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    const auto count=std::int64_t(document_->timeline.frameCount);
    if (first<0 || end<=first || end>count) return ii::core::fail(ErrorCode::out_of_range,"Layer range is outside the timeline");
    const auto *layer=iiSharedCanvas::findLayer(*document_,std::string(id));
    if (!layer) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    const auto &range=iiSharedCanvas::layerProperties(*layer).frameRange;
    if ((range?std::int64_t(range->firstFrame):0)==first &&
        (range?std::int64_t(range->lastFrame)+1:count)==end) return {};
    const auto generation=documentGeneration.get();
    std::optional<iiSharedCanvas::LayerFrameRange> next;
    if (first!=0 || end!=count) next=iiSharedCanvas::LayerFrameRange{
        static_cast<iiSharedCanvas::FrameIndex>(first),static_cast<iiSharedCanvas::FrameIndex>(end-1)};
    if (auto detached=detachForEdit(); !detached) return detached;
    iiSharedCanvas::DocumentEditor editor(*document_);
    auto result=editor.setLayerFrameRange(std::string(id),next);
    if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
    if (result.changed) return changed(generation);
    return {};
}
ii::core::Result<void> DocumentSession::moveLayerKey(std::string_view id,LayerKeyKind kind,std::int64_t from,std::int64_t to) {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (kind!=LayerKeyKind::source && kind!=LayerKeyKind::motion)
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown keyframe type");
    const auto count=std::int64_t(document_->timeline.frameCount);
    if (from<0 || to<0 || from>=count || to>=count)
        return ii::core::fail(ErrorCode::out_of_range,"Keyframe is outside the timeline");
    const std::string layerId(id);
    const auto *layer=findLayer(*document_,layerId);
    if (!layer || !layerKind(*layer)) return ii::core::fail(ErrorCode::invalid_argument,"Layer cannot edit keyframes");
    const auto &properties=layerProperties(*layer);
    const auto first=properties.frameRange?std::int64_t(properties.frameRange->firstFrame):0;
    const auto end=properties.frameRange?std::int64_t(properties.frameRange->lastFrame)+1:count;
    if (from<first || from>=end || to<first || to>=end)
        return ii::core::fail(ErrorCode::out_of_range,"Keyframe is outside the visible layer range");
    if (kind==LayerKeyKind::source) {
        const auto *source=keyframedLayerSource(*layer);
        if (!source || !std::binary_search(source->frameIndices.begin(),source->frameIndices.end(),FrameIndex(from)))
            return ii::core::fail(ErrorCode::invalid_argument,"Source keyframe is unavailable");
        if (from==0 || to==0) return ii::core::fail(ErrorCode::unsupported,"The source frame-zero anchor must remain in place");
        if (from!=to && std::binary_search(source->frameIndices.begin(),source->frameIndices.end(),FrameIndex(to)))
            return ii::core::fail(ErrorCode::invalid_argument,"A source keyframe already exists at the destination");
    } else {
        const auto &keys=properties.motion;
        if (std::ranges::none_of(keys,[&](const auto &key){return key.frame==FrameIndex(from);}))
            return ii::core::fail(ErrorCode::invalid_argument,"Motion keyframe is unavailable");
        if (from!=to && std::ranges::any_of(keys,[&](const auto &key){return key.frame==FrameIndex(to);}))
            return ii::core::fail(ErrorCode::invalid_argument,"A motion keyframe already exists at the destination");
    }
    if (from==to) return {};
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() ||
        visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        auto candidate=std::make_shared<Document>(*document_);
        DocumentEditor editor(*candidate);
        DocumentEditResult edited;
        if (kind==LayerKeyKind::source) edited=editor.moveKeyframe(layerId,FrameIndex(from),FrameIndex(to));
        else {
            auto keys=properties.motion;
            auto key=std::ranges::find_if(keys,[&](const auto &entry){return entry.frame==FrameIndex(from);});
            key->frame=FrameIndex(to);
            std::ranges::sort(keys,{},&MotionKeyframe::frame);
            edited=editor.setLayerMotion(layerId,std::move(keys));
        }
        if (!edited.ok()) return ii::core::fail(ErrorCode::invalid_argument,edited.message);
        const auto verified=validate(*candidate);
        if (!verified.ok()) return ii::core::fail(ErrorCode::invalid_argument,verified.issues.front().message);
        document_=std::move(candidate);
        return changed(generation);
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot move the keyframe",std::current_exception());
    }
}
ii::core::Result<void> DocumentSession::moveLayerRange(std::string_view requested,std::int64_t first,std::int64_t end) {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another layer operation is in progress");
    const auto count=std::int64_t(document_->timeline.frameCount);
    if (first<0 || end<=first || end>count) return ii::core::fail(ErrorCode::out_of_range,"Layer range is outside the timeline");
    const std::string id(requested);
    const auto *source=findLayer(*document_,id);
    if (!source || !layerKind(*source)) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    const auto &properties=layerProperties(*source);
    const auto oldFirst=properties.frameRange?std::int64_t(properties.frameRange->firstFrame):0;
    const auto oldEnd=properties.frameRange?std::int64_t(properties.frameRange->lastFrame)+1:count;
    if (end-first!=oldEnd-oldFirst) return ii::core::fail(ErrorCode::invalid_argument,"Moving a layer must preserve its duration");
    const auto shift=first-oldFirst;
    if (!shift) return {};
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() ||
        visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        std::vector<KeyframePlacement> keys;
        const auto *keyframed=keyframedLayerSource(*source);
        if (keyframed) {
            keys.reserve(keyframed->frameIndices.size());
            for (const auto frame:keyframed->frameIndices) {
                // IISC requires the first owner at zero; it supplies the held
                // base content even when the visible layer starts later.
                const auto destination=frame==0?std::int64_t{0}:std::int64_t(frame)+shift;
                if ((frame!=0 && destination<=0) || destination>=count)
                    return ii::core::fail(ErrorCode::out_of_range,"Source keys would leave the timeline");
                const auto *owner=findFrame(*document_,frame);
                const auto *key=owner?findKeyframe(*owner,id):nullptr;
                if (!key) return ii::core::fail(ErrorCode::failed,"Layer source keys are inconsistent");
                keys.push_back({static_cast<FrameIndex>(destination),key->assetId});
            }
        }
        auto motion=properties.motion;
        for (auto &key:motion) {
            const auto destination=std::int64_t(key.frame)+shift;
            if (destination<0 || destination>=count)
                return ii::core::fail(ErrorCode::out_of_range,"Motion keys would leave the timeline");
            key.frame=static_cast<FrameIndex>(destination);
        }
        auto candidate=std::make_shared<Document>(*document_);
        DocumentEditor editor(*candidate);
        if (keyframed) {
            const auto result=editor.setKeyframedSource(id,std::move(keys));
            if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
        }
        if (!motion.empty()) {
            const auto result=editor.setLayerMotion(id,std::move(motion));
            if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
        }
        std::optional<LayerFrameRange> range;
        if (first!=0 || end!=count) range=LayerFrameRange{static_cast<FrameIndex>(first),static_cast<FrameIndex>(end-1)};
        const auto result=editor.setLayerFrameRange(id,range);
        if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
        document_=std::move(candidate);
        (void)dirty.set(true);
        if (documentGeneration.get()!=generation) return ii::core::fail(ErrorCode::cancelled,"The active document changed during the move");
        (void)revision.set(revision.get()+1);
        if (documentGeneration.get()!=generation) return ii::core::fail(ErrorCode::cancelled,"The active document changed during the move");
        (void)visualRevision.set(visualRevision.get()+1);
        return {};
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot move the selected layer",std::current_exception());
    }
}
ii::core::Result<std::string> DocumentSession::createLayer(iiSharedCanvas::LayerKind kind) {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return std::unexpected(valid.error());
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    bool bitmap=false,dynamic=false;
    switch (kind) {
    case LayerKind::StaticBitmap:bitmap=true;break;
    case LayerKind::StaticVector:break;
    case LayerKind::DynamicBitmap:bitmap=true;dynamic=true;break;
    case LayerKind::DynamicVector:dynamic=true;break;
    default:return ii::core::fail(ErrorCode::invalid_argument,"Unknown layer type");
    }
    // The installed iiSharedCanvas supports sparse bitmap assets only on
    // infinite canvases. Bound the finite fallback to avoid allocating an
    // unbounded canvas-sized bitmap on the view thread.
    constexpr std::uint64_t finiteBitmapPixelBudget=16ULL*1024*1024;
    if (bitmap && document_->canvasMode==CanvasMode::Finite &&
        std::uint64_t(document_->extent.width)*std::uint64_t(document_->extent.height)>finiteBitmapPixelBudget)
        return ii::core::fail(ErrorCode::out_of_range,"The canvas is too large for an interactive empty bitmap layer");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another layer operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() ||
        visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        const auto priorSelection=selectedLayer.get();
        auto candidate=std::make_shared<Document>(*document_);
        auto unique=[&](std::string_view prefix,auto exists) {
            for (std::uint64_t number=1;number<std::numeric_limits<std::uint64_t>::max();++number) {
                auto value=std::string(prefix)+std::to_string(number);
                if (!exists(value)) return value;
            }
            throw std::overflow_error("Document identity is exhausted");
        };
        const auto id=unique("layer.new.",[&](const auto &value) {
            return findLayer(*candidate,value) || std::ranges::any_of(candidate->audioTracks,[&](const auto &entry){return entry.id==value;});
        });
        const auto asset=unique("asset.new.",[&](const auto &value) {
            return findAsset(*candidate,value) || std::ranges::any_of(candidate->audioAssets,[&](const auto &entry){return entry.id==value;});
        });
        const auto name=unique("Layer ",[&](const auto &value) {
            return std::ranges::any_of(candidate->layers,[&](const auto &entry){return layerProperties(entry).name==value;});
        });
        const auto *selected=findLayer(*candidate,priorSelection);
        const auto position=selected?static_cast<std::size_t>(selected-candidate->layers.data())+1:candidate->layers.size();
        DocumentEditor editor(*candidate);
        const auto insertedAsset=!bitmap?editor.insertVectorAsset(asset,candidate->extent,{}):
            candidate->canvasMode==CanvasMode::Infinite?editor.insertChunkedRasterAsset(ChunkedRasterAsset{asset,{}}):
            editor.insertRasterAsset(asset,makeRasterLayer(candidate->extent.width,candidate->extent.height,0));
        if (!insertedAsset.ok()) return ii::core::fail(ErrorCode::failed,insertedAsset.message);
        LayerProperties properties;properties.id=id;properties.name=name;
        const auto representation=bitmap?LayerRepresentation::Bitmap:LayerRepresentation::Vector;
        const auto insertedLayer=dynamic?editor.insertDynamicLayer(std::move(properties),representation,{{0,asset}},position):
                                          editor.insertStaticLayer(std::move(properties),representation,asset,position);
        if (!insertedLayer.ok()) return ii::core::fail(ErrorCode::failed,insertedLayer.message);
        const auto verified=validate(*candidate);
        if (!verified.ok()) return ii::core::fail(ErrorCode::invalid_argument,verified.issues.front().message);
        auto rows=layers.get();
        auto row=std::make_shared<DocumentLayerRow>();row->id=id;row->owner=shared_from_this();
        (void)row->name.set(name);(void)row->visible.set(true);
        rows.insert(rows.begin()+static_cast<std::ptrdiff_t>(position),std::move(row));
        document_=std::move(candidate);
        (void)layers.set(std::move(rows));
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during layer creation");
        if (auto edited=changed(generation); !edited) return std::unexpected(edited.error());
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during layer creation");
        if (selectedLayer.get()==priorSelection) {
            if (auto selectedResult=selectLayer(id); !selectedResult) return std::unexpected(selectedResult.error());
        }
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during layer creation");
        return id;
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot create the layer",std::current_exception());
    }
}
ii::core::Result<std::string> DocumentSession::createAudioTrack() {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return std::unexpected(valid.error());
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        auto unique=[&](std::string_view prefix,auto exists) {
            for (std::uint64_t number=1;number<std::numeric_limits<std::uint64_t>::max();++number) {
                auto value=std::string(prefix)+std::to_string(number);
                if (!exists(value)) return value;
            }
            throw std::overflow_error("Document identity is exhausted");
        };
        const auto id=unique("audio.track.",[&](const auto &value) {
            return findLayer(*document_,value) || findAudioTrack(*document_,value);
        });
        const auto name=unique("Audio ",[&](const auto &value) {
            return std::ranges::any_of(document_->audioTracks,[&](const auto &track){return track.name==value;}) ||
                   std::ranges::any_of(document_->layers,[&](const auto &layer){return layerProperties(layer).name==value;});
        });
        const auto generation=documentGeneration.get();
        if (auto detached=detachForEdit(false); !detached) return std::unexpected(detached.error());
        auto *edited=document_.get();const auto priorVersion=edited->formatVersion;
        try {
            DocumentEditor editor(*edited);
            const auto inserted=editor.insertAudioTrack(AudioTrackLayer{.id=id,.name=name});
            if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        } catch (...) {
            // The ID was unused before this operation. Retire a partial insert
            // if validation or allocation threw inside the editor.
            const auto found=std::ranges::find_if(edited->audioTracks,[&](const auto &track){return track.id==id;});
            if (found!=edited->audioTracks.end()) edited->audioTracks.erase(found);
            edited->formatVersion=priorVersion;
            throw;
        }
        if (auto changedResult=changed(generation,false); !changedResult) return std::unexpected(changedResult.error());
        return id;
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot create the audio track",std::current_exception());
    }
}
ii::core::Result<std::string> DocumentSession::duplicateLayer(std::string_view requested) {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return std::unexpected(valid.error());
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another layer operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    if (visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Canvas visual revision is exhausted");
    const std::string sourceId(requested);
    const auto *source=findLayer(*document_,sourceId);
    if (!source) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    if (!layerKind(*source))
        return ii::core::fail(ErrorCode::invalid_argument,"Only bitmap and vector layers can be duplicated");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        const auto priorSelection=selectedLayer.get();
        auto candidate=std::make_shared<Document>(*document_);
        const auto *original=findLayer(*candidate,sourceId);
        const auto position=static_cast<std::size_t>(original-candidate->layers.data());
        auto copy=*original;
        auto unique=[&](std::string_view prefix,auto exists) {
            for (std::uint64_t number=1;number<std::numeric_limits<std::uint64_t>::max();++number) {
                auto value=std::string(prefix)+std::to_string(number);
                if (!exists(value)) return value;
            }
            throw std::overflow_error("Document identity is exhausted");
        };
        const auto newId=unique("layer.copy.",[&](const auto &value) {
            return findLayer(*candidate,value) || std::ranges::any_of(candidate->audioTracks,[&](const auto &track){return track.id==value;});
        });
        layerProperties(copy).id=newId;
        layerProperties(copy).name+=" copy";
        std::map<std::string,std::string> copies;
        auto cloneAsset=[&](const std::string &oldId) -> std::string {
            if (auto found=copies.find(oldId);found!=copies.end()) return found->second;
            const auto *asset=findAsset(*candidate,oldId);
            if (!asset) throw std::runtime_error("Layer source asset is unavailable");
            auto duplicate=*asset;
            auto id=unique("asset.copy.",[&](const auto &value) {
                return findAsset(*candidate,value) || std::ranges::any_of(candidate->audioAssets,[&](const auto &item){return item.id==value;});
            });
            std::visit([&](auto &item){item.id=id;},duplicate);
            candidate->assets.push_back(std::move(duplicate));
            copies.emplace(oldId,id);
            return id;
        };
        auto &appearance=layerProperties(copy).appearance;
        auto copyMask=[&](std::optional<LayerMask> &mask) {if (mask) mask->assetId=cloneAsset(mask->assetId);};
        copyMask(appearance.rasterMask); copyMask(appearance.vectorMask); copyMask(appearance.filterMask);
        for (auto &entry:appearance.maskStack) entry.mask.assetId=cloneAsset(entry.mask.assetId);
        std::vector<KeyframePlacement> keys;
        if (auto *content=staticLayerSource(copy)) content->assetId=cloneAsset(content->assetId);
        else if (keyframedLayerSource(copy)) {
            for (const auto &frame:candidate->frames)
                for (const auto &key:frame.keyframes)
                    if (key.layerId==sourceId) keys.push_back({frame.index,cloneAsset(key.assetId)});
        } else return ii::core::fail(ErrorCode::invalid_argument,"Unsupported layer source");
        DocumentEditor editor(*candidate);
        const auto result=keyframedLayerSource(copy)
            ?editor.insertKeyframedLayer(std::move(copy),std::move(keys),position+1)
            :editor.insertLayer(std::move(copy),position+1);
        if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
        auto rows=layers.get();
        auto row=std::make_shared<DocumentLayerRow>(); row->id=newId; row->owner=shared_from_this();
        const auto &properties=layerProperties(candidate->layers[position+1]);
        (void)row->name.set(properties.name); (void)row->visible.set(properties.visible);
        rows.insert(rows.begin()+static_cast<std::ptrdiff_t>(position+1),std::move(row));
        document_=std::move(candidate);
        (void)layers.set(std::move(rows));
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during duplication");
        (void)dirty.set(true);
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during duplication");
        if (revision.get()==std::numeric_limits<std::uint64_t>::max())
            return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
        (void)revision.set(revision.get()+1);
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during duplication");
        (void)visualRevision.set(visualRevision.get()+1);
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during duplication");
        if (priorSelection==sourceId && selectedLayer.get()==sourceId) {
            if (auto selected=selectLayer(newId); !selected) return std::unexpected(selected.error());
        }
        return newId;
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot duplicate the selected layer",std::current_exception());
    }
}
ii::core::Result<void> DocumentSession::removeLayer(std::string_view requested) {
    using namespace iiSharedCanvas;
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another layer operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() ||
        visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    const std::string id(requested);
    const auto *existing=findLayer(*document_,id);
    if (!existing) return ii::core::fail(ErrorCode::invalid_argument,"Layer is unavailable");
    struct Guard {bool &flag; explicit Guard(bool &value):flag(value){flag=true;} ~Guard(){flag=false;}} guard(editingStructure_);
    try {
        const auto generation=documentGeneration.get();
        const auto index=static_cast<std::size_t>(existing-document_->layers.data());
        auto candidate=std::make_shared<Document>(*document_);
        DocumentEditor editor(*candidate);
        const auto result=editor.removeLayer(id);
        if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
        auto rows=layers.get();
        if (index>=rows.size() || rows[index]->id!=id)
            return ii::core::fail(ErrorCode::failed,"Layer projection is out of date");
        rows.erase(rows.begin()+static_cast<std::ptrdiff_t>(index));
        const auto fallback=rows.empty()?std::string{}:rows[std::min(index,rows.size()-1)]->id;
        document_=std::move(candidate);
        (void)layers.set(std::move(rows));
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during layer removal");
        if (selectedLayer.get()==id) {
            if (fallback.empty()) {selectedLayerGeneration_=0;(void)selectedLayer.set({});}
            else if (auto selected=selectLayer(fallback); !selected) return selected;
        }
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during layer removal");
        (void)dirty.set(true);
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during layer removal");
        (void)revision.set(revision.get()+1);
        if (documentGeneration.get()!=generation)
            return ii::core::fail(ErrorCode::cancelled,"The active document changed during layer removal");
        (void)visualRevision.set(visualRevision.get()+1);
        return {};
    } catch (...) {
        return ii::core::fail(ErrorCode::failed,"Cannot remove the selected layer",std::current_exception());
    }
}
ii::core::Result<void> DocumentSession::editPublication(std::string_view key,std::string value) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (key!="title" && key!="description") return ii::core::fail(ErrorCode::invalid_argument,"Unknown publication field");
    auto metadata=document_->publicationMetadata.value_or(iiSharedCanvas::PublicationMetadata{});
    if (document_->publicationMetadata && (key=="title"?metadata.title:metadata.description)==value) return {};
    const auto generation=documentGeneration.get();
    if (key=="title") metadata.title=std::move(value); else metadata.description=std::move(value);
    if (auto result=detachForEdit(false); !result) return result;
    iiSharedCanvas::DocumentEditor editor(*document_);
    const auto result=editor.setPublicationMetadata(std::move(metadata));
    if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
    if (result.changed) {
        const auto title=document_->publicationMetadata->title,description=document_->publicationMetadata->description;
        (void)publicationTitle.set(title);
        if (documentGeneration.get()!=generation) return ii::core::fail(ErrorCode::cancelled,"The active document changed during editing");
        (void)publicationDescription.set(description);
        return changed(generation,false);
    }
    return {};
}
ii::core::Result<void> DocumentSession::editPublicationSetting(std::string key,iiSharedCanvas::PublicationSetting value) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (key.empty()) return ii::core::fail(ErrorCode::invalid_argument,"Publication setting requires a key");
    auto metadata=document_->publicationMetadata.value_or(iiSharedCanvas::PublicationMetadata{});
    const auto found=metadata.settings.find(key);
    if (found!=metadata.settings.end() && found->second==value) return {};
    metadata.settings[std::move(key)]=std::move(value);
    const auto generation=documentGeneration.get();
    if (auto result=detachForEdit(false); !result) return result;
    iiSharedCanvas::DocumentEditor editor(*document_);
    const auto result=editor.setPublicationMetadata(std::move(metadata));
    if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
    return result.changed?changed(generation,false):ii::core::Result<void>{};
}
ii::core::Result<void> DocumentSession::removePublicationSetting(std::string_view key) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    if (key.empty()) return ii::core::fail(ErrorCode::invalid_argument,"Publication setting requires a key");
    if (!document_->publicationMetadata || !document_->publicationMetadata->settings.contains(std::string(key))) return {};
    auto metadata=*document_->publicationMetadata;
    metadata.settings.erase(std::string(key));
    const auto generation=documentGeneration.get();
    if (auto result=detachForEdit(false); !result) return result;
    iiSharedCanvas::DocumentEditor editor(*document_);
    const auto result=editor.setPublicationMetadata(std::move(metadata));
    if (!result.ok()) return ii::core::fail(ErrorCode::failed,result.message);
    return result.changed?changed(generation,false):ii::core::Result<void>{};
}
ii::core::Result<bool> DocumentSession::acknowledgeSave(std::uint64_t generation,std::uint64_t savedRevision,
    const iiSharedCanvas::Document &captured,iiSharedCanvas::Document confirmed) {
    if (auto valid=writable(); !valid) return std::unexpected(valid.error());
    if (!document_ || documentGeneration.get()!=generation)
        return ii::core::fail(ErrorCode::cancelled,"The active document changed while saving");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    try {
        if (revision.get()==savedRevision) {
            // The SDK may stamp authorship while committing. Preserve its
            // confirmed document without disturbing selection or playhead.
            document_=std::make_shared<iiSharedCanvas::Document>(std::move(confirmed));
            const auto committedRevision=savedRevision+1;
            (void)revision.set(committedRevision);
            if (documentGeneration.get()!=generation)
                return ii::core::fail(ErrorCode::cancelled,"The active document changed while saving");
            if (revision.get()!=committedRevision) return false;
            (void)dirty.set(false);
            if (documentGeneration.get()!=generation)
                return ii::core::fail(ErrorCode::cancelled,"The active document changed while saving");
            return revision.get()==committedRevision && !dirty.get();
        }
        // Retain subsequent user edits. Bring forward only the SDK's committed
        // authorship when the user has not independently changed that field.
        if (iiSharedCanvas::authorshipDump(document_->authorship)==iiSharedCanvas::authorshipDump(captured.authorship) &&
            iiSharedCanvas::authorshipDump(document_->authorship)!=iiSharedCanvas::authorshipDump(confirmed.authorship)) {
            if (auto result=detachForEdit(false); !result) return std::unexpected(result.error());
            document_->authorship=std::move(confirmed.authorship);
            (void)revision.set(revision.get()+1);
        }
        return false;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot reconcile the saved canvas",std::current_exception());}
}
void DocumentLayerRow::select() {if (auto value=owner.lock()) (void)value->selectLayer(id);}
void DocumentLayerRow::toggleVisible(bool value) {if (auto model=owner.lock()) (void)model->setLayerVisible(id,value);}
}
