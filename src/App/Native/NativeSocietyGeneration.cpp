#include "NativeSocietyGeneration.h"
#include "NativeGenerationMetadata.h"
#include "NativeGenerationStaging.h"
#include "NativeSocietyModels.h"
#include <Generation/ImageParameters.hpp>
#include <iiFileProvider>
#include <iiJson>
#include <iiVisualCodec>
#include <iiSDKCore>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <type_traits>

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
using detail::GenerationStaging;
std::optional<std::int64_t> integer(const vm::Value &value) {
    if (const auto *v=std::get_if<std::int64_t>(&value.storage())) return *v;
    if (const auto *v=std::get_if<std::uint64_t>(&value.storage());v && *v<=std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        return static_cast<std::int64_t>(*v);
    return {};
}
std::optional<std::string> hashBytes(std::string_view bytes) {
    std::array<unsigned char,EVP_MAX_MD_SIZE> digest{};unsigned int length=0;
    if (EVP_Digest(bytes.data(),bytes.size(),digest.data(),&length,EVP_sha256(),nullptr)!=1 || length!=32) return {};
    constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for (unsigned i=0;i<length;++i) {result.push_back(hex[digest[i]>>4]);result.push_back(hex[digest[i]&15]);}
    return result;
}
std::string real(double value) {
    char buffer[64];const auto result=std::to_chars(buffer,buffer+sizeof(buffer),value,
        std::chars_format::general,std::numeric_limits<double>::max_digits10);
    return result.ec==std::errc{}?std::string(buffer,result.ptr):std::string{};
}
ii::core::Result<iiLocalDiffusion::ImageParameters> imageParameters(const generation::QueueRequest &request,bool quick) {
    auto values=iiLocalDiffusion::ImageParameters::defaults();
    if (request.parameters.kind()!=ii::core::ValueKind::object)
        return ii::core::fail(ErrorCode::invalid_argument,"Image generation parameters are missing");
    static const std::set<std::string,std::less<>> quickFields{
        "prompt","negativePrompt","model","width","height","outputCount","seed"};
    static const std::set<std::string,std::less<>> implemented{
        "prompt","negativePrompt","model","width","height","outputCount","seed","steps","cfgScale",
        "hiresFix","denoiseStrength","upscaler"};
    if (quick) for (const auto &key:quickFields) if (!vm::has(request.parameters,key))
        return ii::core::fail(ErrorCode::invalid_argument,"Missing quick generation parameter: "+key);
    for (const auto &[key,input]:vm::object(request.parameters)) {
        if (key=="quick") {
            if (input.kind()!=ii::core::ValueKind::boolean) return ii::core::fail(ErrorCode::invalid_argument,"Invalid quick generation flag");
            continue;
        }
        auto found=values.values.find(key);
        // A restored quick request may carry an explicit HiRes preference,
        // although it does not need to provide that optional field.
        if (found==values.values.end() || (quick && !quickFields.contains(key) && key!="hiresFix"))
            return ii::core::fail(ErrorCode::unsupported,"Unsupported image generation parameter: "+key);
        if (key=="hiresFix" && input.kind()==ii::core::ValueKind::null) continue;
        bool accepted=false;
        std::visit([&](auto &current) {
            using T=std::decay_t<decltype(current)>;
            if (const auto *value=std::get_if<T>(&input.storage())) {current=*value;accepted=true;}
        },found->second);
        if (!accepted) return ii::core::fail(ErrorCode::invalid_argument,"Invalid image generation parameter: "+key);
    }
    if (std::get<std::string>(values.values.at("model"))!=request.model)
        return ii::core::fail(ErrorCode::invalid_argument,"The submitted model does not match its captured identity");
    const auto issues=iiLocalDiffusion::validateImageParameters(values,true);
    if (!issues.empty()) return ii::core::fail(ErrorCode::invalid_argument,issues.front().field+": "+issues.front().message);
    const auto capability=iiLocalDiffusion::nativeParameterIssues(values,true);
    if (!capability.empty()) return ii::core::fail(ErrorCode::unsupported,capability.front().field+": "+capability.front().message);
    const auto defaults=iiLocalDiffusion::ImageParameters::defaults();
    for (const auto &[key,value]:values.values) if (!implemented.contains(key) && value!=defaults.values.at(key))
        return ii::core::fail(ErrorCode::unsupported,"Unsupported image generation parameter: "+key);
    return values;
}
}
ii::core::Result<generation::WorkerJobPlan> prepareSocietyGeneration(const generation::QueueRequest &request,
    const generation::QueueCancellation &cancellation,const SocietyGenerationOptions &options) {
    if (cancellation.load()) return ii::core::fail(ErrorCode::cancelled,"Generation was canceled");
    if (request.media!="Image") return ii::core::fail(ErrorCode::unsupported,"This worker adapter has not prepared video requests yet");
    if (request.model.empty() || vm::text(vm::get(request.modelSource,"id"))!=request.model)
        return ii::core::fail(ErrorCode::invalid_argument,"The selected model identity is unavailable");
    const bool quick=vm::boolean(vm::get(request.parameters,"quick"));
    auto values=imageParameters(request,quick);
    if (!values) return std::unexpected(values.error());
    auto model=resolveSocietyModel(options.storageReader(),request.modelSource,[&] {return cancellation.load();});
    if (!model) return std::unexpected(model.error());
    if (cancellation.load()) return ii::core::fail(ErrorCode::cancelled,"Generation was canceled");
    try {
        std::error_code error;
        const auto parent=std::filesystem::canonical(options.stagingRoot,error);
        if (error || !options.stagingRoot.is_absolute() || parent!=options.stagingRoot.lexically_normal() || !std::filesystem::is_directory(parent))
            return ii::core::fail(ErrorCode::invalid_argument,"Generation staging root must be an existing, unredirected absolute directory");
        auto staging=std::make_shared<GenerationStaging>(parent);
        if (!staging->directory.isValid()) return ii::core::fail(ErrorCode::failed,"Cannot create private generation staging");
        const auto directory=staging->directory.path(),output=directory/"output",cache=directory/"cache";
        auto path=ii::core::path_to_utf8(*model),out=ii::core::path_to_utf8(output),cachePath=ii::core::path_to_utf8(cache),
             stagePath=ii::core::path_to_utf8(directory),identifier=ii::core::path_to_utf8(directory.filename());
        if (!path || !out || !cachePath || !stagePath || !identifier)
            return ii::core::fail(ErrorCode::invalid_argument,"Generation paths must be valid UTF-8");
        auto text=[&](const char *key) -> const std::string & {return std::get<std::string>(values->values.at(key));};
        auto number=[&](const char *key) {return std::get<std::int64_t>(values->values.at(key));};
        std::vector<std::string> arguments{"--model-path",*path,"--prompt",text("prompt"),"--negative-prompt",text("negativePrompt"),
            "--width",std::to_string(number("width")),"--height",std::to_string(number("height")),
            "--num-images",std::to_string(number("outputCount")),"--device","auto","--output-dir",*out,
            "--cache-dir",*cachePath,"--no-default-modifiers"};
        if (number("seed")>=0) {arguments.emplace_back("--seed");arguments.push_back(std::to_string(number("seed")));}
        const auto hires=std::get<bool>(values->values.at("hiresFix"));
        arguments.emplace_back(hires?"--hires-fix":"--no-hires-fix");
        if (!quick) {
            arguments.emplace_back("--steps");arguments.push_back(std::to_string(number("steps")));
            arguments.emplace_back("--guidance-scale");arguments.push_back(real(std::get<double>(values->values.at("cfgScale"))));
            if (hires) {
                arguments.emplace_back("--hires-upscaler");arguments.push_back(std::get<std::string>(values->values.at("upscaler")));
                arguments.emplace_back("--hires-strength");arguments.push_back(real(std::get<double>(values->values.at("denoiseStrength"))));
            }
        }
        auto selection=ii::core::path_to_utf8(options.selection),root=ii::core::path_to_utf8(options.explicitRoot);
        if (!selection || !root) return ii::core::fail(ErrorCode::invalid_argument,"Society paths must be valid UTF-8");
        vm::Object context{{"stagingDirectory",vm::Value(*stagePath)},{"outputDirectory",vm::Value(*out)},
            {"selection",vm::Value(*selection)},{"explicitRoot",vm::Value(*root)},
            {"modelSource",request.modelSource},{"modelLabel",vm::Value(request.modelLabel.empty()?request.model:request.modelLabel)},
            {"parameters",request.parameters},{"media",vm::Value(request.media)}};
        auto captured=vm::Value(std::move(context));staging->context=captured;
        return generation::WorkerJobPlan{*identifier,std::move(arguments),std::move(captured),std::move(staging)};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot prepare private generation staging",std::current_exception());}
}

ii::core::Result<void> publishSocietyGeneration(const generation::WorkerJobPlan &plan,
    const generation::QueueCancellation &cancellation,const SocietyGenerationOptions &options,
    const generation::QueueOutputCallback &emit) {
    namespace fs=std::filesystem;
    auto staging=std::dynamic_pointer_cast<GenerationStaging>(plan.staging);
    if (!staging || !emit) return ii::core::fail(ErrorCode::invalid_argument,"Generation staging or output callback is unavailable");
    const auto stageText=ii::core::path_to_utf8(staging->path),selection=ii::core::path_to_utf8(options.selection),
        explicitRoot=ii::core::path_to_utf8(options.explicitRoot),identifier=ii::core::path_to_utf8(staging->path.filename());
    if (!stageText || !selection || !explicitRoot || !identifier || *identifier!=plan.id || plan.context!=staging->context ||
        vm::text(vm::get(plan.context,"stagingDirectory"))!=*stageText ||
        vm::text(vm::get(plan.context,"selection"))!=*selection || vm::text(vm::get(plan.context,"explicitRoot"))!=*explicitRoot ||
        vm::text(vm::get(plan.context,"media"))!="Image")
        return ii::core::fail(ErrorCode::invalid_argument,"Generation staging does not match its request");
    const auto output=staging->path/"output";
    std::error_code fsError;
    if (fs::exists(output,fsError) && !fsError) staging->preserve=true;
    const auto fail=[&](ErrorCode code,std::string message) -> ii::core::Result<void> {
        if (staging->preserve) message+="\nRecoverable generation files: "+*stageText;
        return ii::core::fail(code,std::move(message));
    };
    try {
        if (cancellation.load()) return fail(ErrorCode::cancelled,"Generation was canceled");
        const auto source=vm::get(plan.context,"modelSource"),parameters=vm::get(plan.context,"parameters");
        const auto width=integer(vm::get(parameters,"width")),height=integer(vm::get(parameters,"height")),
                   count=integer(vm::get(parameters,"outputCount"));
        if (!width || !height || !count || *width<1 || *width>4096 || *height<1 || *height>4096 || *count<1 || *count>1000 ||
            vm::text(vm::get(parameters,"model"))!=vm::text(vm::get(source,"id")) || vm::text(vm::get(plan.context,"outputDirectory"))!=
            ii::core::path_to_utf8(output).value_or(std::string{}))
            return fail(ErrorCode::invalid_argument,"Generation output parameters are invalid");
        const auto read=options.storageReader();
        const auto canceled=[&]{return cancellation.load();};
        const auto verify=[&]() -> ii::core::Result<SocietyListing> {
            auto model=resolveSocietyModel(read,source,canceled);
            if (!model) return std::unexpected(model.error());
            auto history=read({"generation-history","","",SocietyListingKind::section},canceled);
            if (!history) return std::unexpected(history.error());
            auto root=ii::core::path_to_utf8(history->root);
            if (!root || *root!=vm::text(vm::get(source,"root")) || history->containerId!=vm::text(vm::get(source,"containerId")))
                return ii::core::fail(ErrorCode::busy,"The selected Society container changed");
            return history;
        };
        auto history=verify();if (!history) return fail(history.error().code,history.error().message);
        const auto recordPath=output/"generation.json";
        const auto recordIdentity=iiFileProvider::ObjectSource::inspect(recordPath);
        auto record=ii::json::parse(iiFileProvider::native::File::read(recordPath,16*1024*1024));
        if (iiFileProvider::ObjectSource::inspect(recordPath)!=recordIdentity || !record)
            return fail(ErrorCode::failed,"Generation record is unreadable or changed");
        const auto schema=vm::text(vm::get(*record,"schema"));
        if (!((schema=="iild-standalone-image-v1" && vm::text(vm::get(*record,"status"))=="complete") ||
              (schema.empty() && vm::number(vm::get(*record,"schema_version"),0)==1)))
            return fail(ErrorCode::failed,"Generation record has an unsupported schema");
        const auto &records=vm::array(vm::get(*record,"outputs"));
        const auto &images=vm::array(vm::get(*record,"images"));
        if (records.size()!=std::size_t(*count) || (!images.empty() && images.size()!=records.size()))
            return fail(ErrorCode::failed,"Generation record does not match the requested image count");
        struct Input {fs::path path,destination;iiFileProvider::ObjectSourceIdentity identity;std::string hash,recipe;
            std::optional<std::int64_t> seed;};
        std::vector<Input> inputs;inputs.reserve(records.size());std::set<fs::path> paths;
        auto base=*identifier;if (!base.empty() && base.front()=='.') base.erase(base.begin());
        for (std::size_t index=0;index<records.size();++index) {
            if (cancellation.load()) return fail(ErrorCode::cancelled,"Generation was canceled");
            auto sourcePath=ii::core::path_from_utf8(vm::text(vm::get(records[index],"path")));
            auto hash=vm::lower(vm::text(vm::get(records[index],"sha256")));
            if (!sourcePath || !sourcePath->is_absolute() || *sourcePath!=sourcePath->lexically_normal() ||
                sourcePath->parent_path()!=output || sourcePath->extension()!=".png" || !paths.insert(*sourcePath).second ||
                hash.size()!=64 || !std::ranges::all_of(hash,[](unsigned char c){return std::isxdigit(c)!=0;}))
                return fail(ErrorCode::invalid_argument,"Generation output path or hash is invalid");
            auto identity=iiFileProvider::ObjectSource::inspect(*sourcePath);
            auto bytes=iiFileProvider::native::File::read(*sourcePath,128*1024*1024);
            if (iiFileProvider::ObjectSource::inspect(*sourcePath)!=identity)
                return fail(ErrorCode::busy,"Generated image changed while reading");
            const auto actual=hashBytes(bytes);
            if (!actual) return fail(ErrorCode::failed,"Cannot calculate generated image hash");
            if (*actual!=hash) return fail(ErrorCode::failed,"Generated image hash does not match its record");
            auto decoded=ii::visual::image::decode(std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()),bytes.size()));
            if (!decoded || decoded->width!=*width || decoded->height!=*height)
                return fail(ErrorCode::failed,"Generated image is unreadable or has the wrong dimensions");
            auto recipe=vm::object(parameters);
            std::optional<std::int64_t> resolvedSeed;
            if (!images.empty() && vm::has(images[index],"seed")) {
                const auto seed=integer(vm::get(images[index],"seed"));
                if (!seed || *seed<0) return fail(ErrorCode::failed,"Generation record has an invalid resolved seed");
                resolvedSeed=*seed;recipe["resolvedSeed"]=vm::Value(*seed);
            }
            auto json=ii::json::stringify(vm::Value(std::move(recipe)));
            if (!json) return fail(ErrorCode::failed,"Cannot encode generation metadata");
            auto verified=addGenerationRecipe(std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()),bytes.size()),*json);
            if (!verified) return fail(verified.error().code,verified.error().message);
            std::ostringstream name;name<<base<<'-'<<std::setfill('0')<<std::setw(4)<<(index+1)<<".png";
            const auto destination=history->sectionPath/name.str();
            auto state=fs::symlink_status(destination,fsError);
            if (fsError && fsError!=std::errc::no_such_file_or_directory) return fail(ErrorCode::failed,"Cannot inspect generation destination");
            fsError.clear();
            if (fs::exists(state) || fs::is_symlink(state)) return fail(ErrorCode::busy,"A generation output already exists");
            inputs.push_back({std::move(*sourcePath),destination,std::move(identity),std::move(hash),std::move(*json),resolvedSeed});
        }
        for (auto &input:inputs) {
            if (cancellation.load()) return fail(ErrorCode::cancelled,"Generation was canceled");
            if (iiFileProvider::ObjectSource::inspect(input.path)!=input.identity)
                return fail(ErrorCode::busy,"Generated image changed before publication");
            auto bytes=iiFileProvider::native::File::read(input.path,64*1024*1024);
            if (iiFileProvider::ObjectSource::inspect(input.path)!=input.identity)
                return fail(ErrorCode::busy,"Generated image changed during publication");
            const auto actual=hashBytes(bytes);
            if (!actual || *actual!=input.hash) return fail(ErrorCode::busy,"Generated image hash changed before publication");
            auto decorated=addGenerationRecipe(std::span(reinterpret_cast<const std::uint8_t *>(bytes.data()),bytes.size()),input.recipe);
            if (!decorated) return fail(decorated.error().code,decorated.error().message);
            auto destination=ii::core::path_to_utf8(input.destination);
            if (!destination) return fail(ErrorCode::invalid_argument,"Generation destination path is not valid UTF-8");
            const auto url=societyFileUrl(*destination),name=ii::core::path_to_utf8(input.destination.filename()).value_or(std::string{});
            vm::Object submitted;
            for (const auto key:{"model","prompt","negativePrompt","width","height","outputCount","seed"})
                if (vm::has(parameters,key)) submitted.emplace(key,vm::get(parameters,key));
            if (!vm::boolean(vm::get(parameters,"quick"))) for (const auto key:{"steps","cfgScale"})
                if (vm::has(parameters,key)) submitted.emplace(key,vm::get(parameters,key));
            vm::Object fields{{"containerId",vm::Value(history->containerId)},
                {"key",vm::Value("generation-history/"+name)},{"name",vm::Value(name)},{"displayName",vm::Value(name)},
                {"path",vm::Value(*destination)},{"mediaType",vm::Value("Image")},{"isImage",vm::Value(true)},
                {"imageSource",vm::Value(url)},{"thumbnailSource",vm::Value(url)},
                {"modelName",vm::get(plan.context,"modelLabel")},{"prompt",vm::get(parameters,"prompt")},
                {"negativePrompt",vm::get(parameters,"negativePrompt")},{"width",vm::Value(*width)},{"height",vm::Value(*height)},
                {"advancedParameters",vm::Value(std::move(submitted))}};
            if (input.seed) fields["seed"]=vm::Value(*input.seed);
            if (!vm::boolean(vm::get(parameters,"quick")) && vm::has(parameters,"steps")) fields["steps"]=vm::get(parameters,"steps");
            vm::Value entry(std::move(fields));
            iiFileProvider::native::AtomicSave save;save.open(input.destination);
            std::size_t written=0;
            while (written<decorated->size()) {
                const auto amount=save.device().write(reinterpret_cast<const char *>(decorated->data()+written),
                    static_cast<std::int64_t>(decorated->size()-written));
                if (amount<=0) return fail(ErrorCode::failed,"Cannot stage generated image");
                written+=std::size_t(amount);
            }
            auto current=verify();
            if (!current) return fail(current.error().code,current.error().message);
            if (current->sectionPath!=history->sectionPath) return fail(ErrorCode::busy,"Society generation history moved");
            if (cancellation.load()) return fail(ErrorCode::cancelled,"Generation was canceled");
            const auto committed=save.commit(false);
            if (!committed.succeeded) return fail(ErrorCode::failed,"Cannot publish generated image: "+committed.message);
            emit(std::move(entry));
        }
        staging->preserve=false;return {};
    } catch (const std::exception &error) {return fail(ErrorCode::failed,std::string("Cannot publish generated image: ")+error.what());}
    catch (...) {return fail(ErrorCode::failed,"Cannot publish generated image");}
}
generation::WorkerGenerationServices societyGenerationServices(SocietyGenerationOptions options,
    generation::WorkerTransport transport) {
    generation::WorkerGenerationServices services;
    services.prepare=[options](const auto &request,const auto &cancellation) {
        return prepareSocietyGeneration(request,cancellation,options);
    };
    services.transport=std::move(transport);
    services.publish=[options=std::move(options)](const auto &plan,const auto &cancellation,const auto &output) {
        return publishSocietyGeneration(plan,cancellation,options,output);
    };
    return services;
}
}
