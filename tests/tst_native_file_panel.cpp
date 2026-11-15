#include "App/Views/Editor/NativeFilePanel.h"
#include "App/ViewModels/Native/ViewValue.h"
#include <iiFileProviderNative.h>
#include <iiJson/Json.h>
#include <Document/Document.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        using namespace iiSharedCanvas;
        const auto schema=iiFileProvider::native::File::read(
            std::filesystem::path(DREAMSCAPES_TEST_RESOURCES)/"viewmodels/file.json",1024*1024);
        auto document=native::DocumentSession::create();
        auto panel=editor::FilePanelModel::create(document,schema);
        panel->setVisible(true);
        check(panel->informationMode.get() && panel->rows.get().size()>40,
              "the complete Figma information schema was not projected");
        auto value=[&](std::string_view key) {
            for (const auto &row:panel->rows.get()) if (row->key==key) return row->value.get();
            throw std::runtime_error("missing file inspector row: "+std::string(key));
        };
        check(value("Document.formatVersion.major/minor")=="Unavailable",
              "an absent document was given fabricated metadata");
        Document fixture;fixture.extent={8,6};fixture.timeline={{24,1},48};
        fixture.assets.emplace_back(RasterAsset{"pixels",makeRasterLayer(8,6,0xff123456)});
        fixture.layers.emplace_back(StaticBitmapLayer{{"paint","Paint"},StaticSource{"pixels"}});
        StableDiffusionMetadata metadata;metadata.positivePrompt="A real prompt";
        metadata.samplingPasses.push_back(StableDiffusionSamplingPass{.nodeId="sampler",.seed=42,.steps=20});
        for (int index=0;index<260;++index) metadata.loras.push_back(StableDiffusionLora{.name="adapter-"+std::to_string(index)});
        fixture.stableDiffusionMetadata=metadata;
        auto author=iiFileProvider::native::FileAuthor::fromIisaccAccount(
            {{"sub","fixture-author"},{"userId","@fixture"},{"email","fixture@example.org"},{"displayName","Artist"}},
            "https://iisacc.com","2026-01-01T00:00:00.000Z");
        check(author && fixture.authorship.setAuthor(*author,"2026-01-01T00:00:00.000Z"),"cannot create authorship fixture");
        good(document->attach(fixture,"Fixture.iisc"));
        const auto revision=document->revision.get();
        check(value("EditorCanvas.documentName")=="Fixture.iisc" &&
              value("Document.formatVersion.major/minor").starts_with("IISC ") &&
              value("StableDiffusionMetadata.positivePrompt")=="A real prompt" &&
              value("StableDiffusionSamplingPass.seed")=="42" && value("StableDiffusionSamplingPass.steps")=="20" &&
              value("Authorship.firstEditor")=="Artist" && value("IisaccAccount.displayName")=="Artist" &&
              value("StableDiffusionLora.name").find("4 more")!=std::string::npos,
              "information did not use the active document");
        good(panel->select("contents"));
        check(panel->contentsMode.get() && panel->rows.get().size()>50 &&
              value("Document.extent.width/height")=="8 × 6 px" &&
              value("Document.layers.count")=="1" && value("Document.assets.count")=="1" &&
              value("Document.timeline.frameCount")=="48",
              "content rows did not use the real document structure");
        good(document->seek(12));
        check(value("EditorCanvas.frame")=="12" && !document->dirty.get() && document->revision.get()==revision,
              "frame navigation did not update the inspector without editing the document");
        good(panel->select("storage"));
        check(panel->storageMode.get() && value("EditorCanvas.documentReady")=="Yes" &&
              value("EditorCanvas.filePath")=="Unavailable" && !document->dirty.get(),
              "storage view invented a file path or changed the document");
        check(!panel->select("unknown") && panel->storageMode.get(),"unknown inspector segment was accepted");
        const auto parsed=ii::json::parse(schema);check(bool(parsed),"cannot parse file-panel fixture");
        for (const auto &view:vm::array(vm::get(*parsed,"views"))) {
            good(panel->select(vm::text(vm::get(view,"id"))));
            std::size_t position=0;
            for (const auto &section:vm::array(vm::get(view,"sections"))) {
                check(position<panel->rows.get().size() && panel->rows.get()[position++]->section,
                      "inspector omitted a schema section");
                for (const auto &field:vm::array(vm::get(section,"rows"))) {
                    check(position<panel->rows.get().size() &&
                          panel->rows.get()[position]->key==vm::text(vm::get(field,"key")) &&
                          panel->rows.get()[position]->label==vm::text(vm::get(field,"label")),
                          "inspector omitted or reordered a schema field");
                    ++position;
                }
            }
            check(position==panel->rows.get().size(),"inspector invented additional schema rows");
        }
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
