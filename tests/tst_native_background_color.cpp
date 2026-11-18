#include "App/Views/Editor/NativeBackgroundColor.h"
#include "App/Native/NativeDocumentSession.h"
#include <Document/Document.h>
#include <Render/FrameRenderer.h>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}

int main() {
    try {
        using namespace dreamscapes;
        using namespace iiSharedCanvas;
        auto session=native::DocumentSession::create();
        Document document;document.extent={4,2};
        document.assets.emplace_back(RasterAsset{"shared",makeRasterLayer(4,2,0xffabcdef)});
        LayerProperties background{"canvas.background.layer","Existing background"};background.opacity=.75;
        document.layers.emplace_back(StaticBitmapLayer{background,StaticSource{"shared"}});
        document.layers.emplace_back(StaticBitmapLayer{{"artwork","Artwork"},StaticSource{"shared"}});
        good(session->attach(document,"background.iisc"));good(session->selectLayer("artwork"));
        const auto original=session->snapshot();
        auto imageCandidate=editor::prepareBackgroundImage(original,session->documentGeneration.get(),session->revision.get(),
            makeRasterLayer(2,2,0xff112233),editor::BackgroundImageFit::Cover);
        const auto *unchangedArtwork=imageCandidate?std::get_if<StaticBitmapLayer>(findLayer(*imageCandidate->candidate,"artwork")):nullptr;
        check(imageCandidate && unchangedArtwork && unchangedArtwork->content.assetId=="shared" &&
              findRasterAsset(*imageCandidate->candidate,"shared") && session->snapshot()==original,
              "background image preparation changed another layer or its shared asset");
        editor::BackgroundFillSpec fill;
        fill.color={1,0,0,1};fill.alpha=.5;fill.opacity=.5;
        auto prepared=editor::prepareBackgroundFill(original,session->documentGeneration.get(),session->revision.get(),fill);
        check(bool(prepared) && session->snapshot()==original && !session->dirty.get(),"preparing a fill modified the document");
        good(session->commitBackgroundFill(std::move(*prepared)));
        const auto *layer=findLayer(*session->document(),"canvas.background.layer");
        const auto *back=layer?std::get_if<StaticBitmapLayer>(layer):nullptr;
        const auto *asset=back?findRasterAsset(*session->document(),back->content.assetId):nullptr;
        const auto *art=std::get_if<StaticBitmapLayer>(findLayer(*session->document(),"artwork"));
        check(back && asset && asset->pixels.pixels.size()==8 && asset->pixels.pixels[0]==0x40ff0000 &&
              back->properties.name=="Existing background" && back->properties.opacity==.75 &&
              art && art->content.assetId=="shared" && findRasterAsset(*session->document(),"shared") &&
              session->selectedLayer.get()=="artwork" && session->dirty.get(),
              "background fill did not preserve layer properties, selection and shared artwork");
        check(std::get<StaticBitmapLayer>(original->layers[0]).content.assetId=="shared",
              "an immutable rendering snapshot was mutated");

        fill.style=editor::BackgroundFillStyle::Linear;
        fill.alpha=1;fill.opacity=1;fill.perceptual=false;fill.angleDegrees=90;
        fill.stops={{0,{1,0,0,1}},{1,{0,0,1,1}}};
        auto gradient=editor::prepareBackgroundFill(session->snapshot(),session->documentGeneration.get(),session->revision.get(),fill);
        check(bool(gradient),"valid gradient was rejected");
        good(session->commitBackgroundFill(std::move(*gradient)));
        back=std::get_if<StaticBitmapLayer>(findLayer(*session->document(),"canvas.background.layer"));
        asset=back?findRasterAsset(*session->document(),back->content.assetId):nullptr;
        check(asset && asset->pixels.pixels.front()!=asset->pixels.pixels[3],"linear fill produced a uniform background");

        auto stale=editor::prepareBackgroundFill(session->snapshot(),session->documentGeneration.get(),session->revision.get(),fill);
        check(bool(stale),"stale-fill fixture failed");good(session->editPublication("title","Changed while rendering"));
        const auto changed=session->snapshot();
        check(!session->commitBackgroundFill(std::move(*stale)) && session->snapshot()==changed,
              "a stale background replaced a newer document edit");
        fill.stops={{0,{1,0,0,1}}};
        check(!editor::prepareBackgroundFill(session->snapshot(),session->documentGeneration.get(),session->revision.get(),fill),
              "a one-stop gradient was accepted");
        fill.style=editor::BackgroundFillStyle::Solid;
        check(!editor::prepareBackgroundFill(session->snapshot(),session->documentGeneration.get(),session->revision.get(),fill,[]{return true;}),
              "a cancelled background render continued");
        auto infinite=document;infinite.canvasMode=CanvasMode::Infinite;
        check(!editor::prepareBackgroundFill(std::make_shared<const Document>(infinite),1,1,fill),
              "an infinite canvas was accepted for finite background replacement");
        Document empty;empty.extent={3,2};
        auto emptySession=native::DocumentSession::create();good(emptySession->attach(empty,"empty.iisc"));
        fill.style=editor::BackgroundFillStyle::Solid;fill.color={0,1,0,1};fill.alpha=fill.opacity=1;
        auto inserted=editor::prepareBackgroundFill(emptySession->snapshot(),emptySession->documentGeneration.get(),emptySession->revision.get(),fill);
        check(bool(inserted),"empty finite canvas rejected a solid fill");good(emptySession->commitBackgroundFill(std::move(*inserted)));
        check(emptySession->document()->layers.size()==1 && emptySession->layers.get().size()==1 &&
              layerProperties(emptySession->document()->layers[0]).id=="canvas.background.layer",
              "a missing background was not inserted at the bottom and projected as a layer");
        Document destination;destination.extent={4,2};auto imageSession=native::DocumentSession::create();
        good(imageSession->attach(destination,"image-background.iisc"));
        auto originalImage=makeRasterLayer(2,2,0xff123456);originalImage.pixels[0]=0x80123456;
        const auto untouched=imageSession->snapshot();
        auto fitted=editor::prepareBackgroundImage(untouched,imageSession->documentGeneration.get(),imageSession->revision.get(),
                                                    originalImage,editor::BackgroundImageFit::Contain);
        check(bool(fitted) && imageSession->snapshot()==untouched && !imageSession->dirty.get(),"image preparation changed the live document");
        good(imageSession->commitBackgroundFill(std::move(*fitted)));
        const auto *imageLayer=std::get_if<StaticBitmapLayer>(findLayer(*imageSession->document(),"canvas.background.layer"));
        const auto *imageAsset=imageLayer?findRasterAsset(*imageSession->document(),imageLayer->content.assetId):nullptr;
        const auto composed=renderFrame(*imageSession->document(),0);
        check(imageAsset && imageAsset->pixels.width==2 && imageAsset->pixels.height==2 && composed.ok() &&
              composed.pixels.pixels[0]==0 && composed.pixels.pixels[1]==0x80123456 &&
              originalImage.pixels[0]==0x80123456,"image fit lost its alpha, margins or source pixels");
        check(!editor::prepareBackgroundImage(imageSession->snapshot(),imageSession->documentGeneration.get(),imageSession->revision.get(),
                                               originalImage,editor::BackgroundImageFit::Cover,[]{return true;}),
              "cancelled background image preparation continued");
        const auto imageSnapshot=imageSession->snapshot();
        auto recolor=editor::prepareBackgroundFill(imageSnapshot,imageSession->documentGeneration.get(),imageSession->revision.get(),fill);
        check(bool(recolor),"color fill after image background failed");good(imageSession->commitBackgroundFill(std::move(*recolor)));
        const auto *recolored=findLayer(*imageSession->document(),"canvas.background.layer");
        check(recolored && layerProperties(*recolored).transform.m11==1 && layerProperties(*recolored).transform.m22==1 &&
              layerProperties(*recolored).transform.translationX==0 &&
              findRasterAsset(*imageSnapshot,imageLayer->content.assetId),
              "color fill retained an image-fit transform or modified an immutable snapshot");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
