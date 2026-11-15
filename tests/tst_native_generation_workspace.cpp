#include "App/Views/Generation/NativeGenerationWorkspace.h"
#include "App/Views/Home/NativeHomeLocalDraft.h"
#include <iostream>
#include <stdexcept>
#include <thread>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        auto home=home::NativeHomeLocalDraft::create();
        auto image=generation::GenerationWorkspaceModel::create(false,home->model());
        auto video=generation::GenerationWorkspaceModel::create(true,home->model());
        check(!image->canGenerate.get() && !video->canGenerate.get(),"generation was enabled without an engine");
        check(image->field("width") && image->field("sampler") && image->field("transparentBackground"),
              "image parameters were not derived from the SDK");
        check(!image->field("model")->enabled.get(),"model selector was enabled without a provider");
        check(!video->edit("model","unavailable") && !video->field("model")->invalid.get(),
              "an unavailable video model field accepted an edit or showed an input error");
        good(home->setModelCatalog({{"image-a","Image A",true,false},{"image-b","Image B",true,false},
                                    {"ltx","LTX",false,true}}));
        check(image->field("model")->isChoice && image->field("model")->enabled.get() &&
              image->field("model")->options.size()==2,"advanced Image did not receive the Home model catalogue");
        good(image->selectModelAt(1));
        check(home->model()->snapshot.get().modelId=="image-b" && vm::text(vm::get(image->parameters(),"model"))=="image-b",
              "advanced image selection did not update the shared draft");
        check(!image->selectModelAt(2) && home->model()->snapshot.get().modelId=="image-b",
              "an invalid model index changed the shared selection");
        check(vm::number(vm::get(image->parameters(),"width"))==1024 && vm::number(vm::get(image->parameters(),"outputCount"))==4,
              "image defaults do not match the SDK");
        good(image->edit("width","2048"));
        check(!image->edit("width","33") && image->field("width")->invalid.get() &&
              image->field("width")->text.get()=="33" && vm::number(vm::get(image->parameters(),"width"))==2048,
              "invalid dimensions replaced the accepted image draft");
        good(image->edit("width","1024"));
        check(!image->field("width")->invalid.get(),"correcting an image input did not clear the error");
        good(image->toggle("transparentBackground",true));
        check(vm::boolean(vm::get(image->parameters(),"transparentBackground")),"toggle did not update the image draft");
        good(image->select("sampler","heun"));
        check(vm::text(vm::get(image->parameters(),"sampler"))=="heun", "image choice was not applied");
        check(!image->select("sampler","unknown"),"unknown image choice was accepted");
        good(home->model()->dispatch(home::HomeComposerCommandKind::editPrompt,"shared prompt"));
        check(image->field("prompt")->text.get()=="shared prompt" && video->field("prompt")->text.get()=="shared prompt",
              "the Home prompt was not projected into both generation views");
        good(video->edit("prompt","video prompt"));
        check(home->model()->snapshot.get().prompt=="video prompt" && image->field("prompt")->text.get()=="video prompt",
              "generation prompt edit did not return to the shared Home draft");
        check(vm::number(vm::get(video->parameters(),"width"))==1024 && vm::number(vm::get(video->parameters(),"fps"))==24,
              "video defaults do not match the native draft");
        good(video->select("fps","30"));
        check(vm::number(vm::get(video->parameters(),"fps"))==30,"video FPS choice was not applied");
        check(!video->edit("device","unknown") && vm::text(vm::get(video->parameters(),"device"))=="auto",
              "an edit bypassed the video choice catalog");
        check(!video->edit("width","33") && vm::number(vm::get(video->parameters(),"width"))==1024,
              "invalid video dimension was accepted");
        good(video->edit("width","576"));
        check(vm::number(vm::get(video->parameters(),"width"))==576,"video draft edit did not propagate");
        check(vm::number(vm::get(image->parameters(),"width"))==1024,"video edit changed the image draft");
        check(!video->toggle("prompt",true) && vm::text(vm::get(video->parameters(),"prompt"))=="video prompt",
              "a toggle command corrupted a text field");
        check(!video->toggle("width",true) && vm::number(vm::get(video->parameters(),"width"))==576,
              "a toggle command corrupted a numeric field");
        ii::core::ErrorCode foreignResult=ii::core::ErrorCode::failed;
        std::thread foreign([&] {auto result=video->edit("width","640"); if (!result) foreignResult=result.error().code;});
        foreign.join();
        check(foreignResult==ii::core::ErrorCode::wrong_executor && vm::number(vm::get(video->parameters(),"width"))==576,
              "a foreign thread modified the generation draft");
        using Kind=home::HomeComposerCommandKind;
        good(home->model()->dispatch(Kind::chooseResolution,"512"));
        good(home->model()->dispatch(Kind::chooseCount,"3"));
        good(home->model()->dispatch(Kind::applySeed,"42"));
        good(image->adoptQuickSettings(home->model()->snapshot.get()));
        check(vm::number(vm::get(image->parameters(),"width"))==512 &&
              vm::number(vm::get(image->parameters(),"outputCount"))==3 &&
              vm::number(vm::get(image->parameters(),"seed"))==42,
              "Home image settings did not reach Advanced");
        good(image->edit("width","768"));
        good(image->adoptQuickSettings(home->model()->snapshot.get()));
        check(vm::number(vm::get(image->parameters(),"width"))==768,
              "reopening Advanced overwrote a local edit with unchanged Home settings");
        good(home->model()->dispatch(Kind::chooseCount,"4"));
        good(image->adoptQuickSettings(home->model()->snapshot.get()));
        check(vm::number(vm::get(image->parameters(),"width"))==768 &&
              vm::number(vm::get(image->parameters(),"outputCount"))==4,
              "changing the Home count replaced an unrelated advanced edit");
        auto invalidHome=home->model()->snapshot.get(); invalidHome.width=1; invalidHome.outputCount=5;
        check(!image->adoptQuickSettings(invalidHome) && vm::number(vm::get(image->parameters(),"width"))==768 &&
              vm::number(vm::get(image->parameters(),"outputCount"))==4,
              "invalid Home settings partially replaced the advanced draft");
        good(home->model()->dispatch(Kind::chooseMedia,"Video"));
        check(video->field("model")->isChoice && video->field("model")->enabled.get() &&
              vm::text(vm::get(video->parameters(),"model"))=="ltx", "advanced Video did not adopt its own model");
        good(home->model()->dispatch(Kind::chooseRatio,"16:9"));
        good(home->model()->dispatch(Kind::chooseDuration,"3"));
        good(home->model()->dispatch(Kind::chooseFrameRate,"30"));
        good(video->adoptQuickSettings(home->model()->snapshot.get()));
        check(vm::number(vm::get(video->parameters(),"width"))==1024 &&
              vm::number(vm::get(video->parameters(),"height"))==576 &&
              vm::number(vm::get(video->parameters(),"duration"))==3 &&
              vm::number(vm::get(video->parameters(),"fps"))==30 &&
              vm::number(vm::get(video->parameters(),"seed"))==42 &&
              !vm::boolean(vm::get(video->parameters(),"randomSeed")),
              "Home video settings did not reach Advanced");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
