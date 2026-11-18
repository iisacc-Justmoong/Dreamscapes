#include "App/Views/Editor/NativeShapeTool.h"
#include "App/Native/NativeCanvasFiles.h"
#include "NativeAssets.h"
#include <Document/Document.h>
#include <Render/FrameRenderer.h>
#include <Validation/Validation.h>
#include <iiFileProvider>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;using namespace iiSharedCanvas;
        auto catalogDefaults=editor::parseShapeDefaults(native::toolCatalog);
        check(bool(catalogDefaults) && catalogDefaults->fill==0xff8b7cff && catalogDefaults->sides==3 &&
              catalogDefaults->endArrow,"shape defaults did not come from the existing Elements catalog");
        check(!editor::parseShapeDefaults("[]"),"missing shape defaults were silently invented");
        auto session=native::DocumentSession::create();
        Document document;document.extent={100,50};good(session->attach(document,"Shapes.iisc"));
        editor::ShapeDefaults defaults{.fill=0xff123456,.stroke=0xffabcdef,.strokeWidth=4,.radius=0,.sides=3,.star=false};
        auto tool=editor::ShapeToolModel::create(session,defaults);
        auto image=ii::gui::rgba_image(100,50,std::vector<std::uint8_t>(100*50*4,255));
        check(bool(image),"preview fixture failed");
        tool->setViewport(200,200);tool->setFrame(*image);tool->setVisible(true);
        auto send=[&](ii::gui::WidgetInputKind kind,double x,double y,std::uint64_t device=1) {
            ii::gui::WidgetInput input;input.kind=kind;input.device_id=device;input.local_position=ii::gui::WidgetPoint{x,y};
            auto result=tool->input(input);if (!result) throw std::runtime_error(result.error().message);return *result;
        };
        const auto before=session->snapshot();const auto revision=session->revision.get();
        check(send(ii::gui::WidgetInputKind::pointer_down,50,75),"canvas press did not start a shape");
        check(send(ii::gui::WidgetInputKind::pointer_move,150,125) && tool->dragging.get() &&
              !session->dirty.get() && session->revision.get()==revision,"drag preview changed the canvas");
        check(send(ii::gui::WidgetInputKind::pointer_up,150,125),"shape release was not handled");
        check(!tool->dragging.get() && tool->selecting.get() && session->dirty.get() && session->revision.get()==revision+1 &&
              session->document()->layers.size()==1 && before->layers.empty() && validate(*session->document()).ok(),
              "shape release did not atomically add a selected vector layer");
        const auto &layer=std::get<StaticVectorLayer>(session->document()->layers.front());
        const auto &asset=std::get<VectorAsset>(*findAsset(*session->document(),layer.content.assetId));
        const auto start=std::get<MoveTo>(asset.paths.front().commands.front()).point;
        check(std::abs(start.x+layer.properties.transform.translationX-25)<1e-6 &&
              std::abs(start.y+layer.properties.transform.translationY-12.5)<1e-6 &&
              asset.paths.front().fill->argb==defaults.fill && session->selectedLayer.get()==layer.properties.id,
              "shape geometry, fill or selection did not reflect the gesture");
        const auto rendered=renderFrame(*session->document(),0);
        check(rendered.ok() && rendered.pixels.pixels.at(20*100+30)==defaults.fill,
              "the inserted vector did not render its actual fill");
        iiFileProvider::native::TemporaryDirectory temporary(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temporary.isValid(),"cannot create shape file fixture");
        const auto saved=native::createCanvasFile(temporary.path()/"shape.iisc",*session->document());
        const auto reopened=native::openCanvasFile(temporary.path()/"shape.iisc");
        check(bool(saved) && bool(reopened) && reopened->document.layers.size()==1 &&
              renderFrame(reopened->document,0).pixels.pixels.at(20*100+30)==defaults.fill,
              "shape drawing did not survive IISC save and reopen");
        const auto committed=session->revision.get();
        good(tool->choose(editor::ShapeKind::rectangle));
        check(!tool->selecting.get() && tool->rectangle.get(),"choosing a shape did not restore drawing mode");
        check(!send(ii::gui::WidgetInputKind::pointer_down,10,10),"letterbox accepted a canvas gesture");
        check(send(ii::gui::WidgetInputKind::pointer_down,20,70),"second gesture did not start");
        check(!send(ii::gui::WidgetInputKind::pointer_up,100,100,2),"another pointer completed the gesture");
        tool->setVisible(false);
        check(!tool->dragging.get() && session->revision.get()==committed,"hiding the tool committed a gesture");
        tool->setVisible(true);good(tool->choose(editor::ShapeKind::ellipse));
        check(send(ii::gui::WidgetInputKind::pointer_down,20,70),"ellipse gesture did not start");
        Document replacement;replacement.extent={100,50};good(session->attach(replacement,"Other.iisc"));
        check(!tool->dragging.get() && !send(ii::gui::WidgetInputKind::pointer_up,100,100) &&
              session->document()->layers.empty(),"a stale gesture edited a replacement document");
        tool->setFrame(*image);
        for (const auto kind:{editor::ShapeKind::ellipse,editor::ShapeKind::polygon,editor::ShapeKind::line}) {
            good(tool->choose(kind));check(send(ii::gui::WidgetInputKind::pointer_down,40,70),"shape press failed");
            check(send(ii::gui::WidgetInputKind::pointer_up,120,120),"shape release failed");
        }
        check(session->document()->layers.size()==3 && validate(*session->document()).ok() &&
              renderFrame(*session->document(),0).ok(),"ellipse, polygon or line did not produce valid vector content");
        const auto beforeReentrant=session->revision.get();
        good(tool->choose(editor::ShapeKind::rectangle));
        auto hide=tool->dragging.observe([&](bool value,std::uint64_t){if (value) tool->setVisible(false);});
        check(send(ii::gui::WidgetInputKind::pointer_down,40,70) && !tool->dragging.get() && !tool->preview.get().active &&
              session->revision.get()==beforeReentrant,"a reentrant tool close left a stale gesture active");
        auto infinite=native::DocumentSession::create();Document infiniteCanvas;
        infiniteCanvas.extent={256,128};infiniteCanvas.canvasMode=CanvasMode::Infinite;
        infiniteCanvas.infiniteCanvas.origin={-128,-64};good(infinite->attach(infiniteCanvas,"Infinite.iisc"));
        VectorPath negative;negative.fill=SolidPaint{0xff123456};
        negative.commands={MoveTo{{-110,-50}},LineTo{{-70,-50}},LineTo{{-70,-20}},LineTo{{-110,-20}},ClosePath{}};
        auto id=infinite->insertVectorDrawing("Negative",{negative});check(bool(id),"negative-world vector could not be inserted");
        const auto *negativeLayer=std::get_if<StaticVectorLayer>(findLayer(*infinite->document(),*id));
        const auto *negativeAsset=negativeLayer?findVectorAsset(*infinite->document(),negativeLayer->content.assetId):nullptr;
        const auto negativeFrame=renderFrameRegion(*infinite->document(),0,{{-128,-64},{256,128}},{256,128});
        check(negativeAsset && negativeAsset->viewport.width<50 && negativeLayer->properties.transform.translationX<0 &&
              negativeFrame.ok() && negativeFrame.pixels.pixels.at(20*256+25)==0xff123456,
              "negative-world vector did not retain its visible coordinates or compact asset bounds");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
