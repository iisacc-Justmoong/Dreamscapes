#include "App/Views/Editor/NativeMaskLasso.h"
#include "App/Native/NativeCanvasFiles.h"
#include <Document/Document.h>
#include <Render/FrameRenderer.h>
#include <iiFileProvider>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
 try {
    using namespace dreamscapes;using namespace iiSharedCanvas;
    auto document=native::DocumentSession::create();Document doc;doc.extent={100,50};
    doc.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(100,50,0xffffffff)});
    LayerProperties properties;properties.id="layer";properties.name="Layer";properties.transform.translationX=10;
    doc.layers.emplace_back(StaticBitmapLayer{properties,StaticSource{"pixels"}});
    good(document->attach(doc,"Mask.iisc"));good(document->selectLayer("layer"));
    auto panel=editor::MaskPanelModel::create(document);panel->setVisible(true);
    auto tool=editor::MaskLassoModel::create(document,panel);
    auto image=ii::gui::rgba_image(100,50,std::vector<std::uint8_t>(100*50*4,255));check(bool(image),"frame fixture failed");
    tool->setViewport(200,200);tool->setFrame(*image);tool->setVisible(true);
    const auto send=[&](ii::gui::WidgetInputKind kind,double x,double y,std::uint64_t device=1) {
        ii::gui::WidgetInput event;event.kind=kind;event.device_id=device;event.local_position=ii::gui::WidgetPoint{x,y};
        auto result=tool->input(event);if (!result) throw std::runtime_error(result.error().message);return *result;
    };
    const auto original=document->snapshot();const auto revision=document->revision.get();
    check(send(ii::gui::WidgetInputKind::pointer_down,40,70) && send(ii::gui::WidgetInputKind::pointer_move,120,70) &&
          send(ii::gui::WidgetInputKind::pointer_move,120,110) && send(ii::gui::WidgetInputKind::pointer_move,40,110),
          "lasso path was not accepted");
    check(tool->preview.get().active && document->revision.get()==revision,"lasso preview edited the canvas");
    check(send(ii::gui::WidgetInputKind::pointer_up,40,70),"lasso did not finish");
    const auto *layer=findLayer(*document->document(),"layer");const auto &stack=layerProperties(*layer).appearance.maskStack;
    check(document->revision.get()==revision+1 && stack.size()==1 && panel->selectedMask.get()==stack[0].id &&
          layerProperties(*findLayer(*original,"layer")).appearance.maskStack.empty(),"lasso did not add one independent mask");
    const auto rendered=renderFrame(*document->document(),0);
    check(rendered.ok() && (rendered.pixels.pixels.at(20*100+30)>>24)==255 &&
          (rendered.pixels.pixels.at(15*100+15)>>24)==0,"mask did not follow the selected layer transform");
    const auto firstMask=stack.front().id;const auto secondRevision=document->revision.get();
    check(send(ii::gui::WidgetInputKind::pointer_down,80,80) && send(ii::gui::WidgetInputKind::pointer_move,120,80) &&
          send(ii::gui::WidgetInputKind::pointer_move,120,100) && send(ii::gui::WidgetInputKind::pointer_up,80,100),
          "a second lasso was not accepted");
    const auto &nextStack=layerProperties(*findLayer(*document->document(),"layer")).appearance.maskStack;
    check(document->revision.get()==secondRevision+1 && nextStack.size()==2 && nextStack.front().id==firstMask &&
          nextStack.back().combine==MaskCombineMode::Add && panel->selectedMask.get()==nextStack.back().id,
          "a second lasso replaced the original or lost the Add mode");
    iiFileProvider::native::TemporaryDirectory temporary(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
    check(temporary.isValid(),"cannot create mask fixture");
    auto saved=native::createCanvasFile(temporary.path()/"lasso.iisc",*document->document());
    auto reopened=native::openCanvasFile(temporary.path()/"lasso.iisc");
    check(bool(saved) && bool(reopened) && (renderFrame(reopened->document,0).pixels.pixels.at(15*100+15)>>24)==0,
          "vector mask did not survive saving and reopening IISC");
    const auto committed=document->revision.get();
    check(send(ii::gui::WidgetInputKind::pointer_down,40,70) && send(ii::gui::WidgetInputKind::pointer_move,80,70),"cancel fixture failed");
    tool->setVisible(false);check(!tool->preview.get().active && document->revision.get()==committed,"hiding committed a lasso");
    tool->setVisible(true);check(send(ii::gui::WidgetInputKind::pointer_down,40,70),"second lasso did not start");
    Document replacement;replacement.extent={100,50};good(document->attach(replacement,"Other.iisc"));
    check(!tool->preview.get().active && !send(ii::gui::WidgetInputKind::pointer_up,80,80),"stale lasso survived document replacement");
    auto rotated=native::DocumentSession::create();Document turned;turned.extent={100,100};
    turned.assets.emplace_back(RasterAsset{"rotated-pixels",makeRasterLayer(100,100,0xffffffff)});
    LayerProperties rotatedProperties;rotatedProperties.id="rotated";rotatedProperties.name="Rotated";
    rotatedProperties.transform.m11=0;rotatedProperties.transform.m12=1;rotatedProperties.transform.m21=-1;
    rotatedProperties.transform.m22=0;rotatedProperties.transform.translationX=100;
    turned.layers.emplace_back(StaticBitmapLayer{rotatedProperties,StaticSource{"rotated-pixels"}});
    good(rotated->attach(turned,"Rotated.iisc"));good(rotated->selectLayer("rotated"));
    const auto rotatedMask=rotated->createLassoMask("rotated",{{20,20},{60,20},{60,60},{20,60}});
    const auto rotatedFrame=renderFrame(*rotated->document(),0);
    check(bool(rotatedMask) && rotatedFrame.ok() && (rotatedFrame.pixels.pixels.at(30*100+30)>>24)==255 &&
          (rotatedFrame.pixels.pixels.at(10*100+10)>>24)==0,"lasso did not invert a rotated layer transform");
    auto animated=*rotated->document();layerProperties(*findLayer(animated,"rotated")).motion.push_back({0,{}});
    good(rotated->attach(std::move(animated),"Animated.iisc"));good(rotated->selectLayer("rotated"));
    const auto beforeRejected=rotated->revision.get();
    check(!rotated->createLassoMask("rotated",{{20,20},{60,20},{60,60},{20,60}}) && rotated->revision.get()==beforeRejected,
          "an animated layer accepted a mask in an ambiguous coordinate space");
 } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
