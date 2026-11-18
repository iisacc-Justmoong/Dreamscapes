#include "App/Native/NativeSocietyVideoGeneration.h"
#include "App/Native/NativeSocietyModels.h"
#include "App/Native/NativeApplicationShell.h"
#include "App/ViewModels/Native/GenerationDraft.h"
#include <ModelPackaging/ModelPackaging.hpp>
#include <iiFileProvider>
#include <iiJson>
#include <openssl/evp.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace dreamscapes;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void put(const fs::path &path,const vm::Value &value) {
    auto json=ii::json::stringify(value);check(bool(json),"fixture serialization failed");iiFileProvider::native::File::write(path,*json);
}
void safe(const fs::path &path,const std::string &header,std::string_view data) {
    std::string bytes(8,'\0');
    for (unsigned i=0;i<8;++i) bytes[i]=static_cast<char>((header.size()>>(8*i))&255);
    bytes+=header;bytes+=data;iiFileProvider::native::File::write(path,bytes);
}
std::string argument(const std::vector<std::string> &arguments,std::string_view name) {
    const auto it=std::ranges::find(arguments,name);return it==arguments.end() || it+1==arguments.end()?std::string{}:*(it+1);
}
std::string digest(std::string_view bytes) {
    unsigned char hash[EVP_MAX_MD_SIZE];unsigned int size=0;
    check(EVP_Digest(bytes.data(),bytes.size(),hash,&size,EVP_sha256(),nullptr)==1 && size==32,"fixture hashing failed");
    constexpr char digits[]="0123456789abcdef";std::string result;
    for (unsigned i=0;i<size;++i) {result.push_back(digits[hash[i]>>4]);result.push_back(digits[hash[i]&15]);}
    return result;
}
int main() {
    try {
        iiFileProvider::native::TemporaryDirectory temporary(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        const auto root=temporary.path()/"test.societycontainer",selection=temporary.path()/"selection.json";
        const auto model=root/"Models"/"Checkpoint"/"LTX";
        fs::create_directories(model);fs::create_directories(root/"Generation History");
        put(model/"model_index.json",vm::Value(vm::Object{{"_class_name",vm::Value("LTX2Pipeline")}}));
        iiFileProvider::native::File::write(model/"weights.safetensors","synthetic model");
        constexpr auto identifier="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        const vm::Array sections{vm::Value(vm::Object{{"id",vm::Value("models")},{"path",vm::Value("Models")}}),
            vm::Value(vm::Object{{"id",vm::Value("generation-history")},{"path",vm::Value("Generation History")}})};
        put(root/".society-drive.json",vm::Value(vm::Object{{"type",vm::Value("SocietyDrive")},{"schemaVersion",vm::Value(1)},
            {"identifier",vm::Value(identifier)},{"replicaReady",vm::Value(true)},{"sections",vm::Value(sections)}}));
        put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value(identifier)}}));
        auto models=native::browseSocietyModels(selection);check(models && vm::array(models->aliases).size()==1,"video fixture model unavailable");
        const auto source=vm::get(vm::array(models->aliases).front(),"source");
        const native::SocietyGenerationOptions options{selection,{},temporary.path()};
        generation::QueueRequest request{"Video","Checkpoint/LTX",vm::Value(vm::Object{{"quick",vm::Value(true)},
            {"model",vm::Value("Checkpoint/LTX")},{"prompt",vm::Value("움직이는 바다 $(literal)")},
            {"negativePrompt",vm::Value("blur")},{"width",vm::Value(576)},{"height",vm::Value(1024)},
            {"duration",vm::Value(3)},{"fps",vm::Value(24)},{"outputCount",vm::Value(2)},{"seed",vm::Value(-1)}}),"LTX",source};
        generation::QueueCancellation active;
        auto plan=native::prepareSocietyVideoGeneration(request,active,options);
        check(plan && argument(plan->arguments,"--backend")=="video" && argument(plan->arguments,"--model-path")==model.string() &&
            argument(plan->arguments,"--prompt")=="움직이는 바다 $(literal)" && argument(plan->arguments,"--duration")=="3" &&
            argument(plan->arguments,"--fps")=="24" && argument(plan->arguments,"--seed").empty(),
            "quick video settings were not preserved as worker arguments");
        plan->staging.reset();
        // A sharded PyTorch video model is selected as one logical source and
        // passed intact to the worker; its component identities must remain stable.
        const auto splitDir=root/"Models"/"Checkpoint"/"split-video";
        fs::create_directories(splitDir);
        const std::string pickleMagic("\x80\x02\x8a\x0a\x6c\xfc\x9c\x46\xf9\x20\x6a\xa8\x50\x19\x2e",15);
        iiFileProvider::native::File::write(splitDir/"one.bin",pickleMagic+"first");
        iiFileProvider::native::File::write(splitDir/"two.bin",pickleMagic+"second");
        put(splitDir/"model.json",vm::Value(vm::Object{{"weight_map",vm::Value(vm::Object{
            {"audio_vae.decoder.weight",vm::Value("one.bin")},
            {"model.diffusion_model.transformer_blocks.0.weight",vm::Value("one.bin")},
            {"text_embedding_projection.audio_aggregate_embed.weight",vm::Value("two.bin")}})}}));
        auto splitInventory=native::browseSocietyModels(selection);
        check(bool(splitInventory),"split video inventory failed");
        vm::Value splitSource;bool foundSplit=false;
        for (const auto &entry:vm::array(splitInventory->aliases)) {
            if (vm::text(vm::get(entry,"id"))=="Checkpoint/split-video/model.json") {
                foundSplit=vm::boolean(vm::get(entry,"video"));splitSource=vm::get(entry,"source");
            }
        }
        check(foundSplit,"split PyTorch index was not offered as one video model");
        auto splitRequest=request;splitRequest.model="Checkpoint/split-video/model.json";
        splitRequest.modelLabel="Split video";splitRequest.modelSource=splitSource;
        auto splitValues=vm::object(splitRequest.parameters);splitValues["model"]=vm::Value(splitRequest.model);
        splitRequest.parameters=vm::Value(std::move(splitValues));
        auto splitPlan=native::prepareSocietyVideoGeneration(splitRequest,active,options);
        check(splitPlan && argument(splitPlan->arguments,"--backend")=="video" &&
            argument(splitPlan->arguments,"--model-path")== (splitDir/"model.json").string(),
            "split PyTorch video index was not passed intact to the worker");
        splitPlan->staging.reset();
        iiFileProvider::native::File::write(splitDir/"two.bin",pickleMagic+"changed second");
        check(!native::prepareSocietyVideoGeneration(splitRequest,active,options),
            "a changed split PyTorch video shard was accepted");
        // The native worker must receive the selected Society package itself;
        // its runtime owns verified component restoration.
        const auto input=temporary.path()/"wan-package-input";
        fs::create_directories(input);
        safe(input/"model.safetensors",R"({"model.diffusion_model.patch_embedding.weight":{"dtype":"U8","shape":[1],"data_offsets":[0,1]},"model.diffusion_model.blocks.0.cross_attn.norm_k.weight":{"dtype":"U8","shape":[1],"data_offsets":[1,2]}})","ab");
        safe(input/"text_encoder.safetensors",R"({"shared.weight":{"dtype":"U8","shape":[1],"data_offsets":[0,1]}})","c");
        safe(input/"video_vae.safetensors",R"({"decoder.weight":{"dtype":"U8","shape":[1],"data_offsets":[0,1]}})","d");
        const auto packaged=root/"Models"/"Checkpoint"/"wan.safetensors";
        (void)iild::createModelPackage(input,packaged);
        auto inventory=native::browseSocietyModels(selection);check(bool(inventory),"Wan package inventory failed");
        vm::Value wanSource;bool foundWan=false;
        for (const auto &entry:vm::array(inventory->aliases)) if (vm::text(vm::get(entry,"id"))=="Checkpoint/wan.safetensors") {
            foundWan=vm::boolean(vm::get(entry,"video"));wanSource=vm::get(entry,"source");
        }
        check(foundWan,"The packaged Wan model was not offered as video");
        auto wanRequest=request;wanRequest.model="Checkpoint/wan.safetensors";wanRequest.modelLabel="Wan";
        wanRequest.modelSource=wanSource;
        auto wanValues=vm::object(wanRequest.parameters);wanValues["model"]=vm::Value(wanRequest.model);
        wanRequest.parameters=vm::Value(std::move(wanValues));
        auto wanPlan=native::prepareSocietyVideoGeneration(wanRequest,active,options);
        check(wanPlan && argument(wanPlan->arguments,"--model-path")==packaged.string() &&
            argument(wanPlan->arguments,"--video-vae").empty(),"The selected package was not passed intact to the video worker");
        wanPlan->staging.reset();
        auto advanced=vm::object(vm::videoDefaults({}));
        advanced["model"]=vm::Value("Checkpoint/LTX");advanced["prompt"]=vm::Value("Video prompt");
        advanced["outputCount"]=vm::Value(1);advanced["seed"]=vm::Value(123);advanced["randomSeed"]=vm::Value(false);
        advanced["camera1"]=vm::Value("pan-left");advanced["interpolationFactor"]=vm::Value(3);
        advanced["precision"]=vm::Value("float16");advanced["vaeTiling"]=vm::Value(false);
        request.parameters=vm::Value(advanced);plan=native::prepareSocietyVideoGeneration(request,active,options);
        check(plan && argument(plan->arguments,"--seed")=="123" && argument(plan->arguments,"--camera")=="pan-left" &&
            argument(plan->arguments,"--interpolation-factor")=="3" && argument(plan->arguments,"--dtype")=="float16" &&
            std::ranges::find(plan->arguments,"--no-vae-tiling")!=plan->arguments.end(),"advanced video settings were not forwarded");
        plan->staging.reset();
        const auto firstFrame=temporary.path()/"first.png",lastFrame=temporary.path()/"last.png";
        iiFileProvider::native::File::write(firstFrame,"first image bytes");
        iiFileProvider::native::File::write(lastFrame,"last image bytes");
        auto conditioned=advanced;conditioned["firstFrame"]=vm::Value(firstFrame.string());
        conditioned["lastFrame"]=vm::Value(lastFrame.string());request.parameters=vm::Value(conditioned);
        plan=native::prepareSocietyVideoGeneration(request,active,options);
        check(bool(plan),"video image conditions were rejected");
        const fs::path firstSnapshot=argument(plan->arguments,"--first-frame"),lastSnapshot=argument(plan->arguments,"--last-frame");
        check(!firstSnapshot.empty() && !lastSnapshot.empty() && firstSnapshot!=firstFrame && lastSnapshot!=lastFrame &&
              iiFileProvider::native::File::read(firstSnapshot)=="first image bytes" &&
              iiFileProvider::native::File::read(lastSnapshot)=="last image bytes",
              "video image conditions were not captured and forwarded to the worker");
        iiFileProvider::native::File::write(firstFrame,"changed after preparation");
        check(iiFileProvider::native::File::read(firstSnapshot)=="first image bytes",
              "worker input changed after preparation");
        plan->staging.reset();
        check(!fs::exists(firstSnapshot) && !fs::exists(lastSnapshot) &&
              iiFileProvider::native::File::read(firstFrame)=="changed after preparation" &&
              iiFileProvider::native::File::read(lastFrame)=="last image bytes",
              "input cleanup lost an original or leaked a snapshot");
        conditioned["firstFrame"]=vm::Value((temporary.path()/"missing.png").string());
        request.parameters=vm::Value(conditioned);
        check(!native::prepareSocietyVideoGeneration(request,active,options),"a missing frame image was accepted");
        conditioned["firstFrame"]=vm::Value("relative.png");request.parameters=vm::Value(conditioned);
        check(!native::prepareSocietyVideoGeneration(request,active,options),"a relative frame image was accepted");
        const auto emptyImage=temporary.path()/"empty.png";
        iiFileProvider::native::File::write(emptyImage,"");
        conditioned["firstFrame"]=vm::Value(emptyImage.string());request.parameters=vm::Value(conditioned);
        check(!native::prepareSocietyVideoGeneration(request,active,options),"an empty frame image was accepted");
        std::error_code sizeError;fs::resize_file(emptyImage,256ull*1024*1024+1,sizeError);
        if (!sizeError) check(!native::prepareSocietyVideoGeneration(request,active,options),"an oversized frame image was accepted");
        const auto redirect=temporary.path()/"redirect.png";std::error_code symlinkError;
        fs::create_symlink(firstFrame,redirect,symlinkError);
        if (!symlinkError) {
            conditioned["firstFrame"]=vm::Value(redirect.string());request.parameters=vm::Value(conditioned);
            check(!native::prepareSocietyVideoGeneration(request,active,options),"a redirected frame image was accepted");
        }
        generation::QueueCancellation beforeCapture;beforeCapture.request();
        conditioned["firstFrame"]=vm::Value(firstFrame.string());request.parameters=vm::Value(conditioned);
        check(!native::prepareSocietyVideoGeneration(request,beforeCapture,options),"a canceled request captured frame images");
        auto legacy=advanced;legacy.erase("firstFrame");legacy.erase("lastFrame");request.parameters=vm::Value(legacy);
        plan=native::prepareSocietyVideoGeneration(request,active,options);
        check(plan && argument(plan->arguments,"--first-frame").empty() && argument(plan->arguments,"--last-frame").empty(),
              "legacy video requests require image conditions");
        plan->staging.reset();
        auto lowVram=advanced;lowVram["offload"]=vm::Value("sequential");request.parameters=vm::Value(lowVram);
        check(!native::prepareSocietyVideoGeneration(request,active,options),"low VRAM silently overrode the requested VAE setting");
        lowVram["vaeTiling"]=vm::Value(true);request.parameters=vm::Value(lowVram);
        plan=native::prepareSocietyVideoGeneration(request,active,options);
        check(plan && std::ranges::find(plan->arguments,"--low-vram")!=plan->arguments.end(),"sequential offload did not request low VRAM mode");
        plan->staging.reset();
        auto incomplete=advanced;incomplete.erase("steps");request.parameters=vm::Value(incomplete);
        check(!native::prepareSocietyVideoGeneration(request,active,options),"an incomplete advanced video request silently used a default");
        auto opposing=advanced;opposing["camera2"]=vm::Value("pan-right");request.parameters=vm::Value(opposing);
        check(!native::prepareSocietyVideoGeneration(request,active,options),"opposing camera controls were accepted");
        advanced["unsupportedReference"]=vm::Value("example.png");request.parameters=vm::Value(advanced);
        check(!native::prepareSocietyVideoGeneration(request,active,options),"an unsupported video option was silently discarded");
        request.parameters=vm::Value(vm::Object{{"quick",vm::Value(true)},{"model",vm::Value("Checkpoint/LTX")},
            {"prompt",vm::Value("A landscape")},{"negativePrompt",vm::Value("")},{"width",vm::Value(576)},
            {"height",vm::Value(1024)},{"duration",vm::Value(3)},{"fps",vm::Value(24)},
            {"outputCount",vm::Value(2)},{"seed",vm::Value(-1)}});
        std::string video("\0\0\0\x18" "ftypisomsynthetic",21);
        video.resize(3*1024*1024+37,'x'); // Exercise streamed verification and publication.
        int calls=0;bool failSecond=false,reportSeed=true,expectImageCondition=false;std::vector<std::int64_t> requestedSeeds;
        generation::WorkerTransport worker=[&](std::string_view body,std::stop_token,const generation::WorkerReceiver &receive)
            -> ii::core::Result<ii::process::Exit> {
            ++calls;auto parsed=ii::json::parse(body);check(bool(parsed),"worker input malformed");
            std::vector<std::string> args;for (const auto &entry:vm::array(vm::get(*parsed,"arguments"))) args.push_back(vm::text(entry));
            if (expectImageCondition) {
                const auto image=argument(args,"--first-frame");
                check(!image.empty() && iiFileProvider::native::File::read(fs::path(image))=="changed after preparation",
                    "selected video reference did not reach the worker");
            }
            receive(ii::process::Stream::output,"IILD_READY {\"schema\":\"iild-worker-v1\"}\n");
            const auto output=fs::path(argument(args,"--output"));fs::create_directories(output.parent_path());
            iiFileProvider::native::File::write(output,video);
            auto report=output;report.replace_extension(".json");
            const auto width=std::stoi(argument(args,"--width")),height=std::stoi(argument(args,"--height"));
            const auto fps=std::stoi(argument(args,"--fps")),duration=std::stoi(argument(args,"--duration"));
            const auto seedArgument=argument(args,"--seed");
            const std::int64_t resolvedSeed=seedArgument.empty()?456:std::stoll(seedArgument);
            if (!seedArgument.empty()) requestedSeeds.push_back(resolvedSeed);
            vm::Object reportObject{{"schema",vm::Value("iild-temporal-video-v1")},{"status",vm::Value("complete")},
                {"video",vm::Value(vm::Object{{"codec",vm::Value("h264")},{"verified_decode",vm::Value(true)},
                    {"size",vm::Value(vm::Array{vm::Value(width),vm::Value(height)})},{"fps",vm::Value(double(fps))},
                    {"frame_count",vm::Value(fps*duration)},{"duration_seconds",vm::Value(double(duration))},
                    {"size_bytes",vm::Value(static_cast<std::int64_t>(video.size()))},{"sha256",vm::Value(digest(video))}})}};
            if (reportSeed) reportObject["shots"]=vm::Value(vm::Array{vm::Value(vm::Object{{"seed",vm::Value(resolvedSeed)}})});
            put(report,vm::Value(std::move(reportObject)));
            if (failSecond && calls==2) return ii::process::Exit{ii::process::ExitReason::exited,9,0};
            auto response=ii::json::stringify(vm::Value(vm::Object{{"schema",vm::Value("iild-worker-result-v1")},
                {"id",vm::get(*parsed,"id")},{"ok",vm::Value(true)},{"exit_code",vm::Value(0)}}));
            check(bool(response),"worker response serialization failed");
            receive(ii::process::Stream::output,"IILD_RESULT "+*response+"\n");
            return ii::process::Exit{ii::process::ExitReason::exited,0,0};
        };
        std::vector<vm::Value> outputs;
        auto outcome=native::makeSocietyVideoRunner(options,worker)(request,active,{},[&](vm::Value value){outputs.push_back(std::move(value));});
        check(outcome.state==generation::QueueState::Completed && calls==2 && outputs.size()==2 &&
            vm::text(vm::get(outputs[0],"mediaType"))=="Video" && vm::text(vm::get(outputs[0],"modelName"))=="LTX" &&
            vm::text(vm::get(outputs[0],"prompt"))=="A landscape" && vm::number(vm::get(outputs[0],"seed"),-1)==456 &&
            iiFileProvider::native::File::read(fs::path(vm::text(vm::get(outputs[0],"path"))))==video,
            "the video batch did not publish both recorded outputs");
        auto singleRequest=request;auto one=vm::object(request.parameters);one["outputCount"]=vm::Value(1);
        singleRequest.parameters=vm::Value(std::move(one));outputs.clear();calls=0;reportSeed=false;
        outcome=native::makeSocietyVideoRunner(options,worker)(singleRequest,active,{},[&](auto value){outputs.push_back(std::move(value));});
        check(outcome.state==generation::QueueState::Completed && outputs.size()==1 && !vm::has(outputs.front(),"seed"),
            "an unreported video seed was invented");
        reportSeed=true;
        auto seeded=request;auto seededValues=vm::object(request.parameters);seededValues["seed"]=vm::Value(123);
        seeded.parameters=vm::Value(seededValues);outputs.clear();calls=0;requestedSeeds.clear();
        outcome=native::makeSocietyVideoRunner(options,worker)(seeded,active,{},[&](auto value){outputs.push_back(std::move(value));});
        check(outcome.state==generation::QueueState::Completed && requestedSeeds==std::vector<std::int64_t>{123,124} &&
            outputs.size()==2 && vm::number(vm::get(outputs[0],"seed"),-1)==123 && vm::number(vm::get(outputs[1],"seed"),-1)==124,
            "a fixed seed was not incremented across video outputs");
        seededValues["seed"]=vm::Value(std::int64_t(4294967295LL));seeded.parameters=vm::Value(std::move(seededValues));
        check(!native::prepareSocietyVideoGeneration(seeded,active,options),"a video batch accepted seed overflow");
        outputs.clear();calls=0;failSecond=true;
        outcome=native::makeSocietyVideoRunner(options,worker)(request,active,{},[&](vm::Value value){outputs.push_back(std::move(value));});
        check(outcome.state==generation::QueueState::Failed && outputs.size()==1 &&
            fs::exists(fs::path(vm::text(vm::get(outputs[0],"path")))) && outcome.error.find("Recoverable generation files:")!=std::string::npos,
            "a later video failure lost a committed output or the recovery location");
        outputs.clear();calls=0;failSecond=false;generation::QueueCancellation canceled;
        outcome=native::makeSocietyVideoRunner(options,worker)(request,canceled,{},[&](vm::Value value) {
            outputs.push_back(std::move(value));canceled.request();
        });
        check(outcome.state==generation::QueueState::Canceled && calls==1 && outputs.size()==1 &&
            fs::exists(fs::path(vm::text(vm::get(outputs[0],"path")))),"video cancellation discarded a committed result or launched another worker");
        auto badPlan=native::prepareSocietyVideoGeneration(request,active,options);check(bool(badPlan),"invalid record fixture could not be prepared");
        calls=0;auto completed=generation::executeWorker(badPlan->id,badPlan->arguments,active,{},worker);
        check(completed.state==generation::QueueState::Completed,"invalid record fixture worker failed");
        badPlan->staging->workerFinished(completed);
        auto badReport=fs::path(vm::text(vm::get(badPlan->context,"output")));badReport.replace_extension(".json");
        auto record=ii::json::parse(iiFileProvider::native::File::read(badReport));check(bool(record),"fixture record unavailable");
        auto recordObject=vm::object(*record),videoObject=vm::object(vm::get(*record,"video"));
        videoObject["sha256"]=vm::Value(std::string(64,'0'));recordObject["video"]=vm::Value(std::move(videoObject));
        put(badReport,vm::Value(std::move(recordObject)));outputs.clear();
        auto rejected=native::publishSocietyVideoGeneration(*badPlan,active,options,[&](auto entry){outputs.push_back(std::move(entry));});
        check(!rejected && outputs.empty() && rejected.error().message.find("hash")!=std::string::npos,
            "a video with a mismatched record hash was published");
        const auto recovery=fs::path(vm::text(vm::get(badPlan->context,"stagingDirectory")));
        badPlan->staging.reset();check(fs::exists(recovery/"video.mp4"),"a rejected video was not retained for recovery");
        auto unreadable=native::prepareSocietyVideoGeneration(singleRequest,active,options);check(bool(unreadable),"unreadable fixture unavailable");
        calls=0;completed=generation::executeWorker(unreadable->id,unreadable->arguments,active,{},worker);
        check(completed.state==generation::QueueState::Completed,"unreadable fixture worker failed");
        const auto inaccessible=fs::path(vm::text(vm::get(unreadable->context,"stagingDirectory")));
        std::error_code permissionError;fs::permissions(inaccessible,fs::perms::none,fs::perm_options::replace,permissionError);
        if (!permissionError) {
            std::error_code scanError;fs::directory_iterator probe(inaccessible,scanError);
            if (scanError) {
                generation::QueueCompletion failed{generation::QueueState::Failed,"Worker failed"};
                unreadable->staging->workerFinished(failed);
                fs::permissions(inaccessible,fs::perms::owner_all,fs::perm_options::replace);
                unreadable->staging.reset();
                check(fs::exists(inaccessible/"video.mp4") && failed.error.find(inaccessible.string())!=std::string::npos,
                    "an unreadable recovery directory was deleted or hidden");
            } else fs::permissions(inaccessible,fs::perms::owner_all,fs::perm_options::replace);
        }
        ii::runtime::EventLoop loop;std::vector<ii::runtime::Task> tasks;
        auto shell=native::NativeApplicationShell::create(R"(<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"></svg>)");
        ii::gui::FileDialogCompletion imageCompletion;ii::gui::FileDialogOptions imageOptions;
        shell->setCanvasFilePicker([&](auto options,auto callback) -> ii::core::Result<void> {
            imageOptions=std::move(options);imageCompletion=std::move(callback);return {};
        });
        auto videoView=shell->generation(true);
        check(videoView->field("firstFrame")->canChooseFile.get(),"the video reference picker was not connected");
        check(bool(videoView->chooseReference("firstFrame")) && bool(imageCompletion) &&
              imageOptions.mode==ii::gui::FileDialogMode::open && !imageOptions.multiple,
              "video reference did not open a native image picker");
        auto canceledChoice=std::move(imageCompletion);canceledChoice(ii::gui::FileDialogSelection{true,{}});
        check(vm::text(vm::get(videoView->parameters(),"firstFrame")).empty(),"canceling the picker changed the draft");
        check(bool(videoView->chooseReference("firstFrame")),"the video picker could not be reopened");
        auto chosen=std::move(imageCompletion);chosen(ii::gui::FileDialogSelection{false,{firstFrame.string()}});
        check(vm::text(vm::get(videoView->parameters(),"firstFrame"))==firstFrame.string(),
              "the native image picker did not update the video draft");
        check(videoView->serviceStatus.get()=="Generation service unavailable",
              "choosing a frame hid the disconnected generation service");
        check(bool(videoView->chooseReference("firstFrame")),"the picker could not replace a frame");
        check(bool(videoView->clearReference("firstFrame")),"the frame condition could not be cleared");
        auto staleChoice=std::move(imageCompletion);staleChoice(ii::gui::FileDialogSelection{false,{lastFrame.string()}});
        check(vm::text(vm::get(videoView->parameters(),"firstFrame")).empty() &&
              videoView->serviceStatus.get().find("changed")!=std::string::npos,
              "a stale picker selection overwrote a changed frame condition");
        check(bool(videoView->chooseReference("firstFrame")),"the frame picker did not recover after a stale result");
        auto finalChoice=std::move(imageCompletion);finalChoice(ii::gui::FileDialogSelection{false,{firstFrame.string()}});
        check(bool(shell->setGenerationModelCatalog({{"Checkpoint/LTX","LTX",false,true,source}})),"video view catalogue failed");
        check(bool(shell->connectGenerationQueue(loop,[&](auto task){tasks.push_back(std::move(task));return true;},
            native::makeSocietyVideoRunner(options,worker),false,true)),"video view provider failed");
        check(bool(shell->navigate("video")) && bool(shell->generation(true)->edit("prompt","A landscape")) &&
            bool(shell->generation(true)->edit("width","576")) && bool(shell->generation(true)->edit("height","1024")) &&
            bool(shell->generation(true)->edit("duration","3")) && bool(shell->generation(true)->edit("outputCount","1")) &&
            bool(shell->generation(true)->submit()),"the advanced video view did not submit its request");
        check(tasks.size()==1,"the video view did not schedule its worker");
        calls=0;expectImageCondition=true;tasks.front()();(void)loop.poll();
        const auto job=shell->generationQueue()->jobs.get().front();
        if (job->state.get()!=generation::QueueState::Completed || shell->reviewSession()->totalCount()!=1)
            throw std::runtime_error("video view did not publish to its review session: "+job->error.get());
        shell->disconnectGenerationQueue();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
