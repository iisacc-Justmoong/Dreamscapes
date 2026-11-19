#include "NativeSocietyHistory.h"
#include "NativeSocietyStorage.h"
#include <iiSocietyContainer>
#include <algorithm>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
namespace {
using ii::core::ErrorCode;
// Preserve the logical daemon reference on remote results so their media can
// be prepared only when the user opens it.
ii::core::Result<std::vector<vm::Value>> readHistory(const SocietyStorageAccess::Read &read,std::size_t limit,
    std::stop_token stop) {
    auto listing=read({"generation-history","",""},[stop]{return stop.stop_requested();});
    if (!listing) return std::unexpected(listing.error());
    static constexpr std::string_view images[]{".png",".jpg",".jpeg",".webp",".bmp",".gif",".tif",".tiff",".avif",".heic",".heif"};
    static constexpr std::string_view videos[]{".mp4",".mov",".m4v",".webm",".mkv"};
    std::vector<vm::Value> result;
    const iiSocietyContainer::FileTree tree(listing->sectionPath);
    for (const auto &entry:vm::array(listing->aliases)) {
        if (vm::boolean(vm::get(entry,"isDirectory"))) continue;
        const auto name=vm::text(vm::get(entry,"name"));
        const auto dot=name.rfind('.'); const auto extension=dot==name.npos?std::string{}:vm::lower(name.substr(dot));
        const bool image=std::ranges::find(images,extension)!=std::end(images);
        const bool video=std::ranges::find(videos,extension)!=std::end(videos);
        const auto path=vm::text(vm::get(entry,"nativePath"));
        if ((!image && !video) || path.empty()) continue;
        const bool referenced=vm::has(entry,"referenceRoot");
        const bool resident=!vm::has(entry,"resident") || vm::boolean(vm::get(entry,"resident"));
        auto sourceRevision=vm::text(vm::get(entry,"sourceRevision"));
        if (referenced && resident) {
            // The broker reports ISO time at millisecond precision. The
            // original-export check uses the native file clock and exact size.
            const auto relative=ii::core::path_from_utf8(vm::text(vm::get(entry,"relativePath")));
            if (!relative) return ii::core::fail(ErrorCode::invalid_argument,"Invalid Society history file");
            const auto local=tree.entry(*relative);
            if (!local || local.value->kind!=iiSocietyContainer::FileTree::Kind::File)
                return ii::core::fail(ErrorCode::busy,"A Society history file changed while being read");
            sourceRevision=societySourceRevision(local.value->lastModified,local.value->size);
        }
        vm::Object item{{"containerId",vm::Value(listing->containerId)},{"key",vm::get(entry,"key")},
            {"name",vm::Value(name)},{"displayName",vm::Value(name)},{"path",vm::Value(path)},{"mediaType",vm::Value(image?"Image":"Video")},
            {"sourceRevision",vm::Value(sourceRevision)}};
        if (referenced) item.emplace("societyReference",entry);
        if (resident) {
            auto url=societyFileUrl(path);
            if (image) {
                item.emplace("imageSource",vm::Value(url));
                if (url.size()<8000) item.emplace("thumbnailSource",vm::Value(url+"?v="+sourceRevision));
            } else item.emplace("mediaSource",vm::Value(std::move(url)));
        }
        result.emplace_back(std::move(item));
        if (limit && result.size()>=limit) break;
    }
    return result;
}
}
struct SocietyHistoryModel::Bridge final {
    struct Completion {std::uint64_t id;ii::core::Result<std::vector<vm::Value>> result;};
    std::mutex mutex;
    std::optional<Completion> completion;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
SocietyHistoryModel::SocietyHistoryModel(std::shared_ptr<review::Session> session,ii::runtime::EventLoop::Sender sender,
    CanvasFileModel::Submit submit,SocietyStorageAccess::Read read)
    : session_(std::move(session)),sender_(std::move(sender)),submit_(std::move(submit)),read_(std::move(read)) {
    if (!session_ || !submit_ || !read_) throw std::invalid_argument("History requires a session, worker and provider");
}
std::shared_ptr<SocietyHistoryModel> SocietyHistoryModel::create(std::shared_ptr<review::Session> session,
    ii::runtime::EventLoop &loop,CanvasFileModel::Submit submit,std::filesystem::path selection,std::filesystem::path root) {
    if (selection.empty()) throw std::invalid_argument("History requires a selection");
    return create(std::move(session),loop,std::move(submit),localSocietyStorage(std::move(selection),std::move(root)).read);
}

// The read callback is invoked only on the submitted worker.
std::shared_ptr<SocietyHistoryModel> SocietyHistoryModel::create(std::shared_ptr<review::Session> session,
    ii::runtime::EventLoop &loop,CanvasFileModel::Submit submit,SocietyStorageAccess::Read read) {
    auto value=std::shared_ptr<SocietyHistoryModel>(new SocietyHistoryModel(
        std::move(session),loop.sender(),std::move(submit),std::move(read)));
    value->start(); return value;
}
void SocietyHistoryModel::start() {
    const std::weak_ptr<SocietyHistoryModel> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak]{if (auto self=weak.lock()) self->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register history completion");
    wake_=admitted.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
}
SocietyHistoryModel::~SocietyHistoryModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> SocietyHistoryModel::refresh(std::size_t limit) {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"History requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"History is closed");
    if (limit>10000) return ii::core::fail(ErrorCode::out_of_range,"History limit exceeds the storage scan bound");
    if (next_==std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range,"History request identity exhausted");
    const auto id=++next_; pending_=id;pendingLimit_=limit;
    if (running_) requestStop_.request_stop();
    (void)loading.set(true);
    if (!active_ || next_!=id) return {};
    (void)status.set("Reading Society generation history…");
    if (!active_ || next_!=id) return {};
    if (!running_) launch();
    return {};
}
void SocietyHistoryModel::launch() {
    if (!active_ || running_ || !pending_) return;
    const auto id=std::exchange(pending_,0);const auto limit=pendingLimit_; running_=true;
    requestStop_=std::stop_source{};
    auto bridge=bridge_;auto read=read_;
    bool accepted=false;
    try {accepted=submit_([bridge,read=std::move(read),stop=requestStop_.get_token(),id,limit] {
        ii::core::Result<std::vector<vm::Value>> result=ii::core::fail(ErrorCode::failed,"Cannot read generation history");
        try {result=readHistory(read,limit,stop);} catch (...) {
            result=ii::core::fail(ErrorCode::failed,"Cannot read generation history",std::current_exception());
        }
        {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->completion.emplace(Bridge::Completion{id,std::move(result)});}
        (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
    });} catch (...) {accepted=false;}
    if (!accepted) {
        running_=false;
        if (id==next_) fail("Generation history worker is unavailable",id);
        if (active_ && pending_) launch();
    }
}
void SocietyHistoryModel::fail(std::string message,std::uint64_t request) {
    const auto current=[&] {return !request || (active_ && request==next_);};
    session_->replaceEntries({});
    if (!current()) return;
    if (auto handler=snapshot_) handler({});
    if (!current()) return;
    (void)available.set(false);
    if (!current()) return;
    (void)loading.set(false);
    if (current()) (void)status.set(std::move(message));
}
void SocietyHistoryModel::deliver() {
    if (!active_) return;
    std::optional<Bridge::Completion> completion;
    {std::lock_guard lock(bridge_->mutex);completion=std::move(bridge_->completion);bridge_->completion.reset();}
    if (!completion) return;
    running_=false;
    if (completion->id==next_) {
        if (completion->result) {
            session_->replaceEntries(*completion->result);
            if (active_ && completion->id==next_) {
                if (auto handler=snapshot_) handler(*completion->result);
            }
            if (active_ && completion->id==next_) {
                (void)available.set(true);
                if (active_ && completion->id==next_) (void)loading.set(false);
                if (active_ && completion->id==next_) (void)status.set({});
            }
        } else fail(completion->result.error().message,completion->id);
    }
    if (active_ && pending_) launch();
}
void SocietyHistoryModel::setSnapshotHandler(SnapshotHandler value) {
    if (owner_==std::this_thread::get_id() && active_) snapshot_=std::move(value);
}
void SocietyHistoryModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("History requires its owner thread");
    if (!active_) return;
    active_=false;pending_=0;submit_={};snapshot_={};
    requestStop_.request_stop(); read_={};
    {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->completion.reset();}
    (void)sender_.cancel(wake_);
    fail("Generation history unavailable");
}
}
