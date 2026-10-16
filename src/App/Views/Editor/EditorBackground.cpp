#include "EditorCanvas.h"
#include "EditorToolUtils.h"
#include "App/Generation/ImageCanvas.h"
#include <iiColorManager/ColorManager.h>
#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScopeGuard>

using namespace iiSharedCanvas;
using namespace editorTools;
namespace {
std::optional<double> parameter(const QVariantMap &values, const char *key, double fallback, double minimum, double maximum) {
    bool ok=false;
    const auto value=values.value(QLatin1String(key),fallback).toDouble(&ok);
    if(!ok || !std::isfinite(value) || value<minimum || value>maximum) return {};
    return value;
}
iiColorManager::Rgba rgba(const QColor &color) { return {color.redF(),color.greenF(),color.blueF(),color.alphaF()}; }
RasterLayer maskPixels(const RasterMask &mask) {
    auto result=makeRasterLayer(mask.extent.width,mask.extent.height);
    for(std::size_t i=0;i<mask.alpha.size();++i)result.pixels[i]=0xff000000U|std::uint32_t(mask.alpha[i])*0x010101U;
    return result;
}
}

bool EditorCanvas::applyBackgroundFill(const QVariantMap &values) {
    if(!documentReady())return fail(tr("Open a canvas first."));
    const auto opacity=parameter(values,"backgroundOpacity",100,0,100),alpha=parameter(values,"backgroundAlpha",100,0,100),angle=parameter(values,"backgroundAngle",90,-360,360);
    const QColor solid(values.value("backgroundColor","#8066FF").toString());
    if(!opacity||!alpha||!angle||!solid.isValid())return fail(tr("Enter valid background colors and values."));
    const auto extent=editingExtent();
    if(std::uint64_t(extent.width)*extent.height>MediaLimits{}.maxPixelsPerFrame)return fail(tr("The background exceeds the image allocation limit."));
    struct Stop { double position; iiColorManager::Rgba color; iiColorManager::Lab lab; };
    std::vector<Stop> stops;
    const bool gradient=values.value("backgroundFill","Solid").toString()=="Gradient";
    if(gradient) {
        const auto input=values.value("backgroundGradientStops",QVariantList{QVariantMap{{"position",0},{"color","#8066FF"}},QVariantMap{{"position",.5},{"color","#E88DD9"}},QVariantMap{{"position",1},{"color","#FFAA5A"}}}).toList();
        if(input.size()<2||input.size()>32)return fail(tr("A gradient needs 2–32 color stops."));
        for(const auto &entry:input) {
            const auto stop=entry.toMap();const auto position=parameter(stop,"position",0,0,1);const QColor color(stop.value("color").toString());
            if(!position||!color.isValid())return fail(tr("Enter valid gradient stops."));
            stops.push_back({*position,rgba(color),iiColorManager::toLab(rgba(color))});
        }
        std::stable_sort(stops.begin(),stops.end(),[](const auto &a,const auto &b){return a.position<b.position;});
    }
    auto pixels=makeRasterLayer(extent.width,extent.height);
    const double radians=(*angle-90)*std::numbers::pi/180,dx=std::cos(radians),dy=std::sin(radians),span=std::max(1.,std::abs(dx)*extent.width+std::abs(dy)*extent.height);
    const bool radial=values.value("backgroundGradientType","Linear").toString()=="Radial",perceptual=values.value("backgroundInterpolation","Perceptual").toString()=="Perceptual";
    for(int y=0;y<extent.height;++y)for(int x=0;x<extent.width;++x) {
        auto color=rgba(solid);
        if(gradient) {
            const double position=std::clamp(radial?std::hypot((x+.5-extent.width/2.)/std::max(1.,extent.width/2.),(y+.5-extent.height/2.)/std::max(1.,extent.height/2.)): .5+((x+.5-extent.width/2.)*dx+(y+.5-extent.height/2.)*dy)/span,0.,1.);
            const auto upper=std::upper_bound(stops.begin(),stops.end(),position,[](double v,const Stop &s){return v<s.position;});
            if(upper==stops.begin())color=stops.front().color;
            else if(upper==stops.end())color=stops.back().color;
            else {
                const auto &b=*upper,&a=*(upper-1);const double t=std::clamp((position-a.position)/std::max(.000001,b.position-a.position),0.,1.);
                if(perceptual)color=iiColorManager::fromLab({std::lerp(a.lab.l,b.lab.l,t),std::lerp(a.lab.a,b.lab.a,t),std::lerp(a.lab.b,b.lab.b,t)},std::lerp(a.color.a,b.color.a,t)).color;
                else color={std::lerp(a.color.r,b.color.r,t),std::lerp(a.color.g,b.color.g,t),std::lerp(a.color.b,b.color.b,t),std::lerp(a.color.a,b.color.a,t)};
            }
        }
        pixels.pixels[std::size_t(y)*extent.width+x]=QColor::fromRgbF(color.r,color.g,color.b,std::clamp(color.a * *opacity * *alpha /10000.,0.,1.)).rgba();
    }
    const auto layerId=backgroundLayerId(),assetId=layerId+":asset";
    if(m_pixelLocks.contains(QString::fromStdString(layerId)))return fail(tr("Unlock the background layer first."));
    return commit([&](Document &document) {
        if(auto *asset=findAsset(document,assetId))*asset=RasterAsset{assetId,std::move(pixels)};
        else document.assets.emplace_back(RasterAsset{assetId,std::move(pixels)});
        LayerProperties properties{layerId,"Background"};if(activeArtboard())properties.artboardId=m_activeArtboard;
        if(!activeArtboard()){properties.transform.translationX=editingOrigin().x();properties.transform.translationY=editingOrigin().y();}
        if(auto *layer=findLayer(document,layerId))*layer=StaticBitmapLayer{properties,StaticSource{assetId}};
        else document.layers.insert(document.layers.begin(),StaticBitmapLayer{properties,StaticSource{assetId}});
        return true;
    });
}

