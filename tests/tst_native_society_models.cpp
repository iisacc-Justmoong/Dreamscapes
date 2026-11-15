#include "App/Native/NativeSocietyModels.h"
#include <iiFileProviderNative.h>
#include <iiJson/Json.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>

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
int main() {
    try {
        iiFileProvider::native::TemporaryDirectory temp(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temp.isValid(),"fixture unavailable");
        const auto root=temp.path()/"models.societycontainer",selection=temp.path()/"selection.json";
        const auto models=root/"Models";fs::create_directories(models/"Checkpoint"/"LTX");fs::create_directories(models/"VAE");
        safetensor(models/"Checkpoint"/"image.safetensors",R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})");
        safetensor(models/"Checkpoint"/"override.safetensors",R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})");
        put(models/"Checkpoint"/"override.safetensors.model.json",vm::Value(vm::Object{{"society",vm::Value(vm::Object{
            {"modalities",vm::Value(vm::Array{vm::Value("video")})}})}}));
        put(models/"Checkpoint"/"LTX"/"model_index.json",vm::Value(vm::Object{{"_class_name",vm::Value("LTX2Pipeline")}}));
        safetensor(models/"Checkpoint"/"LTX"/"weight.safetensors",R"({"tensor":{}})");
        safetensor(models/"VAE"/"component.safetensors",R"({"tensor":{}})");
        safetensor(models/"Checkpoint"/".hidden.safetensors",R"({"tensor":{}})");
        std::error_code ec;fs::create_symlink(models/"Checkpoint"/"image.safetensors",models/"Checkpoint"/"redirect.safetensors",ec);
        constexpr auto id="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        vm::Array sections{vm::Value(vm::Object{{"id",vm::Value("models")},{"path",vm::Value("Models")}})};
        auto manifest=[&](bool ready) {put(root/".society-drive.json",vm::Value(vm::Object{{"type",vm::Value("SocietyDrive")},
            {"schemaVersion",vm::Value(1)},{"identifier",vm::Value(id)},{"replicaReady",vm::Value(ready)},{"sections",vm::Value(sections)}}));};
        manifest(true);put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value(id)}}));
        auto result=native::browseSocietyModels(selection);
        check(result && vm::array(result->aliases).size()==3,"model catalogue included hidden, redirected, VAE or nested package weights");
        bool image=false,video=false,override=false;
        for (const auto &entry:vm::array(result->aliases)) {
            const auto path=vm::text(vm::get(entry,"id"));
            if (path=="Checkpoint/image.safetensors") image=vm::boolean(vm::get(entry,"image")) && !vm::boolean(vm::get(entry,"video"));
            if (path=="Checkpoint/LTX") video=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));
            if (path=="Checkpoint/override.safetensors") override=vm::boolean(vm::get(entry,"video")) && !vm::boolean(vm::get(entry,"image"));
        }
        check(image && video && override,"SDK metadata or a Society modality override was misclassified");
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
