#include "App/Views/Editor/NativeElementsPanel.h"
#include "App/Native/NativeCanvasFiles.h"
#include <Document/Document.h>
#include <Render/FrameRenderer.h>
#include <Validation/Validation.h>
#include <iiFileProviderNative.h>
#include <cmath>
#include <filesystem>
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
        auto panel=editor::ElementsPanelModel::create(session);
        panel->setVisible(true);
        check(!panel->editable.get() && !panel->apply(),"empty selection allowed geometry editing");
        Document document;document.extent={120,100};
        VectorPath path;
        path.fill=SolidPaint{0xff00ffff};
        // An orphan move is not a painted path and must not enlarge its bounds.
        path.commands={MoveTo{{110,90}},MoveTo{{10,20}},QuadraticTo{{30,60},{50,20}},LineTo{{10,20}},ClosePath{}};
        document.assets.emplace_back(VectorAsset{"vector",{120,100},{path}});
        document.assets.emplace_back(RasterAsset{"bitmap",makeRasterLayer(2,2,0xff112233)});
        document.layers.emplace_back(StaticVectorLayer{{"element","Shape"},StaticSource{"vector"}});
        document.layers.emplace_back(StaticBitmapLayer{{"pixels","Pixels"},StaticSource{"bitmap"}});
        good(session->attach(document,"geometry.iisc"));
        const auto original=session->snapshot();const auto revision=session->revision.get();
        const auto before=renderFrame(*original,0);
        check(before.ok(),"cannot render the original vector fixture");
        good(session->selectLayer("element"));
        check(panel->editable.get() && std::abs(panel->elementWidth.get()-40)<1e-6 &&
              std::abs(panel->elementHeight.get()-20)<1e-6 && !session->dirty.get() &&
              session->revision.get()==revision,"the panel did not project actual curve bounds");
        good(panel->setConstrained(false));
        good(panel->editWidth("80"));
        check(session->snapshot()==original && !session->dirty.get(),"editing text changed the canvas before committing");
        good(panel->apply());
        const auto after=renderFrame(*session->document(),0);
        check(after.ok() && before.pixels.pixels.at(25*120+60)!=after.pixels.pixels.at(25*120+60),
              "resizing the vector did not change the rendered canvas at the new extent");
        const auto &transform=layerProperties(*findLayer(*session->document(),"element")).transform;
        check(std::abs(transform.m11-2)<1e-6 && std::abs(transform.translationX+10)<1e-6 &&
              std::abs(panel->elementWidth.get()-80)<1e-6 && std::abs(panel->elementHeight.get()-20)<1e-6 &&
              layerProperties(*findLayer(*original,"element")).transform.m11==1 && session->dirty.get(),
              "geometry edit did not resize only the selected element around its original origin");
        const auto editedRevision=session->revision.get();
        good(panel->editWidth("invalid"));
        check(!panel->apply() && session->revision.get()==editedRevision && panel->widthText.get()=="80",
              "invalid dimensions changed the canvas or were retained");
        good(panel->editWidth("10"));good(session->selectLayer("pixels"));
        check(!panel->editable.get() && !panel->apply() && session->revision.get()==editedRevision,
              "a stale element draft modified a different layer");
        good(session->selectLayer("element"));
        good(panel->setConstrained(true));good(panel->editWidth("40"));good(panel->apply());
        check(std::abs(panel->elementWidth.get()-40)<1e-6 && std::abs(panel->elementHeight.get()-10)<1e-6,
              "constrained geometry did not preserve the selected element's proportions");
        iiFileProvider::native::TemporaryDirectory temporary(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temporary.isValid(),"cannot create isolated Elements file fixture");
        const auto saved=native::createCanvasFile(temporary.path()/"elements.iisc",*session->document());
        check(bool(saved),"cannot save the edited vector canvas");
        const auto reopened=native::openCanvasFile(temporary.path()/"elements.iisc");
        check(bool(reopened) &&
              std::abs(layerProperties(*findLayer(reopened->document,"element")).transform.m22-.5)<1e-6,
              "the selected vector transform did not survive .iisc save and reopen");
        const auto last=session->revision.get();
        good(panel->editWidth("nan"));
        check(!panel->apply() && session->revision.get()==last,"non-finite geometry changed the canvas");
        Document curved;curved.extent={100,100};
        VectorPath cubicPath;cubicPath.fill=SolidPaint{0xffffffff};
        cubicPath.commands={MoveTo{{0,0}},CubicTo{{0,60},{40,60},{40,0}},ClosePath{}};
        curved.assets.emplace_back(VectorAsset{"cubic-data",{100,100},{cubicPath}});
        curved.layers.emplace_back(StaticVectorLayer{{"cubic","Cubic"},StaticSource{"cubic-data"}});
        good(session->attach(curved,"curved.iisc"));good(session->selectLayer("cubic"));
        check(panel->editable.get() && std::abs(panel->elementWidth.get()-40)<1e-6 &&
              std::abs(panel->elementHeight.get()-45)<1e-6,"cubic extrema did not determine the element bounds");
        auto singular=layerProperties(session->document()->layers[0]).transform;singular.m22=0;
        good(session->setLayerTransform("cubic",singular));
        check(!panel->editable.get(),"a singular transform was offered as editable geometry");
        panel->setVisible(false);good(session->attach(document,"another.iisc"));good(session->selectLayer("element"));
        check(!panel->editable.get(),"the hidden panel processed selection changes");
        panel->setVisible(true);
        check(panel->editable.get() && panel->elementWidth.get()==40,"the reopened Elements panel did not refresh selection");
        bool redirected=false;
        auto redirect=panel->elementWidth.observe([&](double value,std::uint64_t) {
            if (value==40 && !redirected) {redirected=true;good(session->selectLayer("pixels"));}
        });
        good(session->selectLayer("pixels"));good(session->selectLayer("element"));
        check(redirected && session->selectedLayer.get()=="pixels" && !panel->editable.get() && panel->elementWidth.get()==0,
              "reentrant selection left stale element geometry visible");
        redirect={};good(session->selectLayer("element"));
        auto replacement=document;
        auto &replacementPath=std::get<VectorAsset>(replacement.assets[0]).paths[0];
        auto &curve=std::get<QuadraticTo>(replacementPath.commands[2]);curve.control.x=40;curve.end.x=70;
        bool staleSelectionExposed=false;
        auto replacementObserver=panel->elementWidth.observe([&](double value,std::uint64_t) {
            if (value==60) {staleSelectionExposed=true;good(panel->editWidth("20"));(void)panel->apply();}
        });
        good(session->attach(replacement,"replacement.iisc"));
        check(!staleSelectionExposed && session->selectedLayer.get().empty() && !panel->editable.get() &&
              !session->dirty.get() && layerProperties(session->document()->layers[0]).transform.m11==1,
              "document replacement temporarily exposed a stale layer selection to the Elements panel");

        auto colorSession=native::DocumentSession::create();
        auto colors=editor::ElementsPanelModel::create(colorSession);colors->setVisible(true);
        Document colorDoc;colorDoc.extent={8,8};
        VectorPath first;first.fill=SolidPaint{0x80ff0000};
        first.commands={MoveTo{{0,0}},LineTo{{4,0}},LineTo{{4,4}},LineTo{{0,4}},ClosePath{}};
        auto second=first;second.fill=SolidPaint{0x40ff0000};
        colorDoc.assets.emplace_back(VectorAsset{"shared-shape",{8,8},{first,second}});
        colorDoc.layers.emplace_back(StaticVectorLayer{{"selected","Selected"},StaticSource{"shared-shape"}});
        colorDoc.layers.emplace_back(StaticVectorLayer{{"other","Other",false},StaticSource{"shared-shape"}});
        check(validate(colorDoc).ok(),"mixed-color vector fixture is invalid");
        good(colorSession->attach(colorDoc,"colors.iisc"));good(colorSession->selectLayer("selected"));
        check(colors->fillEditable.get() && colors->fillText.get()=="Mixed",
              "the Elements fill did not reflect mixed source colors");
        const auto originalColor=colorSession->snapshot();const auto colorRevision=colorSession->revision.get();
        const auto originalColorFrame=renderFrame(*originalColor,0);
        const auto colorVisual=colorSession->visualRevision.get();
        good(colors->editFill("#123456"));
        check(colorSession->snapshot()==originalColor && !colorSession->dirty.get(),
              "an uncommitted fill draft modified the document");
        good(colors->applyFill());
        const auto *selectedVector=std::get_if<StaticVectorLayer>(findLayer(*colorSession->document(),"selected"));
        const auto *otherVector=std::get_if<StaticVectorLayer>(findLayer(*colorSession->document(),"other"));
        const auto *changedAsset=findVectorAsset(*colorSession->document(),selectedVector->content.assetId);
        check(selectedVector->content.assetId!="shared-shape" && otherVector->content.assetId=="shared-shape" &&
              changedAsset->paths[0].fill->argb==0x80123456 && changedAsset->paths[1].fill->argb==0x40123456 &&
              findVectorAsset(*originalColor,"shared-shape")->paths[0].fill->argb==0x80ff0000 &&
              colorSession->revision.get()==colorRevision+1 && colorSession->visualRevision.get()==colorVisual+1,
              "changing a shared fill did not preserve other references or per-path alpha");
        const auto changedColorFrame=renderFrame(*colorSession->document(),0);
        check(originalColorFrame.ok() && changedColorFrame.ok() &&
              originalColorFrame.pixels.pixels[9]!=changedColorFrame.pixels.pixels[9],
              "the edited vector fill did not change rendered pixels");
        const auto assetCount=colorSession->document()->assets.size();
        const auto *unsharedDocument=colorSession->document();
        good(colors->editFill("#abcdef80"));good(colors->applyFill());
        selectedVector=std::get_if<StaticVectorLayer>(findLayer(*colorSession->document(),"selected"));
        changedAsset=findVectorAsset(*colorSession->document(),selectedVector->content.assetId);
        check(changedAsset->paths[0].fill->argb==0x80abcdef && changedAsset->paths[1].fill->argb==0x80abcdef &&
              colorSession->document()->assets.size()==assetCount && colorSession->document()==unsharedDocument &&
              colors->fillText.get()=="#ABCDEF80",
              "explicit alpha or repeated fill editing did not preserve a single unshared asset");
        const auto colorFile=native::createCanvasFile(temporary.path()/"colors.iisc",*colorSession->document());
        const auto reopenedColor=native::openCanvasFile(temporary.path()/"colors.iisc");
        const auto *reopenedAsset=reopenedColor?findVectorAsset(reopenedColor->document,selectedVector->content.assetId):nullptr;
        check(bool(colorFile) && reopenedAsset && !reopenedAsset->paths.empty() && reopenedAsset->paths[0].fill &&
              reopenedAsset->paths[0].fill->argb==0x80abcdef,
              "vector fill did not survive .iisc save and reopen");
        const auto noOpRevision=colorSession->revision.get();
        good(colors->editFill("#ABCDEF80"));good(colors->applyFill());
        check(colorSession->revision.get()==noOpRevision,"reapplying the same fill changed the document revision");
        const auto beforeInvalid=colorSession->revision.get();
        good(colors->editFill("#not-a-color"));
        check(!colors->applyFill() && colorSession->revision.get()==beforeInvalid,
              "invalid fill input changed the canvas");
        good(colors->editFill("#ffffff"));good(colorSession->selectLayer("other"));
        const auto beforeStale=colorSession->revision.get();
        (void)colors->applyFill();
        check(colorSession->revision.get()==beforeStale,
              "a fill draft from another layer edited the new selection");
        auto maskFixture=colorDoc;maskFixture.layers.pop_back();
        layerProperties(maskFixture.layers[0]).appearance.vectorMask=LayerMask{.assetId="shared-shape"};
        check(validate(maskFixture).ok(),"shared vector-mask fixture is invalid");
        good(colorSession->attach(maskFixture,"mask-fill.iisc"));good(colorSession->selectLayer("selected"));
        good(colors->editFill("#112233"));good(colors->applyFill());
        const auto *masked=std::get_if<StaticVectorLayer>(findLayer(*colorSession->document(),"selected"));
        check(masked && masked->content.assetId!="shared-shape" &&
              masked->properties.appearance.vectorMask->assetId=="shared-shape" &&
              findVectorAsset(*colorSession->document(),"shared-shape")->paths[0].fill->argb==0x80ff0000,
              "changing a fill altered a vector asset still used by a mask");
        auto noFill=colorDoc;auto &unfilled=std::get<VectorAsset>(noFill.assets[0]).paths[0];
        unfilled.fill.reset();unfilled.stroke=StrokeStyle{};
        check(validate(noFill).ok(),"unfilled vector fixture is invalid");
        good(colorSession->attach(noFill,"no-fill.iisc"));good(colorSession->selectLayer("selected"));
        check(!colors->fillEditable.get() && !colors->applyFill() && !colorSession->dirty.get(),
              "a partially unfilled vector was offered as a solid fill edit");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
