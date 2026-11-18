#include "App/Views/Editor/NativeLightAdjustment.h"
#include "App/Native/NativeDocumentSession.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        Document canvas;canvas.extent={2,1};canvas.timeline.frameCount=2;
        auto pixels=makeRasterLayer(2,1,0x80404040);pixels.pixels[1]=0xff203040;
        canvas.assets.emplace_back(RasterAsset{"shared",pixels});
        canvas.layers.emplace_back(StaticBitmapLayer{{"first","First"},StaticSource{"shared"}});
        canvas.layers.emplace_back(StaticBitmapLayer{{"second","Second"},StaticSource{"shared"}});
        auto session=native::DocumentSession::create();good(session->attach(canvas,"light.iisc"));good(session->selectLayer("first"));
        const auto original=session->snapshot();const auto generation=session->documentGeneration.get(),revision=session->revision.get();
        auto automatic=editor::analyzeLightAdjustment(original,"first",0);
        check(bool(automatic) && std::isfinite(automatic->exposure) && session->snapshot()==original && !session->dirty.get(),
              "automatic analysis failed or changed its input");
        check(!editor::analyzeLightAdjustment(original,"missing",0) &&
              !editor::analyzeLightAdjustment(original,"first",0,[]{return true;}),
              "automatic analysis accepted an invalid target or cancellation");
        auto transparent=std::make_shared<Document>(*original);
        std::get<RasterAsset>(transparent->assets.front()).pixels=makeRasterLayer(2,1,0);
        check(!editor::analyzeLightAdjustment(transparent,"first",0),
              "automatic analysis accepted an image without visible pixels");
        iiGraphicAdjust::DevelopLightOptions options;options.exposure=1;
        auto made=editor::prepareLightAdjustment(original,generation,revision,"first",0,options);
        check(bool(made),"light adjustment could not be prepared");
        check(session->snapshot()==original && !session->dirty.get(),"preview modified the live document");
        const auto *first=std::get_if<StaticBitmapLayer>(findLayer(*made->candidate,"first"));
        const auto *second=std::get_if<StaticBitmapLayer>(findLayer(*made->candidate,"second"));
        const auto *adjusted=first?findRasterAsset(*made->candidate,first->content.assetId):nullptr;
        const auto *shared=findRasterAsset(*made->candidate,"shared");
        check(first && second && adjusted && shared && first->content.assetId!="shared" && second->content.assetId=="shared" &&
              shared->pixels.pixels==pixels.pixels && adjusted->pixels.pixels[0]!=pixels.pixels[0] &&
              (adjusted->pixels.pixels[0]>>24)==0x80,
              "light adjustment changed shared pixels, lost alpha or failed to adjust the target");
        good(session->selectLayer("second"));
        check(!session->commitLightAdjustment(std::move(*made)) && session->snapshot()==original,
              "a preview for another layer was committed");
        good(session->selectLayer("first"));
        auto stale=editor::prepareLightAdjustment(original,generation,revision,"first",0,options);
        good(session->editPublication("title","Another edit"));const auto newer=session->snapshot();
        check(stale && !session->commitLightAdjustment(std::move(*stale)) && session->snapshot()==newer,
              "a stale preview overwrote a later document edit");
        auto current=editor::prepareLightAdjustment(newer,session->documentGeneration.get(),session->revision.get(),"first",0,options);
        check(bool(current),"current light adjustment could not be prepared");
        good(session->commitLightAdjustment(std::move(*current)));
        check(session->snapshot()!=newer && session->dirty.get() && session->selectedLayer.get()=="first",
              "applying light did not commit the selected layer exactly once");
        check(!editor::prepareLightAdjustment(session->snapshot(),session->documentGeneration.get(),session->revision.get(),"missing",0,options),
              "an unavailable layer was accepted");
        options.exposure=std::numeric_limits<double>::quiet_NaN();
        check(!editor::prepareLightAdjustment(session->snapshot(),session->documentGeneration.get(),session->revision.get(),"first",0,options),
              "invalid adjustment values were accepted");
        options.exposure=1;
        check(!editor::prepareLightAdjustment(session->snapshot(),session->documentGeneration.get(),session->revision.get(),"first",0,options,[]{return true;}),
              "cancelled adjustment continued");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
