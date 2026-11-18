#include "App/Native/NativeSocietyGeneration.h"
#include "App/Native/NativeGenerationMetadata.h"
#include "App/Native/NativeApplicationShell.h"
#include "App/Native/NativeSocietyModels.h"
#include <iiFileProvider>
#include <iiJson>
#include <iiVisualCodec>
#include <openssl/evp.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace dreamscapes;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void put(const fs::path &path,const vm::Value &value) {
    auto text=ii::json::stringify(value);check(bool(text),"cannot encode fixture");iiFileProvider::native::File::write(path,*text);
}
std::string argument(const std::vector<std::string> &args,const std::string &name) {
    const auto found=std::ranges::find(args,name);return found==args.end() || found+1==args.end()?std::string{}:*(found+1);
}
std::string sha256(const std::vector<std::uint8_t> &bytes) {
    unsigned char digest[EVP_MAX_MD_SIZE];unsigned int size=0;
    check(EVP_Digest(bytes.data(),bytes.size(),digest,&size,EVP_sha256(),nullptr)==1 && size==32,"cannot hash fixture");
    constexpr char hex[]="0123456789abcdef";std::string text;
    for (unsigned i=0;i<size;++i) {text.push_back(hex[digest[i]>>4]);text.push_back(hex[digest[i]&15]);}
    return text;
}
int main() {
    try {
        iiFileProvider::native::TemporaryDirectory temp(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        const auto root=temp.path()/"fixture.societycontainer",selection=temp.path()/"selection.json";
        const auto model=root/"Models"/"Checkpoint"/"model.safetensors";
        fs::create_directories(model.parent_path());fs::create_directories(root/"Generation History");
        fs::create_directories(root/"Files");fs::create_directories(root/"Asset Library");
        const std::string header=R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})";
        std::string data(8,'\0');for (unsigned i=0;i<8;++i) data[i]=char((std::uint64_t(header.size())>>(8*i))&255);data+=header;
        iiFileProvider::native::File::write(model,data);
        constexpr auto id="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        const vm::Array sections{vm::Value(vm::Object{{"id",vm::Value("models")},{"path",vm::Value("Models")}}),
            vm::Value(vm::Object{{"id",vm::Value("files")},{"path",vm::Value("Files")}}),
            vm::Value(vm::Object{{"id",vm::Value("asset-library")},{"path",vm::Value("Asset Library")}}),
            vm::Value(vm::Object{{"id",vm::Value("generation-history")},{"path",vm::Value("Generation History")}})};
        put(root/".society-drive.json",vm::Value(vm::Object{{"type",vm::Value("SocietyDrive")},{"schemaVersion",vm::Value(1)},
            {"identifier",vm::Value(id)},{"replicaReady",vm::Value(true)},{"sections",vm::Value(sections)}}));
        put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value(id)}}));
        auto listing=native::browseSocietyModels(selection);check(listing && vm::array(listing->aliases).size()==1,"fixture model unavailable");
        auto source=vm::get(vm::array(listing->aliases).front(),"source");
        vm::Value parameters(vm::Object{{"quick",vm::Value(true)},{"model",vm::Value("Checkpoint/model.safetensors")},
            {"prompt",vm::Value("한글 $(not a shell)")},{"negativePrompt",vm::Value("blur")},{"width",vm::Value(512)},
            {"height",vm::Value(768)},{"outputCount",vm::Value(2)},{"seed",vm::Value(-1)}});
        generation::QueueRequest request{"Image","Checkpoint/model.safetensors",parameters,"Model",source};
        generation::QueueCancellation cancellation;
        const native::SocietyGenerationOptions options{selection,{},temp.path()};
        auto plan=native::prepareSocietyGeneration(request,cancellation,options);
        check(bool(plan),plan?"":"quick generation preparation failed");
        const auto legacy=model.parent_path()/"legacy.safetensors.index.json";
        iiFileProvider::native::File::write(legacy,std::string("\x80\x02\x8a\x0a\x6c\xfc\x9c\x46\xf9\x20\x6a\xa8\x50\x19\x2e",15)+"fixture");
        const auto legacyListing=native::browseSocietyModels(selection);check(bool(legacyListing),"legacy model inventory failed");
        vm::Value legacySource;
        for (const auto &entry:vm::array(legacyListing->aliases))
            if (vm::text(vm::get(entry,"id"))=="Checkpoint/legacy.safetensors.index.json") legacySource=vm::get(entry,"source");
        auto legacyParameters=vm::object(parameters);legacyParameters["model"]=vm::Value("Checkpoint/legacy.safetensors.index.json");
        generation::QueueRequest legacyRequest{"Image","Checkpoint/legacy.safetensors.index.json",vm::Value(std::move(legacyParameters)),"Legacy",legacySource};
        auto legacyPlan=native::prepareSocietyGeneration(legacyRequest,cancellation,options);
        check(legacyPlan && argument(legacyPlan->arguments,"--model-path")==legacy.string(),
            "selected PyTorch checkpoint was not passed to the image worker");
        legacyPlan->staging.reset();
        const auto split=model.parent_path()/"split-pytorch";fs::create_directories(split);
        const auto legacyBytes=std::string("\x80\x02\x8a\x0a\x6c\xfc\x9c\x46\xf9\x20\x6a\xa8\x50\x19\x2e",15)+"fixture";
        iiFileProvider::native::File::write(split/"first.bin",legacyBytes);
        iiFileProvider::native::File::write(split/"second.bin",legacyBytes);
        iiFileProvider::native::File::write(split/"weights.json",R"({"weight_map":{"first":"first.bin","second":"second.bin"}})");
        const auto splitListing=native::browseSocietyModels(selection);check(bool(splitListing),"PyTorch index inventory failed");
        vm::Value splitSource;
        for (const auto &entry:vm::array(splitListing->aliases))
            if (vm::text(vm::get(entry,"id"))=="Checkpoint/split-pytorch/weights.json") splitSource=vm::get(entry,"source");
        auto splitParameters=vm::object(parameters);splitParameters["model"]=vm::Value("Checkpoint/split-pytorch/weights.json");
        generation::QueueRequest splitRequest{"Image","Checkpoint/split-pytorch/weights.json",vm::Value(std::move(splitParameters)),"Split",splitSource};
        auto splitPlan=native::prepareSocietyGeneration(splitRequest,cancellation,options);
        check(splitPlan && argument(splitPlan->arguments,"--model-path")== (split/"weights.json").string(),
            "selected PyTorch index was not passed to the image worker");
        splitPlan->staging.reset();
        iiFileProvider::native::File::write(split/"second.bin",legacyBytes+"changed");
        check(!native::prepareSocietyGeneration(splitRequest,cancellation,options),"changed PyTorch shard was accepted for generation");
        check(argument(plan->arguments,"--model-path")==model.string() && argument(plan->arguments,"--prompt")=="한글 $(not a shell)" &&
            argument(plan->arguments,"--negative-prompt")=="blur" && argument(plan->arguments,"--width")=="512" &&
            argument(plan->arguments,"--height")=="768" && argument(plan->arguments,"--num-images")=="2" &&
            argument(plan->arguments,"--seed").empty(),"quick request lost its selected model or generation parameters");
        check(std::ranges::find(plan->arguments,"--hires-fix")!=plan->arguments.end(),
            "quick generation did not explicitly enable the SDK's Hires default");
        for (const auto &[setting, flag] : {
                std::pair{vm::Value(false), "--no-hires-fix"},
                std::pair{vm::Value(true), "--hires-fix"},
                std::pair{vm::Value(), "--hires-fix"}}) {
            auto savedQuick=vm::object(parameters);savedQuick["hiresFix"]=setting;
            request.parameters=vm::Value(std::move(savedQuick));
            auto savedPlan=native::prepareSocietyGeneration(request,cancellation,options);
            check(savedPlan && std::ranges::find(savedPlan->arguments,flag)!=savedPlan->arguments.end(),
                "quick generation did not preserve its typed Hires setting");
            savedPlan->staging.reset();
        }
        for (const auto &invalid : {vm::Value(0), vm::Value("false")}) {
            auto savedQuick=vm::object(parameters);savedQuick["hiresFix"]=invalid;
            request.parameters=vm::Value(std::move(savedQuick));
            check(!native::prepareSocietyGeneration(request,cancellation,options),
                "a non-boolean quick Hires value was accepted");
        }
        const auto staged=fs::path(vm::text(vm::get(plan->context,"stagingDirectory")));
        check(!staged.empty() && fs::is_directory(staged) && !fs::exists(staged/"output"),"worker staging was not private and empty");
        plan->staging.reset();check(!fs::exists(staged),"unused private staging was not removed");
        auto missing=vm::object(parameters);missing.erase("width");request.parameters=vm::Value(std::move(missing));
        auto incomplete=native::prepareSocietyGeneration(request,cancellation,options);
        check(!incomplete && incomplete.error().message.find("width")!=std::string::npos,
            "an incomplete quick request silently used a default dimension");
        auto advanced=vm::object(parameters);advanced.erase("quick");advanced["steps"]=vm::Value(12);
        advanced["cfgScale"]=vm::Value(3.0);advanced["seed"]=vm::Value(42);
        request.parameters=vm::Value(std::move(advanced));
        auto explicitPlan=native::prepareSocietyGeneration(request,cancellation,options);
        check(explicitPlan && argument(explicitPlan->arguments,"--steps")=="12" &&
            argument(explicitPlan->arguments,"--guidance-scale")=="3" && argument(explicitPlan->arguments,"--seed")=="42" &&
            argument(explicitPlan->arguments,"--hires-upscaler")=="lanczos" &&
            argument(explicitPlan->arguments,"--hires-strength")=="0.25",
            "explicit sampling settings were not passed to the SDK worker");
        explicitPlan->staging.reset();
        auto nullHires=vm::object(parameters);nullHires.erase("quick");nullHires["hiresFix"]=vm::Value();
        request.parameters=vm::Value(nullHires);
        auto nullPlan=native::prepareSocietyGeneration(request,cancellation,options);
        check(nullPlan && std::ranges::find(nullPlan->arguments,"--hires-fix")!=nullPlan->arguments.end(),
            "an unset Hires value disabled the default");
        nullPlan->staging.reset();
        for (const auto &invalid : {vm::Value(0), vm::Value("false")}) {
            auto malformed=vm::object(parameters);malformed.erase("quick");malformed["hiresFix"]=invalid;
            request.parameters=vm::Value(std::move(malformed));
            check(!native::prepareSocietyGeneration(request,cancellation,options),
                "a non-boolean Hires value was accepted");
        }
        auto large=vm::object(parameters);large["width"]=vm::Value(2304);large["height"]=vm::Value(4096);
        request.parameters=vm::Value(std::move(large));
        auto largePlan=native::prepareSocietyGeneration(request,cancellation,options);
        check(largePlan && argument(largePlan->arguments,"--width")=="2304" &&
            argument(largePlan->arguments,"--height")=="4096","4K resolution did not reach the worker");
        largePlan->staging.reset();
        auto disabled=vm::object(parameters);disabled.erase("quick");disabled["hiresFix"]=vm::Value(false);
        request.parameters=vm::Value(std::move(disabled));
        auto disabledPlan=native::prepareSocietyGeneration(request,cancellation,options);
        check(disabledPlan && std::ranges::find(disabledPlan->arguments,"--no-hires-fix")!=disabledPlan->arguments.end(),
            "explicitly disabled Hires was not passed to the worker");
        disabledPlan->staging.reset();
        auto bad=vm::object(parameters);bad["transparentBackground"]=vm::Value(true);bad["quick"]=vm::Value(false);
        request.parameters=vm::Value(std::move(bad));
        auto unsupported=native::prepareSocietyGeneration(request,cancellation,options);
        check(!unsupported && unsupported.error().message.find("transparentBackground")!=std::string::npos,
            "unsupported advanced setting was silently ignored");
        auto overflow=vm::object(parameters);overflow["seed"]=vm::Value(std::int64_t(4294967295LL));
        request.parameters=vm::Value(std::move(overflow));
        check(!native::prepareSocietyGeneration(request,cancellation,options),"an image batch accepted seed overflow");
        auto single=vm::object(parameters);single["outputCount"]=vm::Value(1);request.parameters=vm::Value(single);
        auto makeOutput=[&](generation::WorkerJobPlan &job,bool corrupt) {
            const auto output=fs::path(vm::text(vm::get(job.context,"outputDirectory")));fs::create_directories(output);
            std::vector<std::uint8_t> pixels(512*768*4,255);
            ii::visual::image::ImageView view{512,768,ii::visual::image::PixelFormat::rgba8,0,pixels};
            auto png=ii::visual::image::encode_png(view);check(bool(png),"cannot encode fixture image");
            vm::Array outputs,images;
            const auto count=int(vm::number(vm::get(vm::get(job.context,"parameters"),"outputCount")));
            for (int index=0;index<count;++index) {
                std::ostringstream name;name<<"image-"<<std::setfill('0')<<std::setw(4)<<(index+1)<<".png";
                const auto file=output/name.str();iiFileProvider::native::File::write(file,std::as_bytes(std::span(*png)));
                outputs.emplace_back(vm::Object{{"path",vm::Value(file.string())},
                    {"sha256",vm::Value(corrupt?std::string(64,'0'):sha256(*png))}});
                images.emplace_back(vm::Object{{"seed",vm::Value(42+index)}});
            }
            put(output/"generation.json",vm::Value(vm::Object{{"schema",vm::Value("iild-standalone-image-v1")},
                {"status",vm::Value("complete")},{"outputs",vm::Value(std::move(outputs))},{"images",vm::Value(std::move(images))}}));
        };
        auto outputPlan=native::prepareSocietyGeneration(request,cancellation,options);check(bool(outputPlan),"output fixture preparation failed");
        makeOutput(*outputPlan,false);generation::QueueCompletion finished{generation::QueueState::Completed,{}};
        outputPlan->staging->workerFinished(finished);std::vector<vm::Value> published;
        auto saved=native::publishSocietyGeneration(*outputPlan,cancellation,options,[&](vm::Value value){published.push_back(std::move(value));});
        check(saved && published.size()==1,"verified image was not published");
        const auto savedPath=fs::path(vm::text(vm::get(published.front(),"path")));
        const auto savedBytes=iiFileProvider::native::File::read(savedPath,64*1024*1024);
        check(savedPath.parent_path()==root/"Generation History" && savedBytes.find("Dreamscapes.Parameters")!=std::string::npos &&
            savedBytes.find("\"resolvedSeed\":42")!=std::string::npos &&
            vm::text(vm::get(published.front(),"containerId"))==id && vm::text(vm::get(published.front(),"modelName"))=="Model" &&
            vm::text(vm::get(published.front(),"prompt"))=="한글 $(not a shell)" && vm::number(vm::get(published.front(),"seed"),-1)==42 &&
            vm::number(vm::get(published.front(),"width"))==512 && vm::number(vm::get(published.front(),"height"))==768,
            "published image lost its proven metadata or Society identity");
        auto decoded=ii::visual::image::decode(std::span(reinterpret_cast<const std::uint8_t *>(savedBytes.data()),savedBytes.size()));
        check(decoded && decoded->width==512 && decoded->height==768,"published image is not a readable image of the requested size");
        auto noSeedPlan=native::prepareSocietyGeneration(request,cancellation,options);check(bool(noSeedPlan),"seedless fixture preparation failed");
        makeOutput(*noSeedPlan,false);noSeedPlan->staging->workerFinished(finished);
        const auto seedlessRecord=fs::path(vm::text(vm::get(noSeedPlan->context,"outputDirectory")))/"generation.json";
        auto parsedSeedless=ii::json::parse(iiFileProvider::native::File::read(seedlessRecord));check(bool(parsedSeedless),"seedless record unavailable");
        auto seedlessObject=vm::object(*parsedSeedless);seedlessObject.erase("images");put(seedlessRecord,vm::Value(std::move(seedlessObject)));
        std::vector<vm::Value> seedless;
        check(bool(native::publishSocietyGeneration(*noSeedPlan,cancellation,options,[&](auto value){seedless.push_back(std::move(value));})) &&
            seedless.size()==1 && !vm::has(seedless.front(),"seed") && !vm::has(seedless.front(),"steps"),
            "an unreported seed or quick-generation step count was invented");
        auto genericPlan=native::prepareSocietyGeneration(request,cancellation,options);
        check(bool(genericPlan),"generic output fixture preparation failed");
        makeOutput(*genericPlan,false);genericPlan->staging->workerFinished(finished);
        const auto genericRecord=fs::path(vm::text(vm::get(genericPlan->context,"outputDirectory")))/"generation.json";
        auto parsedGeneric=ii::json::parse(iiFileProvider::native::File::read(genericRecord));
        check(bool(parsedGeneric),"generic generation record unavailable");
        auto genericObject=vm::object(*parsedGeneric);genericObject.erase("schema");genericObject.erase("status");
        genericObject.erase("images");genericObject["schema_version"]=vm::Value(1);
        genericObject["request"]=vm::Value(vm::Object{{"runner",vm::Value("generic-diffusers")}});
        put(genericRecord,vm::Value(std::move(genericObject)));
        std::vector<vm::Value> genericOutputs;
        check(bool(native::publishSocietyGeneration(*genericPlan,cancellation,options,[&](auto value){genericOutputs.push_back(std::move(value));})) &&
            genericOutputs.size()==1 && !vm::has(genericOutputs.front(),"seed"),
            "generic Diffusers images were not published with evidence-based metadata");
        request.parameters=parameters;
        auto boundaryPlan=native::prepareSocietyGeneration(request,cancellation,options);check(bool(boundaryPlan),"seed boundary fixture preparation failed");
        makeOutput(*boundaryPlan,false);boundaryPlan->staging->workerFinished(finished);
        const auto boundaryRecord=fs::path(vm::text(vm::get(boundaryPlan->context,"outputDirectory")))/"generation.json";
        auto boundaryJson=ii::json::parse(iiFileProvider::native::File::read(boundaryRecord));check(bool(boundaryJson),"seed boundary record unavailable");
        auto boundaryObject=vm::object(*boundaryJson);vm::Array boundaryImages{
            vm::Value(vm::Object{{"seed",vm::Value(std::int64_t(4294967295LL))}}),
            vm::Value(vm::Object{{"seed",vm::Value(std::int64_t(4294967296LL))}})};
        boundaryObject["images"]=vm::Value(std::move(boundaryImages));put(boundaryRecord,vm::Value(std::move(boundaryObject)));
        std::vector<vm::Value> boundaryOutputs;
        check(bool(native::publishSocietyGeneration(*boundaryPlan,cancellation,options,[&](auto value){boundaryOutputs.push_back(std::move(value));})) &&
            boundaryOutputs.size()==2 && vm::number(vm::get(boundaryOutputs.back(),"seed"),-1)==4294967296.0,
            "a valid worker seed above the 32-bit input range discarded generated images");
        request.parameters=vm::Value(single);
        auto replaced=native::addGenerationRecipe(std::span(reinterpret_cast<const std::uint8_t *>(savedBytes.data()),savedBytes.size()),"{\"updated\":true}");
        check(bool(replaced),"existing PNG metadata could not be updated");
        const std::string replacedBytes(replaced->begin(),replaced->end());
        const auto firstKeyword=replacedBytes.find("Dreamscapes.Parameters");
        check(firstKeyword!=std::string::npos && replacedBytes.find("Dreamscapes.Parameters",firstKeyword+1)==std::string::npos &&
            replacedBytes.find("\"updated\":true")!=std::string::npos && replacedBytes.find("resolvedSeed")==std::string::npos,
            "PNG recipe update left conflicting metadata");
        (*replaced)[20]^=1;
        check(!native::addGenerationRecipe(*replaced,"{}"),"a corrupted PNG checksum was accepted");
        auto canceledPlan=native::prepareSocietyGeneration(request,cancellation,options);check(bool(canceledPlan),"canceled fixture preparation failed");
        makeOutput(*canceledPlan,false);
        const auto canceledRecovery=fs::path(vm::text(vm::get(canceledPlan->context,"stagingDirectory")));
        generation::QueueCompletion canceledOutput{generation::QueueState::Canceled,{}};
        canceledPlan->staging->workerFinished(canceledOutput);canceledPlan->staging.reset();
        check(canceledOutput.error.find(canceledRecovery.string())!=std::string::npos && fs::exists(canceledRecovery/"output"/"image-0001.png"),
            "cancellation did not retain and identify generated files");
        generation::WorkerTransport fakeWorker=[&](std::string_view body,std::stop_token,const generation::WorkerReceiver &receive) -> ii::core::Result<ii::process::Exit> {
            auto input=ii::json::parse(body);check(bool(input),"worker request was invalid");
            std::vector<std::string> args;for (const auto &value:vm::array(vm::get(*input,"arguments"))) args.push_back(vm::text(value));
            generation::WorkerJobPlan fixture{"",{},vm::Value(vm::Object{{"outputDirectory",vm::Value(argument(args,"--output-dir"))},
                {"parameters",request.parameters}}),{}};
            receive(ii::process::Stream::output,"IILD_READY {\"schema\":\"iild-worker-v1\"}\n");makeOutput(fixture,false);
            auto response=ii::json::stringify(vm::Value(vm::Object{{"schema",vm::Value("iild-worker-result-v1")},
                {"id",vm::get(*input,"id")},{"ok",vm::Value(true)},{"exit_code",vm::Value(0)}}));
            check(bool(response),"worker response could not be encoded");
            receive(ii::process::Stream::output,"IILD_RESULT "+*response+"\n");
            return ii::process::Exit{ii::process::ExitReason::exited,0,0};
        };
        published.clear();auto runner=generation::makeWorkerGenerationRunner(native::societyGenerationServices(options,fakeWorker));
        auto generated=runner(request,cancellation,{},[&](vm::Value value){published.push_back(std::move(value));});
        check(generated.state==generation::QueueState::Completed && published.size()==1 &&
            fs::exists(fs::path(vm::text(vm::get(published.front(),"path")))),
            "Society generation service did not connect preparation, worker and publication");
        ii::runtime::EventLoop eventLoop;std::vector<ii::runtime::Task> tasks,storageTasks;
        auto shell=native::NativeApplicationShell::create(R"(<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"></svg>)");
        check(bool(shell->connectSocietyStorage(eventLoop,[&](auto task){storageTasks.push_back(std::move(task));return true;},selection)),
            "Society storage was not connected");
        auto drainStorage=[&] {
            for (int count=0;!storageTasks.empty() && count<20;++count) {
                auto task=std::move(storageTasks.front());storageTasks.erase(storageTasks.begin());task();(void)eventLoop.poll();
            }
            check(storageTasks.empty(),"storage refresh failed to settle");
        };
        drainStorage();const auto historyBefore=shell->historySession()->totalCount();
        check(historyBefore>=2,"existing generation history was not loaded");
        check(bool(shell->setGenerationModelCatalog({{"Checkpoint/model.safetensors","Model",true,false,source}})),"fixture catalogue was not installed");
        check(bool(shell->connectGenerationQueue(eventLoop,[&](auto task){tasks.push_back(std::move(task));return true;},
            generation::makeWorkerGenerationRunner(native::societyGenerationServices(options,fakeWorker)),true,false)),"Society generation service was not connected");
        check(bool(shell->navigate("image")) && bool(shell->generation(false)->edit("prompt","A landscape")) &&
            bool(shell->generation(false)->edit("width","512")) && bool(shell->generation(false)->edit("height","768")) &&
            bool(shell->generation(false)->edit("outputCount","1")) && bool(shell->generation(false)->submit()),
            "the advanced view could not submit to the Society generation service");
        check(tasks.size()==1,"the connected service did not schedule a worker");tasks.front()();(void)eventLoop.poll();
        const auto job=shell->generationQueue()->jobs.get().front();
        if (job->state.get()!=generation::QueueState::Completed || shell->reviewSession()->totalCount()!=1)
            throw std::runtime_error("the view-to-Society generation path did not publish a result: "+job->error.get());
        drainStorage();
        check(shell->historySession()->totalCount()==historyBefore+1,
            "a committed generation did not refresh the Society history projection");
        shell->disconnectGenerationQueue();
        shell->disconnectSocietyStorage();
        auto corruptPlan=native::prepareSocietyGeneration(request,cancellation,options);check(bool(corruptPlan),"corrupt output fixture preparation failed");
        makeOutput(*corruptPlan,true);const auto recovery=fs::path(vm::text(vm::get(corruptPlan->context,"stagingDirectory")));
        corruptPlan->staging->workerFinished(finished);published.clear();
        auto rejected=native::publishSocietyGeneration(*corruptPlan,cancellation,options,[&](vm::Value value){published.push_back(std::move(value));});
        check(!rejected && published.empty() && rejected.error().message.find("hash")!=std::string::npos,
            "mismatched output hash was published");
        corruptPlan->staging.reset();check(fs::exists(recovery/"output"/"image-0001.png"),"a failed publication discarded generated output");
        request.parameters=parameters;
        auto partialPlan=native::prepareSocietyGeneration(request,cancellation,options);check(bool(partialPlan),"batch fixture preparation failed");
        makeOutput(*partialPlan,false);partialPlan->staging->workerFinished(finished);
        auto base=partialPlan->id;if (!base.empty() && base.front()=='.') base.erase(base.begin());
        const auto occupied=root/"Generation History"/(base+"-0002.png");published.clear();
        auto partial=native::publishSocietyGeneration(*partialPlan,cancellation,options,[&](vm::Value value) {
            published.push_back(std::move(value));iiFileProvider::native::File::write(occupied,"existing user file");
        });
        check(!partial && published.size()==1 && fs::exists(fs::path(vm::text(vm::get(published.front(),"path")))) &&
            iiFileProvider::native::File::read(occupied)=="existing user file", "partial publication overwrote a file or lost a committed image");
        const auto partialRecovery=fs::path(vm::text(vm::get(partialPlan->context,"stagingDirectory")));
        partialPlan->staging.reset();check(fs::exists(partialRecovery/"output"/"image-0002.png"),"partial failure discarded uncommitted output");
        auto badPngPlan=native::prepareSocietyGeneration(request,cancellation,options);check(bool(badPngPlan),"PNG fixture preparation failed");
        makeOutput(*badPngPlan,false);badPngPlan->staging->workerFinished(finished);
        const auto badDirectory=fs::path(vm::text(vm::get(badPngPlan->context,"outputDirectory")));
        auto badBytes=iiFileProvider::native::File::read(badDirectory/"image-0002.png");badBytes.back()^=1;
        iiFileProvider::native::File::write(badDirectory/"image-0002.png",badBytes);
        auto badRecord=ii::json::parse(iiFileProvider::native::File::read(badDirectory/"generation.json"));check(bool(badRecord),"bad PNG record unavailable");
        auto recordObject=vm::object(*badRecord);auto outputRecords=vm::array(vm::get(*badRecord,"outputs"));
        auto second=vm::object(outputRecords[1]);second["sha256"]=vm::Value(sha256(std::vector<std::uint8_t>(badBytes.begin(),badBytes.end())));
        outputRecords[1]=vm::Value(std::move(second));recordObject["outputs"]=vm::Value(std::move(outputRecords));
        put(badDirectory/"generation.json",vm::Value(std::move(recordObject)));published.clear();
        auto badPng=native::publishSocietyGeneration(*badPngPlan,cancellation,options,[&](vm::Value value){published.push_back(std::move(value));});
        check(!badPng && published.empty(),"an invalid PNG was not rejected before publishing the batch");
        auto tampered=native::prepareSocietyGeneration(request,cancellation,options);check(bool(tampered),"context fixture preparation failed");
        makeOutput(*tampered,false);tampered->staging->workerFinished(finished);
        auto changedContext=vm::object(tampered->context);auto changedParameters=vm::object(vm::get(tampered->context,"parameters"));
        changedParameters["prompt"]=vm::Value("different submitted prompt");changedContext["parameters"]=vm::Value(std::move(changedParameters));
        tampered->context=vm::Value(std::move(changedContext));published.clear();
        check(!native::publishSocietyGeneration(*tampered,cancellation,options,[&](vm::Value value){published.push_back(std::move(value));}) && published.empty(),
            "modified generation context was published under another request's identity");
        request.parameters=parameters;request.model="other";
        check(!native::prepareSocietyGeneration(request,cancellation,options),"mismatched model reference was accepted");
        request.model="Checkpoint/model.safetensors";iiFileProvider::native::File::write(model,"changed");
        check(!native::prepareSocietyGeneration(request,cancellation,options),"changed selected model was accepted");
        cancellation.request();
        auto canceled=native::prepareSocietyGeneration(request,cancellation,options);
        check(!canceled && canceled.error().code==ii::core::ErrorCode::cancelled,"canceled generation prepared work");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
