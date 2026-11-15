#include "NativeCanvasFileModel.h"
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace dreamscapes::native {
using ii::core::ErrorCode;
struct CanvasFileModel::Request final {
    enum class Action { Open,Import,Save,SaveAs } action=Action::Open;
    std::uint64_t id=0,generation=0,revision=0;
    std::filesystem::path path;
    std::optional<CanvasFileVersion> version;
    std::shared_ptr<const iiSharedCanvas::Document> snapshot;
};
struct CanvasFileModel::Bridge final {
    struct Completion final {Request request; ii::core::Result<CanvasFileResult> result;};
    std::mutex mutex;
    std::optional<Completion> completion;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
CanvasFileModel::CanvasFileModel(std::shared_ptr<DocumentSession> document,ii::runtime::EventLoop::Sender sender,Submit submit)
    : document_(std::move(document)),sender_(std::move(sender)),submit_(std::move(submit)) {
    if (!document_ || !submit_) throw std::invalid_argument("Canvas file model requires a document and worker");
}
std::shared_ptr<CanvasFileModel> CanvasFileModel::create(std::shared_ptr<DocumentSession> document,
    ii::runtime::EventLoop &loop,Submit submit) {
    auto value=std::shared_ptr<CanvasFileModel>(new CanvasFileModel(std::move(document),loop.sender(),std::move(submit)));
    value->start(); return value;
}
void CanvasFileModel::start() {
    const std::weak_ptr<CanvasFileModel> weak=shared_from_this();
    auto admitted=sender_.on_signal([weak]{if (auto value=weak.lock()) value->deliver();});
    if (!admitted) throw std::runtime_error("Cannot register canvas file completion");
    wake_=admitted.token;
    bridge_=std::make_shared<Bridge>(); bridge_->sender=sender_; bridge_->wake=wake_;
    identityObserver_=document_->documentGeneration.observe([weak](const auto &,std::uint64_t){
        if (auto value=weak.lock()) {value->version_.reset(); (void)value->path.set({}); value->refresh();}
    });
    auto changed=[weak](const auto &,std::uint64_t){if (auto value=weak.lock()) value->refresh();};
    dirtyObserver_=document_->dirty.observe(changed); documentObserver_=document_->hasDocument.observe(changed);
    refresh();
}
CanvasFileModel::~CanvasFileModel() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
std::optional<CanvasFileVersion> CanvasFileModel::version() const {
    return active_ && owner_==std::this_thread::get_id()?version_:std::nullopt;
}
ii::core::Result<void> CanvasFileModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Canvas files require their owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Canvas file model is closed");
    return {};
}
void CanvasFileModel::refresh() {
    const bool available=active_ && !busy.get() && document_->hasDocument.get();
    (void)canSaveAs.set(available);
    (void)canSave.set(available && document_->dirty.get() && version_ && version_->working);
}
ii::core::Result<void> CanvasFileModel::open(std::filesystem::path pathValue) {
    if (auto valid=writable(); !valid) return valid;
    if (document_->hasDocument.get() && document_->dirty.get())
        return ii::core::fail(ErrorCode::busy,"Save the current canvas before opening another");
    if (!supportsCanvasInput(pathValue)) return ii::core::fail(ErrorCode::invalid_argument,"Choose an absolute canvas or supported image file path");
    Request request; request.action=isCanvasImage(pathValue)?Request::Action::Import:Request::Action::Open;
    request.path=std::move(pathValue);
    return submit(std::move(request));
}
ii::core::Result<void> CanvasFileModel::save() {
    if (auto valid=writable(); !valid) return valid;
    if (!version_ || !version_->working || !document_->hasDocument.get())
        return ii::core::fail(ErrorCode::unsupported,"Choose a new file for this canvas");
    Request request; request.action=Request::Action::Save; request.version=version_;
    return submit(std::move(request));
}
ii::core::Result<void> CanvasFileModel::saveAs(std::filesystem::path pathValue) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_->hasDocument.get()) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    Request request; request.action=Request::Action::SaveAs; request.path=std::move(pathValue);
    return submit(std::move(request));
}
ii::core::Result<void> CanvasFileModel::submit(Request request) {
    if (busy.get()) return ii::core::fail(ErrorCode::busy,"A canvas file operation is already running");
    if (next_==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Canvas file operation identity is exhausted");
    request.id=++next_; request.generation=document_->documentGeneration.get(); request.revision=document_->revision.get();
    const bool reading=request.action==Request::Action::Open || request.action==Request::Action::Import;
    if (!reading) request.snapshot=document_->snapshot();
    current_=request.id; (void)busy.set(true); (void)error.set({});
    (void)status.set(reading?"Opening canvas…":"Saving canvas…"); refresh();
    auto bridge=bridge_;
    bool accepted=false;
    try {
        accepted=submit_([bridge,request=std::move(request)]() mutable {
            ii::core::Result<CanvasFileResult> result=ii::core::fail(ErrorCode::failed,"Canvas file operation failed");
            try {
                switch (request.action) {
                case Request::Action::Open: result=openCanvasFile(request.path); break;
                case Request::Action::Import: result=importCanvasImage(request.path); break;
                case Request::Action::Save: result=saveCanvasFile(*request.version,*request.snapshot); break;
                case Request::Action::SaveAs: result=createCanvasFile(request.path,*request.snapshot); break;
                }
            } catch (...) {result=ii::core::fail(ErrorCode::failed,"Canvas file operation failed",std::current_exception());}
            {
                std::lock_guard lock(bridge->mutex);
                if (bridge->closed) return;
                bridge->completion.emplace(Bridge::Completion{std::move(request),std::move(result)});
            }
            (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
        });
    } catch (...) {accepted=false;}
    if (!accepted) {
        current_=0; (void)busy.set(false); (void)status.set({});
        (void)error.set("Canvas file worker is unavailable"); refresh();
        return ii::core::fail(ErrorCode::busy,error.get());
    }
    return {};
}
void CanvasFileModel::deliver() {
    if (!active_) return;
    std::optional<Bridge::Completion> completed;
    {std::lock_guard lock(bridge_->mutex); completed=std::move(bridge_->completion); bridge_->completion.reset();}
    if (!completed || completed->request.id!=current_) return;
    current_=0;
    const auto finish=[this] {
        if (!active_) return;
        (void)busy.set(false);
        if (active_) refresh();
    };
    auto &request=completed->request; auto &result=completed->result;
    if (!result) {(void)status.set({}); (void)error.set(result.error().message); finish(); return;}
    const bool reading=request.action==Request::Action::Open || request.action==Request::Action::Import;
    if (document_->documentGeneration.get()!=request.generation ||
        (reading && document_->revision.get()!=request.revision)) {
        (void)status.set(reading?"Open canceled because the canvas changed":
                         "Previous canvas was saved; the current canvas was not changed");
        finish(); return;
    }
    if (reading) {
        const auto name=result->version.path.filename().u8string();
        const bool imported=request.action==Request::Action::Import;
        if (auto attached=document_->attach(std::move(result->document),std::string(name.begin(),name.end()),imported); !attached) {
            (void)status.set({}); (void)error.set(attached.error().message); finish(); return;
        }
        if (imported) {
            version_.reset();(void)path.set({});
            (void)status.set(result->warning.empty()?"Image imported; save as a canvas":"Image imported; "+result->warning);
        } else {
            version_=std::move(result->version);
            const auto text=version_->path.u8string(); (void)path.set(std::string(text.begin(),text.end()));
            (void)status.set("Canvas opened");
        }
        if (auto handler=opened_) try {handler();} catch (...) {
            (void)error.set("Canvas opened, but navigation failed");
        }
        finish();
        return;
    }
    version_=std::move(result->version);
    const auto text=version_->path.u8string(); (void)path.set(std::string(text.begin(),text.end()));
    auto acknowledged=document_->acknowledgeSave(request.generation,request.revision,*request.snapshot,std::move(result->document));
    if (!acknowledged) {
        (void)status.set("Canvas was saved"); (void)error.set(acknowledged.error().message); finish(); return;
    }
    if (request.action==Request::Action::SaveAs) {
        const auto name=version_->path.filename().u8string(); (void)document_->title.set(std::string(name.begin(),name.end()));
    }
    (void)status.set(*acknowledged?"Canvas saved":"Canvas saved; newer edits remain unsaved"); finish();
}
void CanvasFileModel::setOpenedHandler(std::function<void()> handler) {
    if (!writable()) return;
    opened_=std::move(handler);
}
void CanvasFileModel::reportError(std::string message) {
    if (!writable()) return;
    (void)status.set({}); (void)error.set(std::move(message));
}
void CanvasFileModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Canvas files require their owner thread");
    if (!active_) return;
    active_=false; current_=0; submit_={}; opened_={};
    identityObserver_={}; dirtyObserver_={}; documentObserver_={};
    if (bridge_) {std::lock_guard lock(bridge_->mutex); bridge_->closed=true; bridge_->completion.reset();}
    (void)sender_.cancel(wake_); (void)busy.set(false); (void)canSave.set(false); (void)canSaveAs.set(false);
}
}
