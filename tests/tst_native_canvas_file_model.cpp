#include "App/Native/NativeCanvasFileModel.h"
#include "App/Native/NativeApplicationShell.h"
#include "App/Views/Editor/NativeLayerPanel.h"
#include "App/Views/Editor/NativeFilePanel.h"
#include "NativeAssets.h"
#include <File/DocumentFile.h>
#include <iiFileProviderNative.h>
#include <iiVisualCodec/ImageCodec.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> result) {if (!result) throw std::runtime_error(result.error().message);}
template<class T> T take(ii::core::Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
int main() {
    try {
        using namespace dreamscapes::native;
        using namespace iiSharedCanvas;
        iiFileProvider::native::TemporaryDirectory temporary(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temporary.isValid(),"cannot create isolated file fixture");
        const auto path=temporary.path()/"input.iisc";
        Document fixture; fixture.extent={8,6}; fixture.timeline={{24,1},24};
        fixture.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(8,6,0xff123456)});
        fixture.layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"pixels"}});
        layerProperties(fixture.layers[0]).motion={{4,{}},{8,{}}};
        take(createCanvasFile(path,fixture));
        auto session=DocumentSession::create();
        ii::runtime::EventLoop loop;
        std::vector<ii::runtime::Task> jobs;
        auto model=CanvasFileModel::create(session,loop,[&](ii::runtime::Task task){jobs.push_back(std::move(task)); return true;});
        bool coherentCompletion=false;
        auto busyObserver=model->busy.observe([&](bool value,std::uint64_t) {
            if (!value && !coherentCompletion) {
                coherentCompletion=session->hasDocument.get() && model->path.get()==path.string();
            }
        });
        auto run=[&] {check(jobs.size()==1,"expected one worker operation"); auto task=std::move(jobs.front()); jobs.clear(); task(); (void)loop.poll();};
        good(model->open(path));
        check(model->busy.get() && !session->hasDocument.get(),"file open ran on the view thread");
        run();
        check(coherentCompletion && session->hasDocument.get() && !session->dirty.get() && model->path.get()==path.string(),
              "opened file did not reach the editor session");
        const auto schema=iiFileProvider::native::File::read(
            std::filesystem::path(DREAMSCAPES_TEST_RESOURCES)/"viewmodels/file.json",1024*1024);
        auto filePanel=dreamscapes::editor::FilePanelModel::create(session,schema);
        filePanel->setFileModel(model); filePanel->setVisible(true); good(filePanel->select("storage"));
        bool pathShown=false;
        for (const auto &row:filePanel->rows.get()) if (row->key=="EditorCanvas.filePath" && row->value.get()==path.string()) pathShown=true;
        check(pathShown,"file inspector did not show the active real .iisc path");
        auto savedVersion=model->version();
        bool revisionShown=false;
        for (const auto &row:filePanel->rows.get()) if (row->key=="DocumentFile.revision" && savedVersion &&
            row->value.get()==std::to_string(savedVersion->revision)) revisionShown=true;
        good(filePanel->select("information"));
        bool formatShown=false,sizeShown=false;
        for (const auto &row:filePanel->rows.get()) {
            if (row->key=="Derived.storageFormat" && row->value.get()=="SQLite working file") formatShown=true;
            if (row->key=="Alias.size" && row->value.get()==std::to_string(std::filesystem::file_size(path))+" bytes") sizeShown=true;
        }
        check(savedVersion && revisionShown && formatShown && sizeShown,
              "file inspector did not use the verified file version, format and size");
        good(session->selectLayer("paint"));
        auto layers=dreamscapes::editor::LayerPanelModel::create(session);
        good(layers->moveOpacity(65)); good(session->setLayerRange("paint",3,18)); good(model->save()); run();
        const auto savedLayer=layerProperties(take(openCanvasFile(path)).document.layers[0]);
        check(!session->dirty.get() && savedLayer.opacity==0.65 && savedLayer.frameRange &&
              savedLayer.frameRange->firstFrame==3 && savedLayer.frameRange->lastFrame==17,
              "layer opacity and range did not persist through the real .iisc save path");
        good(layers->duplicateSelected());
        const auto copiedId=session->selectedLayer.get();
        good(model->save()); run();
        const auto reopened=take(openCanvasFile(path)).document;
        const auto *source=findLayer(reopened,"paint"),*copy=findLayer(reopened,copiedId);
        check(!session->dirty.get() && source && copy &&
              staticLayerSource(*source)->assetId!=staticLayerSource(*copy)->assetId &&
              layerProperties(*copy).frameRange==layerProperties(*source).frameRange,
              "independent layer duplication did not survive the real .iisc save and reopen path");
        good(session->resizeCanvasCentered(10,8));good(model->save());run();
        const auto resized=take(openCanvasFile(path)).document;
        check(!session->dirty.get() && resized.extent.width==10 && resized.extent.height==8 &&
              layerProperties(*findLayer(resized,"paint")).transform.translationX==1 &&
              layerProperties(*findLayer(resized,"paint")).transform.translationY==1,
              "centered canvas size did not survive the actual file save and reopen path");
        good(session->mirrorCanvas(true));good(model->save());run();
        const auto reflected=take(openCanvasFile(path)).document;
        check(layerProperties(*findLayer(reflected,"paint")).transform.m11==-1 &&
              layerProperties(*findLayer(reflected,"paint")).transform.translationX==9,
              "canvas mirror did not survive the actual file save and reopen path");
        good(session->rotateCanvas(90));good(model->save());run();
        const auto rotated=take(openCanvasFile(path)).document;
        check(rotated.extent.width==8 && rotated.extent.height==10 &&
              layerProperties(*findLayer(rotated,"paint")).transform.m12==-1,
              "canvas rotation did not survive the actual file save and reopen path");
        good(session->moveLayerKey("paint",LayerKeyKind::motion,8,10));good(model->save());run();
        const auto movedKey=take(openCanvasFile(path)).document;
        check(layerProperties(*findLayer(movedKey,"paint")).motion[1].frame==10 &&
              layerProperties(*findLayer(movedKey,copiedId)).motion[1].frame==8,
              "moving a motion key did not persist independently through file save and reopen");
        const auto newLayer=take(session->createLayer(LayerKind::DynamicVector));good(model->save());run();
        const auto withNewLayer=take(openCanvasFile(path)).document;
        const auto *savedNew=findLayer(withNewLayer,newLayer);
        check(savedNew && keyframedLayerSource(*savedNew) && keyframedLayerSource(*savedNew)->frameIndices==std::vector<FrameIndex>{0},
              "a newly created dynamic vector layer did not survive file save and reopen");
        good(session->setLayerVisible("paint",false));
        check(model->canSave.get(),"edited working file did not become savable");
        good(model->save());
        good(session->editPublication("title","Edited during save"));
        run();
        check(session->dirty.get() && model->canSave.get(),"in-flight edit was incorrectly marked saved");
        auto first=take(openCanvasFile(path));
        check(!layerProperties(first.document.layers[0]).visible && !first.document.publicationMetadata,
              "first save did not preserve its captured state");
        good(session->editPublicationSetting("publish.destination.x.sequence",std::string("Thread")));
        good(model->save()); run();
        const auto publishedMetadata=take(openCanvasFile(path)).document.publicationMetadata;
        check(!session->dirty.get() && publishedMetadata && publishedMetadata->title=="Edited during save" &&
              std::get<std::string>(publishedMetadata->settings.at("publish.destination.x.sequence"))=="Thread",
              "subsequent save did not persist later publication changes");
        good(session->editPublication("description","Local"));
        iiSharedCanvas::DocumentFile external; check(external.open(path.string()).ok(),"cannot open external fixture writer");
        check(external.edit([](Document &doc){doc.publicationMetadata->description="External"; return true;}).ok(),
              "cannot update external fixture"); external.close();
        good(model->save()); run();
        check(session->dirty.get() && !model->error.get().empty() &&
              take(openCanvasFile(path)).document.publicationMetadata->description=="External",
              "conflicting save lost external data or hid its error");
        check(!model->open(path),"opening a file replaced unsaved work");
        const auto savedAs=temporary.path()/"old-document.iisc";
        good(model->saveAs(savedAs));
        Document replacement; replacement.extent={3,4};
        good(session->attach(replacement,"Replacement.iisc",true));
        run();
        check(session->title.get()=="Replacement.iisc" && session->dirty.get() && model->path.get().empty() &&
              take(openCanvasFile(savedAs)).document.publicationMetadata->description=="Local",
              "a completed save changed a later document session");
        model->close();
        const auto imagePath=temporary.path()/"source.PNG";
        auto pixels=makeRasterLayer(2,2,0xff123456);pixels.pixels[0]=0x80336699;
        const std::vector<std::uint8_t> rgba{0x33,0x66,0x99,0x80,0x12,0x34,0x56,0xff,
                                             0x12,0x34,0x56,0xff,0x12,0x34,0x56,0xff};
        auto encoded=ii::visual::image::encode_png({2,2,ii::visual::image::PixelFormat::rgba8,0,rgba});
        check(bool(encoded),"cannot encode isolated image fixture");
        const std::string imageBytes(reinterpret_cast<const char *>(encoded->data()),encoded->size());
        iiFileProvider::native::File::write(imagePath,imageBytes);
        using ImageFormat=ii::visual::image::Format;
        for (const auto &[suffix,format]:std::array{
                std::pair{".jpg",ImageFormat::jpeg},std::pair{".bmp",ImageFormat::bmp},
                std::pair{".webp",ImageFormat::webp},std::pair{".tif",ImageFormat::tiff},std::pair{".tga",ImageFormat::tga}}) {
            const auto otherPath=temporary.path()/(std::string("source")+suffix);
            const auto other=ii::visual::image::encode({2,2,ii::visual::image::PixelFormat::rgba8,0,rgba},format);
            if (!other) throw std::runtime_error(std::string("cannot encode ")+suffix+" fixture: "+other.error().message);
            const std::string original(reinterpret_cast<const char *>(other->data()),other->size());
            iiFileProvider::native::File::write(otherPath,original);
            const auto imported=take(importCanvasImage(otherPath));
            const auto *asset=std::get_if<RasterAsset>(&imported.document.assets.front());
            check(asset && imported.document.extent.width==2 && imported.document.extent.height==2 &&
                  iiFileProvider::native::File::read(otherPath,original.size()+1)==original,
                  "a supported image format did not import without changing the source");
            if (format!=ImageFormat::jpeg && asset->pixels.pixels!=pixels.pixels)
                throw std::runtime_error(std::string("lossless ")+suffix+" fixture lost pixels");
        }
        auto imageSession=DocumentSession::create();
        auto imageModel=CanvasFileModel::create(imageSession,loop,[&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;});
        good(imageModel->open(imagePath));
        check(imageModel->busy.get() && !imageSession->hasDocument.get(),"image decoding ran on the view thread");
        run();
        check(imageSession->hasDocument.get() && imageSession->dirty.get() && imageModel->path.get().empty() &&
              !imageModel->version() && !imageModel->canSave.get() && imageModel->canSaveAs.get(),
              "image import did not create an unassociated, unsaved canvas");
        const auto *imageDocument=imageSession->document();
        check(imageDocument && imageDocument->extent.width==2 && imageDocument->extent.height==2 &&
              imageDocument->timeline.frameCount==24 && imageDocument->layers.size()==1,
              "the imported canvas lost its extent or default still duration");
        const auto *imageAsset=std::get_if<RasterAsset>(&imageDocument->assets.front());
        check(imageAsset && imageAsset->pixels.pixels==pixels.pixels,"the imported image lost pixels or alpha");
        check(!imageModel->open(path) && !imageModel->save(),"an unsaved imported image could replace or overwrite its source");
        const auto importedCanvas=temporary.path()/"imported.iisc";
        good(imageModel->saveAs(importedCanvas));run();
        const auto importedSaved=take(openCanvasFile(importedCanvas)).document;
        const auto *savedImage=std::get_if<RasterAsset>(&importedSaved.assets.front());
        check(savedImage && savedImage->pixels.pixels==pixels.pixels && !imageSession->dirty.get() &&
              iiFileProvider::native::File::read(imagePath,imageBytes.size()+1)==imageBytes,
              "saving the imported canvas changed the source or lost pixels");
        const auto broken=temporary.path()/"broken.png";
        iiFileProvider::native::File::write(broken,"invalid image data");
        check(!imageModel->open(temporary.path()/"animation.gif"),"an animated image was accepted as a still");
        good(imageModel->open(broken));run();
        check(!imageModel->error.get().empty() && imageSession->title.get()=="imported.iisc" &&
              imageModel->path.get()==importedCanvas.string() && !imageSession->dirty.get() &&
              iiFileProvider::native::File::read(broken,64)=="invalid image data",
              "failed image decoding damaged the current document or source");
        good(imageModel->open(imagePath));
        Document later;later.extent={3,5};good(imageSession->attach(later,"Later.iisc",true));run();
        check(imageSession->title.get()=="Later.iisc" && imageSession->dirty.get() && imageModel->path.get().empty(),
              "a delayed image import replaced a later canvas");
        imageModel->close();
        auto shell=NativeApplicationShell::create(queueReadySvg);
        good(shell->connectCanvasFiles(loop,[&](ii::runtime::Task task){jobs.push_back(std::move(task)); return true;}));
        ii::gui::FileDialogCompletion chooser;
        shell->setCanvasFilePicker([&](ii::gui::FileDialogOptions options,ii::gui::FileDialogCompletion completion) {
            check(options.mode==ii::gui::FileDialogMode::open,"open requested the wrong native dialog");
            check(!options.filters.empty() && std::ranges::find(options.filters.front().extensions,"png")!=options.filters.front().extensions.end() &&
                  std::ranges::find(options.filters.front().extensions,"iisc")!=options.filters.front().extensions.end(),
                  "the default open filter did not include canvases and images");
            chooser=std::move(completion); return ii::core::Result<void>{};
        });
        good(shell->requestCanvasOpen());
        check(bool(chooser),"native file picker was not invoked");
        chooser(ii::gui::FileDialogSelection{.paths={path.string()}});
        run();
        check(shell->documentSession()->hasDocument.get() &&
              shell->documentSession()->title.get()=="input.iisc" && !shell->atHome.get(),
              "chosen file did not navigate to the actual editor document");
        good(shell->documentSession()->editPublication("description","From shell"));
        good(shell->requestCanvasSave()); run();
        check(!shell->documentSession()->dirty.get() &&
              take(openCanvasFile(path)).document.publicationMetadata->description=="From shell",
              "editor save did not commit to the chosen working file");
        good(shell->navigate("publish"));
        good(shell->publishingWorkspace()->editTitle("Saved from publish"));
        good(shell->publishingWorkspace()->setTargetIncluded("x",true));
        check(shell->publishingWorkspace()->canSaveDraft.get(),"the publish draft did not use the canvas save service");
        good(shell->publishingWorkspace()->saveDraft());run();
        const auto publishedDraft=take(openCanvasFile(path)).document.publicationMetadata;
        check(!shell->publishingWorkspace()->canSaveDraft.get() &&
              publishedDraft && publishedDraft->title=="Saved from publish" &&
              std::get<bool>(publishedDraft->settings.at("publish.destination.x.selected")),
              "Save draft did not persist the actual .iisc metadata");
        using dreamscapes::vm::Value;
        using dreamscapes::vm::Object;
        using dreamscapes::vm::Array;
        good(shell->navigate("files"));
        good(shell->storageWorkspace()->setSnapshot(Value(Array{Value(Object{{"section",Value("files")},{"exists",Value(true)},
            {"key",Value("image")},{"name",Value("source.PNG")},{"relativePath",Value("source.PNG")},
            {"nativePath",Value(imagePath.string())}})})));
        good(shell->storageWorkspace()->activate("image"));
        check(shell->storageWorkspace()->canOpen.get(),"a local image was not openable from Files");
        good(shell->storageWorkspace()->openSelected());run();
        check(shell->documentSession()->title.get()=="source.PNG" && shell->documentSession()->dirty.get() &&
              shell->documentSession()->document()->extent.width==2 && !shell->atHome.get(),
              "opening an image from Files did not navigate to its editable canvas");
        shell->disconnectCanvasFiles();
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
