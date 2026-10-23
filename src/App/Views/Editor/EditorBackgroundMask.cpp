#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include <iiGraphicAdjust/Masks.h>
#include <QPainter>
#include <QTransform>

using namespace iiSharedCanvas;
using namespace editorTools;
namespace {
QTransform transformFor(const Document &d, const Layer &layer, FrameIndex frame, QPointF origin) {
    const auto t = sampleLayerAt(d, layer, frame).transform;
    return {t.m11,t.m12,t.m21,t.m22,t.translationX-origin.x(),t.translationY-origin.y()};
}
CanvasExtent sourceExtent(const Document &d,const Layer &layer,FrameIndex frame) {
    const auto *asset=resolveAssetAt(d,layer,frame); if(!asset) return {};
    if(const auto *r=std::get_if<RasterAsset>(asset)) return {r->pixels.width,r->pixels.height};
    if(const auto *v=std::get_if<VectorAsset>(asset)) return v->viewport;
    if(std::holds_alternative<VideoAsset>(*asset)) {
        const auto *video=std::get_if<VideoLayer>(&layer); const auto *pixels=video?resolveVideoFrameAt(d,*video,frame):nullptr;
        if(pixels) return {pixels->width,pixels->height};
    }
    return {};
}
MaskCombineMode combineMode(QString mode) {
    return mode=="Subtract"?MaskCombineMode::Subtract:mode=="Intersect"?MaskCombineMode::Intersect:mode=="Replace"?MaskCombineMode::Replace:MaskCombineMode::Add;
}
QString combineName(MaskCombineMode mode) {
    switch(mode) { case MaskCombineMode::Subtract:return "Subtract"; case MaskCombineMode::Intersect:return "Intersect"; case MaskCombineMode::Replace:return "Replace"; default:return "Add"; }
}
bool regionMatches(const SemanticSegmentation &data,const SemanticRegion &region,bool sky) {
    const auto *type=findSemanticClass(data,region.classId); if(!type) return false;
    QStringList terms{QString::fromStdString(type->key),QString::fromStdString(type->name),QString::fromStdString(type->category),QString::fromStdString(region.name)};
    for(const auto &alias:type->aliases) terms.append(QString::fromStdString(alias));
    const QString joined=terms.join(' ').toLower();
    return sky?joined.contains("sky")||joined.contains(QString::fromUtf8("하늘"))
        :joined.contains("subject")||joined.contains("foreground")||joined.contains("person")||joined.contains("animal")||joined.contains("object")||joined.contains(QString::fromUtf8("피사체"));
}
RasterMaskResult combinePanelMasks(const RasterMask &base,const RasterMask &operand,MaskOperation operation) {
    if(operation==MaskOperation::Replace)return {operand,{}};
    iiGraphicAdjust::Mask a{std::size_t(base.extent.width),std::size_t(base.extent.height),{}},b{std::size_t(operand.extent.width),std::size_t(operand.extent.height),{}};
    a.coverage.reserve(base.alpha.size());b.coverage.reserve(operand.alpha.size());
    for(auto value:base.alpha)a.coverage.push_back(value/255.f);
    for(auto value:operand.alpha)b.coverage.push_back(value/255.f);
    const auto result=iiGraphicAdjust::combineMasks(a.view(),b.view(),operation==MaskOperation::Subtract?iiGraphicAdjust::MaskCombine::Subtract:operation==MaskOperation::Intersect?iiGraphicAdjust::MaskCombine::Intersect:iiGraphicAdjust::MaskCombine::Add);
    if(!result)return {{},std::string(result.error().message)};
    RasterMask mask{base.extent,std::vector<std::uint8_t>(result->coverage.size())};
    for(std::size_t i=0;i<mask.alpha.size();++i)mask.alpha[i]=std::uint8_t(std::lround(result->coverage[i]*255));
    return {std::move(mask),{}};
}
RasterLayer maskAssetPixels(const RasterMask &mask) {
    auto result=makeRasterLayer(mask.extent.width,mask.extent.height);
    for(std::size_t i=0;i<mask.alpha.size();++i) { const auto a=mask.alpha[i]; result.pixels[i]=qRgb(a,a,a); }
    return result;
}
}
bool EditorCanvas::backgroundMaskActive() const { return m_tool=="background"&&m_toolValues.value("backgroundView","Mask").toString()=="Mask"; }
int EditorCanvas::backgroundMaskIndex() const {
    if(!documentReady()) return -1; const auto *layer=findLayer(*document(),selectedLayerId().toStdString()); if(!layer) return -1;
    const auto &stack=layerProperties(*layer).appearance.maskStack;
    for(int i=0;i<int(stack.size());++i) if(stack[i].id==m_selectedBackgroundMask.toStdString()) return i;
    return stack.empty()?-1:int(stack.size()-1);
}
QVariantMap EditorCanvas::maskPanelInfo() const {
    QVariantList masks,targets; QVariantMap selected; bool subject=false,sky=false; QString reason;
    const Layer *layer=documentReady()?findLayer(*document(),selectedLayerId().toStdString()):nullptr;
    const auto extent=layer?sourceExtent(*document(),*layer,frame()):CanvasExtent{};
    bool valid=layer&&layerRole(*layer)==LayerRole::Artwork&&extent.width>0&&extent.height>0;
    if(valid && (std::uint64_t(extent.width+12)*(extent.height+12)>16ULL*1024*1024||std::uint64_t(editingWidth())*editingHeight()>64ULL*1024*1024)) { valid=false; reason=tr("This layer exceeds the mask processing budget."); }
    if(valid && m_pixelLocks.contains(selectedLayerId())) { valid=false; reason=tr("Unlock the selected layer's pixels before editing its mask."); }
    if(!valid&&reason.isEmpty()) reason=tr("Select a bitmap, vector or video artwork layer.");
    if(documentReady()) for(const auto &candidate:document()->layers) {
        if(!belongsToActiveArtboard(candidate)) continue;
        if(layerRole(candidate)==LayerRole::Artwork&&sourceExtent(*document(),candidate,frame()).width>0)
            targets.append(QVariantMap{{"id",QString::fromStdString(layerProperties(candidate).id)},{"name",QString::fromStdString(layerProperties(candidate).name)}});
        if(const auto *segment=std::get_if<SemanticSegmentLayer>(&candidate);segment && segment->control.enabled && layerExistsAt(*document(),candidate,frame()) && resolveAssetAt(*document(),candidate,frame())) for(const auto &region:segment->segmentation.regions) {
            subject=subject||regionMatches(segment->segmentation,region,false); sky=sky||regionMatches(segment->segmentation,region,true);
        }
    }
    bool applied=false,legacy=false;
    if(layer) {
        const auto &appearance=layerProperties(*layer).appearance; const int index=backgroundMaskIndex();
        applied=appearance.maskStackEnabled&&!appearance.maskStack.empty(); legacy=appearance.rasterMask.has_value()||appearance.vectorMask.has_value();
        for(int i=0;i<int(appearance.maskStack.size());++i) {
            const auto &entry=appearance.maskStack[i];
            QVariantMap item{{"id",QString::fromStdString(entry.id)},{"name",QString::fromStdString(entry.name)},
                {"visible",entry.mask.enabled},{"inverted",entry.mask.inverted},{"density",entry.mask.density*100},
                {"feather",entry.mask.featherRadius},{"combine",combineName(entry.combine)},{"linked",entry.mask.linkedToLayer},{"selected",i==index}};
            masks.append(item); if(i==index) {
                if(const auto *asset=findRasterAsset(*document(),entry.mask.assetId)) item["empty"]=std::all_of(asset->pixels.pixels.begin(),asset->pixels.pixels.end(),[&](auto p){return entry.mask.channel==MaskChannel::Alpha?qAlpha(p)==0:qAlpha(p)==0||(p&0x00ffffff)==0;});
                selected=item;
            }
        }
    }
    return {{"canEdit",valid},{"reason",reason},{"masks",masks},{"selected",selected},{"targets",targets},{"targetId",selectedLayerId()},
        {"applied",applied},{"legacyMasks",legacy},{"canSelectSubject",valid&&subject},{"canSelectSky",valid&&sky},{"polygonPoints",m_maskPolygon.size()},
        {"drawing",m_maskGestureActive},{"semanticReason",tr("Subject and sky selection require matching semantic segmentation regions in this artboard.")}};
}
RasterMask EditorCanvas::backgroundMaskPixels() const {
    RasterMask result{{editingWidth(),editingHeight()},std::vector<std::uint8_t>(std::size_t(editingWidth())*editingHeight(),0)};
    const auto index=backgroundMaskIndex(); if(index<0) return result;
    const auto *layer=findLayer(*document(),selectedLayerId().toStdString()); const auto &entry=layerProperties(*layer).appearance.maskStack[index];
    const auto *raw=findAsset(*document(),entry.mask.assetId); if(!raw)return result;
    RasterLayer sample;bool vectorCoverage=false;
    if(const auto *raster=std::get_if<RasterAsset>(raw))sample=raster->pixels;
    else if(const auto *vector=std::get_if<VectorAsset>(raw)) {
        Document maskDocument;maskDocument.extent=vector->viewport;auto white=*vector;
        for(auto &path:white.paths){if(path.fill)path.fill->argb=0xffffffff;if(path.stroke)path.stroke->paint.argb=0xffffffff;}
        maskDocument.assets.emplace_back(white);maskDocument.layers.emplace_back(StaticVectorLayer{{"coverage","Coverage"},StaticSource{white.id}});
        const auto rendered=renderFrame(maskDocument,0);if(!rendered.ok())return result;sample=rendered.pixels;vectorCoverage=true;
    } else return result;
    // Product-created operands are layer-local bitmap masks. Preserve native
    // properties in the renderer; editing samples the raw committed coverage.
    const auto base=entry.mask.linkedToLayer?transformFor(*document(),*layer,frame(),editingOrigin()):QTransform{};
    const auto &t=entry.mask.transform; const QTransform maskTransform(t.m11,t.m12,t.m21,t.m22,t.translationX,t.translationY);
    const auto mapped=maskTransform*base; bool invertible=false; const auto inverse=mapped.inverted(&invertible); if(!invertible) return result;
    for(int y=0;y<result.extent.height;++y) for(int x=0;x<result.extent.width;++x) {
        const auto local=inverse.map(QPointF(x+.5,y+.5)); const int px=int(std::floor(local.x())),py=int(std::floor(local.y()));
        result.alpha[std::size_t(y)*result.extent.width+x]=px>=0&&py>=0&&px<sample.width&&py<sample.height
            ?std::uint8_t(entry.mask.channel==MaskChannel::Alpha||vectorCoverage?qAlpha(sample.pixels[std::size_t(py)*sample.width+px]):int(std::lround(qAlpha(sample.pixels[std::size_t(py)*sample.width+px])*(.2126*qRed(sample.pixels[std::size_t(py)*sample.width+px])+.7152*qGreen(sample.pixels[std::size_t(py)*sample.width+px])+.0722*qBlue(sample.pixels[std::size_t(py)*sample.width+px]))/255.)))
            :std::uint8_t(std::lround(entry.mask.outsideCoverage*255));
    }
    return result;
}
RasterMask EditorCanvas::backgroundMaskToLayer(const RasterMask &canvas) const {
    const auto *layer=findLayer(*document(),selectedLayerId().toStdString()); const auto extent=sourceExtent(*document(),*layer,frame());
    RasterMask result{extent,std::vector<std::uint8_t>(std::size_t(extent.width)*extent.height,0)};
    const auto transform=transformFor(*document(),*layer,frame(),editingOrigin());
    for(int y=0;y<extent.height;++y) for(int x=0;x<extent.width;++x) {
        const auto p=transform.map(QPointF(x+.5,y+.5)); const int px=int(std::floor(p.x())),py=int(std::floor(p.y()));
        if(px>=0&&py>=0&&px<canvas.extent.width&&py<canvas.extent.height) result.alpha[std::size_t(y)*extent.width+x]=canvas.alpha[std::size_t(py)*canvas.extent.width+px];
    }
    return result;
}
bool EditorCanvas::selectBackgroundMask(const QString &id) {
    if(m_maskGestureActive) return fail(tr("Finish the mask gesture first."));
    const auto *layer=documentReady()?findLayer(*document(),selectedLayerId().toStdString()):nullptr; if(!layer) return false;
    const auto &stack=layerProperties(*layer).appearance.maskStack;
    if(std::none_of(stack.begin(),stack.end(),[&](const auto &entry){return entry.id==id.toStdString();})) return fail(tr("Select an existing mask."));
    m_selectedBackgroundMask=id; m_selection=backgroundMaskPixels(); refreshSelectionStatistics(); clearError(); emit toolStateChanged(); return true;
}
bool EditorCanvas::completeBackgroundMask(RasterMask next,const QString &kind,bool replaceCurrent) {
    const auto info=maskPanelInfo(); if(!info["canEdit"].toBool()) return fail(info["reason"].toString());
    if(next.extent.width!=editingWidth()||next.extent.height!=editingHeight()||next.alpha.size()!=std::size_t(editingWidth())*editingHeight()) return fail(tr("The mask extent must match the active artboard."));
    int index=backgroundMaskIndex(); const auto *layer=findLayer(*document(),selectedLayerId().toStdString());
    const auto &stack=layerProperties(*layer).appearance.maskStack;
    const auto *existing=index>=0?findRasterAsset(*document(),stack[index].mask.assetId):nullptr;
    const bool blank=existing&&std::all_of(existing->pixels.pixels.begin(),existing->pixels.pixels.end(),[](auto p){return qGray(p)==0;});
    const bool append=kind=="Empty"||index<0||(!replaceCurrent&&kind!="Eraser"&&!blank&&m_toolValues.value("maskNewEach",true).toBool());
    const auto mode=kind=="Eraser"?QStringLiteral("Subtract"):m_toolValues.value("maskMode","Add").toString();
    if(!append&&!replaceCurrent) {
        const auto operation=mode=="Subtract"?MaskOperation::Subtract:mode=="Intersect"?MaskOperation::Intersect:mode=="Replace"?MaskOperation::Replace:MaskOperation::Add;
        const auto combined=combinePanelMasks(backgroundMaskPixels(),next,operation); if(!combined.ok()) return fail(QString::fromStdString(combined.error)); next=combined.mask;
    }
    auto local=backgroundMaskToLayer(next); const auto pixels=maskAssetPixels(local);
    const auto id=append?unique("mask."):stack[index].id; const auto asset=unique("mask.asset.");
    if(!commit([&](Document &draft){
        auto &appearance=layerProperties(*findLayer(draft,selectedLayerId().toStdString())).appearance;
        draft.assets.emplace_back(RasterAsset{asset,pixels});
        if(append) {
            LayerMaskEntry entry{id,QString("Mask %1 · %2").arg(appearance.maskStack.size()+1,2,10,QChar('0')).arg(kind=="Empty"?"Painted":kind).toStdString(),LayerMask{.assetId=asset,.featherRadius=2,.outsideCoverage=0},combineMode(mode)};
            if(appearance.maskStack.empty()) appearance.maskStackEnabled=false;
            appearance.maskStack.push_back(std::move(entry));
        } else {
            auto &entry=appearance.maskStack[index]; const auto previous=entry.mask.assetId;
            entry.mask.assetId=asset; entry.mask.channel=MaskChannel::Luminance; entry.mask.linkedToLayer=true; entry.mask.transform={}; entry.mask.outsideCoverage=0;
            if(assetReferences(draft,previous).empty()) std::erase_if(draft.assets,[&](const auto &a){return assetId(a)==previous;});
        }
        return true;
    })) return false;
    m_selectedBackgroundMask=QString::fromStdString(id); m_selection=std::move(next); refreshSelectionStatistics(); clearError(); emit toolStateChanged(); return true;
}
bool EditorCanvas::createBackgroundMask() {
    cancelBackgroundMaskGesture(); const auto info=maskPanelInfo(); if(!info["canEdit"].toBool()) return fail(info["reason"].toString());
    const auto previous=m_selectedBackgroundMask; m_selectedBackgroundMask.clear();
    // Explicit New always appends an empty hide-all operand; Apply controls when
    // the stack affects the artwork. A first stroke fills this empty operand.
    auto next=RasterMask{{editingWidth(),editingHeight()},std::vector<std::uint8_t>(std::size_t(editingWidth())*editingHeight(),0)};
    const auto old=m_toolValues; m_toolValues["maskNewEach"]=true;
    const bool ok=completeBackgroundMask(std::move(next),"Empty"); m_toolValues=old;
    if(!ok) m_selectedBackgroundMask=previous; return ok;
}
bool EditorCanvas::duplicateBackgroundMask() {
    if(m_maskGestureActive) return fail(tr("Finish the mask gesture first."));
    const auto info=maskPanelInfo();if(!info["canEdit"].toBool())return fail(info["reason"].toString());
    const int index=backgroundMaskIndex(); if(index<0) return fail(tr("Select a mask first."));
    const auto id=unique("mask."); const bool ok=commit([&](Document &draft){auto &stack=layerProperties(*findLayer(draft,selectedLayerId().toStdString())).appearance.maskStack; auto copy=stack[index]; copy.id=id; copy.name+= " copy"; stack.insert(stack.begin()+index+1,std::move(copy)); return true;});
    if(ok){m_selectedBackgroundMask=QString::fromStdString(id); emit toolStateChanged();} return ok;
}
bool EditorCanvas::deleteBackgroundMask() {
    const auto info=maskPanelInfo();if(!info["canEdit"].toBool())return fail(info["reason"].toString());
    cancelBackgroundMaskGesture(); const int index=backgroundMaskIndex(); if(index<0) return fail(tr("Select a mask first."));
    const bool ok=commit([&](Document &draft){auto &stack=layerProperties(*findLayer(draft,selectedLayerId().toStdString())).appearance.maskStack; const auto asset=stack[index].mask.assetId; stack.erase(stack.begin()+index); if(assetReferences(draft,asset).empty()) std::erase_if(draft.assets,[&](const auto &a){return assetId(a)==asset;}); return true;});
    if(ok){m_selectedBackgroundMask.clear(); m_selection={}; m_selectedPixelCount=0; emit toolStateChanged();} return ok;
}
bool EditorCanvas::setBackgroundMaskProperty(const QString &field,const QVariant &value) {
    if(m_maskGestureActive) return fail(tr("Finish the mask gesture first."));
    const auto info=maskPanelInfo(); if(!info["canEdit"].toBool()) return fail(info["reason"].toString());
    const int index=backgroundMaskIndex(); if(index<0) return fail(tr("Select a mask first."));
    return commit([&](Document &draft){
        auto &entry=layerProperties(*findLayer(draft,selectedLayerId().toStdString())).appearance.maskStack[index];
        if(field=="density"||field=="feather") {
            bool valid=false; const double number=value.toDouble(&valid); const double limit=field=="density"?100:128;
            if(!valid||!std::isfinite(number)||number<0||number>limit) return fail(tr("Enter a value within the mask property's range."));
            if(field=="density") entry.mask.density=number/100; else entry.mask.featherRadius=number;
        } else if(field=="visible") entry.mask.enabled=value.toBool();
        else if(field=="inverted") entry.mask.inverted=value.toBool();
        else if(field=="combine") {const auto mode=value.toString(); if(!QStringList{"Add","Subtract","Intersect","Replace"}.contains(mode)) return fail(tr("Choose a supported mask combination.")); entry.combine=combineMode(mode);}
        else if(field=="name") {const auto name=value.toString().trimmed(); if(name.isEmpty()||name.size()>256) return fail(tr("Enter a mask name of 1–256 characters.")); entry.name=name.toStdString();}
        else if(field=="reset") {entry.mask.density=1;entry.mask.featherRadius=2;entry.mask.inverted=false;entry.mask.enabled=true;entry.combine=MaskCombineMode::Add;}
        else return fail(tr("Unsupported mask property."));
        return true;
    });
}
bool EditorCanvas::detectBackgroundMask(bool sky) {
    if(!maskPanelInfo().value(sky?"canSelectSky":"canSelectSubject").toBool()) return fail(maskPanelInfo()["semanticReason"].toString());
    RasterMask next{{editingWidth(),editingHeight()},std::vector<std::uint8_t>(std::size_t(editingWidth())*editingHeight(),0)};
    for(const auto &layer:document()->layers) if(belongsToActiveArtboard(layer)) if(const auto *segment=std::get_if<SemanticSegmentLayer>(&layer)) {
        QSet<std::uint32_t> ids; for(const auto &region:segment->segmentation.regions) if(regionMatches(segment->segmentation,region,sky)) ids.insert(region.id);
        if(ids.isEmpty()) continue; const auto map=renderSemanticControlMap(*document(),segment->properties.id,frame()); if(!map.ok()) return fail(QString::fromStdString(map.message));
        QImage source(map.pixels.width,map.pixels.height,QImage::Format_ARGB32); source.fill(Qt::transparent);
        for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) if(ids.contains(map.regionIds[std::size_t(y)*source.width()+x])) source.setPixel(x,y,0xffffffff);
        QImage mapped(editingWidth(),editingHeight(),QImage::Format_ARGB32); mapped.fill(Qt::transparent); QPainter painter(&mapped);
        painter.setTransform(transformFor(*document(),layer,frame(),editingOrigin())); painter.drawImage(QPointF{},source); painter.end();
        for(int y=0;y<mapped.height();++y) for(int x=0;x<mapped.width();++x) next.alpha[std::size_t(y)*mapped.width()+x]=std::max(next.alpha[std::size_t(y)*mapped.width()+x],std::uint8_t(qAlpha(mapped.pixel(x,y))));
    }
    if(std::none_of(next.alpha.begin(),next.alpha.end(),[](auto value){return value!=0;}))return fail(tr("There are no matching semantic regions in the current frame."));
    return completeBackgroundMask(std::move(next),sky?"Sky":"Subject");
}
bool EditorCanvas::backgroundMaskAction(const QString &action) {
    if(action!="clear") {const auto info=maskPanelInfo();if(!info["canEdit"].toBool())return fail(info["reason"].toString());}
    if(action=="subject"||action=="sky") return detectBackgroundMask(action=="sky");
    if(action=="clear") {cancelBackgroundMaskGesture(); m_maskPolygon.clear(); clearAreaSelection(); clearError(); return true;}
    if(action=="closePath") {if(m_maskPolygon.size()<3) return fail(tr("Place at least three polygon points before closing the path.")); const auto points=m_maskPolygon; m_maskPolygon.clear(); return completeBackgroundMaskShape(points);}
    if(action=="selectAll") return completeBackgroundMask({{editingWidth(),editingHeight()},std::vector<std::uint8_t>(std::size_t(editingWidth())*editingHeight(),255)},"Selection");
    if(action=="invertSelection") {if(m_selection.alpha.empty()) return fail(tr("Create a selection first.")); auto next=m_selection;for(auto &v:next.alpha)v=255-v;return completeBackgroundMask(std::move(next),"Selection",true);}
    if(action=="invert") {const auto selected=maskPanelInfo()["selected"].toMap();return setBackgroundMaskProperty("inverted",!selected["inverted"].toBool());}
    if(action=="reset") return setBackgroundMaskProperty("reset",true);
    if(action=="apply"||action=="bypass") {
        if(m_maskGestureActive||!m_maskPolygon.empty()) return fail(tr("Finish the current mask or close the polygon path first."));
        if(backgroundMaskIndex()<0) return fail(tr("Create a mask first."));
        return commit([&](Document &draft){layerProperties(*findLayer(draft,selectedLayerId().toStdString())).appearance.maskStackEnabled=action=="apply";return true;});
    }
    return fail(tr("Unsupported mask action."));
}
namespace {
std::optional<RasterMask> shapeCoverage(const QString &tool,const QVector<QPointF> &points,CanvasExtent requestedExtent) {
    if(points.size()<2)return std::nullopt;
    const auto extent=requestedExtent; RasterMask next{extent,std::vector<std::uint8_t>(std::size_t(extent.width)*extent.height,0)};
    if(tool=="Gradient") {
        const auto delta=points.last()-points.first(); const double length=QPointF::dotProduct(delta,delta); if(length<.01) return std::nullopt;
        for(int y=0;y<extent.height;++y) for(int x=0;x<extent.width;++x) next.alpha[std::size_t(y)*extent.width+x]=std::uint8_t(std::lround(255*std::clamp(QPointF::dotProduct(QPointF(x+.5,y+.5)-points.first(),delta)/length,0.,1.)));
    } else {
        QPainterPath path; const auto bounds=QRectF(points.first(),points.last()).normalized();
        if(tool=="Ellipse") path.addEllipse(bounds); else if(tool=="Rectangle") path.addRect(bounds);
        else {if(points.size()<3)return std::nullopt;path.moveTo(points.first());for(int i=1;i<points.size();++i)path.lineTo(points[i]);path.closeSubpath();}
        QImage image(extent.width,extent.height,QImage::Format_ARGB32);image.fill(Qt::transparent);QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);painter.fillPath(path,Qt::white);painter.end();
        for(int y=0;y<extent.height;++y)for(int x=0;x<extent.width;++x)next.alpha[std::size_t(y)*extent.width+x]=qAlpha(image.pixel(x,y));
    }
    return next;
}
}
bool EditorCanvas::completeBackgroundMaskShape(const QVector<QPointF> &points) {
    const auto tool=m_toolValues.value("maskTool","Brush").toString();
    auto mask=shapeCoverage(tool,points,editingExtent());
    if(!mask)return fail(tr("Draw a nonzero shape or a closed path with at least three points."));
    return completeBackgroundMask(std::move(*mask),tool);
}
bool EditorCanvas::beginBackgroundMaskGesture(const QPointF &point,qreal pressure) {
    if(!backgroundMaskActive()) return false;
    const auto info=maskPanelInfo();if(!info["canEdit"].toBool())return fail(info["reason"].toString());
    if(!std::isfinite(point.x())||!std::isfinite(point.y())||!std::isfinite(pressure)||pressure<0||pressure>1)return fail(tr("Mask pointer input must be finite with pressure in [0,1]."));
    m_maskGestureTool=m_toolValues.value("maskTool","Brush").toString();m_maskGestureTarget=selectedLayerId();
    if(m_maskGestureTool=="Polygon") {m_maskPolygon.append(point);emit maskPanelChanged();return true;}
    if(m_maskGestureTool=="Color range") {
        auto draft=*document();auto *layer=findLayer(draft,selectedLayerId().toStdString());const auto index=layerIndex(draft,selectedLayerId().toStdString());
        layerProperties(*layer).visible=true;layerProperties(*layer).opacity=1;layerProperties(*layer).appearance={};
        const auto rendered=renderFrameLayerTiles(draft,frame(),*index,{{{ {int(editingOrigin().x()),int(editingOrigin().y())},editingExtent()},editingExtent()}});
        if(!rendered.ok()||rendered.tiles.empty())return fail(tr("The layer could not be sampled for color range."));
        const auto &pixels=rendered.tiles[0].pixels;const int x=int(std::floor(point.x())),y=int(std::floor(point.y()));
        if(x<0||y<0||x>=pixels.width||y>=pixels.height||!qAlpha(pixels.pixels[std::size_t(y)*pixels.width+x]))return fail(tr("Sample a visible source color inside the layer."));
        const auto image=iiGraphicAdjust::fromArgb32(pixels.width,pixels.height,pixels.pixels);if(!image)return fail(QString::fromUtf8(image.error().message.data(),image.error().message.size()));
        iiGraphicAdjust::ColorRangeOptions options;options.samples={image->pixels[std::size_t(y)*pixels.width+x]};options.range=m_toolValues.value("maskFuzziness",50).toDouble();
        const auto result=iiGraphicAdjust::colorRangeMask(image->view(),options);if(!result)return fail(QString::fromUtf8(result.error().message.data(),result.error().message.size()));
        RasterMask mask{editingExtent(),std::vector<std::uint8_t>(result->coverage.size())};for(std::size_t i=0;i<mask.alpha.size();++i)mask.alpha[i]=std::uint8_t(std::lround(result->coverage[i]*255));
        return completeBackgroundMask(std::move(mask),"Color range");
    }
    m_maskStrokeDevice=m_maskInputDevice==PointerDeviceKind::Mouse && pressure!=1 ? PointerDeviceKind::Tablet : m_maskInputDevice;
    m_maskGesturePoints.clear();m_maskClock.start();
    if(m_maskGestureTool!="Brush" && m_maskGestureTool!="Eraser")m_maskGesturePoints={point};
    if(m_maskGestureTool=="Brush"||m_maskGestureTool=="Eraser") {
        if(m_maskGestureTool=="Eraser"&&backgroundMaskIndex()<0)return fail(tr("Create or select a mask to erase."));
        if(!m_maskPainter.createMask(editingWidth(),editingHeight()))return fail(QString::fromStdString(m_maskPainter.lastError()));
        LocalMaskBrushOptions options;options.sizePixels=m_toolValues.value("maskSize",40).toDouble();options.feather=1-m_toolValues.value("maskHardness",60).toDouble()/100;
        options.flow=m_toolValues.value("maskFlow",100).toDouble()/100;options.strokeOpacity=m_toolValues.value("maskOpacity",100).toDouble()/100;
        options.pressureAffectsSize=options.pressureAffectsOpacity=m_toolValues.value("maskPressure",true).toBool();
        if(!m_maskPainter.setBrushOptions(options))return fail(QString::fromStdString(m_maskPainter.lastError()));
        const auto update=m_maskPainter.processPointerEvent({.device=m_maskStrokeDevice,.phase=PointerEventPhase::Press,.documentPosition={point.x(),point.y()},.pressure=pressure,.button=PointerButton::Primary,.primaryButtonDown=true});
        if(!update.accepted)return fail(QString::fromStdString(update.error));
    } else if(!QStringList{"Lasso","Rectangle","Ellipse","Gradient"}.contains(m_maskGestureTool))return fail(tr("Choose a supported mask tool."));
    m_maskGestureActive=true;clearError();refreshBackgroundMaskOverlay();return true;
}
bool EditorCanvas::continueBackgroundMaskGesture(const QPointF &point,qreal pressure) {
    if(!m_maskGestureActive||m_maskGestureTarget!=selectedLayerId())return false;
    if(!std::isfinite(point.x())||!std::isfinite(point.y())||!std::isfinite(pressure)||pressure<0||pressure>1)return fail(tr("Invalid mask pointer input."));
    if(!m_maskPainter.strokeActive())m_maskGesturePoints.append(point);
    if(m_maskPainter.strokeActive()) {const auto update=m_maskPainter.processPointerEvent({.device=m_maskStrokeDevice,.phase=PointerEventPhase::Move,.documentPosition={point.x(),point.y()},.pressure=pressure,.time=m_maskClock.elapsed()/1000.,.button=PointerButton::Primary,.primaryButtonDown=true});if(!update.accepted)return fail(QString::fromStdString(update.error));}
    refreshBackgroundMaskOverlay();return true;
}
bool EditorCanvas::endBackgroundMaskGesture(const QPointF &point,qreal pressure) {
    if(m_maskGestureTool=="Polygon"||m_maskGestureTool=="Color range")return true;
    if(!m_maskGestureActive||m_maskGestureTarget!=selectedLayerId())return false;
    if(!std::isfinite(point.x())||!std::isfinite(point.y())||!std::isfinite(pressure)||pressure<0||pressure>1){cancelBackgroundMaskGesture();return fail(tr("Invalid mask pointer input."));}
    if(!m_maskPainter.strokeActive())m_maskGesturePoints.append(point);m_maskGestureActive=false;
    if(m_maskPainter.strokeActive()) {
        const auto update=m_maskPainter.processPointerEvent({.device=m_maskStrokeDevice,.phase=PointerEventPhase::Release,.documentPosition={point.x(),point.y()},.pressure=pressure,.time=m_maskClock.elapsed()/1000.,.button=PointerButton::Primary});
        if(!update.accepted){m_maskPainter.cancelStroke();return fail(QString::fromStdString(update.error));}
        const auto &mask=m_maskPainter.mask();RasterMask next{editingExtent(),std::vector<std::uint8_t>(mask.coverage.size())};for(std::size_t i=0;i<next.alpha.size();++i)next.alpha[i]=std::uint8_t(std::lround(mask.coverage[i]*255));
        return completeBackgroundMask(std::move(next),m_maskGestureTool=="Brush"?"Painted":"Eraser");
    }
    const auto points=std::move(m_maskGesturePoints);m_maskGesturePoints.clear();
    return completeBackgroundMaskShape(points);
}
void EditorCanvas::cancelBackgroundMaskGesture() {m_maskPainter.cancelStroke();m_maskGestureActive=false;m_maskGesturePoints.clear();m_maskPolygon.clear();refreshBackgroundMaskOverlay();}
void EditorCanvas::refreshBackgroundMaskOverlay() {
    m_backgroundMaskOverlay=QUrl{};
    if(backgroundMaskActive()&&documentReady()&&m_toolValues.value("maskOverlay",true).toBool()&&maskPanelInfo()["canEdit"].toBool()) {
        RasterMask mask=backgroundMaskPixels();
        bool previewing=m_maskPainter.strokeActive();
        RasterMask operand;
        if(previewing) {const auto &preview=m_maskPainter.previewMask();operand={editingExtent(),std::vector<std::uint8_t>(preview.coverage.size())};for(std::size_t i=0;i<operand.alpha.size();++i)operand.alpha[i]=std::uint8_t(std::lround(preview.coverage[i]*255));}
        else if(m_maskGestureActive) {auto shape=shapeCoverage(m_maskGestureTool,m_maskGesturePoints,editingExtent());if(shape){operand=std::move(*shape);previewing=true;}}
        if(previewing) {
            if(backgroundMaskIndex()>=0 && (m_maskGestureTool=="Eraser" || !m_toolValues.value("maskNewEach",true).toBool())) {
                const auto mode=m_maskGestureTool=="Eraser"?QStringLiteral("Subtract"):m_toolValues.value("maskMode","Add").toString();
                const auto combined=combinePanelMasks(mask,operand,mode=="Subtract"?MaskOperation::Subtract:mode=="Intersect"?MaskOperation::Intersect:mode=="Replace"?MaskOperation::Replace:MaskOperation::Add);
                if(combined.ok())mask=combined.mask;
            } else mask=std::move(operand);
        }
        const auto index=backgroundMaskIndex();const auto *layer=findLayer(*document(),selectedLayerId().toStdString());
        if(index>=0&&!previewing) {
            const auto &entry=layerProperties(*layer).appearance.maskStack[index];
            if(entry.mask.featherRadius>0){auto feathered=featherMask(mask,entry.mask.featherRadius);if(feathered.ok())mask=std::move(feathered.mask);}
            for(auto &value:mask.alpha){double coverage=value/255.;if(entry.mask.inverted)coverage=1-coverage;value=std::uint8_t(std::lround(255*(1-entry.mask.density+entry.mask.density*coverage)));}
        }
        if(index>=0||previewing||!m_maskPolygon.empty()) {
            const QColor color(m_toolValues.value("maskOverlayColor","#FF5C77").toString());const double opacity=std::clamp(m_toolValues.value("maskOverlayOpacity",50).toDouble()/100.,0.,1.)*color.alphaF();
            QImage overlay(mask.extent.width,mask.extent.height,QImage::Format_ARGB32);overlay.fill(Qt::transparent);
            for(int y=0;y<overlay.height();++y)for(int x=0;x<overlay.width();++x)overlay.setPixel(x,y,qRgba(color.red(),color.green(),color.blue(),int(std::lround(mask.alpha[std::size_t(y)*overlay.width()+x]*opacity))));
            if(!m_maskPolygon.empty()) {QPainter painter(&overlay);QPen pen(color,1.5/std::max(.01,double(zoom())));pen.setStyle(Qt::DashLine);painter.setPen(pen);painter.drawPolyline(m_maskPolygon);for(const auto &point:m_maskPolygon)painter.drawEllipse(point,2./std::max(.01,double(zoom())),2./std::max(.01,double(zoom())));}
            m_backgroundMaskOverlay=dataImage(overlay);
        }
    }
    emit maskPanelChanged();
    emit selectionOverlayChanged();
}
