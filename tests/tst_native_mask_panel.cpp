#include "App/Views/Editor/NativeMaskPanel.h"
#include "App/Native/NativeCanvasFiles.h"
#include <Document/Document.h>
#include <Render/FrameRenderer.h>
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
        auto session=native::DocumentSession::create();
        auto panel=editor::MaskPanelModel::create(session);panel->setVisible(true);
        const auto minimumSliderWidth=lvui::SliderConfiguration{}.range_inset*2;
        check(panel->contentWidth.get()>=minimumSliderWidth,"initial mask slider width violates LVUI constraints");
        panel->sizeChanged(0,1,16,44,280);
        check(panel->contentWidth.get()>=minimumSliderWidth,"narrow mask panel produced an invalid slider width");
        check(!panel->canAdd.get() && !panel->createMask(),"an empty document allowed adding a mask");
        Document doc;doc.extent={2,2};
        doc.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(2,2,0xffff0000)});
        doc.layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"pixels"}});
        good(session->attach(doc,"mask.iisc"));good(session->selectLayer("paint"));
        check(panel->canAdd.get() && panel->rows.get().empty() && !session->dirty.get(),
              "the mask panel did not reflect the selected document layer");
        const auto before=session->snapshot();const auto baseline=renderFrame(*before,0);
        check(baseline.ok(),"cannot render the unmasked fixture");
        good(panel->createMask());
        check(panel->rows.get().size()==1 && panel->canEdit.get() && session->dirty.get() &&
              layerProperties(before->layers[0]).appearance.maskStack.empty(),
              "creating a mask did not preserve the previous document snapshot");
        const auto first=panel->selectedMask.get();
        const auto &added=layerProperties(session->document()->layers[0]).appearance.maskStack;
        check(added.size()==1 && added[0].id==first && renderFrame(*session->document(),0).pixels.pixels==baseline.pixels.pixels,
              "a new identity mask changed the rendered layer");
        good(panel->setInverted(true));
        auto inverted=renderFrame(*session->document(),0);
        check(inverted.ok() && (inverted.pixels.pixels[0]>>24)==0,"mask inversion did not affect rendered pixels");
        good(panel->setStackEnabled(false));
        check(renderFrame(*session->document(),0).pixels.pixels==baseline.pixels.pixels,
              "bypassing the mask stack did not restore the layer");
        good(panel->setStackEnabled(true));
        good(panel->setCombine(MaskCombineMode::Subtract));
        check(renderFrame(*session->document(),0).pixels.pixels==baseline.pixels.pixels,
              "subtracting an inverted identity mask did not follow stack composition");
        good(panel->setCombine(MaskCombineMode::Add));
        good(panel->setEnabled(false));
        check(renderFrame(*session->document(),0).pixels.pixels==baseline.pixels.pixels,
              "disabling the mask did not restore the layer");
        good(panel->setEnabled(true));good(panel->setDensity(0));
        check(renderFrame(*session->document(),0).pixels.pixels==baseline.pixels.pixels,
              "zero mask density did not restore the layer");
        good(panel->setDensity(.75));good(panel->setFeather(2));
        good(panel->duplicateSelected());
        const auto second=panel->selectedMask.get();
        const auto &copied=layerProperties(session->document()->layers[0]).appearance.maskStack;
        check(second!=first && copied.size()==2 && copied[0].mask.assetId!=copied[1].mask.assetId &&
              copied[1].mask.inverted && copied[1].mask.density==.75 && copied[1].mask.featherRadius==2,
              "duplicating a mask lost settings or reused the original mutable asset");
        const auto beforeNavigation=session->revision.get();
        const auto retiredRow=panel->rows.get().back();
        panel->setVisible(false);panel->setVisible(true);
        retiredRow->toggleCurrent();
        check(panel->selectedMask.get()==second && panel->rows.get().back()!=retiredRow && session->revision.get()==beforeNavigation,
              "mask panel navigation lost selection or modified the document");
        bool reopenedPanel=false;
        auto reopenObserver=panel->canAdd.observe([&](bool value,std::uint64_t) {
            if (!value && !reopenedPanel) {reopenedPanel=true;panel->setVisible(true);}
        });
        panel->setVisible(false);
        check(reopenedPanel && panel->canAdd.get() && panel->canEdit.get() && panel->rows.get().size()==2 &&
              panel->selectedMask.get()==second && session->revision.get()==beforeNavigation,
              "a reentrant panel reopen lost the active mask projection");
        good(panel->setInverted(false));
        const auto &independent=layerProperties(session->document()->layers[0]).appearance.maskStack;
        check(independent[0].mask.inverted && !independent[1].mask.inverted,"editing a copied mask changed its source");
        const auto revision=session->revision.get();
        check(!panel->setDensity(-1) && !panel->setFeather(129) &&
              !panel->setCombine(static_cast<MaskCombineMode>(255)) && session->revision.get()==revision,
              "invalid mask values modified the document");
        good(panel->removeSelected());
        check(panel->rows.get().size()==1 && panel->selectedMask.get()==first,"removing a mask did not select the remaining operand");
        iiFileProvider::native::TemporaryDirectory temporary(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temporary.isValid(),"cannot create isolated mask file fixture");
        const auto saved=native::createCanvasFile(temporary.path()/"mask.iisc",*session->document());
        const auto reopened=native::openCanvasFile(temporary.path()/"mask.iisc");
        check(bool(saved)&&bool(reopened)&&layerProperties(reopened->document.layers[0]).appearance.maskStack.size()==1 &&
              layerProperties(reopened->document.layers[0]).appearance.maskStack[0].mask.inverted,
              "mask settings did not survive .iisc save and reopen");
        auto staleRow=panel->rows.get().front();const auto sameIds=*session->document();
        good(session->attach(sameIds,"same-ids.iisc"));good(session->selectLayer("paint"));
        check(panel->rows.get().size()==1 && panel->rows.get().front()!=staleRow,
              "a replacement document reused an old mask row");
        staleRow->toggleCurrent();
        check(!session->dirty.get() && layerProperties(session->document()->layers[0]).appearance.maskStack[0].mask.enabled,
              "a retired mask row changed the replacement document");
        good(session->attach(doc,"replacement.iisc"));
        check(!panel->canEdit.get() && !panel->setInverted(false) && !session->dirty.get(),
              "stale mask selection modified a replacement document");
        auto legacy=doc;legacy.formatVersion.minor=20;
        legacy.assets.emplace_back(RasterAsset{"legacy-mask",makeRasterLayer(2,2,0x00000000)});
        LayerMask oldMask;oldMask.assetId="legacy-mask";
        layerProperties(legacy.layers[0]).appearance.rasterMask=oldMask;
        good(session->attach(legacy,"legacy-mask.iisc"));good(session->selectLayer("paint"));
        check(panel->legacyMasks.get() && panel->rows.get().empty() && panel->canAdd.get(),
              "the panel did not report an existing legacy mask");
        good(panel->createMask());
        check(layerProperties(session->document()->layers[0]).appearance.rasterMask->assetId=="legacy-mask" &&
              session->document()->formatVersion.minor==CurrentFormatMinor,
              "adding a mask discarded a legacy layer mask");
        good(panel->setStackEnabled(false));
        const auto legacyRendered=renderFrame(*session->document(),0);
        check(legacyRendered.ok() && (legacyRendered.pixels.pixels[0]>>24)==0,
              "bypassing the new mask stack also bypassed the legacy mask");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
