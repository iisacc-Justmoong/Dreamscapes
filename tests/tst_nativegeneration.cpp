#include "NativeGenerationWork.h"
#include <iiFileProviderNative.h>
#include <iiImageCodec/ImageCodec.h>
#include <cstdlib>
#include <iostream>
using namespace dreamscapes;
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while(false)
int main(int argc,char** argv) {
    CHECK(argc==2);
    iiFileProvider::native::TemporaryDirectory folder{std::filesystem::path(argv[1])}; CHECK(folder.isValid());
    const auto directory=std::filesystem::canonical(folder.path());
    NativeGenerationInput input; input.state=std::make_shared<NativeGenerationState>(); input.previewDirectory=directory;
    input.lifetime=std::make_shared<int>(1);
    int calls=0;
    input.preview=[&](const NativePreviewArtifact& artifact) {
        CHECK(artifact.name=="step-000002.png"); ++calls;
        const auto png=iiFileProvider::native::File::read(directory/artifact.name);
        auto image=ii::image::decode({reinterpret_cast<const std::uint8_t*>(png.data()),png.size()});
        CHECK(image && image->width==2 && image->height==1 && image->rgba[0]==11 && image->rgba[3]==255);
    };
    input.generate=[](const auto&,const auto&,const auto&,const auto&,const auto& preview) {
        iiLocalDiffusion::NativeGenerationPreview frame{{11,12,13,14,15,16},2,1,2,1,2};
        preview(frame); preview(frame); // duplicate sequence is suppressed
        frame.sequence=1; preview(frame); // older sequence is suppressed
        frame.sequence=3; frame.rgb.clear(); preview(frame); // malformed pixels are suppressed
        return iiLocalDiffusion::NativeGenerationResult{};
    };
    const auto state=input.state;
    auto result=runNativeGeneration(std::move(input)); CHECK(result.error.empty() && calls==1 && !state->accepting);
    NativeGenerationInput cancelled; cancelled.state=std::make_shared<NativeGenerationState>(); cancelled.state->cancelled=true;
    cancelled.generate=[](const auto&,const auto&,const auto&,const auto&,const auto&)->iiLocalDiffusion::NativeGenerationResult { CHECK(false); return {}; };
    CHECK(runNativeGeneration(std::move(cancelled)).cancelled);
    NativeGenerationInput throwing; throwing.state=std::make_shared<NativeGenerationState>();
    throwing.generate=[](const auto&,const auto&,const auto&,const auto&,const auto&)->iiLocalDiffusion::NativeGenerationResult { throw std::runtime_error("provider failed"); };
    CHECK(runNativeGeneration(std::move(throwing)).error=="provider failed");
    NativeGenerationInput noLease; noLease.state=std::make_shared<NativeGenerationState>(); noLease.previewDirectory=directory;
    noLease.preview=[&](const auto&) { CHECK(false); }; noLease.generate=[](const auto&,const auto&,const auto&,const auto&,const auto& preview) {
        preview({{11,12,13},1,1,4,1,1}); return iiLocalDiffusion::NativeGenerationResult{};
    };
    CHECK(runNativeGeneration(std::move(noLease)).error.empty()); CHECK(!std::filesystem::exists(directory/"step-000004.png"));
    std::cout << "Qt-free native generation boundary passed\n";
}
