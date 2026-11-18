#include "NativeBackgroundColor.h"
#include <Document/DocumentEditor.h>
#include <Media/MediaIo.h>
#include <Validation/Validation.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
using namespace iiSharedCanvas;
constexpr auto backgroundId="canvas.background.layer";
constexpr auto assetPrefix="dreamscapes.background.fill.";
constexpr auto imageAssetPrefix="dreamscapes.background.image.";
struct ColorStop final {double position;iiColorManager::Rgba color;iiColorManager::Lab lab;};
std::uint32_t argb(iiColorManager::Rgba c,double alpha) {
    const auto byte=[](double value) {return std::uint32_t(std::lround(std::clamp(value,0.0,1.0)*255));};
    return byte(c.a*alpha)<<24 | byte(c.r)<<16 | byte(c.g)<<8 | byte(c.b);
}
}
ii::core::Result<PreparedBackgroundFill> prepareBackgroundFill(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,const BackgroundFillSpec &fill,std::function<bool()> cancelled) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    if (source->canvasMode!=CanvasMode::Finite || !source->artboards.empty())
        return ii::core::fail(ErrorCode::unsupported,"Background fill is unavailable for infinite canvases or artboards");
    const auto width=source->extent.width,height=source->extent.height;
    const MediaLimits limits;
    if (width<=0 || height<=0 || std::uint64_t(width)*std::uint64_t(height)>limits.maxPixelsPerFrame ||
        std::uint64_t(width)*std::uint64_t(height)>limits.maxDecodedBytes/sizeof(std::uint32_t))
        return ii::core::fail(ErrorCode::out_of_range,"The background exceeds the image allocation limit");
    const auto fraction=[](double value){return std::isfinite(value) && value>=0 && value<=1;};
    if (!iiColorManager::validRgba(fill.color) || !fraction(fill.alpha) || !fraction(fill.opacity) ||
        !std::isfinite(fill.angleDegrees) || fill.angleDegrees < -360 || fill.angleDegrees > 360 ||
        (fill.style!=BackgroundFillStyle::Solid && fill.style!=BackgroundFillStyle::Linear && fill.style!=BackgroundFillStyle::Radial))
        return ii::core::fail(ErrorCode::invalid_argument,"Enter valid background colors and values");
    try {
        std::vector<ColorStop> stops;
        if (fill.style!=BackgroundFillStyle::Solid) {
            if (fill.stops.size()<2 || fill.stops.size()>32)
                return ii::core::fail(ErrorCode::invalid_argument,"A gradient needs 2–32 color stops");
            stops.reserve(fill.stops.size());
            for (const auto &stop:fill.stops) {
                if (!fraction(stop.position) || !iiColorManager::validRgba(stop.color))
                    return ii::core::fail(ErrorCode::invalid_argument,"Enter valid gradient stops");
                stops.push_back({stop.position,stop.color,iiColorManager::toLab(stop.color)});
            }
            std::stable_sort(stops.begin(),stops.end(),[](const auto &a,const auto &b){return a.position<b.position;});
        }
        const auto *existing=findLayer(*source,backgroundId);
        const auto *oldBitmap=existing?std::get_if<StaticBitmapLayer>(existing):nullptr;
        const auto *oldVector=existing?std::get_if<StaticVectorLayer>(existing):nullptr;
        if (existing && !oldBitmap && !oldVector)
            return ii::core::fail(ErrorCode::unsupported,"The background layer must be static");
        if (existing && (layerProperties(*existing).artboardId || !layerProperties(*existing).motion.empty()))
            return ii::core::fail(ErrorCode::unsupported,"The background layer has unsupported artboard or motion settings");
        const std::string oldAsset=oldBitmap?oldBitmap->content.assetId:oldVector?oldVector->content.assetId:std::string{};
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
        auto pixels=makeRasterLayer(width,height);
        const double radians=(fill.angleDegrees-90)*std::numbers::pi/180,dx=std::cos(radians),dy=std::sin(radians);
        const auto span=std::max(1.0,std::abs(dx)*width+std::abs(dy)*height);
        for (std::int32_t y=0;y<height;++y) {
          if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
          for (std::int32_t x=0;x<width;++x) {
            auto color=fill.color;
            if (!stops.empty()) {
                const auto position=std::clamp(fill.style==BackgroundFillStyle::Radial?
                    std::hypot((x+.5-width/2.0)/std::max(1.0,width/2.0),(y+.5-height/2.0)/std::max(1.0,height/2.0)):
                    .5+((x+.5-width/2.0)*dx+(y+.5-height/2.0)*dy)/span,0.0,1.0);
                const auto upper=std::upper_bound(stops.begin(),stops.end(),position,[](double value,const auto &stop){return value<stop.position;});
                if (upper==stops.begin()) color=stops.front().color;
                else if (upper==stops.end()) color=stops.back().color;
                else {
                    const auto &a=*(upper-1),&b=*upper;
                    const auto t=std::clamp((position-a.position)/(b.position-a.position),0.0,1.0);
                    if (fill.perceptual) color=iiColorManager::fromLab({std::lerp(a.lab.l,b.lab.l,t),std::lerp(a.lab.a,b.lab.a,t),std::lerp(a.lab.b,b.lab.b,t)},std::lerp(a.color.a,b.color.a,t)).color;
                    else color={std::lerp(a.color.r,b.color.r,t),std::lerp(a.color.g,b.color.g,t),std::lerp(a.color.b,b.color.b,t),std::lerp(a.color.a,b.color.a,t)};
                }
            }
            pixels.pixels[std::size_t(y)*std::size_t(width)+std::size_t(x)]=argb(color,fill.alpha*fill.opacity);
          }
        }
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
        auto candidate=std::make_shared<Document>(*source);
        std::string newAsset;
        for (std::uint64_t index=1;index<std::numeric_limits<std::uint64_t>::max();++index) {
            auto id=std::string(assetPrefix)+std::to_string(index);
            if (!findAsset(*candidate,id) && !findAudioAsset(*candidate,id)) {newAsset=std::move(id);break;}
        }
        if (newAsset.empty()) return ii::core::fail(ErrorCode::out_of_range,"Background asset identity is exhausted");
        DocumentEditor editor(*candidate);
        auto result=editor.insertRasterAsset(newAsset,std::move(pixels));
        if (!result.ok()) return ii::core::fail(ErrorCode::invalid_argument,result.message);
        if (auto *layer=findLayer(*candidate,backgroundId)) {
            auto properties=layerProperties(*layer);
            if (oldAsset.starts_with(imageAssetPrefix)) properties.transform={};
            *layer=StaticBitmapLayer{std::move(properties),StaticSource{newAsset}};
            recordDocumentChange(*candidate);
        } else {
            LayerProperties properties{backgroundId,"Background"};
            result=editor.insertStaticLayer(std::move(properties),LayerRepresentation::Bitmap,newAsset,0);
            if (!result.ok()) return ii::core::fail(ErrorCode::invalid_argument,result.message);
        }
        if ((oldAsset.starts_with(assetPrefix) || oldAsset.starts_with(imageAssetPrefix)) && assetReferences(*candidate,oldAsset).empty()) {
            result=editor.removeAsset(oldAsset);
            if (!result.ok()) return ii::core::fail(ErrorCode::invalid_argument,result.message);
        }
        const auto checked=validate(*candidate);
        if (!checked.ok()) return ii::core::fail(ErrorCode::invalid_argument,checked.issues.front().message);
        return PreparedBackgroundFill{std::move(source),std::move(candidate),generation,revision,!existing};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare background fill",std::current_exception());}
}
ii::core::Result<PreparedBackgroundFill> prepareBackgroundImage(std::shared_ptr<const Document> source,
    std::uint64_t generation,std::uint64_t revision,const RasterLayer &image,BackgroundImageFit fit,std::function<bool()> cancelled) {
    if (!source) return ii::core::fail(ErrorCode::closed,"Open a canvas first");
    if (source->canvasMode!=CanvasMode::Finite || !source->artboards.empty())
        return ii::core::fail(ErrorCode::unsupported,"Background image is unavailable for infinite canvases or artboards");
    const MediaLimits limits;
    if (source->extent.width<=0 || source->extent.height<=0 || image.width<=0 || image.height<=0 ||
        std::uint64_t(image.width)*image.height>limits.maxPixelsPerFrame ||
        std::uint64_t(image.width)*image.height>limits.maxDecodedBytes/sizeof(std::uint32_t) ||
        std::uint64_t(image.width)*image.height!=image.pixels.size())
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid background image or canvas dimensions");
    if (fit!=BackgroundImageFit::Cover && fit!=BackgroundImageFit::Contain && fit!=BackgroundImageFit::Stretch)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid background image fit");
    const auto *existing=findLayer(*source,backgroundId);
    const auto *oldBitmap=existing?std::get_if<StaticBitmapLayer>(existing):nullptr;
    const auto *oldVector=existing?std::get_if<StaticVectorLayer>(existing):nullptr;
    if (existing && ((!oldBitmap && !oldVector) || layerProperties(*existing).artboardId || !layerProperties(*existing).motion.empty()))
        return ii::core::fail(ErrorCode::unsupported,"The background layer has unsupported content or motion");
    const std::string oldAsset=oldBitmap?oldBitmap->content.assetId:oldVector?oldVector->content.assetId:std::string{};
    try {
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
        auto candidate=std::make_shared<Document>(*source);
        if (cancelled && cancelled()) return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
        std::string newAsset;
        for (std::uint64_t index=1;index<std::numeric_limits<std::uint64_t>::max();++index) {
            auto id=std::string(imageAssetPrefix)+std::to_string(index);
            if (!findAsset(*candidate,id) && !findAudioAsset(*candidate,id)) {newAsset=std::move(id);break;}
        }
        if (newAsset.empty()) return ii::core::fail(ErrorCode::out_of_range,"Background asset identity is exhausted");
        DocumentEditor editor(*candidate);auto inserted=editor.insertRasterAsset(newAsset,image);
        if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        LayerProperties properties=existing?layerProperties(*existing):LayerProperties{backgroundId,"Background"};
        double sx=double(source->extent.width)/image.width,sy=double(source->extent.height)/image.height;
        if (fit!=BackgroundImageFit::Stretch) sx=sy=fit==BackgroundImageFit::Cover?std::max(sx,sy):std::min(sx,sy);
        properties.transform={sx,0,0,sy,(source->extent.width-image.width*sx)/2,(source->extent.height-image.height*sy)/2};
        if (auto *layer=findLayer(*candidate,backgroundId)) {
            *layer=StaticBitmapLayer{std::move(properties),StaticSource{newAsset}};recordDocumentChange(*candidate);
        } else {
            inserted=editor.insertStaticLayer(std::move(properties),LayerRepresentation::Bitmap,newAsset,0);
            if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        }
        if ((oldAsset.starts_with(assetPrefix) || oldAsset.starts_with(imageAssetPrefix)) && assetReferences(*candidate,oldAsset).empty()) {
            inserted=editor.removeAsset(oldAsset);if (!inserted.ok()) return ii::core::fail(ErrorCode::invalid_argument,inserted.message);
        }
        const auto checked=validate(*candidate);
        if (!checked.ok()) return ii::core::fail(ErrorCode::invalid_argument,checked.issues.front().message);
        return PreparedBackgroundFill{std::move(source),std::move(candidate),generation,revision,!existing};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare background image",std::current_exception());}
}
}
