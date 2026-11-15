#include "NativeHomeLocalDraft.h"
#include "App/Generation/ModelPreferences.h"
#include <charconv>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
std::optional<std::int64_t> integer(std::string_view text) {
    std::int64_t value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) return {};
    return value;
}
HomeComposerChoices choices(bool video) {
    HomeComposerChoices result;
    result.media={{"Image","Image"},{"Video","Video"}};
    for (const auto value : {"1:1","4:3","3:4","16:9","9:16"}) result.ratios.push_back({value,value});
    for (int value : {1,2,3,4,5,6,7,8,9,10,15,20,25,30,40,50,100,200,500,1000}) {
        auto key=std::to_string(value);
        result.counts.push_back({key,key+(video ? value==1?" video":" videos" : value==1?" image":" images")});
    }
    for (int value : {512,576,768,1024,1536}) {auto key=std::to_string(value); result.resolutions.push_back({key,key});}
    for (int value : {1,2,3,4,5,6,7,8,9,10,15,20,30}) {auto key=std::to_string(value); result.durations.push_back({key,key+" s"});}
    for (int value : {12,24,30,60,120}) {auto key=std::to_string(value); result.frameRates.push_back({key,key+" FPS"});}
    return result;
}
}
NativeHomeLocalDraft::NativeHomeLocalDraft() : model_(HomeComposerModel::create()) {}
std::shared_ptr<NativeHomeLocalDraft> NativeHomeLocalDraft::create() {
    auto value=std::shared_ptr<NativeHomeLocalDraft>(new NativeHomeLocalDraft());
    value->start(); return value;
}
NativeHomeLocalDraft::~NativeHomeLocalDraft() {if (model_ && model_->active()) (void)model_->setHandler({});}
void NativeHomeLocalDraft::start() {
    const std::weak_ptr<NativeHomeLocalDraft> weak=shared_from_this();
    auto attached=model_->setHandler([weak](const HomeComposerCommand &command) -> ii::core::Result<void> {
        if (auto value=weak.lock()) return value->command(command);
        return ii::core::fail(ii::core::ErrorCode::closed,"Generation draft is closed");
    });
    if (!attached) throw std::runtime_error(attached.error().message);
    if (auto result=synchronize(); !result) throw std::runtime_error(result.error().message);
}
void NativeHomeLocalDraft::setAdvancedHandler(std::function<ii::core::Result<void>(bool)> handler) {
    advanced_=std::move(handler); (void)synchronize();
}
ii::core::Result<void> NativeHomeLocalDraft::setDefaultPrompts(const ModelPreferences &preferences) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Generation draft requires its owner thread");
    vm::Object values;
    for (std::size_t index=0;index<generationPromptCategories.size();++index) {
        const auto &prompts=preferences.prompts[index];
        values.emplace(std::string(generationPromptCategories[index]),vm::Value(vm::Object{
            {"prompt",vm::Value(prompts.prompt)},{"negativePrompt",vm::Value(prompts.negativePrompt)}}));
    }
    if (!draft_.edits().send({"defaultPrompts",vm::Value(std::move(values))}) ||
        !draft_.edits().send({"promptDefaultsReady",vm::Value(true)}))
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot update generation defaults");
    imageModel_.preferred=preferences.imageModel;videoModel_.preferred=preferences.videoModel;reconcileModels();
    return synchronize();
}
void NativeHomeLocalDraft::reconcileModels() {
    auto update=[&](ModelSelection &selection,bool video) {
        auto eligible=[&](const auto &item) {return video?item.video:item.image;};
        auto has=[&](const std::string &id) {return !id.empty() && std::ranges::any_of(models_,[&](const auto &item){return item.id==id && eligible(item);});};
        if (selection.overridden && has(selection.selected)) return;
        selection.overridden=false;
        if (has(selection.preferred)) selection.selected=selection.preferred;
        else {
            const auto first=std::ranges::find_if(models_,eligible);
            selection.selected=first==models_.end()?std::string{}:first->id;
        }
    };
    update(imageModel_,false);update(videoModel_,true);
}
ii::core::Result<void> NativeHomeLocalDraft::setModelCatalog(std::vector<GenerationModelOption> values,std::string error) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Generation draft requires its owner thread");
    std::set<std::string,std::less<>> ids;
    for (const auto &item:values) if (item.id.empty() || item.id.size()>4096 || item.name.empty() ||
        (!item.image && !item.video) || !ids.insert(item.id).second)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Model catalogue contains an invalid or duplicate model");
    const auto previousImage=imageModel_,previousVideo=videoModel_;
    auto previousModels=std::move(models_); auto previousError=std::move(modelError_);
    models_=std::move(values);modelError_=std::move(error);reconcileModels();
    auto result=synchronize();
    if (!result) {
        models_=std::move(previousModels);modelError_=std::move(previousError);
        imageModel_=previousImage;videoModel_=previousVideo;
    }
    return result;
}
ii::core::Result<void> NativeHomeLocalDraft::synchronize() {
    const auto state=draft_.presentation();
    auto value=model_->snapshot.get();
    value.prompt=vm::text(vm::get(state,"prompt")); value.negativePrompt=vm::text(vm::get(state,"negativePrompt"));
    value.mediaType=vm::text(vm::get(state,"mediaType")); value.video=vm::boolean(vm::get(state,"video"));
    value.aspectRatio=vm::text(vm::get(state,"aspectRatio"));
    value.outputCount=int(vm::number(vm::get(state,"generationCount")));
    value.videoDuration=int(vm::number(vm::get(state,"videoDuration"))); value.videoFps=int(vm::number(vm::get(state,"videoFps")));
    value.shortSide=int(vm::number(vm::get(state,"shortSide"))); value.seed=vm::number(vm::get(state,"seed"));
    const auto &size=vm::get(state,"outputSize"); value.width=int(vm::number(vm::get(size,"width"))); value.height=int(vm::number(vm::get(size,"height")));
    value.resolutionLabel=std::to_string(value.width)+" × "+std::to_string(value.height);
    value.seedLabel=value.seed<0?"Random":std::to_string(std::int64_t(value.seed));
    value.choices=choices(value.video);
    const auto &selection=value.video?videoModel_:imageModel_;
    value.modelId=selection.selected;value.modelLabel.clear();
    for (const auto &item:models_) if (value.video?item.video:item.image) {
        value.choices.models.push_back({item.id,item.name});
        if (item.id==selection.selected) value.modelLabel=item.name;
    }
    value.errorText=modelError_;value.generationAvailable=false; value.attachmentsAvailable=false;
    value.paintAvailable=false; value.advancedAvailable=bool(advanced_);
    return model_->setSnapshot(std::move(value));
}
ii::core::Result<void> NativeHomeLocalDraft::command(const HomeComposerCommand &command) {
    using Kind=HomeComposerCommandKind;
    const char *key=nullptr; vm::Value value;
    switch (command.kind) {
    case Kind::editPrompt: key="prompt"; value=vm::Value(command.value); break;
    case Kind::editNegativePrompt: key="negativePrompt"; value=vm::Value(command.value); break;
    case Kind::chooseMedia: key="mediaType"; value=vm::Value(command.value); break;
    case Kind::chooseModel: {
        auto &selection=model_->snapshot.get().video?videoModel_:imageModel_;
        selection.selected=command.value;selection.overridden=true;return synchronize();
    }
    case Kind::chooseRatio: key="aspectRatio"; value=vm::Value(command.value); break;
    case Kind::chooseResolution: key="shortSide"; break;
    case Kind::chooseCount: key="generationCount"; break;
    case Kind::chooseDuration: key="videoDuration"; break;
    case Kind::chooseFrameRate: key="videoFps"; break;
    case Kind::applySeed: key="seed"; break;
    case Kind::randomSeed: key="seed"; value=vm::Value(-1); break;
    case Kind::advanced: if (advanced_) {auto callback=advanced_; return callback(model_->snapshot.get().video);} [[fallthrough]];
    default: return ii::core::fail(ii::core::ErrorCode::unsupported,"Generation service is unavailable");
    }
    if (key && value.kind()==ii::core::ValueKind::null) {
        const auto number=integer(command.value);
        if (!number || (command.kind==Kind::applySeed && (*number < -1 || *number > 4294967295LL)))
            return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid generation input");
        value=vm::Value(*number);
    }
    if (!draft_.edits().send({key,std::move(value)})) return ii::core::fail(ii::core::ErrorCode::failed,"Cannot update generation draft");
    return synchronize();
}
}
