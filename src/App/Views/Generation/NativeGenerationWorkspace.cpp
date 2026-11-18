#include "NativeGenerationWorkspace.h"
#include "App/ViewModels/Native/GenerationDraft.h"
#include <Generation/ImageParameters.hpp>
#include <iiSDKCore>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace dreamscapes::generation {
namespace {
using ii::core::ErrorCode;
std::string label(std::string_view key) {
    if (key=="firstFrame") return "First frame image";
    if (key=="lastFrame") return "Last frame image";
    std::string result;
    for (char c : key) {
        if (c>='A' && c<='Z' && !result.empty()) result.push_back(' ');
        result.push_back(c);
    }
    if (!result.empty() && result.front()>='a' && result.front()<='z') result.front()-='a'-'A';
    if (result.starts_with("Cfg")) result.replace(0,3,"CFG");
    if (result.starts_with("Fps")) result.replace(0,3,"FPS");
    if (result.starts_with("Vae")) result.replace(0,3,"VAE");
    return result;
}
vm::Value toValue(const iiLocalDiffusion::ImageParameterValue &value) {
    return std::visit([](const auto &item){return vm::Value(item);},value);
}
std::string stringValue(const vm::Value &value) {
    return std::visit([](const auto &v) -> std::string {
        using T=std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T,std::string>) return v;
        else if constexpr (std::is_same_v<T,std::int64_t> || std::is_same_v<T,std::uint64_t>) return std::to_string(v);
        else if constexpr (std::is_same_v<T,double>) {
            char buffer[64]; const auto result=std::to_chars(buffer,buffer+sizeof(buffer),v);
            return result.ec==std::errc{} ? std::string(buffer,result.ptr) : std::string{};
        } else return {};
    },value.storage());
}
const std::map<std::string,std::vector<std::string>,std::less<>> videoChoices{
    {"fps",{"12","24","30","60","120"}},
    {"device",{"auto","cpu","mps","cuda","rocm"}},
    {"precision",{"auto","float32","float16","bfloat16"}},
    {"offload",{"auto","none","model","sequential"}},
    {"encodingPreset",{"medium","fast","slow","veryfast","veryslow"}},
    {"camera1",{"none","static","dolly-in","dolly-out","pan-left","pan-right","tilt-up","tilt-down","orbit-left","orbit-right","crane-up","crane-down","zoom-in","zoom-out","handheld","tracking","dolly-zoom"}},
    {"camera2",{"none","static","dolly-in","dolly-out","pan-left","pan-right","tilt-up","tilt-down","orbit-left","orbit-right","crane-up","crane-down","zoom-in","zoom-out","handheld","tracking","dolly-zoom"}},
    {"camera3",{"none","static","dolly-in","dolly-out","pan-left","pan-right","tilt-up","tilt-down","orbit-left","orbit-right","crane-up","crane-down","zoom-in","zoom-out","handheld","tracking","dolly-zoom"}},
};
const std::vector<std::pair<std::string,std::string>> videoOrder{
    {"prompt","Essentials"},{"negativePrompt","Essentials"},{"model","Essentials"},{"width","Essentials"},
    {"height","Essentials"},{"duration","Essentials"},{"fps","Essentials"},{"outputCount","Essentials"},
    {"firstFrame","Image conditions"},{"lastFrame","Image conditions"},
    {"camera1","Camera"},{"camera2","Camera"},{"camera3","Camera"},
    {"steps","Sampling"},{"cfgScale","Sampling"},{"randomSeed","Sampling"},{"seed","Sampling"},
    {"interpolationFactor","Frame interpolation"},{"decodeTimestep","Temporal decoding"},
    {"decodeNoiseScale","Temporal decoding"},{"imageConditionNoise","Temporal decoding"},
    {"device","Execution"},{"precision","Execution"},{"offload","Execution"},
    {"cpuTextEncoding","Execution"},{"vaeTiling","Execution"},{"crf","Video output"},{"encodingPreset","Video output"},
};
bool unavailable(std::string_view key) {
    return key=="model" || key=="vae" || key=="refinerModel" || key=="upscalerModel" ||
        key=="detailerModel" || key=="textualEmbeddings";
}
}
struct GenerationWorkspaceModel::Impl final {
    explicit Impl(std::shared_ptr<home::HomeComposerModel> value)
        : home(std::move(value)),image(iiLocalDiffusion::ImageParameters::defaults()),videoDraft({}) {
        const auto state=home->snapshot.get();
        if (!state.video && state.width>=64 && state.height>=64 && state.width<=4096 && state.height<=4096 &&
            state.width%8==0 && state.height%8==0) {
            image.values["width"]=std::int64_t(state.width);
            image.values["height"]=std::int64_t(state.height);
        } else {
            image.values["width"]=std::int64_t(vm::defaultImageShortSide);
            image.values["height"]=std::int64_t(vm::defaultImageShortSide);
        }
    }
    std::shared_ptr<home::HomeComposerModel> home;
    iiLocalDiffusion::ImageParameters image;
    vm::VideoDraft videoDraft;
    ii::core::ObservableSubscription homeObserver;
    home::HomeComposerChoiceList modelChoices;
    std::string selectedModel, sharedModelLabel;
    bool sharedMediaIsVideo = false;
    std::optional<home::HomeComposerSnapshot> adoptedQuick;
    std::function<ii::core::Result<void>(vm::Value)> submit;
    std::function<ii::core::Result<void>(std::string_view)> referencePicker;
    std::uint64_t referenceRevision=0;
    const std::thread::id owner=std::this_thread::get_id();
};
GenerationWorkspaceModel::GenerationWorkspaceModel(bool isVideo,std::shared_ptr<home::HomeComposerModel> home)
    : video(isVideo),impl_(std::make_unique<Impl>(std::move(home))) {}
