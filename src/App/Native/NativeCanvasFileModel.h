#pragma once
#include "NativeCanvasFiles.h"
#include "NativeDocumentSession.h"
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace dreamscapes::native {
class CanvasFileModel final : public std::enable_shared_from_this<CanvasFileModel> {
  public:
    // submit must either accept a task once or return false without running it.
    using Submit=std::function<bool(ii::runtime::Task)>;
    /// Prepares a referenced file on the file worker before opening its bytes.
    /// The returned local path must match the captured path.
    using Resolve=std::function<ii::core::Result<std::filesystem::path>(std::stop_token)>;
    static std::shared_ptr<CanvasFileModel> create(std::shared_ptr<DocumentSession>,ii::runtime::EventLoop &,Submit);
    ~CanvasFileModel();
    [[nodiscard]] ii::core::Result<void> open(std::filesystem::path,Resolve resolve={});
    [[nodiscard]] ii::core::Result<void> save();
    [[nodiscard]] ii::core::Result<void> saveAs(std::filesystem::path);
    void setOpenedHandler(std::function<void()>);
    void reportError(std::string);
    void close();
    // Last version verified by the file worker; no filesystem access is done here.
    [[nodiscard]] std::optional<CanvasFileVersion> version() const;
    ii::core::Observable<bool> busy{false},canSave{false},canSaveAs{false};
    ii::core::Observable<std::string> path,status,error;
  private:
    struct Bridge;
    struct Request;
    CanvasFileModel(std::shared_ptr<DocumentSession>,ii::runtime::EventLoop::Sender,Submit);
    void start();
    [[nodiscard]] ii::core::Result<void> submit(Request);
    [[nodiscard]] ii::core::Result<void> writable() const;
    void deliver();
    void refresh();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<DocumentSession> document_;
    ii::runtime::EventLoop::Sender sender_;
    Submit submit_;
    std::shared_ptr<Bridge> bridge_;
    ii::runtime::TaskToken wake_;
    ii::core::ObservableSubscription identityObserver_,dirtyObserver_,documentObserver_;
    std::optional<CanvasFileVersion> version_;
    std::function<void()> opened_;
    std::stop_source openStop_;
    std::uint64_t next_=0,current_=0;
    bool active_=true;
};
}
