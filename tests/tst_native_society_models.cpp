#include "App/Native/NativeSocietyModels.h"
#include <ModelPackaging/ModelPackaging.hpp>
#include <iiFileProvider>
#include <iiJson>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
using namespace dreamscapes;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void put(const fs::path &path,const vm::Value &value) {
    auto text=ii::json::stringify(value);check(bool(text),"fixture encoding failed");
    iiFileProvider::native::File::write(path,*text);
}
void safetensor(const fs::path &path,const std::string &json) {
    std::string bytes(8,'\0');const auto length=std::uint64_t(json.size());
    for (unsigned i=0;i<8;++i) bytes[i]=char((length>>(8*i))&255);
    bytes+=json;iiFileProvider::native::File::write(path,bytes);
}
void shard(const fs::path &path,const std::string &name,char value) {
    const auto json="{\""+name+"\":{\"dtype\":\"F32\",\"shape\":[1],\"data_offsets\":[0,4]}}";
    std::string bytes(8,'\0');const auto length=std::uint64_t(json.size());
    for (unsigned i=0;i<8;++i) bytes[i]=char((length>>(8*i))&255);
    bytes+=json;bytes.append(4,value);iiFileProvider::native::File::write(path,bytes);
}
void zipCandidate(const fs::path &path,const std::string &entry) {
    std::string bytes(30,'\0');bytes[0]='P';bytes[1]='K';bytes[2]=3;bytes[3]=4;
    bytes[26]=static_cast<char>(entry.size()&255);bytes[27]=static_cast<char>((entry.size()>>8)&255);
    iiFileProvider::native::File::write(path,bytes+entry+"fixture");
}
void diffusersWan(const fs::path &path) {
    const std::vector<std::string> names{"patch_embedding.weight","blocks.0.attn1.to_q.weight",
        "blocks.0.attn2.norm_k.weight","condition_embedder.time_embedder.linear_1.weight"};
    std::string header="{";
    for (std::size_t i=0;i<names.size();++i) {
        if (i) header+=",";
        header+="\""+names[i]+"\":{\"dtype\":\"F32\",\"shape\":[1],\"data_offsets\":["+
            std::to_string(i*4)+","+std::to_string((i+1)*4)+"]}";
    }
    header+="}";
    std::string bytes(8,'\0');const auto length=std::uint64_t(header.size());
    for (unsigned i=0;i<8;++i) bytes[i]=char((length>>(8*i))&255);
    bytes+=header;bytes.append(names.size()*4,'\0');iiFileProvider::native::File::write(path,bytes);
}
void ggufVideo(const fs::path &path) {
    std::string bytes="GGUF";
    auto number=[&](std::uint64_t value,unsigned width) {
        for (unsigned i=0;i<width;++i) bytes+=char((value>>(8*i))&255);
    };
    auto string=[&](std::string_view value) {number(value.size(),8);bytes+=value;};
    number(3,4);number(1,8);number(1,8);
    string("general.architecture");number(8,4);string("ltxv");
    string("weight");number(1,4);number(1,8);number(0,4);number(0,8);
    bytes.append((32-bytes.size()%32)%32,'\0');bytes.append(4,'\0');
    iiFileProvider::native::File::write(path,bytes);
}
void ggufWan(const fs::path &path) {
    std::string bytes="GGUF";
    auto number=[&](std::uint64_t value,unsigned width) {
        for (unsigned i=0;i<width;++i) bytes+=char((value>>(8*i))&255);
    };
    auto string=[&](std::string_view value) {number(value.size(),8);bytes+=value;};
    number(3,4);number(2,8);number(1,8);
    string("general.architecture");number(8,4);string("wan");
    for (const auto &[name,offset] : {std::pair{"patch_embedding.weight",0},
                                      std::pair{"blocks.0.cross_attn.norm_k.weight",32}}) {
        string(name);number(1,4);number(1,8);number(0,4);number(offset,8);
    }
    bytes.append((32-bytes.size()%32)%32,'\0');bytes.append(36,'\0');
    iiFileProvider::native::File::write(path,bytes);
}
int main() {
    try {
        iiFileProvider::native::TemporaryDirectory temp(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temp.isValid(),"fixture unavailable");
        const auto root=temp.path()/"models.societycontainer",selection=temp.path()/"selection.json";
        const auto models=root/"Models";fs::create_directories(models/"Checkpoint"/"LTX");fs::create_directories(models/"VAE");
        safetensor(models/"Checkpoint"/"image.safetensors",R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})");
        safetensor(models/"Checkpoint"/"override.safetensors",R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})");
        safetensor(models/"Checkpoint"/"renamed.bin",R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})");
        safetensor(models/"Checkpoint"/"misleading.safetensors.index.json",R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})");
        ggufVideo(models/"Checkpoint"/"video.data");
        ggufVideo(models/"Checkpoint"/"video.gguf");
        safetensor(models/"Checkpoint"/"unsupported.onnx",R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})");
        iiFileProvider::native::File::write(models/"Checkpoint"/"tokenizer.bin","not model weights");
        put(models/"Checkpoint"/"override.safetensors.model.json",vm::Value(vm::Object{{"society",vm::Value(vm::Object{
            {"modalities",vm::Value(vm::Array{vm::Value("video")})}})}}));
        put(models/"Checkpoint"/"LTX"/"model_index.json",vm::Value(vm::Object{{"_class_name",vm::Value("LTX2Pipeline")}}));
        safetensor(models/"Checkpoint"/"LTX"/"weight.safetensors",R"({"tensor":{}})");
        iiFileProvider::native::File::write(models/"Checkpoint"/"LTX-old","unrelated sibling");
        safetensor(models/"VAE"/"component.safetensors",R"({"tensor":{}})");
        safetensor(models/"Checkpoint"/".hidden.safetensors",R"({"tensor":{}})");
        std::error_code ec;fs::create_symlink(models/"Checkpoint"/"image.safetensors",models/"Checkpoint"/"redirect.safetensors",ec);
        constexpr auto id="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        vm::Array sections{vm::Value(vm::Object{{"id",vm::Value("models")},{"path",vm::Value("Models")}})};
        auto manifest=[&](bool ready) {put(root/".society-drive.json",vm::Value(vm::Object{{"type",vm::Value("SocietyDrive")},
            {"schemaVersion",vm::Value(1)},{"identifier",vm::Value(id)},{"replicaReady",vm::Value(ready)},{"sections",vm::Value(sections)}}));};
        manifest(true);put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value(id)}}));
        auto result=native::browseSocietyModels(selection);
        check(result && vm::array(result->aliases).size()==7,"model catalogue omitted renamed models or included unrelated files");
        bool image=false,video=false,override=false,renamed=false,gguf=false,renamedGguf=false;
        vm::Value imageReference,videoReference,renamedReference,renamedGgufReference,misleadingReference;
        for (const auto &entry:vm::array(result->aliases)) {
            const auto path=vm::text(vm::get(entry,"id"));
            if (path=="Checkpoint/image.safetensors") {image=vm::boolean(vm::get(entry,"image")) && !vm::boolean(vm::get(entry,"video"));imageReference=vm::get(entry,"source");}
            if (path=="Checkpoint/LTX") {video=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));videoReference=vm::get(entry,"source");}
            if (path=="Checkpoint/override.safetensors") override=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));
            if (path=="Checkpoint/renamed.bin") {renamed=vm::boolean(vm::get(entry,"image")) && !vm::boolean(vm::get(entry,"video"));renamedReference=vm::get(entry,"source");}
            if (path=="Checkpoint/misleading.safetensors.index.json") misleadingReference=vm::get(entry,"source");
            if (path=="Checkpoint/video.gguf") gguf=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));
            if (path=="Checkpoint/video.data") {renamedGguf=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));renamedGgufReference=vm::get(entry,"source");}
        }
        check(image && video && override && renamed && gguf && renamedGguf,"SDK metadata or a Society modality override was misclassified");
        auto resolved=native::resolveSocietyModel(selection,imageReference);
        check(resolved && *resolved==models/"Checkpoint"/"image.safetensors","selected image model identity did not resolve");
        auto package=native::resolveSocietyModel(selection,videoReference);
        check(package && *package==models/"Checkpoint"/"LTX","selected video package identity did not resolve");
        auto renamedModel=native::resolveSocietyModel(selection,renamedReference);
        check(renamedModel && *renamedModel==models/"Checkpoint"/"renamed.bin","renamed image model identity did not resolve");
        auto renamedVideo=native::resolveSocietyModel(selection,renamedGgufReference);
        check(renamedVideo && *renamedVideo==models/"Checkpoint"/"video.data","renamed GGUF identity did not resolve");
        auto misleading=native::resolveSocietyModel(selection,misleadingReference);
        check(misleading && *misleading==models/"Checkpoint"/"misleading.safetensors.index.json","misleading suffix overrode safetensors content");
        const auto torch=models/"Checkpoint"/"legacy.ckpt";
        const auto renamedTorch=models/"Checkpoint"/"legacy.data";
        const auto misleadingTorch=models/"Checkpoint"/"legacy.safetensors.index.json";
        const auto pickle=models/"Checkpoint"/"legacy.pth";
        zipCandidate(torch,"model/data.pkl");zipCandidate(renamedTorch,"model/data.pkl");
        zipCandidate(misleadingTorch,"model/data.pkl");
        zipCandidate(models/"Checkpoint"/"unrelated.zip","archive/readme.txt");
        zipCandidate(models/"Checkpoint"/"legacy.onnx","model/data.pkl");
        iiFileProvider::native::File::write(pickle,std::string("\x80\x02\x8a\x0a\x6c\xfc\x9c\x46\xf9\x20\x6a\xa8\x50\x19\x2e",15)+"fixture");
        put(models/"Checkpoint"/"legacy.pth.model.json",vm::Value(vm::Object{{"society",vm::Value(vm::Object{
            {"modalities",vm::Value(vm::Array{vm::Value("video")})}})}}));
        const auto torchScan=native::browseSocietyModels(selection);check(bool(torchScan),"PyTorch inventory failed");
        bool foundTorch=false,foundRenamed=false,foundMisleading=false,foundPickle=false,foundOther=false;
        vm::Value torchReference,misleadingTorchReference;
        for (const auto &entry:vm::array(torchScan->aliases)) {
            const auto path=vm::text(vm::get(entry,"id"));
            if (path=="Checkpoint/legacy.ckpt") {foundTorch=vm::boolean(vm::get(entry,"image"));torchReference=vm::get(entry,"source");}
            if (path=="Checkpoint/legacy.data") foundRenamed=vm::boolean(vm::get(entry,"image"));
            if (path=="Checkpoint/legacy.safetensors.index.json") {foundMisleading=true;misleadingTorchReference=vm::get(entry,"source");}
            if (path=="Checkpoint/legacy.pth") foundPickle=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));
            if (path=="Checkpoint/unrelated.zip" || path=="Checkpoint/legacy.onnx") foundOther=true;
        }
        check(foundTorch && foundRenamed && foundMisleading && foundPickle && !foundOther,
              "PyTorch model discovery ignored content, modality, or unrelated archives");
        const auto resolvedTorch=native::resolveSocietyModel(selection,torchReference);
        const auto resolvedMisleadingTorch=native::resolveSocietyModel(selection,misleadingTorchReference);
        check(resolvedTorch && *resolvedTorch==torch && resolvedMisleadingTorch && *resolvedMisleadingTorch==misleadingTorch,
              "PyTorch model source did not resolve");
        zipCandidate(torch,"model/changed.pkl");
        check(!native::resolveSocietyModel(selection,torchReference),"Changed PyTorch checkpoint retained a stale identity");
        const auto input=temp.path()/"packaged-wan-input";fs::create_directories(input);
        ggufWan(input/"main.gguf");
        const auto packedWan=models/"Checkpoint"/"packaged-wan.safetensors";
        (void)iild::createModelPackage(input,packedWan);
        auto packedScan=native::browseSocietyModels(selection);check(bool(packedScan),"packaged GGUF inventory failed");
        bool packedVideo=false;vm::Value packedReference;
        for (const auto &entry:vm::array(packedScan->aliases)) if (vm::text(vm::get(entry,"id"))=="Checkpoint/packaged-wan.safetensors") {
            packedVideo=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));
            packedReference=vm::get(entry,"source");
        }
        check(packedVideo,"A packaged Wan GGUF was not offered as a video model");
        auto selectedPacked=native::resolveSocietyModel(selection,packedReference);
        check(selectedPacked && *selectedPacked==packedWan,"The packaged GGUF selection did not resolve");
        const auto standaloneWan=models/"Checkpoint"/"diffusers-wan.safetensors";
        diffusersWan(standaloneWan);
        auto standaloneScan=native::browseSocietyModels(selection);check(bool(standaloneScan),"Diffusers Wan inventory failed");
        bool standaloneVideo=false;vm::Value standaloneReference;
        for (const auto &entry:vm::array(standaloneScan->aliases)) if (vm::text(vm::get(entry,"id"))=="Checkpoint/diffusers-wan.safetensors") {
            standaloneVideo=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));
            standaloneReference=vm::get(entry,"source");
        }
        check(standaloneVideo,"A standalone Diffusers Wan checkpoint was not offered as video");
        auto selectedStandalone=native::resolveSocietyModel(selection,standaloneReference);
        check(selectedStandalone && *selectedStandalone==standaloneWan,"The standalone Wan selection did not resolve");
        const auto split=models/"Checkpoint"/"split";
        fs::create_directories(split);
        shard(split/"part1.safetensors","audio_vae.decoder.weight",'a');
        shard(split/"part2.safetensors","model.diffusion_model.transformer_blocks.0.weight",'b');
        shard(split/"part3.safetensors","text_embedding_projection.audio_aggregate_embed.weight",'c');
        iiFileProvider::native::File::write(split/"model.SAFETENSORS.Index.JSON",
            R"({"weight_map":{"audio_vae.decoder.weight":"part1.safetensors","model.diffusion_model.transformer_blocks.0.weight":"part2.safetensors","text_embedding_projection.audio_aggregate_embed.weight":"part3.safetensors"}})");
        auto splitScan=native::browseSocietyModels(selection);
        check(bool(splitScan),"sharded model inventory failed");
        vm::Value splitReference;
        int groups=0,fragments=0;
        for (const auto &entry:vm::array(splitScan->aliases)) {
            const auto path=vm::text(vm::get(entry,"id"));
            if (path=="Checkpoint/split/model.SAFETENSORS.Index.JSON") {
                ++groups;splitReference=vm::get(entry,"source");
                check(vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image")),"sharded video classification failed");
            }
            if (path.starts_with("Checkpoint/split/part")) ++fragments;
        }
        check(groups==1 && fragments==0,"shards were not presented as one model");
        auto resolvedSplit=native::resolveSocietyModel(selection,splitReference);
        check(resolvedSplit && *resolvedSplit==split/"model.SAFETENSORS.Index.JSON","shard index source did not resolve");
        shard(split/"part1.safetensors","audio_vae.decoder.weight",'x');
        check(!native::resolveSocietyModel(selection,splitReference),"changed shard retained a stale model selection");
        const auto renamedSplit=models/"Checkpoint"/"renamed-split";
        fs::create_directories(renamedSplit);
        shard(renamedSplit/"part1.bin","audio_vae.decoder.weight",'a');
        shard(renamedSplit/"part2.bin","model.diffusion_model.transformer_blocks.0.weight",'b');
        shard(renamedSplit/"part3.bin","text_embedding_projection.audio_aggregate_embed.weight",'c');
        iiFileProvider::native::File::write(renamedSplit/"model-index.data",
            R"({"weight_map":{"audio_vae.decoder.weight":"part1.bin","model.diffusion_model.transformer_blocks.0.weight":"part2.bin","text_embedding_projection.audio_aggregate_embed.weight":"part3.bin"}})");
        auto renamedScan=native::browseSocietyModels(selection);check(bool(renamedScan),"renamed shard inventory failed");
        vm::Value renamedSplitReference;groups=0;fragments=0;
        for (const auto &entry:vm::array(renamedScan->aliases)) {
            const auto path=vm::text(vm::get(entry,"id"));
            if (path=="Checkpoint/renamed-split/model-index.data") {
                ++groups;renamedSplitReference=vm::get(entry,"source");
                check(vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image")),
                      "renamed sharded video classification failed");
            }
            if (path.starts_with("Checkpoint/renamed-split/part")) ++fragments;
        }
        check(groups==1 && fragments==0,"renamed shard index was not presented as one model");
        auto renamedSplitResolved=native::resolveSocietyModel(selection,renamedSplitReference);
        check(renamedSplitResolved && *renamedSplitResolved==renamedSplit/"model-index.data",
              "renamed shard index source did not resolve");
        shard(renamedSplit/"part1.bin","audio_vae.decoder.weight",'x');
        check(!native::resolveSocietyModel(selection,renamedSplitReference),"renamed index ignored a changed shard");
        const auto torchSplit=models/"Checkpoint"/"torch-split";fs::create_directories(torchSplit);
        const auto torchBytes=std::string("\x80\x02\x8a\x0a\x6c\xfc\x9c\x46\xf9\x20\x6a\xa8\x50\x19\x2e",15)+"fixture";
        iiFileProvider::native::File::write(torchSplit/"part-a.bin",torchBytes);
        iiFileProvider::native::File::write(torchSplit/"part-b.data",torchBytes);
        iiFileProvider::native::File::write(torchSplit/"model.json",
            R"({"weight_map":{"audio_vae.decoder.weight":"part-a.bin","model.diffusion_model.transformer_blocks.0.weight":"part-b.data","text_embedding_projection.audio_aggregate_embed.weight":"part-b.data"}})");
        auto torchSplitScan=native::browseSocietyModels(selection);check(bool(torchSplitScan),"PyTorch shard inventory failed");
        vm::Value torchSplitReference;groups=0;fragments=0;
        for (const auto &entry:vm::array(torchSplitScan->aliases)) {
            const auto path=vm::text(vm::get(entry,"id"));
            if (path=="Checkpoint/torch-split/model.json") {++groups;torchSplitReference=vm::get(entry,"source");
                check(vm::boolean(vm::get(entry,"video")),"PyTorch index did not retain its detected modality");}
            if (path.starts_with("Checkpoint/torch-split/part")) ++fragments;
        }
        check(groups==1 && fragments==0,"PyTorch shards were not presented as one model");
        const auto selectedTorchSplit=native::resolveSocietyModel(selection,torchSplitReference);
        check(selectedTorchSplit && *selectedTorchSplit==torchSplit/"model.json","PyTorch index source did not resolve");
        iiFileProvider::native::File::write(torchSplit/"part-b.data",torchBytes+"changed");
        check(!native::resolveSocietyModel(selection,torchSplitReference),"PyTorch index ignored a changed shard");
        const auto wan=models/"Checkpoint"/"Wan";
        fs::create_directories(wan);
        put(wan/"model_index.json",vm::Value(vm::Object{{"_class_name",vm::Value("WanImageToVideoPipeline")}}));
        safetensor(wan/"weight.safetensors",R"({"tensor":{}})");
        auto videoScan=native::browseSocietyModels(selection);check(bool(videoScan),"Wan inventory failed");
        bool wanVideo=false;
        for (const auto &entry:vm::array(videoScan->aliases)) if (vm::text(vm::get(entry,"id"))=="Checkpoint/Wan")
            wanVideo=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));
        check(wanVideo,"Wan image-to-video model was not presented as a video model");
        safetensor(models/"Checkpoint"/"image.safetensors",R"({"__metadata__":{"_class_name":"ChangedPipeline"},"tensor":{}})");
        check(!native::resolveSocietyModel(selection,imageReference),"changed model file was accepted for an earlier selection");
        auto sidecarScan=native::browseSocietyModels(selection);check(bool(sidecarScan),"model reinspection failed");
        for (const auto &entry:vm::array(sidecarScan->aliases)) if (vm::text(vm::get(entry,"id"))=="Checkpoint/image.safetensors") imageReference=vm::get(entry,"source");
        put(models/"Checkpoint"/"image.civitai.info",vm::Value(vm::Object{{"baseModel",vm::Value("changed")}}));
        check(!native::resolveSocietyModel(selection,imageReference),"new model sidecar was ignored for an earlier selection");
        safetensor(models/"Checkpoint"/"LTX"/"weight.safetensors",R"({"changed":{}})");
        check(!native::resolveSocietyModel(selection,videoReference),"changed package member was accepted for an earlier selection");
        check(!native::resolveSocietyModel(selection,vm::Value(vm::Object{{"id",vm::Value("../outside")}})),
            "an invalid model reference was accepted");
        auto fresh=native::browseSocietyModels(selection);check(bool(fresh),"updated fixture catalogue failed");
        for (const auto &entry:vm::array(fresh->aliases)) if (vm::text(vm::get(entry,"id"))=="Checkpoint/LTX") videoReference=vm::get(entry,"source");
        safetensor(models/"Checkpoint"/"LTX"/"added.safetensors",R"({"tensor":{}})");
        check(!native::resolveSocietyModel(selection,videoReference),"new package member was ignored for an earlier selection");
        fresh=native::browseSocietyModels(selection);check(bool(fresh),"expanded fixture catalogue failed");
        for (const auto &entry:vm::array(fresh->aliases)) if (vm::text(vm::get(entry,"id"))=="Checkpoint/LTX") videoReference=vm::get(entry,"source");
        int resolutionChecks=0;
        auto changedSelection=native::resolveSocietyModel(selection,videoReference,{},[&] {
            if (++resolutionChecks==2) put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},
                {"path",vm::Value(root.string())},{"containerId",vm::Value("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff")}}));
            return false;
        });
        check(!changedSelection && resolutionChecks>=2,"a changed Society selection was accepted during model resolution");
        put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value(id)}}));
        check(!native::browseSocietyModels(selection,{},[]{return true;}),"a canceled model scan continued");
        int calls=0;
        auto changing=native::browseSocietyModels(selection,{},[&] {
            if (++calls==2) safetensor(models/"Checkpoint"/"new.safetensors",R"({"tensor":{}})");
            return false;
        });
        check(!changing && calls>=2,"a changing model inventory was presented as a consistent snapshot");
        manifest(false);check(!native::browseSocietyModels(selection),"an unready model store appeared available");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
