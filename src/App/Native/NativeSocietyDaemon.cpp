#include "NativeSocietyDaemon.h"
#include <iiJson>
#include <iiSocietyContainer>
#include <iiSocietyHelper>
#include <algorithm>
#include <chrono>
#include <set>
#include <utility>

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
namespace fs = std::filesystem;
using namespace std::chrono_literals;
constexpr std::size_t entryLimit = 10000;
constexpr std::size_t pageLimit = 64;
constexpr std::size_t depthLimit = 64;
constexpr auto catalogDeadline = 20s;
constexpr auto materializeDeadline = 5min;

// Both user input and daemon-returned locators stay in the logical namespace.
bool logicalPath(std::string_view text) {
    if (text.empty() || text.size()>4096 || text.find_first_of("\\:")!=text.npos || text.find('\0')!=text.npos)
        return false;
    for (;;) {
        const auto slash=text.find('/');
        const auto component=text.substr(0,slash);
        if (component.empty() || component.front()=='.') return false;
        if (slash==text.npos) return true;
        text.remove_prefix(slash+1);
    }
}

// A page token is valid only for one directory revision on one socket.
struct Directory final {
    std::string path, generation, revision;
    std::size_t count;
};

/// Owns a worker-local broker connection. Every receive has the same absolute
/// deadline and validates drive changes even when they arrive between replies.
class Session final {
  public:
    Session(std::chrono::steady_clock::duration duration, SocietyStorageAccess::Canceled canceled)
        : wait_{std::chrono::steady_clock::now()+duration,std::move(canceled)} {}

    /// Reads the initial daemon announcement and checks an optional expected root.
    ii::core::Result<fs::path> connect(const fs::path &broker, const fs::path &expected) {
        if (auto opened=client_.connect(broker,wait_); !opened) return transportError(opened.error());
        auto message=next();
        if (!message) return std::unexpected(message.error());
        if (vm::text(vm::get(*message,"type"))!="state")
            return ii::core::fail(ErrorCode::invalid_argument,"The Society daemon did not announce its selected drive");
        const auto value=ii::core::path_from_utf8(vm::text(vm::get(vm::get(*message,"state"),"selectedContainer")));
        if (!value || !value->is_absolute() || value->lexically_normal()!=*value)
            return ii::core::fail(ErrorCode::closed,"Select a storage drive in Society");
        if (!expected.empty() && *value!=expected)
            return ii::core::fail(ErrorCode::busy,"The Society daemon selected a different drive");
        root_=vm::text(vm::get(vm::get(*message,"state"),"selectedContainer"));
        return *value;
    }

    /// Opens one logical directory and holds its first ready revision.
    ii::core::Result<Directory> directory(const std::string &path) {
        changed_={};
        auto reply=command("directoryOpen",{{"path",vm::Value(path)}});
        if (!reply) return std::unexpected(reply.error());
        auto state=vm::get(*reply,"directory");
        const auto generation=vm::text(vm::get(state,"generation"));
        if (generation.empty() || vm::text(vm::get(state,"path"))!=path)
            return ii::core::fail(ErrorCode::invalid_argument,"The Society daemon returned an invalid directory");
        for (;;) {
            if (vm::text(vm::get(changed_,"generation"))==generation && vm::text(vm::get(changed_,"path"))==path)
                state=std::exchange(changed_,vm::Value{});
            const auto status=vm::text(vm::get(state,"status"));
            const auto revision=vm::text(vm::get(state,"revision"));
            if (status=="ready") {
                const auto count=vm::number(vm::get(state,"count"),-1);
                if (revision.empty() || count<0 || count>entryLimit || std::floor(count)!=count)
                    return ii::core::fail(ErrorCode::out_of_range,"The Society directory exceeds its read bound or has an invalid revision");
                return Directory{path,generation,revision,static_cast<std::size_t>(count)};
            }
            if (status!="loading")
                return ii::core::fail(ErrorCode::closed,"The requested Society directory is unavailable");
            if (auto event=next(); !event) return std::unexpected(event.error());
        }
    }

