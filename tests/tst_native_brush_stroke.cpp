#include "App/Views/Editor/NativeBrushStroke.h"
#include "App/Native/NativeDocumentSession.h"
#include <Validation/Validation.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool condition,const char *message) {if (!condition) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
template<class T> T take(ii::core::Result<T> value) {if (!value) throw std::runtime_error(value.error().message);return std::move(*value);}

int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        Document canvas;canvas.extent={16,16};canvas.timeline.frameCount=4;
        canvas.assets.emplace_back(RasterAsset{"shared",makeRasterLayer(16,16,0x00000000)});
        canvas.assets.emplace_back(RasterAsset{"later",makeRasterLayer(16,16,0xff112233)});
        canvas.layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"shared"}});
        canvas.layers.emplace_back(StaticBitmapLayer{{"other","Other"},StaticSource{"shared"}});
        canvas.layers.emplace_back(DynamicBitmapLayer{{"dynamic","Dynamic"},KeyframedSource{{0,3}}});
        canvas.frames={{0,{{"dynamic","shared"}}},{3,{{"dynamic","later"}}}};
        check(validate(canvas).ok(),"invalid brush fixture");
        auto session=native::DocumentSession::create();good(session->attach(canvas,"paint.iisc"));good(session->selectLayer("paint"));
        const auto source=session->snapshot();const auto generation=session->documentGeneration.get(),revision=session->revision.get();
        BitmapBrush brush;brush.argb=0xffff0000;brush.size=4;brush.opacity=1;brush.flow=1;brush.hardness=1;
        auto stroke=take(editor::BrushStroke::create(source,generation,revision,"paint",0,brush));
        good(stroke->begin({.position={4,4},.pressure=1,.time=1}));
        good(stroke->move({.position={8,4},.pressure=1,.time=2}));
        const auto *previewLayer=findStaticBitmapLayer(stroke->document(),"paint");
        const auto *preview=previewLayer?findRasterAsset(stroke->document(),previewLayer->content.assetId):nullptr;
        check(preview && preview->pixels.pixels[4*16+4]!=0 && session->snapshot()==source && !session->dirty.get(),
              "brush preview did not paint privately");
        auto made=take(stroke->finish({.position={10,4},.pressure=1,.time=3}));
        const auto *other=findStaticBitmapLayer(*made.candidate,"other");
        check(other && other->content.assetId=="shared" && findRasterAsset(*made.candidate,"shared")->pixels.pixels==
              findRasterAsset(*source,"shared")->pixels.pixels,"brush changed a shared source asset");
        good(session->commitBrushStroke(std::move(made)));
        check(session->snapshot()!=source && session->dirty.get(),"completed stroke did not commit");
        auto staleSource=session->snapshot();
        auto stale=take(editor::BrushStroke::create(staleSource,session->documentGeneration.get(),session->revision.get(),"paint",0,brush));
        good(stale->begin({.position={4,8},.pressure=1,.time=1}));
        auto old=take(stale->finish({.position={8,8},.pressure=1,.time=2}));
        good(session->selectLayer("other"));
        check(!session->commitBrushStroke(std::move(old)) && session->snapshot()==staleSource,
              "stale stroke was committed to another selection");
        good(session->selectLayer("dynamic"));good(session->seek(1));
        auto animatedSource=session->snapshot();
        auto dynamic=take(editor::BrushStroke::create(animatedSource,session->documentGeneration.get(),session->revision.get(),"dynamic",1,brush));
        good(dynamic->begin({.position={5,5},.pressure=1,.time=1}));
        auto animated=take(dynamic->finish({.position={7,5},.pressure=1,.time=2}));
        const auto *layer=findLayer(*animated.candidate,"dynamic");
        const auto *zero=layer?resolveAssetAt(*animated.candidate,*layer,0):nullptr;
        const auto *one=layer?resolveAssetAt(*animated.candidate,*layer,1):nullptr;
        const auto *two=layer?resolveAssetAt(*animated.candidate,*layer,2):nullptr;
        const auto *three=layer?resolveAssetAt(*animated.candidate,*layer,3):nullptr;
        check(zero && one && two && three && std::get<RasterAsset>(*zero).id=="shared" &&
              std::get<RasterAsset>(*one).id==std::get<RasterAsset>(*two).id &&
              std::get<RasterAsset>(*one).id!="shared" && std::get<RasterAsset>(*three).id=="later",
              "dynamic painting changed prior or later keys");
        good(session->commitBrushStroke(std::move(animated)));
        auto cancelledSource=session->snapshot();
        auto cancelled=take(editor::BrushStroke::create(cancelledSource,session->documentGeneration.get(),session->revision.get(),"dynamic",1,brush));
        good(cancelled->begin({.position={2,2},.pressure=1,.time=1}));cancelled->cancel();
        check(!cancelled->finish({.position={3,3},.pressure=1,.time=2}) && session->snapshot()==cancelledSource,
              "cancelled brush stroke changed the document");
        auto badInput=take(editor::BrushStroke::create(cancelledSource,session->documentGeneration.get(),session->revision.get(),"dynamic",1,brush));
        good(badInput->begin({.position={2,2},.pressure=1,.time=1}));
        check(!badInput->move({.position={std::numeric_limits<double>::quiet_NaN(),3},.pressure=1,.time=2}) &&
              !badInput->finish({.position={3,3},.pressure=1,.time=3}) && session->snapshot()==cancelledSource,
              "invalid brush input did not cancel its private gesture");
        auto hidden=std::make_shared<Document>(*cancelledSource);
        hidden->artboards.push_back({"board","Hidden",{{0,0},{16,16}},0xffffffff,false});
        layerProperties(*findLayer(*hidden,"dynamic")).artboardId="board";
        check(validate(*hidden).ok() && !editor::BrushStroke::create(hidden,session->documentGeneration.get(),session->revision.get(),"dynamic",1,brush),
              "brush accepted a layer in a hidden artboard");
        brush.opacity=0;
        auto empty=take(editor::BrushStroke::create(cancelledSource,session->documentGeneration.get(),session->revision.get(),"dynamic",1,brush));
        good(empty->begin({.position={2,2},.pressure=1,.time=1}));
        check(!empty->finish({.position={3,3},.pressure=1,.time=2}) && session->snapshot()==cancelledSource,
              "an unchanged stroke created a document edit");
        brush.opacity=1;
        check(!editor::BrushStroke::create(cancelledSource,session->documentGeneration.get(),session->revision.get(),"missing",1,brush),
              "brush accepted an unknown layer");
        brush.size=std::numeric_limits<double>::quiet_NaN();
        check(!editor::BrushStroke::create(cancelledSource,session->documentGeneration.get(),session->revision.get(),"dynamic",1,brush),
              "brush accepted invalid settings");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
