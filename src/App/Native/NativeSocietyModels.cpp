#include "NativeSocietyModels.h"
#include <ModelPackaging/ModelInspection.hpp>
#include <ModelPackaging/ModelPackaging.hpp>
#include <iiFileProvider>
#include <algorithm>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
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
std::string utf8(const std::filesystem::path &path) {
    const auto value=path.generic_u8string();return {value.begin(),value.end()};
}
struct IndexedEntry final {std::string path;const vm::Value *value;};
std::vector<IndexedEntry> index(const SocietyListing &listing) {
    std::vector<IndexedEntry> result;result.reserve(vm::array(listing.aliases).size());
    for (const auto &entry:vm::array(listing.aliases)) result.push_back({vm::text(vm::get(entry,"relativePath")),&entry});
    std::ranges::sort(result,{},&IndexedEntry::path);return result;
}
ii::core::Result<vm::Value> capture(const SocietyListing &listing,const std::string &id,bool directory,
    const std::vector<IndexedEntry> &index,const std::function<bool()> &canceled,
    const iild::SafetensorsIndex *knownShards=nullptr,const iild::PyTorchIndex *knownTorchShards=nullptr) {
    try {
        std::vector<const vm::Value *> entries;
        const auto prefix=id+"/";
        const auto addExact=[&](const std::string &path) -> bool {
            auto it=std::ranges::lower_bound(index,path,{},&IndexedEntry::path);
            if (it==index.end() || it->path!=path) return false;
            entries.push_back(it->value);return true;
        };
        addExact(id);
        if (directory) {
            for (auto it=std::ranges::lower_bound(index,prefix,{},&IndexedEntry::path);
                 it!=index.end() && it->path.starts_with(prefix);++it) entries.push_back(it->value);
        } else {
            auto sidecar=nativePath(id);sidecar.replace_extension(".model.json");addExact(utf8(sidecar));
            addExact(id+".model.json");
            sidecar=nativePath(id);sidecar.replace_extension(".civitai.info");addExact(utf8(sidecar));
            addExact(id+".civitai.info");
            const auto found=std::ranges::lower_bound(index,id,{},&IndexedEntry::path);
            if (found==index.end() || found->path!=id) return ii::core::fail(ErrorCode::failed,"Selected model is unavailable");
            const auto container=iild::detectModelContainer(nativePath(vm::text(vm::get(*found->value,"nativePath"))));
            if (knownTorchShards || container=="pytorch-index") {
                std::optional<iild::PyTorchIndex> discovered;
                if (!knownTorchShards) {
                    discovered.emplace(iild::inspectPyTorchIndex(nativePath(vm::text(vm::get(*found->value,"nativePath")))));
                    knownTorchShards=&*discovered;
                }
                if (!iild::verifyPyTorchIndex(*knownTorchShards)) return ii::core::fail(ErrorCode::busy,"The selected model shards changed");
                for (const auto &shard : knownTorchShards->shards) {
                    const auto relative=utf8(nativePath(id).parent_path()/shard.relativePath);
                    if (!addExact(relative)) return ii::core::fail(ErrorCode::failed,"A selected model shard is unavailable in Society");
                }
            } else if (knownShards || iild::hasSafetensorsIndexSuffix(id) || container=="safetensors-index") {
                std::optional<iild::SafetensorsIndex> discovered;
                if (knownShards || (container!="safetensors" && container!="gguf" && container!="pytorch")) {
                    if (!knownShards) {
                        discovered.emplace(iild::inspectSafetensorsIndex(nativePath(vm::text(vm::get(*found->value,"nativePath")))));
                        knownShards=&*discovered;
                    }
                    if (!iild::verifySafetensorsIndex(*knownShards)) return ii::core::fail(ErrorCode::busy,"The selected model shards changed");
                    for (const auto &shard : knownShards->shards) {
                        const auto relative=utf8(nativePath(id).parent_path()/shard.relativePath);
                        if (!addExact(relative)) return ii::core::fail(ErrorCode::failed,"A selected model shard is unavailable in Society");
                    }
                }
            }
        }
        std::ranges::sort(entries,[](const auto *a,const auto *b) {
            return vm::text(vm::get(*a,"relativePath"))<vm::text(vm::get(*b,"relativePath"));
        });
        entries.erase(std::unique(entries.begin(),entries.end()),entries.end());
        if (entries.empty()) return ii::core::fail(ErrorCode::failed,"Selected model is unavailable");
        vm::Array members;members.reserve(entries.size());
        bool foundRoot=false;
        for (const auto *entry:entries) {
            if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model identity capture canceled");
            const auto relative=vm::text(vm::get(*entry,"relativePath"));
            const bool isDirectory=vm::boolean(vm::get(*entry,"isDirectory"));
            if (relative==id) {if (isDirectory!=directory) return ii::core::fail(ErrorCode::failed,"Selected model type changed");foundRoot=true;}
            vm::Object member{{"path",vm::Value(relative)},{"directory",vm::Value(isDirectory)}};
            if (!isDirectory) {
                const auto identity=iiFileProvider::ObjectSource::inspect(nativePath(vm::text(vm::get(*entry,"nativePath"))));
                member.emplace("stamp",vm::Value(identity.stamp));member.emplace("size",vm::Value(identity.size));
            }
            members.emplace_back(std::move(member));
        }
        if (!foundRoot) return ii::core::fail(ErrorCode::failed,"Selected model is unavailable");
        return vm::Value(vm::Object{{"containerId",vm::Value(listing.containerId)},{"root",vm::Value(utf8(listing.root))},
            {"id",vm::Value(id)},{"directory",vm::Value(directory)},{"members",vm::Value(std::move(members))}});
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot capture the selected model identity",std::current_exception());}
}
}
ii::core::Result<SocietyListing> browseSocietyModels(const std::filesystem::path &selection,
    const std::filesystem::path &explicitRoot,std::function<bool()> canceled) {
    return browseSocietyModels(localSocietyStorage(selection,explicitRoot).read,std::move(canceled));
}

