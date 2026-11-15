#pragma once
#include "NativeCanvasFileModel.h"
#include "App/Views/Result/NativeReviewSession.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::native {
// Owner-thread history projection. Filesystem access and metadata conversion
// happen on the submitted worker; review state remains in the supplied session.
class SocietyHistoryModel final : public std::enable_shared_from_this<SocietyHistoryModel> {
  public:
    static std::shared_ptr<SocietyHistoryModel> create(std::shared_ptr<review::Session>,
        ii::runtime::EventLoop &,CanvasFileModel::Submit,std::filesystem::path selection,std::filesystem::path explicitRoot={});
    ~SocietyHistoryModel();
    // A zero limit requests all results supported by the bounded storage scan.
    [[nodiscard]] ii::core::Result<void> refresh(std::size_t limit=0);
    using SnapshotHandler=std::function<void(const std::vector<vm::Value> &)>;
    void setSnapshotHandler(SnapshotHandler);
    void close();
    ii::core::Observable<bool> available{false},loading{false};
    ii::core::Observable<std::string> status{"Generation history unavailable"};
  private:
    struct Bridge;
    SocietyHistoryModel(std::shared_ptr<review::Session>,ii::runtime::EventLoop::Sender,
        CanvasFileModel::Submit,std::filesystem::path,std::filesystem::path);
    void start();
    void launch();
    void deliver();
    void fail(std::string,std::uint64_t request=0);
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<review::Session> session_;
    ii::runtime::EventLoop::Sender sender_;
    CanvasFileModel::Submit submit_;
    std::filesystem::path selection_,root_;
    SnapshotHandler snapshot_;
    std::shared_ptr<Bridge> bridge_;
    ii::runtime::TaskToken wake_;
    std::uint64_t next_=0,pending_=0;
    std::size_t pendingLimit_=0;
    bool active_=true,running_=false;
};
}
