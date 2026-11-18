#include "App/Views/Home/NativeHomeLocalDraft.h"
#include "App/Views/Home/NativeHomeComposerChoice.h"
#include "App/Generation/ModelPreferences.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes::home;
        auto draft=NativeHomeLocalDraft::create();
        auto model=draft->model();
        check(model->snapshot.get().width==1536 && model->snapshot.get().height==1536,
              "the default image resolution was not increased");
        const auto resolution = HomeComposerChoiceModel::create(model, HomeComposerChoiceRole::resolution,
                                                                "Resolution", "Choose resolution", false);
        good(model->dispatch(HomeComposerCommandKind::chooseRatio, "3:4"));
        const auto &portraitChoices = resolution->choices.get();
        const auto portrait = std::ranges::find(portraitChoices, "1024", &HomeComposerChoiceRow::key);
        check(portrait != portraitChoices.end() && portrait->label == "1024 × 1360",
              "resolution rows do not show both dimensions for the selected aspect ratio");
        good(resolution->choose("1024"));
        check(resolution->value.get() == "1024 × 1360" && model->snapshot.get().height == 1360,
              "selecting a resolution did not use the dimensions from its row");

        good(resolution->choose("4096"));
        struct MaximumResolution { const char *ratio; int width, height; };
        const std::array maximumResolutions{
            MaximumResolution{"1:1", 4096, 4096}, MaximumResolution{"4:3", 4096, 3072},
            MaximumResolution{"3:4", 3072, 4096}, MaximumResolution{"16:9", 4096, 2304},
            MaximumResolution{"9:16", 2304, 4096}};
        for (const auto &[ratio, width, height] : maximumResolutions) {
            good(model->dispatch(HomeComposerCommandKind::chooseRatio, ratio));
            const auto &snapshot = model->snapshot.get();
            const auto &last = resolution->choices.get().back();
            const auto label = std::to_string(width) + " × " + std::to_string(height);
            check(snapshot.width == width && snapshot.height == height && resolution->value.get() == label &&
                  last.key == "4096" && last.label == label && last.state == HomeComposerMenuState::selected,
                  "4K resolution was lost or exceeded the long-edge limit after changing the aspect ratio");
        }
        check(!resolution->choose("8192"), "the resolution menu accepted an unsupported size");
        good(model->dispatch(HomeComposerCommandKind::chooseRatio,"16:9"));
        good(model->dispatch(HomeComposerCommandKind::chooseResolution,"2048"));
        check(model->snapshot.get().width==3648 && model->snapshot.get().height==2048,
              "the expanded resolution choice did not preserve the aspect ratio and sixteen-pixel grid");
        good(model->dispatch(HomeComposerCommandKind::chooseRatio,"1:1"));
        check(model->commandsAvailable.get(),"local draft does not accept view edits");
        check(!model->canSubmit.get(),"generation was enabled without a backend");
        check(model->snapshot.get().choices.models.empty(),"unavailable models were invented");
        dreamscapes::ModelPreferences defaults;
        defaults.imageModel="img-b";defaults.videoModel="vid";
        defaults.prompts[0]={"default image","image negative"};
        defaults.prompts[1]={"default video","video negative"};
        good(draft->setDefaultPrompts(defaults));
        check(model->snapshot.get().prompt=="default image" && model->snapshot.get().negativePrompt=="image negative",
              "persisted defaults did not initialize the image draft");
        good(model->dispatch(HomeComposerCommandKind::chooseMedia,"Video"));
        check(model->snapshot.get().prompt=="default video" && model->snapshot.get().negativePrompt=="video negative",
              "media changes did not adopt the corresponding defaults");
        check(!resolution->enabled.get() && !resolution->choose("4096"),
              "an image-only resolution could be selected for video");
        good(model->dispatch(HomeComposerCommandKind::editPrompt,"한글 prompt"));
        good(model->dispatch(HomeComposerCommandKind::editNegativePrompt,"blur"));
        defaults.prompts[1]={"new default","new negative"};
        good(draft->setDefaultPrompts(defaults));
        check(model->snapshot.get().prompt=="한글 prompt" && model->snapshot.get().negativePrompt=="blur",
              "updated defaults overwrote an edited generation draft");
        good(model->dispatch(HomeComposerCommandKind::chooseMedia,"Video"));
        good(model->dispatch(HomeComposerCommandKind::chooseRatio,"16:9"));
        good(model->dispatch(HomeComposerCommandKind::chooseCount,"4"));
        good(model->dispatch(HomeComposerCommandKind::chooseDuration,"3"));
        good(model->dispatch(HomeComposerCommandKind::chooseFrameRate,"30"));
        good(model->dispatch(HomeComposerCommandKind::applySeed,"42"));
        const auto value=model->snapshot.get();
        check(value.prompt=="한글 prompt" && value.negativePrompt=="blur" && value.video &&
              value.width==1024 && value.height==576 && value.outputCount==4 &&
              value.videoDuration==3 && value.videoFps==30 && value.seed==42,
              "home edits did not update the native draft projection");
        check(!model->dispatch(HomeComposerCommandKind::applySeed,"invalid") && model->snapshot.get().seed==42,
              "invalid seed changed the accepted draft");
        check(!model->dispatch(HomeComposerCommandKind::submit),"unavailable generation accepted submission");
        check(!model->dispatch(HomeComposerCommandKind::chooseAttachments),"unavailable attachment picker accepted a command");
        bool opened=false,video=false;
        draft->setAdvancedHandler([&](bool isVideo) -> ii::core::Result<void> {opened=true; video=isVideo; return {};});
        good(model->dispatch(HomeComposerCommandKind::advanced));
        check(opened && video,"Advanced did not use the current media type");
        good(model->dispatch(HomeComposerCommandKind::randomSeed));
        check(model->snapshot.get().seed==-1,"random seed did not return to automatic mode");
        good(draft->setModelCatalog({{"img-a","Image A",true,false},{"img-b","Image B",true,false},
                                     {"vid","Video",false,true}}));
        check(model->snapshot.get().modelId=="vid" && model->snapshot.get().choices.models.size()==1,
              "video did not adopt its available stored default");
        good(model->dispatch(HomeComposerCommandKind::chooseMedia,"Image"));
        check(model->snapshot.get().modelId=="img-b" && model->snapshot.get().choices.models.size()==2,
              "image did not adopt its eligible stored default");
        const auto choicesBeforeTyping = model->snapshot.get().choices.models;
        const auto resolutionsBeforeTyping = model->snapshot.get().choices.resolutions;
        good(model->dispatch(HomeComposerCommandKind::editPrompt, "new prompt"));
        good(model->dispatch(HomeComposerCommandKind::editNegativePrompt, "new negative prompt"));
        check(model->snapshot.get().choices.models.data() == choicesBeforeTyping.data(),
              "editing a prompt rebuilt the unchanged model catalogue");
        check(model->snapshot.get().choices.resolutions.data() == resolutionsBeforeTyping.data(),
              "editing a prompt rebuilt the unchanged resolution menu");
        good(model->dispatch(HomeComposerCommandKind::chooseModel,"img-a"));
        good(model->dispatch(HomeComposerCommandKind::chooseMedia,"Video"));
        good(model->dispatch(HomeComposerCommandKind::chooseMedia,"Image"));
        check(model->snapshot.get().modelId=="img-a" && !model->canSubmit.get(),
              "model selection was lost on media change or incorrectly enabled inference");
        int submissions=0;
        draft->setSubmitHandler([&](const auto &) -> ii::core::Result<void> {++submissions;return {};},true,true);
        good(model->dispatch(HomeComposerCommandKind::applySeed,"4294967295"));
        check(!model->dispatch(HomeComposerCommandKind::submit) && submissions==0 &&
              model->snapshot.get().errorText.find("seed")!=std::string::npos,
              "Home queued a batch whose fixed seed overflows");
        good(model->dispatch(HomeComposerCommandKind::applySeed,"4294967292"));
        check(model->snapshot.get().errorText.empty() && bool(model->dispatch(HomeComposerCommandKind::submit)) && submissions==1,
              "correcting the Home batch seed did not restore submission");
        draft->setSubmitHandler({},false,false);
        check(!draft->setModelCatalog({{"same","A",true,false},{"same","B",false,true}}) &&
              model->snapshot.get().modelId=="img-a","invalid catalogue replaced the accepted selection");
        bool wrongThread=false;
        std::thread foreign([&] {auto result=draft->setModelCatalog({});wrongThread=!result && result.error().code==ii::core::ErrorCode::wrong_executor;});
        foreign.join();check(wrongThread && model->snapshot.get().modelId=="img-a","foreign thread changed the catalogue");
        good(draft->setModelCatalog({},"Model store unavailable"));
        check(model->snapshot.get().choices.models.empty() && model->snapshot.get().modelId.empty() &&
              model->snapshot.get().errorText=="Model store unavailable","catalogue failure retained stale selectable models");
        check(choicesBeforeTyping.size() == 2 && choicesBeforeTyping.front().key == "img-a",
              "replacing the catalogue mutated an earlier snapshot");
        auto reentrant=NativeHomeLocalDraft::create();
        bool attempted=false,rejected=false;
        auto observer=reentrant->model()->snapshot.observe([&](const auto &snapshot,std::uint64_t) {
            if (!attempted && snapshot.modelId=="accepted") {
                attempted=true; rejected=!reentrant->setModelCatalog({{"rejected","Rejected",true,false}});
            }
        });
        good(reentrant->setModelCatalog({{"accepted","Accepted",true,false}}));
        good(reentrant->model()->dispatch(HomeComposerCommandKind::chooseMedia,"Video"));
        good(reentrant->model()->dispatch(HomeComposerCommandKind::chooseMedia,"Image"));
        check(attempted && rejected && reentrant->model()->snapshot.get().modelId=="accepted",
              "a rejected reentrant catalogue replaced the accepted internal state");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
