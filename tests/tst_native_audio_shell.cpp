#include "App/Native/NativeApplicationShell.h"
#include "App/Native/NativeCanvasFiles.h"
#include "NativeAssets.h"
#include <Audio/AudioCodec.h>
#include <iiFileProvider>
#include <iiVisualCodec>
#include <filesystem>
#include <iostream>
#include <stdexcept>
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
        using namespace dreamscapes;using namespace iiSharedCanvas;
        iiFileProvider::native::TemporaryDirectory fixture(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(fixture.isValid(),"cannot create audio fixture");const auto path=fixture.path()/"voice.wav";
        AudioAsset wave{"fixture",48000,1,std::vector<std::int16_t>(48000,12)};
        const auto encoded=encodeAudioWav(wave);check(encoded.ok(),"cannot encode WAV fixture");
        iiFileProvider::native::File::write(path,std::string_view(reinterpret_cast<const char *>(encoded.bytes.data()),encoded.bytes.size()));
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto shell=native::NativeApplicationShell::create(native::queueReadySvg);good(shell->connectFrameRenderer(loop,worker,fixture.path()));
        ii::gui::FileDialogCompletion completion;ii::gui::FileDialogOptions captured;
        shell->setCanvasFilePicker([&](ii::gui::FileDialogOptions options,ii::gui::FileDialogCompletion callback) -> ii::core::Result<void> {
            captured=std::move(options);completion=std::move(callback);return {};
        });
        Document canvas;canvas.extent={4,4};canvas.timeline={{24,1},24};good(shell->documentSession()->attach(canvas,"audio.iisc"));
        good(shell->navigate("editor"));auto editor=shell->editorWorkspace();editor->showAudioPanel();
        check(bool(editor->lightPanel.get()) && editor->lightAvailable.get(),"the application did not connect the light panel");
        check(bool(editor->brushPanel.get()) && editor->brushAvailable.get(),"the application did not connect the brush panel");
        auto panel=editor->audioPanel.get();check(panel && editor->audioPanelActive.get(),"the application did not connect the audio panel");
        auto drain=[&] {for (int count=0;!worker->tasks.empty();++count) {
            if (count>100) throw std::runtime_error("audio worker did not settle");
            auto task=std::move(worker->tasks.front());worker->tasks.erase(worker->tasks.begin());task();(void)loop.poll();
        }};
        drain();good(panel->createTrack());drain();
        check(panel->canChoose.get(),"the application file picker did not reach the audio model");
        good(panel->chooseFile());check(bool(completion) && captured.mode==ii::gui::FileDialogMode::open &&
            !captured.multiple && captured.filters.size()==1 && captured.filters[0].extensions==std::vector<std::string>{"wav"},
            "audio picker options were not passed to the native host");
        good(shell->documentSession()->editPublication("title","Changed while choosing"));
        auto first=std::move(completion);first(ii::gui::FileDialogSelection{false,{path.string()}});drain();
        check(shell->documentSession()->document()->audioTracks.front().clips.empty() &&
              panel->status.get().find("changed")!=std::string::npos,"a stale native picker selection imported audio");
        good(panel->chooseFile());auto second=std::move(completion);second(ii::gui::FileDialogSelection{false,{path.string()}});drain();
        check(shell->documentSession()->document()->audioTracks.front().clips.size()==1,
              "native picker selection did not import into the real editor document");
        std::vector<std::uint8_t> rgba(16,255);auto png=ii::visual::image::encode_png({2,2,ii::visual::image::PixelFormat::rgba8,0,rgba});
        check(bool(png),"cannot encode image fixture");const auto imagePath=fixture.path()/"background.png";
        const std::string original(reinterpret_cast<const char *>(png->data()),png->size());
        iiFileProvider::native::File::write(imagePath,original);
        auto background=editor->backgroundPanel.get();
        const auto sharedDocument=shell->documentSession()->snapshot();
        good(shell->navigate("src/App/Views/Editor/EditorBackgroundColorPanel.lvui"));drain();
        check(background && !background->replaceMode.get() && editor->activePanel.get()=="background-color" &&
              shell->documentSession()->snapshot()==sharedDocument,
              "the direct background color route did not use the live document and panel model");
        good(shell->navigate("src/App/Views/Editor/EditorBackgroundReplacePanel.qml"));drain();
        check(background->replaceMode.get() && editor->activePanel.get()=="background-color" &&
              shell->documentSession()->snapshot()==sharedDocument,
              "the direct background replace route did not use the live document and panel model");
        check(background && background->replaceMode.get() && background->canChooseImage.get(),
              "background image picker was not attached to the application");
        good(background->chooseImage());
        bool supportsCanvas=false;
        for (const auto &filter:captured.filters)
            for (const auto &extension:filter.extensions) if (extension=="iisc") supportsCanvas=true;
        check(bool(completion) && supportsCanvas,
              "background picker did not offer IISC sources to the native host");
        good(shell->documentSession()->editPublication("title","Changed during image selection"));
        auto stale=std::move(completion);stale(ii::gui::FileDialogSelection{false,{imagePath.string()}});drain();
        check(!background->canApplyImage.get() && background->status.get().find("changed")!=std::string::npos,
              "stale background file selection was accepted");
        good(background->chooseImage());auto chosen=std::move(completion);chosen(ii::gui::FileDialogSelection{false,{imagePath.string()}});
        check(background->canApplyImage.get(),"selected background image was not accepted");
        good(background->applyImage());drain();
        const auto *layer=findLayer(*shell->documentSession()->document(),"canvas.background.layer");
        const auto *bitmap=layer?std::get_if<StaticBitmapLayer>(layer):nullptr;
        check(bitmap && bitmap->content.assetId.starts_with("dreamscapes.background.image.") &&
              iiFileProvider::native::File::read(imagePath,1024*1024)==original,
              "background image did not reach the canvas or changed its source file");
        Document sourceCanvas;sourceCanvas.extent={2,1};
        sourceCanvas.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(2,1,0xff536f8c)});
        sourceCanvas.layers.emplace_back(StaticBitmapLayer{{"source","Source"},StaticSource{"pixels"}});
        const auto sourcePath=fixture.path()/"background.iisc";
        const auto sourceFile=native::createCanvasFile(sourcePath,sourceCanvas);
        check(bool(sourceFile),"cannot create IISC picker fixture");
        good(background->chooseImage());auto canvasChoice=std::move(completion);
        canvasChoice(ii::gui::FileDialogSelection{false,{sourcePath.string()}});
        good(background->applyImage());drain();
        layer=findLayer(*shell->documentSession()->document(),"canvas.background.layer");
        bitmap=layer?std::get_if<StaticBitmapLayer>(layer):nullptr;
        const auto *pixels=bitmap?findRasterAsset(*shell->documentSession()->document(),bitmap->content.assetId):nullptr;
        const auto sourceAfter=native::openCanvasFile(sourcePath);
        check(pixels && pixels->pixels.pixels==std::vector<std::uint32_t>({0xff536f8c,0xff536f8c}) &&
              sourceAfter && sourceAfter->version.identity==sourceFile->version.identity,
              "the IISC picker did not apply the source frame or modified the source");
        good(background->applyImage());good(shell->documentSession()->editPublication("title","Changed during image decode"));
        const auto current=shell->documentSession()->snapshot();drain();
        check(shell->documentSession()->snapshot()==current && background->status.get().find("changed")!=std::string::npos,
              "stale background image work overwrote the current document");
        editor->showAudioPanel();
        shell->setCanvasFilePicker([](auto,auto) -> ii::core::Result<void> {throw std::runtime_error("picker failure");});
        check(!panel->chooseFile() && panel->status.get().find("picker")!=std::string::npos && !panel->busy.get(),
              "a throwing native picker did not return a recoverable error");
        good(shell->navigate("publish"));good(shell->publishingWorkspace()->select("x"));drain();
        check(shell->publishingWorkspace()->plannedPosts.get()==1 &&
              std::filesystem::is_regular_file(shell->publishingWorkspace()->plan().posts.front().post.media.front().localFile),
              "the application did not connect current canvas media to publication review");
        shell->disconnectFrameRenderer();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
