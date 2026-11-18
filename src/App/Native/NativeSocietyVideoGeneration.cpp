#include "NativeSocietyVideoGeneration.h"
#include "NativeGenerationStaging.h"
#include "NativeSocietyModels.h"
#include "NativeSocietyStorage.h"
#include "App/ViewModels/Native/GenerationDraft.h"
#include <iiFileProvider>
#include <iiJson>
#include <iiSDKCore>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
namespace fs=std::filesystem;
using detail::GenerationStaging;
std::string real(double value) {
    char output[64];const auto converted=std::to_chars(output,output+sizeof(output),value,
        std::chars_format::general,std::numeric_limits<double>::max_digits10);
    return converted.ec==std::errc{}?std::string(output,converted.ptr):std::string{};
}
bool numeric(const vm::Value &value) {
    return value.kind()==ii::core::ValueKind::signed_integer || value.kind()==ii::core::ValueKind::unsigned_integer ||
        value.kind()==ii::core::ValueKind::real;
}
ii::core::Result<vm::Value> videoParameters(const generation::QueueRequest &request) {
    if (request.media!="Video" || request.parameters.kind()!=ii::core::ValueKind::object || request.model.empty() ||
        vm::text(vm::get(request.parameters,"model"))!=request.model || vm::text(vm::get(request.modelSource,"id"))!=request.model)
        return ii::core::fail(ErrorCode::invalid_argument,"Video request or captured model identity is invalid");
    const bool quick=vm::boolean(vm::get(request.parameters,"quick"));
    if (vm::has(request.parameters,"quick") && vm::get(request.parameters,"quick").kind()!=ii::core::ValueKind::boolean)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid quick generation flag");
    static const std::set<std::string,std::less<>> quickFields{
        "model","prompt","negativePrompt","width","height","duration","fps","outputCount","seed"};
    static const std::set<std::string,std::less<>> optionalImages{"firstFrame","lastFrame"};
    if (quick) for (const auto &key:quickFields) if (!vm::has(request.parameters,key))
        return ii::core::fail(ErrorCode::invalid_argument,"Missing quick video parameter: "+key);
    auto defaults=vm::videoDefaults({});auto values=vm::object(defaults);
    if (!quick) for (const auto &[key,value]:values) if (!optionalImages.contains(key) && !vm::has(request.parameters,key))
        return ii::core::fail(ErrorCode::invalid_argument,"Missing video generation parameter: "+key);
    for (const auto &[key,value]:vm::object(request.parameters)) {
        if (key=="quick" || key=="model") continue;
        const auto found=values.find(key);
        if (found==values.end() || (quick && !quickFields.contains(key) && !optionalImages.contains(key)))
            return ii::core::fail(ErrorCode::unsupported,"Unsupported video generation parameter: "+key);
        const auto kind=found->second.kind();
        if ((kind==ii::core::ValueKind::text && value.kind()!=kind) ||
            (kind==ii::core::ValueKind::boolean && value.kind()!=kind) ||
            (numeric(found->second) && !numeric(value)))
            return ii::core::fail(ErrorCode::invalid_argument,"Invalid video generation parameter: "+key);
        values[key]=value;
    }
    values["model"]=vm::Value(request.model);
    if (quick) values["randomSeed"]=vm::Value(vm::number(values.at("seed"),-2)<0);
    const auto combined=vm::Value(values);
    for (const auto &[key,value]:values) {
        if (key=="model" || key=="randomSeed" || (key=="seed" && vm::boolean(values.at("randomSeed")) && vm::number(value)==-1)) continue;
        auto edit=vm::planVideoEdit(defaults,vm::Value(vm::Object{}),{key,value});
        if (!edit.valid) return ii::core::fail(ErrorCode::invalid_argument,"Invalid video generation parameter: "+key);
    }
    const auto prompt=vm::text(values.at("prompt")),negative=vm::text(values.at("negativePrompt"));
    if (vm::trim(prompt).empty() || prompt.size()>32000 || negative.size()>32000 || !ii::core::valid_utf8(prompt) ||
        !ii::core::valid_utf8(negative) || prompt.find('\0')!=prompt.npos || negative.find('\0')!=negative.npos)
        return ii::core::fail(ErrorCode::invalid_argument,"Video prompt is invalid");
    for (const auto &key:optionalImages) {
        const auto path=vm::text(values.at(key));
        if (path.empty()) continue;
        auto native=ii::core::path_from_utf8(path);
        if (!native || path.find('\0')!=path.npos || !native->is_absolute() || *native!=native->lexically_normal())
            return ii::core::fail(ErrorCode::invalid_argument,"Video image condition must be an absolute UTF-8 path: "+key);
    }
    const std::map<std::string,std::set<std::string,std::less<>>,std::less<>> choices{
        {"device",{"auto","cpu","mps","cuda","rocm"}},{"precision",{"auto","float32","float16","bfloat16"}},
        {"offload",{"auto","none","model","sequential"}},{"encodingPreset",{"medium","fast","slow","veryfast","veryslow"}},
        {"camera1",{"none","static","dolly-in","dolly-out","pan-left","pan-right","tilt-up","tilt-down","orbit-left","orbit-right","crane-up","crane-down","zoom-in","zoom-out","handheld","tracking","dolly-zoom"}}};
    for (const auto &key:{"device","precision","offload","encodingPreset","camera1","camera2","camera3"}) {
        const auto &allowed=choices.at(std::string_view(key).starts_with("camera")?"camera1":key);
        if (!allowed.contains(vm::text(values.at(key)))) return ii::core::fail(ErrorCode::invalid_argument,"Invalid video option: "+std::string(key));
    }
    if (!vm::videoCameraValid(combined) || (!vm::boolean(values.at("randomSeed")) && vm::number(values.at("seed"),-1)<0))
        return ii::core::fail(ErrorCode::invalid_argument,"Video camera or seed settings are invalid");
    if (!vm::videoSeedSequenceValid(combined))
        return ii::core::fail(ErrorCode::invalid_argument,"Video seed sequence exceeds the unsigned 32-bit range");
    if (!vm::videoExecutionValid(combined))
        return ii::core::fail(ErrorCode::invalid_argument,"Sequential video offload requires CPU text encoding and VAE tiling");
    return combined;
}
std::string label(std::size_t index,std::size_t count) {
    return "Video "+std::to_string(index+1)+" / "+std::to_string(count);
}
}
ii::core::Result<generation::WorkerJobPlan> prepareSocietyVideoGeneration(const generation::QueueRequest &request,
    const generation::QueueCancellation &cancellation,const SocietyGenerationOptions &options,std::size_t outputIndex) {
    if (cancellation.load()) return ii::core::fail(ErrorCode::cancelled,"Generation was canceled");
    auto values=videoParameters(request);if (!values) return std::unexpected(values.error());
    if (outputIndex>=static_cast<std::size_t>(vm::number(vm::get(*values,"outputCount"))))
        return ii::core::fail(ErrorCode::invalid_argument,"Video batch index is outside the request");
    auto effective=vm::object(*values);
    if (!vm::boolean(vm::get(*values,"randomSeed")))
        effective["seed"]=vm::Value(static_cast<std::int64_t>(vm::number(vm::get(*values,"seed")))+
            static_cast<std::int64_t>(outputIndex));
    values=vm::Value(std::move(effective));
    auto model=resolveSocietyModel(options.selection,request.modelSource,options.explicitRoot,[&]{return cancellation.load();});
    if (!model) return std::unexpected(model.error());
    try {
        std::error_code error;const auto parent=fs::canonical(options.stagingRoot,error);
        if (error || !options.stagingRoot.is_absolute() || parent!=options.stagingRoot.lexically_normal() || !fs::is_directory(parent))
            return ii::core::fail(ErrorCode::invalid_argument,"Generation staging root must be an existing, unredirected absolute directory");
        auto stage=std::make_shared<GenerationStaging>(parent);
        if (!stage->directory.isValid()) return ii::core::fail(ErrorCode::failed,"Cannot create private video staging");
        auto modelPath=ii::core::path_to_utf8(*model),stagePath=ii::core::path_to_utf8(stage->path),
             identifier=ii::core::path_to_utf8(stage->path.filename()),output=ii::core::path_to_utf8(stage->path/"video.mp4"),
             cache=ii::core::path_to_utf8(stage->path/"cache"),selection=ii::core::path_to_utf8(options.selection),
             root=ii::core::path_to_utf8(options.explicitRoot);
        if (!modelPath || !stagePath || !identifier || !output || !cache || !selection || !root)
            return ii::core::fail(ErrorCode::invalid_argument,"Generation paths must be valid UTF-8");
        auto text=[&](const char *key){return vm::text(vm::get(*values,key));};
        auto number=[&](const char *key){return real(vm::number(vm::get(*values,key)));};
        std::vector<std::string> args{"--backend","video","--model-path",*modelPath,"--prompt",text("prompt"),
            "--negative-prompt",text("negativePrompt"),"--width",number("width"),"--height",number("height"),
            "--duration",number("duration"),"--fps",number("fps"),"--interpolation-factor",number("interpolationFactor"),
            "--steps",number("steps"),"--guidance-scale",number("cfgScale"),"--device",text("device"),
            "--dtype",text("precision"),"--offload",text("offload"),
            vm::boolean(vm::get(*values,"cpuTextEncoding"))?"--cpu-text-encoding":"--no-cpu-text-encoding",
            vm::boolean(vm::get(*values,"vaeTiling"))?"--vae-tiling":"--no-vae-tiling",
            "--decode-timestep",number("decodeTimestep"),"--decode-noise-scale",number("decodeNoiseScale"),
            "--image-cond-noise-scale",number("imageConditionNoise"),"--video-crf",number("crf"),
            "--video-preset",text("encodingPreset"),"--cache-dir",*cache,"--output",*output,"--camera"};
        bool camera=false;for (const auto key:{"camera1","camera2","camera3"}) if (auto value=text(key);value!="none") {args.push_back(value);camera=true;}
        if (!camera) args.emplace_back("none");
        for (const auto &[key,option]:{std::pair{"firstFrame","--first-frame"},std::pair{"lastFrame","--last-frame"}}) {
            const auto sourceText=text(key);if (sourceText.empty()) continue;
            const auto source=ii::core::path_from_utf8(sourceText);
            if (!source) return ii::core::fail(ErrorCode::invalid_argument,"Invalid video image condition path");
            try {
                const auto identity=iiFileProvider::ObjectSource::inspect(*source);
                if (!identity.size || identity.size>256ull*1024*1024)
                    return ii::core::fail(ErrorCode::invalid_argument,"Video image condition must be a non-empty file of at most 256 MiB: "+sourceText);
                auto snapshot=std::make_shared<iiFileProvider::ObjectSource>(*source,parent,cancellation.token(),identity);
                const auto snapshotPath=ii::core::path_to_utf8(snapshot->path());
                if (!snapshotPath) return ii::core::fail(ErrorCode::invalid_argument,"Video image snapshot path must be UTF-8");
                stage->inputs.push_back(std::move(snapshot));
                args.emplace_back(option);args.push_back(*snapshotPath);
            } catch (const std::exception &error) {
                if (cancellation.load()) return ii::core::fail(ErrorCode::cancelled,"Generation was canceled");
                return ii::core::fail(ErrorCode::failed,"Cannot capture video image condition "+sourceText+": "+error.what());
            }
        }
        if (text("offload")=="sequential") args.emplace_back("--low-vram");
        if (!vm::boolean(vm::get(*values,"randomSeed"))) {args.emplace_back("--seed");args.push_back(number("seed"));}
        stage->context=vm::Value(vm::Object{{"stagingDirectory",vm::Value(*stagePath)},{"output",vm::Value(*output)},
            {"selection",vm::Value(*selection)},{"explicitRoot",vm::Value(*root)},{"modelSource",request.modelSource},
            {"modelLabel",vm::Value(request.modelLabel.empty()?request.model:request.modelLabel)},
            {"parameters",request.parameters},{"values",*values}});
        return generation::WorkerJobPlan{*identifier,std::move(args),stage->context,std::move(stage)};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare private video staging",std::current_exception());}
}
ii::core::Result<void> publishSocietyVideoGeneration(const generation::WorkerJobPlan &plan,
    const generation::QueueCancellation &cancellation,const SocietyGenerationOptions &options,
    const generation::QueueOutputCallback &emit) {
    auto stage=std::dynamic_pointer_cast<GenerationStaging>(plan.staging);
    if (!stage || !emit) return ii::core::fail(ErrorCode::invalid_argument,"Video staging or output callback is unavailable");
    const auto stageText=ii::core::path_to_utf8(stage->path),selection=ii::core::path_to_utf8(options.selection),
        root=ii::core::path_to_utf8(options.explicitRoot),identifier=ii::core::path_to_utf8(stage->path.filename()),
        outputText=ii::core::path_to_utf8(stage->path/"video.mp4");
    if (!stageText || !selection || !root || !identifier || !outputText || *identifier!=plan.id || plan.context!=stage->context ||
        vm::text(vm::get(plan.context,"stagingDirectory"))!=*stageText || vm::text(vm::get(plan.context,"output"))!=*outputText ||
        vm::text(vm::get(plan.context,"selection"))!=*selection || vm::text(vm::get(plan.context,"explicitRoot"))!=*root)
        return ii::core::fail(ErrorCode::invalid_argument,"Video staging does not match its request");
    stage->preserve=true;
    const auto fail=[&](ErrorCode code,std::string message) -> ii::core::Result<void> {
        return ii::core::fail(code,std::move(message)+"\nRecoverable generation files: "+*stageText);
    };
    try {
        if (cancellation.load()) return fail(ErrorCode::cancelled,"Generation was canceled");
        const auto values=vm::get(plan.context,"values"),source=vm::get(plan.context,"modelSource");
        if (vm::text(vm::get(values,"model"))!=vm::text(vm::get(source,"id")))
            return fail(ErrorCode::invalid_argument,"Video model identity is invalid");
        const auto verify=[&]() -> ii::core::Result<SocietyListing> {
            auto model=resolveSocietyModel(options.selection,source,options.explicitRoot,[&]{return cancellation.load();});
            if (!model) return std::unexpected(model.error());
            auto history=browseSociety(options.selection,"generation-history","","",options.explicitRoot);
            if (!history) return std::unexpected(history.error());
            auto selectedRoot=ii::core::path_to_utf8(history->root);
            if (!selectedRoot || *selectedRoot!=vm::text(vm::get(source,"root")) ||
                history->containerId!=vm::text(vm::get(source,"containerId")))
                return ii::core::fail(ErrorCode::busy,"The selected Society container changed");
            return history;
        };
        auto history=verify();if (!history) return fail(history.error().code,history.error().message);
        const auto videoPath=stage->path/"video.mp4",reportPath=stage->path/"video.json";
        std::error_code error;
        const auto videoStatus=fs::symlink_status(videoPath,error);
        if (error || !fs::is_regular_file(videoStatus) || fs::is_symlink(videoStatus))
            return fail(ErrorCode::failed,"Generated video is unavailable or redirected");
        const auto reportStatus=fs::symlink_status(reportPath,error);
        if (error || !fs::is_regular_file(reportStatus) || fs::is_symlink(reportStatus))
            return fail(ErrorCode::failed,"Video generation record is unavailable or redirected");
        const auto reportIdentity=iiFileProvider::ObjectSource::inspect(reportPath);
        auto report=ii::json::parse(iiFileProvider::native::File::read(reportPath,16*1024*1024));
        if (!report || iiFileProvider::ObjectSource::inspect(reportPath)!=reportIdentity ||
            vm::text(vm::get(*report,"schema"))!="iild-temporal-video-v1" || vm::text(vm::get(*report,"status"))!="complete")
            return fail(ErrorCode::failed,"Video generation record is invalid or changed");
        const auto &record=vm::get(*report,"video");const auto &size=vm::array(vm::get(record,"size"));
        const auto width=vm::number(vm::get(values,"width")),height=vm::number(vm::get(values,"height")),
            fps=vm::number(vm::get(values,"fps")),duration=vm::number(vm::get(values,"duration"));
        const auto bytes=vm::number(vm::get(record,"size_bytes"),-1),frames=vm::number(vm::get(record,"frame_count"),-1),
            actualFps=vm::number(vm::get(record,"fps"),-1),actualDuration=vm::number(vm::get(record,"duration_seconds"),-1);
        auto hash=vm::lower(vm::text(vm::get(record,"sha256")));
        if (size.size()!=2 || vm::number(size[0],-1)!=width || vm::number(size[1],-1)!=height ||
            vm::text(vm::get(record,"codec"))!="h264" || !vm::boolean(vm::get(record,"verified_decode")) ||
            frames!=std::round(duration*fps) || std::abs(actualFps-fps)>0.000001 || !std::isfinite(actualDuration) ||
            std::abs(actualDuration-frames/fps)>1.0/fps || !std::isfinite(bytes) || bytes<12 || std::floor(bytes)!=bytes ||
            bytes>9007199254740991.0 || hash.size()!=64 ||
            !std::ranges::all_of(hash,[](unsigned char c){return (c>='0'&&c<='9') || (c>='a'&&c<='f');}))
            return fail(ErrorCode::failed,"Video output record does not match the request");
        const auto identity=iiFileProvider::ObjectSource::inspect(videoPath);
        iiFileProvider::native::ReadFile input(videoPath);
        if (input.size()!=static_cast<std::uint64_t>(bytes)) return fail(ErrorCode::failed,"Video size does not match its record");
        auto base=*identifier;if (!base.empty() && base.front()=='.') base.erase(base.begin());
        const auto destination=history->sectionPath/(base+".mp4");
        auto destinationText=ii::core::path_to_utf8(destination),name=ii::core::path_to_utf8(destination.filename());
        if (!destinationText || !name) return fail(ErrorCode::invalid_argument,"Video destination path is invalid");
        iiFileProvider::native::AtomicSave save;save.open(destination);
        std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> digest(EVP_MD_CTX_new(),EVP_MD_CTX_free);
        if (!digest || EVP_DigestInit_ex(digest.get(),EVP_sha256(),nullptr)!=1)
            return fail(ErrorCode::failed,"Cannot initialize video verification");
        std::vector<char> buffer(256*1024);std::array<char,12> prefix{};
        std::size_t prefixSize=0;std::uint64_t copied=0;
        for (;;) {
            if (cancellation.load()) return fail(ErrorCode::cancelled,"Generation was canceled");
            const auto count=input.read(buffer.data(),static_cast<std::int64_t>(buffer.size()));
            if (count<0) return fail(ErrorCode::failed,"Cannot read generated video");
            if (!count) break;
            const auto prefixPart=std::min<std::size_t>(prefix.size()-prefixSize,static_cast<std::size_t>(count));
            std::copy_n(buffer.data(),prefixPart,prefix.data()+prefixSize);prefixSize+=prefixPart;
            if (EVP_DigestUpdate(digest.get(),buffer.data(),static_cast<std::size_t>(count))!=1)
                return fail(ErrorCode::failed,"Cannot verify generated video");
            std::int64_t written=0;
            while (written<count) {
                const auto n=save.device().write(buffer.data()+written,count-written);
                if (n<=0) return fail(ErrorCode::failed,"Cannot stage generated video");
                written+=n;
            }
            copied+=static_cast<std::uint64_t>(count);
        }
        std::array<unsigned char,EVP_MAX_MD_SIZE> raw{};unsigned int length=0;
        if (prefixSize!=prefix.size() || std::string_view(prefix.data()+4,4)!="ftyp" || copied!=input.size() ||
            EVP_DigestFinal_ex(digest.get(),raw.data(),&length)!=1 || length!=32 ||
            iiFileProvider::ObjectSource::inspect(videoPath)!=identity)
            return fail(ErrorCode::failed,"Generated video changed or is not an MP4 file");
        constexpr char digits[]="0123456789abcdef";std::string actual;actual.reserve(64);
        for (unsigned i=0;i<length;++i) {actual.push_back(digits[raw[i]>>4]);actual.push_back(digits[raw[i]&15]);}
        if (actual!=hash) return fail(ErrorCode::failed,"Generated video hash does not match its record");
        auto current=verify();if (!current) return fail(current.error().code,current.error().message);
        if (current->sectionPath!=history->sectionPath) return fail(ErrorCode::busy,"Society generation history moved");
        if (cancellation.load()) return fail(ErrorCode::cancelled,"Generation was canceled");
        const auto committed=save.commit(false);
        if (!committed.succeeded) return fail(ErrorCode::failed,"Cannot publish generated video: "+committed.message);
        const auto url=societyFileUrl(*destinationText);
        vm::Object fields{{"containerId",vm::Value(history->containerId)},
            {"key",vm::Value("generation-history/"+*name)},{"name",vm::Value(*name)},{"displayName",vm::Value(*name)},
            {"path",vm::Value(*destinationText)},{"mediaType",vm::Value("Video")},{"mediaSource",vm::Value(url)},
            {"width",vm::Value(width)},{"height",vm::Value(height)},{"fps",vm::Value(fps)},
            {"duration",vm::Value(duration)},{"modelName",vm::get(plan.context,"modelLabel")},
            {"prompt",vm::get(values,"prompt")},{"negativePrompt",vm::get(values,"negativePrompt")},
            {"steps",vm::get(values,"steps")},{"videoParameters",values}};
        const auto &shots=vm::array(vm::get(*report,"shots"));
        if (!shots.empty() && vm::has(shots.front(),"seed")) {
            const auto &rawSeed=vm::get(shots.front(),"seed");const auto seed=vm::number(rawSeed,-1);
            if (numeric(rawSeed) && std::isfinite(seed) && seed>=0 && seed<=4294967295.0 && std::floor(seed)==seed)
                fields["seed"]=vm::Value(static_cast<std::int64_t>(seed));
        }
        emit(vm::Value(std::move(fields)));
        stage->preserve=false;return {};
    } catch (const std::exception &error) {return fail(ErrorCode::failed,std::string("Cannot publish generated video: ")+error.what());}
    catch (...) {return fail(ErrorCode::failed,"Cannot publish generated video");}
}
generation::QueueRunner makeSocietyVideoRunner(SocietyGenerationOptions options,generation::WorkerTransport transport) {
    return [options=std::move(options),transport=std::move(transport)](const generation::QueueRequest &request,
        const generation::QueueCancellation &cancellation,const generation::QueueProgressCallback &progress,
        const generation::QueueOutputCallback &output) -> generation::QueueCompletion {
        if (cancellation.load()) return {generation::QueueState::Canceled,{}};
        if (!transport || !output) return {generation::QueueState::Failed,"Video worker services are unavailable"};
        auto values=videoParameters(request);
        if (!values) return {generation::QueueState::Failed,values.error().message};
        const auto count=static_cast<std::size_t>(vm::number(vm::get(*values,"outputCount")));
        for (std::size_t index=0;index<count;++index) {
            if (cancellation.load()) return {generation::QueueState::Canceled,{}};
            auto plan=prepareSocietyVideoGeneration(request,cancellation,options,index);
            if (!plan) return {cancellation.load()?generation::QueueState::Canceled:generation::QueueState::Failed,
                label(index,count)+": "+plan.error().message};
            const generation::QueueProgressCallback update=[&](generation::QueueProgress value) {
                if (progress) {value.stage=label(index,count)+" · "+value.stage;progress(std::move(value));}
            };
            auto completed=generation::executeWorker(plan->id,plan->arguments,cancellation,update,transport);
            if (plan->staging) plan->staging->workerFinished(completed);
            if (completed.state!=generation::QueueState::Completed) {
                if (!completed.error.empty()) completed.error=label(index,count)+": "+completed.error;
                return completed;
            }
            if (cancellation.load()) {
                generation::QueueCompletion canceled{generation::QueueState::Canceled,{}};
                if (plan->staging) plan->staging->workerFinished(canceled);
                return canceled;
            }
            auto result=publishSocietyVideoGeneration(*plan,cancellation,options,output);
            if (!result) return {cancellation.load() || result.error().code==ErrorCode::cancelled?
                generation::QueueState::Canceled:generation::QueueState::Failed,label(index,count)+": "+result.error().message};
            if (cancellation.load()) return {generation::QueueState::Canceled,{}};
        }
        return {generation::QueueState::Completed,{}};
    };
}
}
