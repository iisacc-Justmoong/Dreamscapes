#include "NativeSocietyStorageModel.h"
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
struct SocietyStorageModel::Request final {
    std::uint64_t id;
    std::string section,directory,query;
};
struct SocietyStorageModel::Bridge final {
    struct Completion {std::uint64_t id; ii::core::Result<SocietyListing> result;};
    std::mutex mutex;
    std::optional<Completion> completion;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
SocietyStorageModel::SocietyStorageModel(std::shared_ptr<storage::WorkspaceModel> workspace,
    ii::runtime::EventLoop::Sender sender,CanvasFileModel::Submit submit,std::filesystem::path selection,std::filesystem::path root,Browse browse)
    : workspace_(std::move(workspace)),sender_(std::move(sender)),submit_(std::move(submit)),selection_(std::move(selection)),explicitRoot_(std::move(root)),browse_(std::move(browse)) {
    if (!workspace_ || !submit_ || selection_.empty()) throw std::invalid_argument("Society storage requires a worker and selection");
    if (!browse_) browse_=[](const auto &path,const auto &section,const auto &directory,const auto &query,const auto &explicitRoot,Canceled) {
        return browseSociety(path,section,directory,query,explicitRoot);
    };
}
std::shared_ptr<SocietyStorageModel> SocietyStorageModel::create(std::shared_ptr<storage::WorkspaceModel> workspace,
    ii::runtime::EventLoop &loop,CanvasFileModel::Submit submit,std::filesystem::path selection,std::filesystem::path root,Browse browse) {
    auto value=std::shared_ptr<SocietyStorageModel>(new SocietyStorageModel(std::move(workspace),loop.sender(),std::move(submit),std::move(selection),std::move(root),std::move(browse)));
    value->start(); return value;
}
void SocietyStorageModel::start() {
    const std::weak_ptr<SocietyStorageModel> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak]{if (auto self=weak.lock()) self->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register Society storage completion");
    wake_=admitted.token; bridge_=std::make_shared<Bridge>(); bridge_->sender=sender_; bridge_->wake=wake_;
    workspace_->setBrowseHandler([weak](auto section,auto directory,auto query) {
        if (auto self=weak.lock()) self->request(std::move(section),std::move(directory),std::move(query));
    });
}
SocietyStorageModel::~SocietyStorageModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
void SocietyStorageModel::request(std::string section,std::string directory,std::string query) {
    if (!active_ || owner_!=std::this_thread::get_id()) return;
    if (next_==std::numeric_limits<std::uint64_t>::max()) {workspace_->setUnavailable("Storage request identity exhausted"); return;}
    pending_=std::make_unique<Request>(Request{++next_,std::move(section),std::move(directory),std::move(query)});
    if (cancel_) cancel_->store(true);
    if (!running_) launch();
}
void SocietyStorageModel::launch() {
    if (!active_ || running_ || !pending_) return;
    auto request=std::move(*pending_); pending_.reset(); running_=true;const auto id=request.id;
    auto bridge=bridge_; auto selection=selection_; auto root=explicitRoot_;auto browse=browse_;
    auto cancel=std::make_shared<std::atomic_bool>(false);cancel_=cancel;
    bool accepted=false;
    try {accepted=submit_([bridge,selection=std::move(selection),root=std::move(root),request=std::move(request),browse=std::move(browse),cancel]() mutable {
        ii::core::Result<SocietyListing> result=ii::core::fail(ii::core::ErrorCode::failed,"Cannot read Society storage");
        try {result=browse(selection,request.section,request.directory,request.query,root,[cancel]{return cancel->load();});}
        catch (...) {result=ii::core::fail(ii::core::ErrorCode::failed,"Cannot read Society storage",std::current_exception());}
        {
            std::lock_guard lock(bridge->mutex);
            if (bridge->closed) return;
            bridge->completion.emplace(Bridge::Completion{request.id,std::move(result)});
        }
        (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
    });} catch (...) {accepted=false;}
    if (!accepted) {
        running_=false;cancel_.reset();
        if (id==next_) {
            if (auto handler=listing_) {
                ii::core::Result<SocietyListing> failure=ii::core::fail(ii::core::ErrorCode::closed,"Society storage worker is unavailable");
                handler(failure);
            }
            if (active_ && id==next_) workspace_->setUnavailable("Society storage worker is unavailable");
            if (active_ && id==next_ && location_) location_(std::nullopt);
        }
        if (pending_) launch();
    }
}
void SocietyStorageModel::deliver() {
    if (!active_) return;
    std::optional<Bridge::Completion> completion;
    {std::lock_guard lock(bridge_->mutex); completion=std::move(bridge_->completion); bridge_->completion.reset();}
    if (!completion) return;
    running_=false;cancel_.reset();
    if (completion->id==next_) {
        if (auto handler=listing_) handler(completion->result);
        if (!active_ || completion->id!=next_) {if (active_ && pending_) launch();return;}
        if (completion->result) {
            auto &listing=*completion->result;
            if (auto result=workspace_->setSnapshot(std::move(listing.aliases)); !result) workspace_->setUnavailable(result.error().message);
            else if (active_ && completion->id==next_) {
                auto handler=location_; if (handler) handler(listing.root);
            }
        } else {
            workspace_->setUnavailable(completion->result.error().message);
            if (active_ && completion->id==next_) {auto handler=location_; if (handler) handler(std::nullopt);}
        }
    }
    if (active_ && pending_) launch();
}
void SocietyStorageModel::setLocationHandler(std::function<void(std::optional<std::filesystem::path>)> value) {
    if (owner_==std::this_thread::get_id() && active_) location_=std::move(value);
}
void SocietyStorageModel::setListingHandler(std::function<void(const ii::core::Result<SocietyListing> &)> value) {
    if (owner_==std::this_thread::get_id() && active_) listing_=std::move(value);
}
void SocietyStorageModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Society storage requires its owner thread");
    if (!active_) return;
    active_=false; pending_.reset(); submit_={}; location_={};listing_={};browse_={};
    if (cancel_) cancel_->store(true);
    workspace_->setBrowseHandler({}); workspace_->setUnavailable("Storage unavailable");
    {std::lock_guard lock(bridge_->mutex); bridge_->closed=true; bridge_->completion.reset();}
    (void)sender_.cancel(wake_);
}
}
