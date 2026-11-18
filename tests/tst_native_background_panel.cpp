#include "App/Views/Editor/NativeBackgroundPanel.h"
#include "App/Views/Editor/NativeEditorWorkspace.h"
#include "App/Native/NativeCanvasFiles.h"
#include <iiFileProvider>
#include <iiVisualCodec>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    void post(std::function<void()> task) override {tasks.push_back(std::move(task));}
    std::size_t maxConcurrency() const noexcept override {return 1;}
    std::vector<std::function<void()>> tasks;
};
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        using namespace iiSharedCanvas;
        constexpr auto catalog=R"JSON({"color":{"backgroundColor":"#8066FF","backgroundAlpha":100,"backgroundOpacity":100,"backgroundPickerMode":0,"backgroundFill":"Solid","backgroundGradientStops":[{"position":0,"color":"#000000"},{"position":1,"color":"#FFFFFF"}],"backgroundGradientType":"Linear","backgroundInterpolation":"Perceptual","backgroundAngle":90},"presets":[{"name":"Mono","colors":["#000000","#FFFFFF"]}]})JSON";
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto document=native::DocumentSession::create();
        auto model=editor::BackgroundPanelModel::create(document,loop,worker,catalog);
        ii::core::ErrorCode foreignError=ii::core::ErrorCode::failed;
        std::thread foreign([&] {auto result=model->setAlpha(20);if (!result) foreignError=result.error().code;});foreign.join();
        check(foreignError==ii::core::ErrorCode::wrong_executor && model->alpha.get()==100,
              "a foreign thread changed the background draft");
        check(model->modeNames.size()==iiColorManager::modes().size() && model->modeNames.size()==9,
              "the picker mode list did not come from iiColorManager");
        check(model->allChannels.get().size()==13,"the RGB, HSB, Lab and CMYK controls were not projected");
        model->setVisible(true);
        check(!model->canApply.get(),"a background was applicable without a document");
        Document canvas;canvas.extent={6,4};good(document->attach(canvas,"color.iisc"));
        check(model->canApply.get(),"a finite document did not enable the background draft");
        const auto before=document->snapshot();
        good(model->editHex("#123456"));good(model->commitHex());
        check(model->hex.get()=="#123456" && document->snapshot()==before && !document->dirty.get(),
              "picker editing changed the document or failed to update the draft");
        good(model->editHex("#invalid"));
        check(!model->commitHex() && !model->canApply.get() && document->snapshot()==before,
              "an invalid hexadecimal draft remained applicable");
        good(model->editHex("#123456"));good(model->commitHex());
        for (std::size_t index=0;index<model->modeNames.size();++index) {
            good(model->selectMode(static_cast<int>(index)));
            check(model->hex.get()=="#123456" && !model->channels.get().empty(),
                  "switching picker modes changed the draft color or omitted its SDK channels");
        }
        check(!model->selectMode(99) && model->hex.get()=="#123456","an invalid picker mode was accepted");
        good(model->editHex("#12345644"));good(model->commitHex());
        check(model->hex.get()=="#12345644","the color field hid its explicitly entered alpha");
        good(model->editHex("#123456FF"));good(model->commitHex());
        good(model->selectMode(4));
        auto row=model->channels.get().front();good(row->setValue(255));
        check(model->hex.get()=="#FF3456" && document->snapshot()==before,
              "an SDK RGB channel did not update the draft color");
        check(!row->setValue(999),"an out-of-range channel was accepted");
        good(model->selectMode(3));
        check(!row->setValue(0),"a retired color-channel row could still edit the draft");
        good(model->selectMode(4));
        auto lab=std::ranges::find_if(model->allChannels.get(),[](const auto &item){return item->key=="Lightness";});
        check(lab!=model->allChannels.get().end(),"Lab channel was unavailable");
        good((*lab)->setValue(50));
        check(model->hex.get()!="#FF3456" && document->snapshot()==before,"Lab editing did not update only the draft");
        good(model->selectFill(1));good(model->selectStop(0));
        good(model->editHex("#FF0000"));good(model->commitHex());
        good(model->selectStop(1));good(model->editHex("#0000FF"));good(model->commitHex());
        check(model->stopNames.get().size()==2 && document->snapshot()==before,
              "gradient editing changed the document or lost a stop");
        while (!worker->tasks.empty()) {auto task=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());task();(void)loop.poll();}
        good(model->apply());
        check(model->busy.get() && document->snapshot()==before && worker->tasks.size()==1,
              "background application did not start on the worker");
        auto task=std::move(worker->tasks.front());worker->tasks.clear();task();(void)loop.poll();
        check(!model->busy.get() && document->dirty.get() && findLayer(*document->document(),"canvas.background.layer"),
              "the worker result was not committed to the live canvas");
        check(model->recentNames.get().size()==2 && model->recentNames.get()[1]=="#0000FF",
              "the applied color was not added to the session's recent colors");
        good(model->apply());good(document->editPublication("title","Later edit"));
        const auto changed=document->snapshot();task=std::move(worker->tasks.front());worker->tasks.clear();task();(void)loop.poll();
        check(document->snapshot()==changed && model->status.get().find("changed")!=std::string::npos,
              "a late background result overwrote another edit");
        auto workspace=editor::WorkspaceModel::create(document);workspace->setVisible(true);
        workspace->setBackgroundPanel(model);workspace->showMaskPanel();
        check(workspace->maskPanel()->colorAvailable.get(),"background color navigation did not become available");
        workspace->maskPanel()->showColor();
        check(workspace->activePanel.get()=="background-color" && workspace->backgroundPanelActive.get() &&
              !workspace->maskPanelActive.get() && model->canApply.get(),"mask to color navigation lost the live panel model");
        model->showReplace();check(model->replaceMode.get(),"replace view did not open");
        iiFileProvider::native::TemporaryDirectory fixture(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(fixture.isValid(),"cannot create background image fixture");const auto path=fixture.path()/"background.png";
        std::vector<std::uint8_t> rgba(2*2*4,255);auto png=ii::visual::image::encode_png({2,2,ii::visual::image::PixelFormat::rgba8,0,rgba});
        check(bool(png),"cannot encode background image fixture");
        iiFileProvider::native::File::write(path,std::string_view(reinterpret_cast<const char *>(png->data()),png->size()));
        good(model->setImagePath(path));good(model->selectImageFit(1));
        const auto beforeImage=document->snapshot();good(model->applyImage());
        check(model->busy.get() && document->snapshot()==beforeImage,"background image read ran on the view thread");
        while (!worker->tasks.empty()) {auto queued=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());queued();(void)loop.poll();}
        const auto *background=findLayer(*document->document(),"canvas.background.layer");
        const auto *bitmap=background?std::get_if<StaticBitmapLayer>(background):nullptr;
        check(bitmap && bitmap->content.assetId.starts_with("dreamscapes.background.image.") &&
              document->snapshot()!=beforeImage,"background image did not update the document");
        const auto saved=native::createCanvasFile(fixture.path()/"replacement.iisc",*document->document());
        check(bool(saved),"cannot save background image canvas");
        const auto reopened=native::openCanvasFile(fixture.path()/"replacement.iisc");
        const auto *savedLayer=reopened?findLayer(reopened->document,"canvas.background.layer"):nullptr;
        const auto *savedBitmap=savedLayer?std::get_if<StaticBitmapLayer>(savedLayer):nullptr;
        const auto *savedImage=savedBitmap?findRasterAsset(reopened->document,savedBitmap->content.assetId):nullptr;
        check(savedImage && savedImage->pixels.width==2 && savedBitmap->properties.transform.m11==2 &&
              savedBitmap->properties.transform.translationX==1,
              "background source and fit did not survive IISC save and reopen");
        Document sourceCanvas;sourceCanvas.extent={2,1};sourceCanvas.timeline.frameCount=2;
        sourceCanvas.assets.emplace_back(RasterAsset{"source.pixels",makeRasterLayer(2,1,0xffc71234)});
        sourceCanvas.assets.emplace_back(RasterAsset{"source.next",makeRasterLayer(2,1,0xff3456cd)});
        sourceCanvas.layers.emplace_back(DynamicBitmapLayer{{"source.layer","Source"},KeyframedSource{{0,1}}});
        sourceCanvas.frames={{0,{{"source.layer","source.pixels"}}},{1,{{"source.layer","source.next"}}}};
        const auto sourcePath=fixture.path()/"source.iisc";
        auto stored=native::createCanvasFile(sourcePath,sourceCanvas);
        check(bool(stored),"cannot create the IISC background source");
        const auto identity=stored->version.identity;
        good(model->setImagePath(sourcePath));
        const auto beforeCanvas=document->snapshot();good(model->applyImage());
        check(model->busy.get() && document->snapshot()==beforeCanvas,
              "IISC background rendering did not run on the worker");
        while (!worker->tasks.empty()) {auto queued=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());queued();(void)loop.poll();}
        background=findLayer(*document->document(),"canvas.background.layer");
        bitmap=background?std::get_if<StaticBitmapLayer>(background):nullptr;
        const auto *canvasImage=bitmap?findRasterAsset(*document->document(),bitmap->content.assetId):nullptr;
        const auto originalCanvas=native::openCanvasFile(sourcePath);
        check(canvasImage && canvasImage->pixels.width==2 && canvasImage->pixels.height==1 &&
              canvasImage->pixels.pixels==std::vector<std::uint32_t>({0xffc71234,0xffc71234}) &&
              originalCanvas && originalCanvas->version.identity==identity && originalCanvas->document.layers.size()==1,
              "IISC frame zero was not applied or the source file changed");
        Document huge;huge.extent={8193,8193};const auto hugePath=fixture.path()/"huge.iisc";
        check(bool(native::createCanvasFile(hugePath,huge)),"cannot create the oversized IISC fixture");
        good(model->setImagePath(hugePath));const auto beforeHuge=document->snapshot();good(model->applyImage());
        while (!worker->tasks.empty()) {auto queued=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());queued();(void)loop.poll();}
        check(document->snapshot()==beforeHuge && model->status.get().find("limit")!=std::string::npos,
              "an oversized IISC source was rendered or changed the destination");
        check(model->recentNames.get().size()==2,"image replacement incorrectly added a recent color");
        model->showColor();
        model->showMask();
        check(workspace->activePanel.get()=="mask" && workspace->maskPanelActive.get() &&
              workspace->backgroundPanelActive.get(),"color to mask navigation lost the background toolbar state");
        workspace->setBackgroundPanel({});
        check(!workspace->maskPanel()->colorAvailable.get(),"a disconnected background provider remained enabled");
        model->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