    /// Reads an exact, revision-pinned page. Partial or mixed snapshots fail.
    ii::core::Result<vm::Value> page(const Directory &directory, std::size_t offset) {
        const auto limit=std::min(pageLimit,directory.count-offset);
        auto reply=command("directoryPage",{{"generation",vm::Value(directory.generation)},
            {"revision",vm::Value(directory.revision)},{"offset",vm::Value(static_cast<int>(offset))},
            {"limit",vm::Value(static_cast<int>(limit))}});
        if (!reply) return std::unexpected(reply.error());
        const auto &entries=vm::get(*reply,"entries");
        if (vm::text(vm::get(*reply,"generation"))!=directory.generation ||
            vm::text(vm::get(*reply,"revision"))!=directory.revision ||
            vm::number(vm::get(*reply,"offset"),-1)!=offset || vm::number(vm::get(*reply,"count"),-1)!=directory.count ||
            entries.kind()!=ii::core::ValueKind::array || vm::array(entries).size()!=limit)
            return ii::core::fail(ErrorCode::busy,"The Society directory changed while reading it");
        return entries;
    }

    /// Requests a local file from the daemon. A resident file may report its
    /// completion before the acknowledgement, so both frames are correlated.
    ii::core::Result<fs::path> activate(const Directory &directory, const std::string &logical) {
        activation_={};
        activationId_=std::to_string(sequence_+1);
        auto result=command("directoryActivate",{{"generation",vm::Value(directory.generation)},
            {"revision",vm::Value(directory.revision)},{"path",vm::Value(logical)}});
        if (!result) return std::unexpected(result.error());
        while (activation_.kind()==ii::core::ValueKind::null) {
            if (auto event=next(); !event) return std::unexpected(event.error());
        }
        if (vm::text(vm::get(activation_,"generation"))!=directory.generation ||
            vm::text(vm::get(activation_,"relative"))!=logical)
            return ii::core::fail(ErrorCode::busy,"The Society file changed while preparing it");
        if (auto error=vm::text(vm::get(activation_,"error")); !error.empty())
            return ii::core::fail(ErrorCode::failed,std::move(error));
        auto path=ii::core::path_from_utf8(vm::text(vm::get(activation_,"path")));
        if (!path || !path->is_absolute() || path->lexically_normal()!=*path)
            return ii::core::fail(ErrorCode::invalid_argument,"The Society daemon did not return a local file");
        return *path;
    }

  private:
    /// Maps a closed or canceled transport back to the worker result.
    std::unexpected<ii::core::Error> transportError(std::string message) const {
        return ii::core::fail(wait_.canceled && wait_.canceled()?ErrorCode::cancelled:ErrorCode::failed,
                              std::move(message));
    }

    /// Receives one frame and retains asynchronous state for this connection.
    ii::core::Result<vm::Value> next() {
        auto frame=client_.receiveJson(wait_);
        if (!frame) return transportError(frame.error());
        auto decoded=ii::json::parse(*frame);
        if (!decoded || !std::holds_alternative<vm::Object>(decoded->storage()))
            return ii::core::fail(ErrorCode::invalid_argument,"The Society daemon returned invalid JSON");
        const auto type=vm::text(vm::get(*decoded,"type"));
        if (!root_.empty()) {
            if ((type=="state" && vm::text(vm::get(vm::get(*decoded,"state"),"selectedContainer"))!=root_) ||
                ((type=="directoryChanged" || type=="directoryActivated") && vm::text(vm::get(*decoded,"container"))!=root_))
                return ii::core::fail(ErrorCode::busy,"The Society daemon changed its selected drive");
        }
        if (type=="directoryChanged") changed_=vm::get(*decoded,"directory");
        if (type=="directoryActivated" && vm::text(vm::get(*decoded,"id"))==activationId_) activation_=*decoded;
        return std::move(*decoded);
    }

    /// Sends one request and accepts only its matching result on this socket.
    ii::core::Result<vm::Value> command(const char *operation, vm::Object fields) {
        const auto id=std::to_string(++sequence_);
        fields.emplace("id",vm::Value(id));
        fields.emplace("op",vm::Value(operation));
        fields.emplace("container",vm::Value(root_));
        auto frame=ii::json::stringify(vm::Value(std::move(fields)));
        if (!frame) return ii::core::fail(ErrorCode::invalid_argument,"Cannot encode the Society daemon request");
        if (auto sent=client_.sendJson(*frame,wait_); !sent) return transportError(sent.error());
        for (;;) {
            auto reply=next();
            if (!reply) return reply;
            if (vm::text(vm::get(*reply,"type"))!="result" || vm::text(vm::get(*reply,"id"))!=id) continue;
            if (!vm::boolean(vm::get(*reply,"accepted")))
                return ii::core::fail(ErrorCode::failed,vm::text(vm::get(*reply,"error"),"The Society daemon rejected the request"));
            return reply;
        }
    }

