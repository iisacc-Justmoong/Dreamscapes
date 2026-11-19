#include "NativeSocietyStorage.h"
#include <iiSocietyContainer>
#include <iiFileProvider>
#include <iiJson>
#include <iiSocietyHelper>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <functional>

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
namespace fs=std::filesystem;
using Tree=iiSocietyContainer::FileTree;
std::string utf8(const fs::path &path) {
    auto value=path.generic_u8string(); return {value.begin(),value.end()};
}
fs::path nativePath(std::string_view input) {
    std::u8string bytes; bytes.reserve(input.size());
    for (unsigned char c:input) bytes.push_back(static_cast<char8_t>(c));
    return fs::path(bytes);
}
std::string localDate(const fs::file_time_type &value) {
#ifdef _MSC_VER
    const auto time=std::chrono::clock_cast<std::chrono::system_clock>(value);
#else
    const auto time=fs::file_time_type::clock::to_sys(value);
#endif
    const auto seconds=std::chrono::floor<std::chrono::seconds>(time);
    const auto instant=std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(seconds));
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local,&instant)!=0) return {};
#else
    if (!localtime_r(&instant,&local)) return {};
#endif
    char formatted[16]{};
    return std::strftime(formatted,sizeof(formatted),"%Y-%m-%d",&local)==10?std::string(formatted):std::string{};
}
ii::core::Result<vm::Value> readManifest(const fs::path &root) {
    try {
        auto parsed=ii::json::parse(iiFileProvider::native::File::read(root/".society-drive.json",65536));
        if (!parsed) return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society manifest");
        if (vm::has(*parsed,"replicaReady") && !vm::boolean(vm::get(*parsed,"replicaReady")))
            return ii::core::fail(ErrorCode::busy,"Society storage replica is not ready");
        return std::move(*parsed);
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot read Society manifest",std::current_exception());}
}

// A manifest section must name a direct, real directory within the drive.
ii::core::Result<fs::path> sectionLocation(const fs::path &root, const vm::Value &manifest, std::string_view section) {
    std::string folder;
    int matches=0;
    for (const auto &entry:vm::array(vm::get(manifest,"sections"))) if (vm::text(vm::get(entry,"id"))==section) {
        folder=vm::text(vm::get(entry,"path"));
        ++matches;
    }
    const auto component=nativePath(folder);
    if (matches!=1 || folder.empty() || folder.front()=='.' || folder.find('\\')!=std::string::npos ||
        component.is_absolute() || component!=component.filename() || component=="..")
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society section path");
    Tree container(root);
    auto entry=container.entry(component);
    if (!container.isValid() || container.rootPath()!=root || !entry || entry.value->kind!=Tree::Kind::Directory)
        return ii::core::fail(ErrorCode::failed,"Society section is missing or redirected");
    return root/component;
}

ii::core::Result<SocietyListing> browse(const fs::path &selectionFile,const std::string &section,
    const std::string &directory,const std::string &query,const fs::path &explicitRoot,bool recent,std::size_t limit,bool modelInventory) {
    if (section!="files" && section!="asset-library" && section!="generation-history" && !(modelInventory && section=="models"))
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown Society storage section");
    if (!selectionFile.is_absolute() || selectionFile.lexically_normal()!=selectionFile)
        return ii::core::fail(ErrorCode::invalid_argument,"Society selection must be an absolute normalized path");
    try {
        auto selection=iiSocietyHelper::Monitoring::readSelection(selectionFile);
        if (!selection) return ii::core::fail(ErrorCode::failed,"Society storage unavailable: "+selection.error());
        if (!explicitRoot.empty() && explicitRoot!=selection->root)
            return ii::core::fail(ErrorCode::unsupported,
                "The configured Society root differs from the shared selection; select it in Society before browsing");
        auto manifest=readManifest(selection->root);
        if (!manifest) return std::unexpected(manifest.error());
        const auto location=sectionLocation(selection->root,*manifest,section);
        if (!location) return std::unexpected(location.error());
        const auto &sectionPath=*location;
        Tree tree(sectionPath);
        if (!tree.isValid() || tree.rootPath()!=sectionPath)
            return ii::core::fail(ErrorCode::failed,"Society section is missing or redirected");
        const auto relative=nativePath(directory);
        if (relative.is_absolute() || (!relative.empty() && (relative.lexically_normal()!=relative || directory.find('\\')!=std::string::npos)))
            return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society directory");
        const auto needle=vm::lower(vm::trim(query));
        const bool recursive=recent || modelInventory || !needle.empty();
        const Tree::Options options{false,recursive?64U:1U,10000};
        auto found=tree.snapshot(relative,options);
        if (!found || found.value->kind!=Tree::Kind::Directory)
            return ii::core::fail(ErrorCode::failed,"Cannot read Society directory"+(found.error?": "+found.error.message():std::string{}));
        vm::Array rows;
        bool incomplete=false;
        std::vector<const Tree::Entry *> matching;
        std::function<void(const Tree::Entry &)> collect=[&](const Tree::Entry &entry) {
            if (recursive && entry.kind==Tree::Kind::Directory && !entry.childrenLoaded) incomplete=true;
            const auto path=utf8(entry.relativePath);
            if ((!recent || entry.kind==Tree::Kind::File) && (needle.empty() || vm::lower(path).find(needle)!=std::string::npos))
                matching.push_back(&entry);
            if (recursive) for (const auto &child:entry.children) collect(child);
        };
        for (const auto &child:found.value->children) collect(child);
        if (incomplete) return ii::core::fail(ErrorCode::out_of_range,"Society search exceeded the directory depth limit");
        if (recent || (section=="generation-history" && directory.empty() && needle.empty()))
            std::ranges::sort(matching,[](const auto *a,const auto *b) {
                return a->lastModified==b->lastModified ? a->relativePath<b->relativePath : a->lastModified>b->lastModified;
            });
        for (const auto *entry:matching) {
            const auto name=utf8(entry->path.filename()),path=utf8(entry->relativePath);
            vm::Object row{{"section",vm::Value(section)},{"exists",vm::Value(true)},{"key",vm::Value(section+"/"+path)},
                {"name",vm::Value(name)},{"relativePath",vm::Value(path)},{"nativePath",vm::Value(utf8(entry->path))},
                {"isDirectory",vm::Value(entry->kind==Tree::Kind::Directory)},
                {"sourceRevision",vm::Value(societySourceRevision(entry->lastModified,entry->size))}};
            if (auto date=localDate(entry->lastModified); !date.empty()) row.emplace("dateText",vm::Value(std::move(date)));
            rows.emplace_back(std::move(row));
            if (recent && rows.size()>=limit) break;
        }
        auto after=iiSocietyHelper::Monitoring::readSelection(selectionFile);
        auto afterManifest=readManifest(selection->root);
        if (!after || !afterManifest || after->root!=selection->root || after->containerId!=selection->containerId || *afterManifest!=*manifest)
            return ii::core::fail(ErrorCode::busy,"Society selection changed while browsing");
        return SocietyListing{selection->root,selection->containerId,vm::Value(std::move(rows)),sectionPath};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot browse Society storage",std::current_exception());}
}
}
ii::core::Result<SocietyListing> describeSocietySection(const fs::path &root, std::string_view section) {
    if (!root.is_absolute() || root.lexically_normal()!=root ||
        (section!="files" && section!="asset-library" && section!="generation-history" && section!="models"))
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society drive or section");
    try {
        Tree drive(root);
        const auto record=drive.entry(".society-drive.json");
        if (!drive.isValid() || drive.rootPath()!=root || !record || record.value->kind!=Tree::Kind::File)
            return ii::core::fail(ErrorCode::failed,"The Society drive manifest is unavailable");
        auto manifest=readManifest(root);
        if (!manifest) return std::unexpected(manifest.error());
        auto identity=vm::text(vm::get(*manifest,"identifier"));
        if (identity.empty()) return ii::core::fail(ErrorCode::invalid_argument,"The Society drive has no identifier");
        auto location=sectionLocation(root,*manifest,section);
        if (!location) return std::unexpected(location.error());
        return SocietyListing{root,std::move(identity),vm::Value(vm::Array{}),std::move(*location)};
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot validate the Society drive",std::current_exception());}
}

SocietyStorageAccess localSocietyStorage(fs::path selection, fs::path root) {
    return {.read=[selection=std::move(selection),root=std::move(root)](
        const SocietyStorageQuery &request, SocietyStorageAccess::Canceled canceled) -> ii::core::Result<SocietyListing> {
        // Model inventory callers already check cancellation around this
        // synchronous filesystem snapshot and throughout metadata inspection.
        if (request.kind!=SocietyListingKind::modelEntries && canceled && canceled())
            return ii::core::fail(ErrorCode::cancelled,"Society storage request canceled");
        switch (request.kind) {
        case SocietyListingKind::recent:
            return browseSocietyRecentFiles(selection,request.limit,root);
        case SocietyListingKind::modelEntries:
            return browseSocietyModelEntries(selection,root);
        case SocietyListingKind::section:
            // The local browse validates the selection both before and after reading it.
            return browseSociety(selection,request.section,"","",root);
        case SocietyListingKind::directory:
            return browseSociety(selection,request.section,request.directory,request.filter,root);
        }
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society storage request");
    }};
}

std::string societySourceRevision(fs::file_time_type modified,std::uintmax_t bytes) {
    const auto time=std::chrono::duration_cast<std::chrono::microseconds>(modified.time_since_epoch()).count();
    return std::to_string(time)+'-'+std::to_string(bytes);
}

ii::core::Result<SocietyListing> browseSociety(const fs::path &selection,const std::string &section,
    const std::string &directory,const std::string &query,const fs::path &root) {
    return browse(selection,section,directory,query,root,false,0,false);
}
ii::core::Result<SocietyListing> browseSocietyRecentFiles(const fs::path &selection,std::size_t limit,const fs::path &root) {
    if (!limit || limit>10000) return ii::core::fail(ErrorCode::out_of_range,"Invalid recent files limit");
    return browse(selection,"files","","",root,true,limit,false);
}
ii::core::Result<SocietyListing> browseSocietyModelEntries(const fs::path &selection,const fs::path &root) {
    return browse(selection,"models","","",root,false,0,true);
}
std::string societyFileUrl(std::string_view path) {
    constexpr char hex[]="0123456789ABCDEF";
    std::string result="file://";
#ifdef _WIN32
    if (!path.starts_with('/')) result.push_back('/');
#endif
    for (const unsigned char c:path) {
        if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
            c=='/' || c==':' || c=='-' || c=='_' || c=='.' || c=='~') result.push_back(static_cast<char>(c));
        else {result.push_back('%');result.push_back(hex[c>>4]);result.push_back(hex[c&15]);}
    }
    return result;
}
}