GenerationWorkspaceModel::~GenerationWorkspaceModel()=default;
std::shared_ptr<GenerationWorkspaceModel> GenerationWorkspaceModel::create(bool video,std::shared_ptr<home::HomeComposerModel> home) {
    if (!home || !home->active()) throw std::invalid_argument("Generation view requires an active Home draft");
    auto value=std::shared_ptr<GenerationWorkspaceModel>(new GenerationWorkspaceModel(video,std::move(home)));
    value->start(); return value;
}
void GenerationWorkspaceModel::start() {
    std::vector<Field> entries;
    auto append=[&](std::string key,std::string section,vm::Value initial,std::vector<std::string> options) {
        auto field=std::make_shared<GenerationField>();
        field->key=std::move(key); field->label=label(field->key); field->section=std::move(section);
        field->showSection=entries.empty() || entries.back()->section!=field->section;
        field->isFile=video && (field->key=="firstFrame" || field->key=="lastFrame");
        if (field->isFile) {field->chooseAccessibility="Choose "+field->label;field->clearAccessibility="Clear "+field->label;}
        field->isToggle=initial.kind()==ii::core::ValueKind::boolean;
        field->isChoice=field->key=="model" || !options.empty(); field->isText=!field->isFile && !field->isToggle && !field->isChoice && initial.kind()==ii::core::ValueKind::text;
        field->isNumber=!field->isFile && !field->isToggle && !field->isChoice && !field->isText;
        field->options=std::move(options); field->owner=shared_from_this();
        (void)field->enabled.set(field->key != "model" && !unavailable(field->key));
        entries.push_back(std::move(field));
    };
    if (!video) {
        for (const auto &spec : iiLocalDiffusion::imageParameterSpecs()) append(spec.key,label(spec.section),toValue(spec.initial),spec.choices);
    } else {
        const auto defaults=impl_->videoDraft.values();
        for (const auto &[key,section] : videoOrder) {
            const auto found=videoChoices.find(key);
            append(key,section,key=="model"?vm::Value(""):vm::get(defaults,key),found==videoChoices.end()?std::vector<std::string>{}:found->second);
        }
    }
    (void)fields.set(std::move(entries));
    for (const auto &entry : fields.get()) publishField(entry);
    const std::weak_ptr<GenerationWorkspaceModel> weak=shared_from_this();
    impl_->homeObserver=impl_->home->snapshot.observe([weak](const auto &snapshot,std::uint64_t) {if (auto model=weak.lock()) model->syncShared(snapshot);});
    syncShared(impl_->home->snapshot.get());
}
GenerationWorkspaceModel::Field GenerationWorkspaceModel::field(std::string_view key) const {
    const auto &entries=fields.get();
    const auto found=std::ranges::find_if(entries,[&](const auto &value){return value->key==key;});
    return found==entries.end()?Field{}:*found;
}
vm::Value GenerationWorkspaceModel::parameters() const {
    vm::Object values;
    if (video) values=vm::object(impl_->videoDraft.values());
    else for (const auto &[key,value] : impl_->image.values) values.emplace(key,toValue(value));
    values["model"]=vm::Value(impl_->selectedModel);
    return vm::Value(std::move(values));
}
void GenerationWorkspaceModel::publishField(const Field &entry) {
    const auto values=parameters(); const auto &value=vm::get(values,entry->key);
    (void)entry->text.set(stringValue(value));
    if (entry->isFile) {
        const auto path=stringValue(value);
        const auto native=ii::core::path_from_utf8(path);
        (void)entry->fileName.set(native?ii::core::path_to_utf8(native->filename()).value_or(path):path);
        (void)entry->hasFile.set(!path.empty());
    }
    if (entry->isToggle) (void)entry->checked.set(vm::boolean(value));
    if (entry->isChoice) {
        const auto selected=std::ranges::find(entry->options,stringValue(value));
        (void)entry->index.set(selected==entry->options.end()?0:int(std::distance(entry->options.begin(),selected)));
    }
    if (video && entry->key=="seed") (void)entry->enabled.set(!vm::boolean(vm::get(values,"randomSeed")));
}
void GenerationWorkspaceModel::syncShared(const home::HomeComposerSnapshot &state) {
    if (std::this_thread::get_id()!=impl_->owner) return;
    bool changed = impl_->sharedMediaIsVideo != state.video;
    impl_->sharedMediaIsVideo = state.video;
    if (state.video==video) {
        const bool choicesChanged=impl_->modelChoices!=state.choices.models;
        const bool selectionChanged = impl_->selectedModel != state.modelId || impl_->sharedModelLabel != state.modelLabel;
        if (choicesChanged || selectionChanged) changed = true;
        if (choicesChanged) impl_->modelChoices = state.choices.models;
        if (selectionChanged) {
            impl_->selectedModel = state.modelId;
            impl_->sharedModelLabel = state.modelLabel;
        }
        auto row=field("model");
        if (row && choicesChanged) {
            auto replacement=std::make_shared<GenerationField>();
            replacement->key=row->key;replacement->label=row->label;replacement->section=row->section;
            replacement->showSection=row->showSection;replacement->isChoice=true;replacement->owner=shared_from_this();
            for (const auto &choice:impl_->modelChoices) replacement->options.push_back(choice.label);
            auto next=fields.get();
            for (auto &entry:next) if (entry->key=="model") {entry=replacement;break;}
            row=std::move(replacement);(void)fields.set(std::move(next));
        }
        if (row && (choicesChanged || selectionChanged)) {
            const auto found=std::ranges::find_if(impl_->modelChoices,[&](const auto &choice){return choice.key==state.modelId;});
            (void)row->text.set(state.modelLabel);(void)row->index.set(found==impl_->modelChoices.end()?0:int(found-impl_->modelChoices.begin()));
            (void)row->enabled.set(!impl_->modelChoices.empty());
        }
    }
    for (const auto *key : {"prompt", "negativePrompt"}) {
        const auto &text = std::string_view(key) == "prompt" ? state.prompt : state.negativePrompt;
        const auto row = field(key);
        if (!row || (row->text.get() == text && !row->invalid.get())) continue;
        if (video) (void)impl_->videoDraft.edits().send({key,vm::Value(text)});
        else impl_->image.values[key]=text;
        (void)row->text.set(text);
        (void)row->invalid.set(false);
        (void)row->error.set({});
        changed = true;
    }
    if (changed) updateCanGenerate();
}
void GenerationWorkspaceModel::setSubmitHandler(std::function<ii::core::Result<void>(vm::Value)> handler) {
    if (std::this_thread::get_id()!=impl_->owner) return;
    impl_->submit=std::move(handler);
    (void)serviceStatus.set(impl_->submit?std::string{}:"Generation service unavailable");
    updateCanGenerate();
}
void GenerationWorkspaceModel::updateCanGenerate() {
    const auto &state=impl_->home->snapshot.get();
    std::string issue;
    if (video && impl_->submit) {
        const auto values=impl_->videoDraft.values();
        if (!vm::videoCameraValid(values)) issue="Conflicting camera motions";
        else if (!vm::videoSeedSequenceValid(values)) issue="The seed sequence exceeds the allowed range";
        else if (!vm::videoExecutionValid(values)) issue="Sequential offload requires CPU text encoding and VAE tiling";
    }
    (void)validationStatus.set(std::move(issue));
    const bool valid=bool(impl_->submit) && state.video==video && !impl_->selectedModel.empty() &&
        !vm::trim(state.prompt).empty() && (!video || !impl_->videoDraft.invalid()) &&
        std::ranges::none_of(fields.get(),[](const auto &row){return row->invalid.get();});
    (void)canGenerate.set(valid);
}
ii::core::Result<void> GenerationWorkspaceModel::submit() {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    if (!canGenerate.get() || !impl_->submit) return ii::core::fail(ErrorCode::unsupported,"Generation inputs or service are unavailable");
    auto handler=impl_->submit;auto result=handler(parameters());
    (void)serviceStatus.set(result?std::string{}:result.error().message);
    return result;
}
ii::core::Result<void> GenerationWorkspaceModel::apply(std::string_view key,const vm::Value &value,const std::string *raw) {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    auto row=field(key);
    if (!row || !row->enabled.get()) return ii::core::fail(ErrorCode::unsupported,"Generation input is unavailable");
    if (key=="prompt" || key=="negativePrompt") {
        auto result=impl_->home->dispatch(key=="prompt"?home::HomeComposerCommandKind::editPrompt:home::HomeComposerCommandKind::editNegativePrompt,vm::text(value));
        if (!result) return result;
        return {};
    }
    bool accepted=false;
    if (video) {
        auto reply=impl_->videoDraft.edits().send({std::string(key),value}); accepted=reply && *reply;
    } else {
        auto next=impl_->image;
        auto found=next.values.find(std::string(key));
        if (found!=next.values.end()) {
            std::visit([&](auto &current) {
                using T=std::decay_t<decltype(current)>;
                if (const auto *exact=std::get_if<T>(&value.storage())) {current=*exact; accepted=true;}
            },found->second);
            if (accepted && !iiLocalDiffusion::validateImageParameters(next).empty()) accepted=false;
            if (accepted) impl_->image=std::move(next);
        }
    }
    if (raw) (void)row->text.set(*raw);
    (void)row->invalid.set(!accepted);
    (void)row->error.set(accepted?std::string{}:"Invalid value");
    updateCanGenerate();
    if (!accepted) return ii::core::fail(ErrorCode::invalid_argument,"Invalid generation parameter");
    if (row->isFile) ++impl_->referenceRevision;
    if (!raw || row->isChoice || row->isFile) publishField(row);
    if (video && key=="randomSeed") if (auto seed=field("seed")) publishField(seed);
    return {};
}
ii::core::Result<void> GenerationWorkspaceModel::edit(std::string_view key,std::string input) {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    auto row=field(key);
    if (!row) return ii::core::fail(ErrorCode::invalid_argument,"Unknown generation field");
    if (!row->enabled.get()) return ii::core::fail(ErrorCode::unsupported,"Generation input is unavailable");
    if (row->isToggle) return ii::core::fail(ErrorCode::invalid_argument,"Generation field does not accept text");
    if (row->isChoice && std::ranges::find(row->options,input)==row->options.end())
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown generation choice");
    const auto values=parameters(); const auto &current=vm::get(values,key);
    if (current.kind()==ii::core::ValueKind::text) return apply(key,vm::Value(input),&input);
    if (current.kind()==ii::core::ValueKind::signed_integer || current.kind()==ii::core::ValueKind::unsigned_integer) {
        std::int64_t number=0; const auto parsed=std::from_chars(input.data(),input.data()+input.size(),number);
        if (parsed.ec==std::errc{} && parsed.ptr==input.data()+input.size()) return apply(key,vm::Value(number),&input);
    } else if (current.kind()==ii::core::ValueKind::real) {
        double number=0; const auto parsed=std::from_chars(input.data(),input.data()+input.size(),number);
        if (parsed.ec==std::errc{} && parsed.ptr==input.data()+input.size() && std::isfinite(number)) return apply(key,vm::Value(number),&input);
    }
    (void)row->text.set(std::move(input)); (void)row->invalid.set(true); (void)row->error.set("Invalid value");updateCanGenerate();
    return ii::core::fail(ErrorCode::invalid_argument,"Invalid generation input");
}
ii::core::Result<void> GenerationWorkspaceModel::toggle(std::string_view key,bool value) {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    auto row=field(key);
    if (!row || !row->isToggle) return ii::core::fail(ErrorCode::invalid_argument,"Generation field is not a toggle");
    return apply(key,vm::Value(value));
}
ii::core::Result<void> GenerationWorkspaceModel::select(std::string_view key,std::string value) {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    if (key=="model") {
        if (impl_->home->snapshot.get().video!=video) return ii::core::fail(ErrorCode::unsupported,"This generation view is not active");
        return impl_->home->dispatch(home::HomeComposerCommandKind::chooseModel,std::move(value));
    }
    auto row=field(key);
    if (!row || !row->isChoice || std::ranges::find(row->options,value)==row->options.end())
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown generation choice");
    const auto current=parameters();
    if (vm::get(current,key).kind()==ii::core::ValueKind::signed_integer) return edit(key,std::move(value));
    return apply(key,vm::Value(std::move(value)));
}
ii::core::Result<void> GenerationWorkspaceModel::selectModelAt(int index) {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    if (index<0 || std::size_t(index)>=impl_->modelChoices.size()) return ii::core::fail(ErrorCode::invalid_argument,"Unknown generation model");
    return select("model",impl_->modelChoices[std::size_t(index)].key);
}
void GenerationWorkspaceModel::setReferencePicker(std::function<ii::core::Result<void>(std::string_view)> picker) {
    if (std::this_thread::get_id()!=impl_->owner) return;
    impl_->referencePicker=std::move(picker);
    for (const auto key:{"firstFrame","lastFrame"}) if (auto row=field(key))
        (void)row->canChooseFile.set(bool(impl_->referencePicker));
}
ii::core::Result<void> GenerationWorkspaceModel::chooseReference(std::string_view key) {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    auto row=field(key);
    if (!video || !row || !row->isFile || !impl_->referencePicker)
        return ii::core::fail(ErrorCode::unsupported,"Video image picker is unavailable");
    auto picker=impl_->referencePicker;auto result=picker(key);
    if (!result) (void)serviceStatus.set(result.error().message);
    return result;
}
ii::core::Result<void> GenerationWorkspaceModel::clearReference(std::string_view key) {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    auto row=field(key);
    if (!row || !row->isFile) return ii::core::fail(ErrorCode::invalid_argument,"Not a video image condition");
    return edit(key,{});
}
std::uint64_t GenerationWorkspaceModel::referenceRevision() const {return impl_->referenceRevision;}
ii::core::Result<void> GenerationWorkspaceModel::adoptQuickSettings(const home::HomeComposerSnapshot &state) {
    if (std::this_thread::get_id()!=impl_->owner) return ii::core::fail(ErrorCode::wrong_executor,"Generation view requires its owner thread");
    if (state.video!=video || !std::isfinite(state.seed) || std::floor(state.seed)!=state.seed ||
        state.seed < -1 || state.seed > 4294967295.0)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid Home generation settings");
    const auto &previous=impl_->adoptedQuick;
    std::vector<std::pair<std::string,vm::Value>> edits;
    if (!previous || state.width!=previous->width || state.height!=previous->height) {
        edits.emplace_back("width",vm::Value(state.width)); edits.emplace_back("height",vm::Value(state.height));
    }
    if (!previous || state.outputCount!=previous->outputCount) edits.emplace_back("outputCount",vm::Value(state.outputCount));
    if (video) {
        if (!previous || state.videoDuration!=previous->videoDuration) edits.emplace_back("duration",vm::Value(state.videoDuration));
        if (!previous || state.videoFps!=previous->videoFps) edits.emplace_back("fps",vm::Value(state.videoFps));
    }
    const bool seedChanged=!previous || state.seed!=previous->seed;
    if (seedChanged) {
        if (!video || state.seed>=0) edits.emplace_back("seed",vm::Value(std::int64_t(state.seed)));
        if (video) edits.emplace_back("randomSeed",vm::Value(state.seed<0));
    }
    if (video) {
        auto values=impl_->videoDraft.values(), invalid=impl_->videoDraft.invalidInputs();
        for (const auto &[key,value] : edits) {
            auto plan=vm::planVideoEdit(values,invalid,{key,value});
            if (!plan.valid) return ii::core::fail(ErrorCode::invalid_argument,"Home video settings are invalid");
            values=std::move(plan.values); invalid=std::move(plan.invalidInputs);
        }
        for (const auto &[key,value] : edits) {
            auto accepted=impl_->videoDraft.edits().send({key,value});
            if (!accepted || !*accepted) return ii::core::fail(ErrorCode::failed,"Cannot adopt Home video settings");
        }
    } else {
        auto next=impl_->image;
        for (const auto &[key,value] : edits) {
            const auto found=next.values.find(key);
            const auto *integer=std::get_if<std::int64_t>(&value.storage());
            if (found==next.values.end() || !integer) return ii::core::fail(ErrorCode::invalid_argument,"Invalid Home image setting");
            found->second=*integer;
        }
        if (!iiLocalDiffusion::validateImageParameters(next).empty())
            return ii::core::fail(ErrorCode::invalid_argument,"Home image settings are invalid");
        impl_->image=std::move(next);
    }
    impl_->adoptedQuick=state;
    for (const auto &[key,value] : edits) if (auto row=field(key)) {
        publishField(row); (void)row->invalid.set(false); (void)row->error.set({});
    }
    if (video && seedChanged) if (auto seed=field("seed")) {
        publishField(seed);
        if (state.seed<0) {(void)seed->invalid.set(false); (void)seed->error.set({});}
    }
    updateCanGenerate();
    return {};
}
void GenerationWorkspaceModel::sizeChanged(double width,double height,double maximumPanel,double minimumPreview) {
    if (std::this_thread::get_id()!=impl_->owner || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(maximumPanel) || !std::isfinite(minimumPreview) || width<0 || height<0 ||
        maximumPanel<=0 || minimumPreview<0 || width>8192 || height>8192) return;
    (void)viewportWidth.set(width); (void)viewportHeight.set(height);
    const bool show=width>=maximumPanel+minimumPreview;
    const double panel=show?std::min(width,maximumPanel):width;
    (void)panelWidth.set(std::max(1.0,panel));
    (void)previewWidth.set(show?std::max(0.0,width-panel):0);
    (void)showPreview.set(show);
}
void GenerationField::edited(std::string value) {if (auto model=owner.lock()) (void)model->edit(key,std::move(value));}
void GenerationField::toggled(bool value) {if (auto model=owner.lock()) (void)model->toggle(key,value);}
void GenerationField::selected(int value) {
    if (value>=0 && std::size_t(value)<options.size()) if (auto model=owner.lock()) {
        if (key=="model") (void)model->selectModelAt(value); else (void)model->select(key,options[std::size_t(value)]);
    }
}
void GenerationField::chooseFile() {if (auto model=owner.lock()) (void)model->chooseReference(key);}
void GenerationField::clearFile() {if (auto model=owner.lock()) (void)model->clearReference(key);}
}
