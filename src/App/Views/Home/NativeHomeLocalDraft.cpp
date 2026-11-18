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
constexpr std::initializer_list<int> imageShortSides{512, 576, 768, 1024, 1280, 1536, 1792, 2048};

std::optional<std::int64_t> integer(std::string_view text) {
    std::int64_t value=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) return {};
    return value;
}
std::vector<HomeComposerChoice> numberedChoices(std::initializer_list<int> values, const char *suffix) {
    std::vector<HomeComposerChoice> result;
    result.reserve(values.size());
    for (int value : values) {
        auto key = std::to_string(value);
        result.push_back({key, key + suffix});
    }
    return result;
}
const HomeComposerChoices &choices(bool video) {
    static const HomeComposerChoices image = [] {
        auto counts = numberedChoices({1,2,3,4,5,6,7,8,9,10,15,20,25,30,40,50,100,200,500,1000}, " images");
        counts.front().label = "1 image";
        return HomeComposerChoices{
            .media = {{"Image","Image"},{"Video","Video"}},
            .ratios = {{"1:1","1:1"},{"4:3","4:3"},{"3:4","3:4"},{"16:9","16:9"},{"9:16","9:16"}},
            .counts = std::move(counts),
            .durations = numberedChoices({1,2,3,4,5,6,7,8,9,10,15,20,30}, " s"),
            .frameRates = numberedChoices({12,24,30,60,120}, " FPS")};
    }();
    static const HomeComposerChoices motion = [] {
        auto result = image;
        result.resolutions = numberedChoices(imageShortSides, "");
        auto counts = image.counts.entries();
        for (auto &choice : counts) choice.label = choice.key + (choice.key == "1" ? " video" : " videos");
        result.counts = std::move(counts);
        return result;
    }();
    return video ? motion : image;
}

std::string resolutionLabel(int width, int height) {
    return std::to_string(width) + " × " + std::to_string(height);
}