QVariantMap EditorCanvas::previewBackgroundRemoval(const QVariantMap &values) {
    m_backgroundRemovalMask={};m_backgroundRemovalTarget.clear();
    const auto reject=[&](const QString &message){fail(message);return QVariantMap{{"ready",false},{"error",message}};};
    if(!documentReady()||!selectedRasterPixels())return reject(tr("Select a bitmap layer first."));
    const auto threshold=parameter(values,"removeThreshold",50,0,100),radius=parameter(values,"removeRadius",3,0,10),smooth=parameter(values,"removeSmooth",20,0,100),feather=parameter(values,"removeFeather",.8,0,10),shift=parameter(values,"removeShift",0,-100,100);
    if(!threshold||!radius||!smooth||!feather||!shift)return reject(tr("Enter edge settings within their ranges."));
    const auto previous=m_selection;const auto restore=qScopeGuard([&]{m_selection=previous;updateSelectionOverlay();});
    const bool subjectOnly=values.value("removeKeep","All foreground subjects").toString()=="Selected subject";
    if(!semanticSelection({{"field-9",*threshold},{"field-10",subjectOnly?"Subject":"Background"},{"field-12",0}},true))return reject(error());
    if(!subjectOnly)for(auto &value:m_selection.alpha)value=255-value;
    auto mask=layerMask();const auto original=*selectedRasterPixels();
    const int shiftSteps=int(std::ceil(std::abs(*shift)* *radius/100));
    for(int step=0;step<shiftSteps;++step) {
        auto copy=mask.alpha;
        for(int y=0;y<mask.extent.height;++y)for(int x=0;x<mask.extent.width;++x) {
            auto value=mask.alpha[std::size_t(y)*mask.extent.width+x];
            for(int yy=std::max(0,y-1);yy<=std::min(mask.extent.height-1,y+1);++yy)for(int xx=std::max(0,x-1);xx<=std::min(mask.extent.width-1,x+1);++xx) {
                const auto neighbor=mask.alpha[std::size_t(yy)*mask.extent.width+xx];value=*shift>0?std::max(value,neighbor):std::min(value,neighbor);
            }
            copy[std::size_t(y)*mask.extent.width+x]=value;
        }
        mask.alpha=std::move(copy);
    }
    const double softness=*feather+*radius * *smooth/100*(values.value("removeHair",true).toBool()?.25:1.);
    if(softness>0){const auto softened=featherMask(mask,softness);if(!softened.ok())return reject(QString::fromStdString(softened.error));mask=softened.mask;}
    const auto cutout=maskRaster(original,mask);if(!cutout.ok())return reject(QString::fromStdString(cutout.error));
    m_backgroundRemovalMask=mask;m_backgroundRemovalTarget=selectedLayerId();m_backgroundRemovalRevision=revision();
    return {{"ready",true},{"before",dataImage(image(original))},{"after",dataImage(image(cutout.pixels))},{"mask",dataImage(image(maskPixels(mask)))},{"provider","Canvas semantic regions"}};
}