    iiSocietyHelper::ResidentNative client_;
    iiSocietyHelper::ResidentWait wait_;
    std::string root_,activationId_;
    vm::Value changed_,activation_;
    std::uint64_t sequence_=0;
};

// Transform only validated entries in the requested directory.
ii::core::Result<vm::Value> alias(const vm::Value &row, const SocietyListing &drive,
    const std::string &section, const std::string &logicalSection, const std::string &folder) {
    const auto path=vm::text(vm::get(row,"path"));
    const auto name=vm::text(vm::get(row,"name"));
    const auto child=ii::core::path_from_utf8(name);
    if (!logicalPath(path) || !child || child->filename()!=*child || path!=folder+'/'+name ||
        !path.starts_with(logicalSection+'/') ||
        vm::get(row,"directory").kind()!=ii::core::ValueKind::boolean ||
        vm::get(row,"resident").kind()!=ii::core::ValueKind::boolean)
        return ii::core::fail(ErrorCode::invalid_argument,"The Society daemon returned a file outside the requested directory");
    const auto relative=path.substr(logicalSection.size()+1);
    const auto native=ii::core::path_from_utf8(relative);
    auto root=ii::core::path_to_utf8(drive.root);
    if (!native) return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society file location");
    const auto location=ii::core::path_to_utf8(drive.sectionPath / *native);
    if (!root || !location) return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society file location");
    const auto modified=vm::text(vm::get(row,"modified"));
    return vm::Value(vm::Object{{"section",vm::Value(section)},{"exists",vm::Value(true)},
        {"key",vm::Value(section+'/'+relative)},{"name",vm::Value(name)},{"relativePath",vm::Value(relative)},
        {"nativePath",vm::Value(*location)},{"isDirectory",vm::get(row,"directory")},{"resident",vm::get(row,"resident")},
        {"referenceRoot",vm::Value(*root)},{"containerId",vm::Value(drive.containerId)},{"modified",vm::Value(modified)},
        {"dateText",vm::Value(modified.substr(0,10))},{"sourceRevision",vm::Value(modified+'-'+vm::text(vm::get(row,"size")))}});
}

// Expand recursive searches and inventories over one connection. The bound
// counts every inspected entry, including results excluded by a filter.
ii::core::Result<vm::Array> readDirectory(Session &session, const SocietyListing &drive,
    const SocietyStorageQuery &request, const std::string &logicalSection) {
    const auto filter=vm::lower(vm::trim(request.filter));
    const bool recursive=request.kind==SocietyListingKind::recent ||
        request.kind==SocietyListingKind::modelEntries || !filter.empty();
    std::vector<std::pair<std::string,std::size_t>> folders{{request.directory.empty()
        ? logicalSection : logicalSection+'/'+request.directory,0}};
    std::set<std::string> keys;
    vm::Array result;
    for (std::size_t index=0;index<folders.size();++index) {
        const auto [folder,depth]=folders[index];
        auto directory=session.directory(folder);
        if (!directory) return std::unexpected(directory.error());
        if (directory->count>entryLimit-keys.size())
            return ii::core::fail(ErrorCode::out_of_range,"The Society inventory exceeds its read bound");
        for (std::size_t offset=0;offset<directory->count;offset+=pageLimit) {
            auto page=session.page(*directory,offset);
            if (!page) return std::unexpected(page.error());
            for (const auto &entry:vm::array(*page)) {
                auto projected=alias(entry,drive,request.section,logicalSection,folder);
                if (!projected) return std::unexpected(projected.error());
                const auto path=vm::text(vm::get(entry,"path"));
                if (!keys.insert(path).second)
                    return ii::core::fail(ErrorCode::invalid_argument,"The Society daemon returned a duplicate file");
                const bool resident=vm::boolean(vm::get(entry,"resident"));
                const bool directory=vm::boolean(vm::get(entry,"directory"));
                if (request.kind==SocietyListingKind::modelEntries && !resident) continue;
                if (recursive && directory) {
                    if (depth>=depthLimit)
                        return ii::core::fail(ErrorCode::out_of_range,"The Society inventory exceeds its directory depth bound");
                    folders.emplace_back(path,depth+1);
                }
                const auto relative=vm::text(vm::get(*projected,"relativePath"));
                if ((request.kind!=SocietyListingKind::recent || (!directory && resident)) &&
                    (filter.empty() || vm::lower(relative).find(filter)!=filter.npos))
                    result.push_back(std::move(*projected));
            }
        }
    }
    if (request.kind==SocietyListingKind::recent ||
        (request.section=="generation-history" && request.directory.empty() && filter.empty())) {
        std::ranges::sort(result,[](const vm::Value &a,const vm::Value &b) {
            const auto left=vm::text(vm::get(a,"modified")), right=vm::text(vm::get(b,"modified"));
            return left==right ? vm::text(vm::get(a,"key"))<vm::text(vm::get(b,"key")) : left>right;
        });
    }
    if (request.limit && result.size()>request.limit) result.resize(request.limit);
    return result;
}
}

