#include "App/Views/Editor/NativeCanvasPanel.h"
#include "App/Views/Editor/NativeEditorWorkspace.h"
#include <Document/Document.h>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        using namespace iiSharedCanvas;
        auto session=native::DocumentSession::create();
        auto panel=editor::CanvasPanelModel::create(session);
        panel->sizeChanged(0,0,0);
        check(panel->contentWidth.get()>=lvui::SliderConfiguration{}.range_inset*2,
              "a narrow canvas panel produced an invalid LVUI slider width");
        panel->setVisible(true);
        check(!panel->editable.get() && !panel->apply(),"empty canvas panel accepted a resize");
        Document doc;doc.extent={8,6};
        doc.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(8,6,0xff123456)});
        doc.layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"pixels"}});
        good(session->attach(doc,"canvas.iisc"));
        check(panel->editable.get() && panel->widthText.get()=="8" && panel->heightText.get()=="6",
              "canvas panel did not display real document dimensions");
        const auto original=session->snapshot();
        good(panel->editWidth("invalid"));good(panel->editHeight("10"));
        check(!panel->apply() && !session->dirty.get() && session->snapshot()==original,
              "invalid input changed the canvas");
        good(panel->editWidth("12 px"));
        good(session->editPublication("title","Unrelated metadata"));
        check(panel->widthText.get()=="12 px" && panel->heightText.get()=="10",
              "an unrelated metadata edit discarded the canvas input draft");
        good(panel->apply());
        check(session->document()->extent.width==12 && session->document()->extent.height==10 &&
              panel->widthText.get()=="12" && panel->heightText.get()=="10" && session->dirty.get(),
              "canvas panel did not commit the entered dimensions");
        good(panel->editWidth("20"));good(panel->reset());
        check(panel->widthText.get()=="12" && panel->heightText.get()=="10",
              "reset did not restore the current document dimensions");
        const auto revisionBeforeMode=session->revision.get();
        good(panel->showMirror());good(panel->chooseVertical());
        check(panel->mirrorMode.get() && panel->vertical.get() && session->revision.get()==revisionBeforeMode,
              "selecting the mirror mode modified the document");
        good(panel->applyMirror());
        check(layerProperties(session->document()->layers[0]).transform.m22==-1,
              "the canvas panel did not mirror the document");
        good(panel->reset());
        check(panel->horizontal.get() && panel->mirrorMode.get(),"reset did not restore the mirror axis");
        const auto beforeRotate=session->revision.get();
        good(panel->showRotate());good(panel->chooseRight());
        good(panel->editAngle("invalid"));
        check(!panel->applyRotate() && session->revision.get()==beforeRotate,
              "invalid custom rotation changed the document");
        good(panel->editAngle("0"));good(panel->applyRotate());
        check(session->document()->extent.width==10 && session->document()->extent.height==12 &&
              panel->rotateMode.get(),"the canvas panel did not rotate the actual document");
        good(panel->showSize());
        auto dissolve=doc;layerProperties(dissolve.layers[0]).blendMode=RasterBlendMode::Dissolve;
        good(session->attach(dissolve,"dissolve.iisc"));
        check(!panel->editable.get() && !panel->status.get().empty() && !panel->apply() && !session->dirty.get(),
              "an unsupported dissolve canvas did not report its restriction");
        good(session->attach(doc,"second.iisc"));
        check(panel->editable.get() && panel->widthText.get()=="8" && panel->heightText.get()=="6",
              "a document switch retained stale canvas dimensions");
        auto workspace=editor::WorkspaceModel::create(session);workspace->setVisible(true);
        bool redirected=false;
        auto observer=workspace->filePanelActive.observe([&](bool value,std::uint64_t) {
            if (value && !redirected) {redirected=true;workspace->showCanvasPanel();}
        });
        workspace->showFilePanel();
        check(redirected && workspace->activePanel.get()=="canvas" && workspace->canvasPanelActive.get() &&
              !workspace->filePanelActive.get() && !workspace->layerPanelActive.get() && workspace->canvasPanel()->editable.get(),
              "a reentrant tool selection left inconsistent panel state");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