bool EditorCanvas::applyBackgroundRemoval(const QVariantMap &values) {
    if(!documentReady()||m_backgroundRemovalMask.alpha.empty()||m_backgroundRemovalTarget!=selectedLayerId()||m_backgroundRemovalRevision!=revision())return fail(tr("Create a new removal preview for the current layer first."));
    if(m_pixelLocks.contains(selectedLayerId()))return fail(tr("Unlock the selected layer first."));
    const auto *source=findLayer(*document(),selectedLayerId().toStdString());if(!source)return false;
    const bool preserve=values.value("removePreserve",true).toBool(),duplicate=preserve||values.value("removeResult","New layer with mask").toString()=="New layer with mask";
    const auto sourceId=selectedLayerId().toStdString(),destination=duplicate?unique("cutout.layer."):sourceId,asset=unique("cutout.mask.");
    const auto mask=maskPixels(m_backgroundRemovalMask);auto copy=*source;
    if(!commit([&](Document &document) {
        auto *layer=findLayer(document,sourceId);if(!layer)return false;
        document.assets.emplace_back(RasterAsset{asset,mask});
        if(duplicate) {
            auto &p=layerProperties(copy);p.id=destination;p.name+=" · Cutout";p.visible=true;
            // Keep the source editable, hidden beneath the derived cutout.
            const auto index=std::find_if(document.layers.begin(),document.layers.end(),[&](const auto &entry){return layerProperties(entry).id==sourceId;})-document.layers.begin();
            if(preserve) {
                layerProperties(*layer).visible=false;
                document.layers.insert(document.layers.begin()+index+1,copy);
            } else document.layers[index]=copy;
            layer=findLayer(document,destination);
        }
        auto &appearance=layerProperties(*layer).appearance;
        appearance.maskStack.push_back({unique("cutout.operand."),"Background removal",LayerMask{.assetId=asset,.featherRadius=0,.outsideCoverage=0},appearance.maskStack.empty()?MaskCombineMode::Add:MaskCombineMode::Intersect});
        appearance.maskStackEnabled=true;return true;
    }))return false;
    m_backgroundRemovalMask={};m_backgroundRemovalTarget.clear();return selectLayer(QString::fromStdString(destination));
}

bool EditorCanvas::captureBackgroundClipboard() {
    const auto *clipboard=QGuiApplication::clipboard();
    m_backgroundClipboard=clipboard?clipboard->image():QImage{};
    if(m_backgroundClipboard.isNull())return fail(tr("The clipboard does not contain an image."));
    clearError();return true;
}