HomeComposerChoiceList imageResolutionChoices(std::string_view ratio) {
    std::vector<HomeComposerChoice> result;
    result.reserve(imageShortSides.size() + 1);
    const auto append = [&](int extent) {
        const auto size = vm::imageSize(ratio, extent);
        const auto width = static_cast<int>(vm::number(vm::get(size, "width")));
        const auto height = static_cast<int>(vm::number(vm::get(size, "height")));
        result.push_back({std::to_string(extent), resolutionLabel(width, height)});
    };
    for (int extent : imageShortSides) append(extent);
    append(vm::maximumImageDimension);
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
void NativeHomeLocalDraft::setSubmitHandler(std::function<ii::core::Result<void>(const HomeComposerSnapshot &)> handler,
                                            bool image,bool video) {
    if (owner_!=std::this_thread::get_id()) return;
    submit_=std::move(handler);imageAvailable_=image;videoAvailable_=video;submissionError_.clear();(void)synchronize();
}
ii::core::Result<void> NativeHomeLocalDraft::setDefaultPrompts(const ModelPreferences &preferences) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Generation draft requires its owner thread");
    vm::Object values;
    for (std::size_t index=0;index<generationPromptCategories.size();++index) {
        const auto &prompts=preferences.prompts[index];
        values.emplace(std::string(generationPromptCategories[index]),vm::Value(vm::Object{
            {"prompt",vm::Value(prompts.prompt)},{"negativePrompt",vm::Value(prompts.negativePrompt)}}));
    }
    if (!draft_.edits().send({"defaultPrompts",vm::Value(std::move(values))}))
        return ii::core::fail(ii::core::ErrorCode::failed,"Cannot update generation defaults");
    auto presentation = draft_.edits().send({"promptDefaultsReady",vm::Value(true)});
    if (!presentation) return ii::core::fail(ii::core::ErrorCode::failed,"Cannot update generation defaults");
    imageModel_.preferred=preferences.imageModel;videoModel_.preferred=preferences.videoModel;reconcileModels();
    return synchronize(*presentation);
}
void NativeHomeLocalDraft::reconcileModels() {
    const auto update = [](ModelSelection &selection) {
        const auto has = [&](const std::string &id) {
            return !id.empty() && std::ranges::find(selection.choices, id, &HomeComposerChoice::key) != selection.choices.end();
        };
        if (selection.overridden && has(selection.selected)) return;
        selection.overridden=false;
        if (has(selection.preferred)) selection.selected=selection.preferred;
        else selection.selected = selection.choices.empty() ? std::string{} : selection.choices.front().key;
    };
    update(imageModel_);
    update(videoModel_);
}
ii::core::Result<void> NativeHomeLocalDraft::setModelCatalog(std::vector<GenerationModelOption> values,std::string error) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Generation draft requires its owner thread");
    std::set<std::string,std::less<>> ids;
    for (const auto &item:values) if (item.id.empty() || item.id.size()>4096 || item.name.empty() ||
        (!item.image && !item.video) || !ids.insert(item.id).second)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Model catalogue contains an invalid or duplicate model");
    std::vector<HomeComposerChoice> images, videos;
    for (const auto &item : values) {
        if (item.image) images.push_back({item.id,item.name});
        if (item.video) videos.push_back({item.id,item.name});
    }
    HomeComposerChoiceList imageChoices(std::move(images)), videoChoices(std::move(videos));
    const auto previousImage=imageModel_,previousVideo=videoModel_;
    if (imageModel_.choices != imageChoices) imageModel_.choices = std::move(imageChoices);
    if (videoModel_.choices != videoChoices) videoModel_.choices = std::move(videoChoices);
    auto previousModels=std::move(models_); auto previousError=std::move(modelError_);
    models_=std::move(values);modelError_=std::move(error);reconcileModels();
    auto result=synchronize();
    if (!result) {
        models_=std::move(previousModels);modelError_=std::move(previousError);
        imageModel_=previousImage;videoModel_=previousVideo;
    }
    return result;
}
vm::Value NativeHomeLocalDraft::modelSource(std::string_view id) const {
    if (owner_!=std::this_thread::get_id()) return {};
    const auto found=std::ranges::find_if(models_,[&](const auto &model){return model.id==id;});
    return found==models_.end()?vm::Value{}:found->source;
}
ii::core::Result<void> NativeHomeLocalDraft::synchronize() {
    return synchronize(draft_.presentation());
}
ii::core::Result<void> NativeHomeLocalDraft::synchronize(const vm::Value &state) {
    const auto &current = model_->snapshot.get();
    auto value = current;
    value.prompt=vm::text(vm::get(state,"prompt")); value.negativePrompt=vm::text(vm::get(state,"negativePrompt"));
    value.mediaType=vm::text(vm::get(state,"mediaType")); value.video=vm::boolean(vm::get(state,"video"));
    value.aspectRatio=vm::text(vm::get(state,"aspectRatio"));
    value.outputCount=int(vm::number(vm::get(state,"generationCount")));
    value.videoDuration=int(vm::number(vm::get(state,"videoDuration"))); value.videoFps=int(vm::number(vm::get(state,"videoFps")));
    value.shortSide=int(vm::number(vm::get(state,"shortSide"))); value.seed=vm::number(vm::get(state,"seed"));
    const auto &size=vm::get(state,"outputSize"); value.width=int(vm::number(vm::get(size,"width"))); value.height=int(vm::number(vm::get(size,"height")));
    value.resolutionLabel=resolutionLabel(value.width, value.height);
    value.seedLabel=value.seed<0?"Random":std::to_string(std::int64_t(value.seed));
    const auto &selection=value.video?videoModel_:imageModel_;
    if (value.modelId != selection.selected || value.choices.models != selection.choices) {
        const auto found = std::ranges::find(selection.choices, selection.selected, &HomeComposerChoice::key);
        value.modelId = selection.selected;
        value.modelLabel = found == selection.choices.end() ? std::string{} : found->label;
    }
    value.choices = choices(value.video);
    if (!value.video) {
        const bool sameRatio = !current.video && current.aspectRatio == value.aspectRatio;
        value.choices.resolutions = sameRatio && !current.choices.resolutions.empty()
            ? current.choices.resolutions : imageResolutionChoices(value.aspectRatio);
    }
    value.choices.models = selection.choices;
    value.errorText=submissionError_.empty()?modelError_:submissionError_;
    value.generationAvailable=bool(submit_) && (value.video?videoAvailable_:imageAvailable_); value.attachmentsAvailable=false;
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
        selection.selected=command.value;selection.overridden=true;submissionError_.clear();return synchronize();
    }
    case Kind::chooseRatio: key="aspectRatio"; value=vm::Value(command.value); break;
    case Kind::chooseResolution: key="shortSide"; break;
    case Kind::chooseCount: key="generationCount"; break;
    case Kind::chooseDuration: key="videoDuration"; break;
    case Kind::chooseFrameRate: key="videoFps"; break;
    case Kind::applySeed: key="seed"; break;
    case Kind::randomSeed: key="seed"; value=vm::Value(-1); break;
    case Kind::submit: {
        const auto snapshot=model_->snapshot.get();
        if (!submit_ || !(snapshot.video?videoAvailable_:imageAvailable_))
            return ii::core::fail(ii::core::ErrorCode::unsupported,"Generation service is unavailable");
        if (snapshot.seed>=0 && snapshot.seed>4294967295.0-(snapshot.outputCount-1)) {
            submissionError_="The seed sequence exceeds the allowed range";
            (void)synchronize();
            return ii::core::fail(ii::core::ErrorCode::invalid_argument,submissionError_);
        }
        auto handler=submit_;auto result=handler(snapshot);
        submissionError_=result?std::string{}:result.error().message;(void)synchronize();return result;
    }
    case Kind::advanced: if (advanced_) {auto callback=advanced_; return callback(model_->snapshot.get().video);} [[fallthrough]];
    default: return ii::core::fail(ii::core::ErrorCode::unsupported,"Generation service is unavailable");
    }
    if (key && value.kind()==ii::core::ValueKind::null) {
        const auto number=integer(command.value);
        if (!number || (command.kind==Kind::applySeed && (*number < -1 || *number > 4294967295LL)))
            return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid generation input");
        value=vm::Value(*number);
    }
    auto presentation = draft_.edits().send({key,std::move(value)});
    if (!presentation) return ii::core::fail(ii::core::ErrorCode::failed,"Cannot update generation draft");
    submissionError_.clear();
    return synchronize(*presentation);
}
}
