#pragma once
#include "App/ViewModels/Native/ViewValue.h"
#include "NativeCanvasFileModel.h"
#include <iiGUI>
#include <iiSDKCore>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::native {
// Owns the picker and worker lifecycle for saving one review original.
// Neither the selection nor a canvas document is modified by this operation.
class ReviewFileSave final : public std::enable_shared_from_this<ReviewFileSave> {
  public:
    using Picker=std::function<ii::core::Result<void>(ii::gui::FileDialogOptions,ii::gui::FileDialogCompletion)>;
    static std::shared_ptr<ReviewFileSave> create(ii::runtime::EventLoop &,CanvasFileModel::Submit);
    ~ReviewFileSave();
    void setPicker(Picker);
    [[nodiscard]] bool canSave(const vm::Value &) const;
    [[nodiscard]] ii::core::Result<void> save(const vm::Value &);
    void close();
    ii::core::Observable<bool> busy{false};
    ii::core::Observable<std::string> status;
  private:
    struct Bridge;
    struct Request;
    ReviewFileSave(ii::runtime::EventLoop::Sender,CanvasFileModel::Submit);
    void start();
    void picked(std::uint64_t,Request,ii::core::Result<ii::gui::FileDialogSelection>);
    void deliver();
    void finish(std::string);
    const std::thread::id owner_=std::this_thread::get_id();
    ii::runtime::EventLoop::Sender sender_;
    CanvasFileModel::Submit submit_;
    Picker picker_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<std::atomic_bool> canceled_;
    ii::runtime::TaskToken wake_;
    std::uint64_t next_=0,current_=0;
    bool active_=true,dialogPending_=false;
};
}
