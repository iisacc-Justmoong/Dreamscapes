#include "NativeSocietyModels.h"
#include <ModelPackaging/ModelInspection.hpp>
#include <algorithm>
#include <exception>
#include <set>
#include <string_view>
#include <utility>

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
std::string parent(std::string_view path) {
    const auto slash=path.rfind('/');return slash==path.npos?std::string{}:std::string(path.substr(0,slash));
}
bool withinPackage(std::string path,const std::set<std::string,std::less<>> &packages) {
    while (!(path=parent(path)).empty()) if (packages.contains(path)) return true;
    return false;
}
std::filesystem::path nativePath(std::string_view value) {
    std::u8string bytes;bytes.reserve(value.size());
    for (const unsigned char byte:value) bytes.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(bytes);
}
}
ii::core::Result<SocietyListing> browseSocietyModels(const std::filesystem::path &selection,
    const std::filesystem::path &explicitRoot,std::function<bool()> canceled) {
    try {
        if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model inventory canceled");
        auto listing=browseSocietyModelEntries(selection,explicitRoot);
        if (!listing) return std::unexpected(listing.error());
        const auto original=listing->aliases;
        std::set<std::string,std::less<>> packages;
        for (const auto &entry:vm::array(original)) {
            if (!vm::boolean(vm::get(entry,"isDirectory")) && vm::text(vm::get(entry,"name"))=="model_index.json") {
                auto directory=parent(vm::text(vm::get(entry,"relativePath")));
                if (!directory.empty()) packages.insert(std::move(directory));
            }
        }
        vm::Array result;
        for (const auto &entry:vm::array(original)) {
            if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model inventory canceled");
            const auto id=vm::text(vm::get(entry,"relativePath"));
            const auto name=vm::text(vm::get(entry,"name"));
            const bool directory=vm::boolean(vm::get(entry,"isDirectory"));
            const auto lower=vm::lower(id);
            if (lower=="vae" || lower.starts_with("vae/") || withinPackage(id,packages)) continue;
            const auto lowerName=vm::lower(name);
            const bool file=lowerName.ends_with(".safetensors") || lowerName.ends_with(".safetensor") || lowerName.ends_with(".gguf");
            if (directory?!packages.contains(id):!file) continue;
            const auto path=vm::text(vm::get(entry,"nativePath"));
            if (path.empty()) continue;
            const auto info=iild::inspectModel(nativePath(path));
            bool image=std::ranges::find(info.modalities,"image")!=info.modalities.end();
            const bool video=std::ranges::find(info.modalities,"video")!=info.modalities.end();
            // Legacy unclassified checkpoints remain image-selectable; an
            // explicit Society modality override takes precedence.
            if (info.modalities.empty()) image=true;
            if (!image && !video) continue;
            result.emplace_back(vm::Object{{"id",vm::Value(id)},{"key",vm::Value("models/"+id)},
                {"section",vm::Value("models")},{"exists",vm::Value(true)},{"relativePath",vm::Value(id)},
                {"isDirectory",vm::Value(directory)},{"name",vm::Value(name)},
                {"image",vm::Value(image)},{"video",vm::Value(video)},{"nativePath",vm::Value(path)}});
        }
        std::ranges::sort(result,[](const auto &a,const auto &b) {
            const auto left=vm::text(vm::get(a,"id")),right=vm::text(vm::get(b,"id"));
            const auto lowerLeft=vm::lower(left),lowerRight=vm::lower(right);
            return lowerLeft==lowerRight?left<right:lowerLeft<lowerRight;
        });
        if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model inventory canceled");
        auto after=browseSocietyModelEntries(selection,explicitRoot);
        if (!after || after->root!=listing->root || after->containerId!=listing->containerId || after->aliases!=original)
            return ii::core::fail(ErrorCode::busy,"Society models changed during inspection");
        listing->aliases=vm::Value(std::move(result));return listing;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot inspect Society models",std::current_exception());}
}
}