bool EditorCanvas::applyBackgroundReplacement(const QUrl &source,const QVariantMap &values) {
    if(!documentReady())return fail(tr("Open a canvas first."));
    const auto scale=parameter(values,"replaceScale",100,1,10000),angle=parameter(values,"replaceAngle",0,-360,360),x=parameter(values,"replaceX",0,-65536,65536),y=parameter(values,"replaceY",0,-65536,65536),opacity=parameter(values,"replaceOpacity",100,0,100),match=parameter(values,"replaceMatch",84,0,100);
    if(!scale||!angle||!x||!y||!opacity||!match)return fail(tr("Enter replacement transforms within their ranges."));
    const auto blendName=values.value("replaceBlend","Normal").toString();
    if(!QStringList{"Normal","Multiply","Screen","Overlay","Soft Light","Darken","Lighten"}.contains(blendName))return fail(tr("Choose a supported replacement blend mode."));
    QImage input;
    if(source.scheme()=="clipboard")input=m_backgroundClipboard;
    else {
        const auto path=source.isLocalFile()?source.toLocalFile():source.scheme().isEmpty()?source.toString():QString{};
        if(path.isEmpty())return fail(tr("Choose a local image or canvas file."));
        QString message;const auto working=dreamscapes::persistImageCanvas(path,&message,values.value("libraryDirectory").toString());
        if(working.isEmpty())return fail(message);
        const auto canvas=dreamscapes::readImageCanvas(working);if(!canvas.ok())return fail(QString::fromStdString(canvas.result.message));
        const auto frame=renderFrame(canvas.document,0);if(!frame.ok())return fail(QString::fromStdString(frame.message));input=image(frame.pixels);
    }
    if(input.isNull()||std::uint64_t(input.width())*input.height()>MediaLimits{}.maxPixelsPerFrame)return fail(tr("Choose a decodable replacement within the image limit."));
    auto pixels=raster(input);
    if(values.value("replaceLighting",true).toBool()&&selectedRasterPixels()) {
        const auto average=[](const RasterLayer &pixels){std::array<double,3> sum{};double count=0;for(auto p:pixels.pixels)if(qAlpha(p)){sum[0]+=qRed(p);sum[1]+=qGreen(p);sum[2]+=qBlue(p);++count;}for(auto &c:sum)c/=std::max(1.,count);return sum;};
        const auto reference=average(*selectedRasterPixels()),current=average(pixels);const double strength=*match/100;
        for(auto &p:pixels.pixels)p=qRgba(std::clamp(int(std::lround(qRed(p)+(reference[0]-current[0])*strength)),0,255),std::clamp(int(std::lround(qGreen(p)+(reference[1]-current[1])*strength)),0,255),std::clamp(int(std::lround(qBlue(p)+(reference[2]-current[2])*strength)),0,255),qAlpha(p));
    }
    const auto fit=values.value("replaceFit","Fill").toString();double sx=double(editingWidth())/pixels.width,sy=double(editingHeight())/pixels.height;
    if(fit!="Stretch")sx=sy=fit=="Fit"?std::min(sx,sy):std::max(sx,sy);sx*=*scale/100;sy*=*scale/100;
    const double radians=*angle*std::numbers::pi/180,c=std::cos(radians),s=std::sin(radians);
    const bool replace=values.value("replaceResult","New editable layer").toString()=="Replace background layer";
    const auto id=replace?backgroundLayerId():unique("background.replacement."),asset=unique("background.source.");
    if(replace&&m_pixelLocks.contains(QString::fromStdString(id)))return fail(tr("Unlock the background layer first."));
    LayerProperties properties{id,"Background replacement"};if(activeArtboard())properties.artboardId=m_activeArtboard;
    properties.opacity=*opacity/100;properties.blendMode=blendName=="Soft Light"?RasterBlendMode::SoftLight:blendName=="Darken"?RasterBlendMode::Darken:blendName=="Lighten"?RasterBlendMode::Lighten:blend(blendName);
    const auto origin=activeArtboard()?QPointF{}:editingOrigin();
    properties.transform={c*sx,s*sx,-s*sy,c*sy,origin.x()+(editingWidth()-(c*sx*pixels.width-s*sy*pixels.height))/2+*x,origin.y()+(editingHeight()-(s*sx*pixels.width+c*sy*pixels.height))/2+*y};
    if(!commit([&](Document &document){document.assets.emplace_back(RasterAsset{asset,pixels});if(auto *layer=replace?findLayer(document,id):nullptr)*layer=StaticBitmapLayer{properties,StaticSource{asset}};else if(values.value("replacePreserve",true).toBool())document.layers.insert(document.layers.begin(),StaticBitmapLayer{properties,StaticSource{asset}});else document.layers.emplace_back(StaticBitmapLayer{properties,StaticSource{asset}});return true;}))return false;
    return selectLayer(QString::fromStdString(id));
}