SocietyStorageAccess societyDaemonStorage(fs::path broker, fs::path expectedRoot) {
    return {
        .read=[broker,expectedRoot](const SocietyStorageQuery &request, SocietyStorageAccess::Canceled canceled)
            -> ii::core::Result<SocietyListing> {
            if ((!request.directory.empty() && !logicalPath(request.directory)) || request.filter.size()>256 ||
                request.limit>entryLimit)
                return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society directory query");
            Session session(catalogDeadline,std::move(canceled));
            auto root=session.connect(broker,expectedRoot);
            if (!root) return std::unexpected(root.error());
            auto drive=describeSocietySection(*root,request.section);
            if (!drive || request.kind==SocietyListingKind::section) return drive;
            auto folder=ii::core::path_to_utf8(drive->sectionPath.filename());
            if (!folder) return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society section");
            auto listed=readDirectory(session,*drive,request,*folder);
            if (!listed) return std::unexpected(listed.error());
            const auto after=describeSocietySection(*root,request.section);
            if (!after || after->containerId!=drive->containerId || after->sectionPath!=drive->sectionPath)
                return ii::core::fail(ErrorCode::busy,"The Society drive changed during the request");
            drive->aliases=vm::Value(std::move(*listed));
            return drive;
        },
        .materialize=[broker,expectedRoot](const vm::Value &item, SocietyStorageAccess::Canceled canceled)
            -> ii::core::Result<fs::path> {
            const auto section=vm::text(vm::get(item,"section"));
            const auto relative=vm::text(vm::get(item,"relativePath"));
            if (!logicalPath(relative) || vm::boolean(vm::get(item,"isDirectory")))
                return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society file reference");
            Session session(materializeDeadline,std::move(canceled));
            auto root=session.connect(broker,expectedRoot);
            if (!root) return std::unexpected(root.error());
            auto drive=describeSocietySection(*root,section);
            if (!drive || ii::core::path_to_utf8(*root).value_or("")!=vm::text(vm::get(item,"referenceRoot")) ||
                drive->containerId!=vm::text(vm::get(item,"containerId")))
                return ii::core::fail(ErrorCode::busy,"The selected Society drive changed");
            auto folder=ii::core::path_to_utf8(drive->sectionPath.filename());
            const auto native=ii::core::path_from_utf8(relative);
            if (!folder || !native) return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society file reference");
            const auto logical=*folder+'/'+relative;
            auto directory=session.directory(logical.substr(0,logical.rfind('/')));
            if (!directory) return std::unexpected(directory.error());
            auto path=session.activate(*directory,logical);
            if (!path) return path;
            const auto after=describeSocietySection(*root,section);
            iiSocietyContainer::FileTree tree(drive->sectionPath);
            const auto entry=tree.entry(*native);
            if (*path!=drive->sectionPath / *native || !after || after->containerId!=drive->containerId ||
                after->sectionPath!=drive->sectionPath || !entry || entry.value->kind!=iiSocietyContainer::FileTree::Kind::File)
                return ii::core::fail(ErrorCode::busy,"The Society file changed or is not available locally");
            return path;
        }};
}
}