ii::core::Result<SocietyListing> browseSocietyModels(const SocietyStorageAccess::Read &read,
    SocietyStorageAccess::Canceled canceled) {
    try {
        if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model inventory canceled");
        const SocietyStorageQuery query{"models","","",SocietyListingKind::modelEntries};
        auto listing=read(query,canceled);
        if (!listing) return std::unexpected(listing.error());
        const auto original=listing->aliases;
        const auto indexed=index(*listing);
        std::set<std::string,std::less<>> packages;
        for (const auto &entry:vm::array(original)) {
            if (!vm::boolean(vm::get(entry,"isDirectory")) && vm::text(vm::get(entry,"name"))=="model_index.json") {
                auto directory=parent(vm::text(vm::get(entry,"relativePath")));
                if (!directory.empty()) packages.insert(std::move(directory));
            }
        }
        std::map<std::string,iild::SafetensorsIndex,std::less<>> shardedGroups;
        std::map<std::string,iild::PyTorchIndex,std::less<>> torchGroups;
        std::set<std::string,std::less<>> shardMembers;
        for (const auto &entry : vm::array(original)) {
            if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model inventory canceled");
            if (vm::boolean(vm::get(entry,"isDirectory"))) continue;
            const auto id=vm::text(vm::get(entry,"relativePath"));
            if (withinPackage(id,packages)) continue;
            const auto path=nativePath(vm::text(vm::get(entry,"nativePath")));
            const auto format=iild::detectModelContainer(path);
            if (!iild::hasSafetensorsIndexSuffix(id) && format!="safetensors-index" && format!="pytorch-index") continue;
            try {
                std::vector<std::string> members;
                const auto collect=[&](const auto &shards) {
                    for (const auto &shard : shards) {
                        const auto relative=utf8(nativePath(id).parent_path()/shard.relativePath);
                        const auto found=std::ranges::lower_bound(indexed,relative,{},&IndexedEntry::path);
                        if (found==indexed.end() || found->path!=relative || vm::boolean(vm::get(*found->value,"isDirectory")))
                            throw std::runtime_error("Model shard is unavailable in Society");
                        members.push_back(relative);
                    }
                };
                if (format=="pytorch-index") {
                    auto group=iild::inspectPyTorchIndex(path);collect(group.shards);
                    std::vector<iild::PyTorchIndexTensor>().swap(group.tensors);
                    torchGroups.emplace(id,std::move(group));
                } else {
                    auto group=iild::inspectSafetensorsIndex(path);collect(group.shards);
                    std::vector<iild::SafetensorsIndexTensor>().swap(group.tensors);
                    group.sharedMetadata.clear();
                    shardedGroups.emplace(id,std::move(group));
                }
                for (const auto &member : members) shardMembers.insert(member);
            } catch (const std::exception &) {
                // Incomplete groups remain unavailable as a combined model;
                // unrelated files and independently selectable shards remain.
            }
        }
        vm::Array result;
        for (const auto &entry:vm::array(original)) {
            if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model inventory canceled");
            const auto id=vm::text(vm::get(entry,"relativePath"));
            const auto name=vm::text(vm::get(entry,"name"));
            const bool directory=vm::boolean(vm::get(entry,"isDirectory"));
            const auto lower=vm::lower(id);
            if (lower=="vae" || lower.starts_with("vae/") || withinPackage(id,packages) || shardMembers.contains(id)) continue;
            const auto lowerName=vm::lower(name);
            const auto path=vm::text(vm::get(entry,"nativePath"));
            if (path.empty()) continue;
            if (directory) {
                if (!packages.contains(id)) continue;
            } else {
                const auto content=iild::detectModelContainer(nativePath(path));
                if ((iild::hasSafetensorsIndexSuffix(id) || content=="safetensors-index" || content=="pytorch-index")
                    && !shardedGroups.contains(id) && !torchGroups.contains(id)) {
                    if (content!="safetensors" && content!="gguf" && content!="pytorch") continue;
                }
                // ONNX is excluded by the package/engine policy even if a
                // differently formatted file was given that extension.
                if (lowerName.ends_with(".onnx")) continue;
                const bool known=lowerName.ends_with(".safetensors") || lowerName.ends_with(".safetensor") || lowerName.ends_with(".gguf");
                if (!known && content=="unknown") continue;
            }
            const auto info=iild::inspectModel(nativePath(path));
            bool image=std::ranges::find(info.modalities,"image")!=info.modalities.end();
            const bool video=std::ranges::find(info.modalities,"video")!=info.modalities.end();
            // Legacy unclassified checkpoints remain image-selectable; an
            // explicit Society modality override takes precedence.
            if (info.modalities.empty()) image=true;
            if (!image && !video) continue;
            const auto found=shardedGroups.find(id);
            const auto torch= torchGroups.find(id);
            auto source=capture(*listing,id,directory,indexed,canceled,found==shardedGroups.end()?nullptr:&found->second,
                                torch==torchGroups.end()?nullptr:&torch->second);
            if (!source && source.error().code==ErrorCode::cancelled) return std::unexpected(source.error());
            vm::Object row{{"id",vm::Value(id)},{"key",vm::Value("models/"+id)},
                {"section",vm::Value("models")},{"exists",vm::Value(true)},{"relativePath",vm::Value(id)},
                {"isDirectory",vm::Value(directory)},{"name",vm::Value(name)},
                {"image",vm::Value(image)},{"video",vm::Value(video)},{"nativePath",vm::Value(path)}};
            if (source) row.emplace("source",std::move(*source));
            else row.emplace("sourceError",vm::Value(source.error().message));
            result.emplace_back(std::move(row));
        }
        std::ranges::sort(result,[](const auto &a,const auto &b) {
            const auto left=vm::text(vm::get(a,"id")),right=vm::text(vm::get(b,"id"));
            const auto lowerLeft=vm::lower(left),lowerRight=vm::lower(right);
            return lowerLeft==lowerRight?left<right:lowerLeft<lowerRight;
        });
        if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model inventory canceled");
        auto after=read(query,canceled);
        if (!after && after.error().code==ErrorCode::cancelled) return std::unexpected(after.error());
        if (!after || after->root!=listing->root || after->containerId!=listing->containerId || after->aliases!=original)
            return ii::core::fail(ErrorCode::busy,"Society models changed during inspection");
        listing->aliases=vm::Value(std::move(result));return listing;
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot inspect Society models",std::current_exception());}
}
ii::core::Result<std::filesystem::path> resolveSocietyModel(const std::filesystem::path &selection,
    const vm::Value &source,const std::filesystem::path &explicitRoot,std::function<bool()> canceled) {
    return resolveSocietyModel(localSocietyStorage(selection,explicitRoot).read,source,std::move(canceled));
}

ii::core::Result<std::filesystem::path> resolveSocietyModel(const SocietyStorageAccess::Read &read,
    const vm::Value &source,SocietyStorageAccess::Canceled canceled) {
    if (canceled && canceled()) return ii::core::fail(ErrorCode::cancelled,"Model resolution canceled");
    const auto id=vm::text(vm::get(source,"id"));
    if (id.empty() || vm::text(vm::get(source,"root")).empty() || vm::text(vm::get(source,"containerId")).empty() ||
        vm::get(source,"directory").kind()!=ii::core::ValueKind::boolean || vm::array(vm::get(source,"members")).empty())
        return ii::core::fail(ErrorCode::invalid_argument,"Model reference is incomplete");
    const SocietyStorageQuery query{"models","","",SocietyListingKind::modelEntries};
    auto listing=read(query,canceled);
    if (!listing) return std::unexpected(listing.error());
    if (utf8(listing->root)!=vm::text(vm::get(source,"root")) || listing->containerId!=vm::text(vm::get(source,"containerId")))
        return ii::core::fail(ErrorCode::busy,"The selected Society container changed");
    const bool directory=vm::boolean(vm::get(source,"directory"));
    const auto indexed=index(*listing);
    auto current=capture(*listing,id,directory,indexed,canceled);
    if (!current) return std::unexpected(current.error());
    if (*current!=source) return ii::core::fail(ErrorCode::busy,"The selected model changed after it was chosen");
    auto after=read(query,canceled);
    if (!after && after.error().code==ErrorCode::cancelled) return std::unexpected(after.error());
    if (!after || after->root!=listing->root || after->containerId!=listing->containerId || after->aliases!=listing->aliases)
        return ii::core::fail(ErrorCode::busy,"Society models changed during model resolution");
    for (const auto &entry:vm::array(listing->aliases)) if (vm::text(vm::get(entry,"relativePath"))==id)
        return nativePath(vm::text(vm::get(entry,"nativePath")));
    return ii::core::fail(ErrorCode::failed,"The selected model is unavailable");
}
}
