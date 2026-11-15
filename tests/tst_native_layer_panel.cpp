#include "App/Views/Editor/NativeLayerPanel.h"
#include <Document/Document.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        using namespace iiSharedCanvas;
        auto session=native::DocumentSession::create();
        auto panel=editor::LayerPanelModel::create(session);
        check(!panel->hasSelection.get() && !panel->moveOpacity(25),"empty layer panel accepted an edit");
        Document doc; doc.extent={2,2};
        doc.assets.emplace_back(RasterAsset{"red",makeRasterLayer(2,2,0xffff0000)});
        doc.assets.emplace_back(RasterAsset{"blue",makeRasterLayer(2,2,0xff0000ff)});
        doc.layers.emplace_back(StaticBitmapLayer{{"back","Background",true,0.25,{},RasterBlendMode::Multiply},StaticSource{"red"}});
        doc.layers.emplace_back(StaticBitmapLayer{{"front","Subject",true,0.75,{},RasterBlendMode::Screen},StaticSource{"blue"}});
        good(session->attach(doc,"layers.iisc"));
        const auto snapshot=session->snapshot();
        const auto revision=session->revision.get();
        good(session->selectLayer("back"));
        check(panel->hasSelection.get() && panel->selectedName.get()=="Background" &&
              panel->opacity.get()==25 && panel->opacityText.get()=="25%" && panel->blend.get()=="Multiply",
              "inspector did not project the selected layer properties");
        check(!session->dirty.get() && session->revision.get()==revision,"selection modified the document");
        good(panel->showInformation());
        auto info=[&](std::string_view id) {
            for (const auto &row:panel->information.get()) if (row.id==id) return row.value;
            return std::string{};
        };
        check(panel->informationMode.get() && info("dimensions")=="2 × 2 px" &&
              info("profile")=="Unavailable" && info("bitDepth")=="8 bit / channel" &&
              info("source")=="red" && !session->dirty.get() && session->revision.get()==revision,
              "information segment invented metadata or changed the document");
        good(panel->showLayer());
        good(panel->editOpacityText("60"));
        check(session->snapshot()==snapshot && !session->dirty.get(),"an incomplete text edit modified the document");
        good(session->selectLayer("front"));
        check(panel->opacityText.get()=="75%" && panel->blend.get()=="Screen","switching selection did not discard the prior draft");
        good(panel->editOpacityText("invalid"));
        check(!panel->commitOpacity() && panel->opacityText.get()=="75%" && session->snapshot()==snapshot,
              "invalid opacity changed the document or was retained as the committed value");
        good(panel->editOpacityText("62.5%")); good(panel->commitOpacity());
        check(std::abs(layerProperties(session->document()->layers[1]).opacity-0.625)<0.0001 &&
              layerProperties(session->document()->layers[0]).opacity==0.25 && session->dirty.get() &&
              layerProperties(snapshot->layers[1]).opacity==0.75,
              "opacity edit did not update only the selected document layer");
        good(panel->moveOpacity(40));
        check(std::abs(layerProperties(session->document()->layers[1]).opacity-0.4)<0.0001,
              "slider did not edit the selected layer");
        const auto version=session->revision.get();
        check(!panel->moveOpacity(std::numeric_limits<double>::quiet_NaN()) && !panel->moveOpacity(101) &&
              session->revision.get()==version,"invalid slider values changed the document");
        good(panel->chooseBlend("Overlay"));
        check(layerProperties(session->document()->layers[1]).blendMode==RasterBlendMode::Overlay && panel->blend.get()=="Overlay",
              "blend choice did not reach the document");
        check(!panel->chooseBlend("not-a-mode") && session->revision.get()==version+1,
              "unknown blend modified the document");
        check(std::ranges::none_of(panel->blends.get(),[](const auto &entry){return entry.key=="Erase";}) &&
              !panel->chooseBlend("Erase") && !session->setLayerBlend("front",RasterBlendMode::DestinationOut) &&
              session->revision.get()==version+1,"a blend mode forbidden for document layers was offered or applied");
        bool redirected=false;
        auto observe=panel->opacity.observe([&](double value,std::uint64_t){
            if (value==25 && !redirected) {redirected=true; good(session->selectLayer("front"));}
        });
        good(session->selectLayer("back"));
        check(redirected && session->selectedLayer.get()=="front" && panel->selectedName.get()=="Subject" &&
              panel->opacity.get()==40 && panel->blend.get()=="Overlay",
              "reentrant selection left stale layer properties in the panel");
        check(!session->layers.get()[0]->selected.get() && session->layers.get()[1]->selected.get(),
              "reentrant selection left the wrong row selected");
        Document dynamic; dynamic.extent={4,4}; dynamic.timeline={{24,1},2};
        dynamic.assets.emplace_back(RasterAsset{"first",makeRasterLayer(2,2,0xff000000)});
        dynamic.assets.emplace_back(RasterAsset{"second",makeRasterLayer(3,4,0xffffffff)});
        dynamic.layers.emplace_back(DynamicBitmapLayer{{"moving","Animation"},KeyframedSource{{0,1}}});
        dynamic.frames={{0,{{"moving","first"}}},{1,{{"moving","second"}}}};
        good(session->attach(dynamic,"dynamic.iisc")); good(session->selectLayer("moving"));
        good(panel->showInformation());
        const auto currentRevision=session->revision.get();
        check(info("source")=="first" && info("dimensions")=="2 × 2 px", "initial dynamic information was wrong");
        good(session->seek(1));
        check(info("source")=="second" && info("dimensions")=="3 × 4 px" && !session->dirty.get() &&
              session->revision.get()==currentRevision,"frame seek did not refresh source information without editing");
        good(panel->showLayer());
        check(panel->canDuplicate.get(),"selected bitmap layer was not duplicable");
        good(panel->duplicateSelected());
        const auto duplicate=session->selectedLayer.get();
        check(duplicate!="moving" && panel->selectedName.get()=="Animation copy" &&
              session->document()->layers.size()==2 && session->dirty.get(),
              "layer panel duplication did not update the document and selection");
        const auto *originalAsset=resolveAssetAt(*session->document(),*findLayer(*session->document(),"moving"),1);
        const auto *copiedAsset=resolveAssetAt(*session->document(),*findLayer(*session->document(),duplicate),1);
        check(originalAsset && copiedAsset && std::get<RasterAsset>(*originalAsset).id!=std::get<RasterAsset>(*copiedAsset).id,
              "layer panel copy reused the original raster asset");
        good(panel->removeSelected());
        check(session->document()->layers.size()==1 && session->selectedLayer.get()=="moving" &&
              panel->selectedName.get()=="Animation", "layer panel delete did not update selection and document");
        good(panel->removeSelected());
        check(!panel->hasSelection.get() && !panel->removeSelected(),"empty layer panel accepted deletion");
        good(panel->createLayer(LayerKind::DynamicBitmap));
        check(panel->hasSelection.get() && session->document()->layers.size()==1 &&
              keyframedLayerSource(session->document()->layers[0]) &&
              keyframedLayerSource(session->document()->layers[0])->frameIndices==std::vector<FrameIndex>{0},
              "new layer action did not create and select a dynamic layer");
        Document first;first.extent={2,2};
        first.assets.emplace_back(RasterAsset{"a",makeRasterLayer(2,2,0xff000000)});
        first.layers.emplace_back(StaticBitmapLayer{{"same","First",true,.25},StaticSource{"a"}});
        good(session->attach(first,"first.iisc"));good(session->selectLayer("same"));
        auto second=first;layerProperties(second.layers[0]).name="Second";layerProperties(second.layers[0]).opacity=.8;
        bool stale=false;
        auto replacementObserver=panel->opacity.observe([&](double value,std::uint64_t) {if (value==80) stale=true;});
        good(session->attach(second,"second.iisc"));
        check(!stale && !panel->hasSelection.get() && session->selectedLayer.get().empty() && !session->dirty.get(),
              "replacing a document exposed an old layer selection in the layer inspector");
        good(session->selectLayer("same"));
        check(panel->hasSelection.get() && panel->selectedName.get()=="Second" && panel->opacity.get()==80,
              "a new document's explicit layer selection was not projected");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
