#pragma once
#include "NativeSocietyStorage.h"
#include "NativeCanvasFileModel.h"
#include "App/Views/Home/NativeStorageWorkspace.h"
#include <iiSDKCore>
#include <optional>
#include <atomic>
#include <functional>
#include <thread>

namespace dreamscapes::native {
class SocietyStorageModel final : public std::enable_shared_from_this<SocietyStorageModel> {
  public:
    using Canceled=std::function<bool()>;
    using Browse=std::function<ii::core::Result<SocietyListing>(const std::filesystem::path &,const std::string &,
        const std::string &,const std::string &,const std::filesystem::path &,Canceled)>;
    static std::shared_ptr<SocietyStorageModel> create(std::shared_ptr<storage::WorkspaceModel>,
        ii::runtime::EventLoop &,CanvasFileModel::Submit,std::filesystem::path selection,std::filesystem::path explicitRoot={},Browse browse={});
    ~SocietyStorageModel();
    void setLocationHandler(std::function<void(std::optional<std::filesystem::path>)>);
    // The result is valid for the duration of the callback; consumers copy
    // any values they need after it returns.
    void setListingHandler(std::function<void(const ii::core::Result<SocietyListing> &)>);
    void close();
  private:
    struct Request;
    struct Bridge;
    SocietyStorageModel(std::shared_ptr<storage::WorkspaceModel>,ii::runtime::EventLoop::Sender,
                        CanvasFileModel::Submit,std::filesystem::path,std::filesystem::path,Browse);
    void start();
    void request(std::string,std::string,std::string);
    void launch();
    void deliver();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<storage::WorkspaceModel> workspace_;
    ii::runtime::EventLoop::Sender sender_;
    CanvasFileModel::Submit submit_;
    std::filesystem::path selection_;
    std::filesystem::path explicitRoot_;
    Browse browse_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<std::atomic_bool> cancel_;
    ii::runtime::TaskToken wake_;
    std::function<void(std::optional<std::filesystem::path>)> location_;
    std::function<void(const ii::core::Result<SocietyListing> &)> listing_;
    std::uint64_t next_=0;
    std::unique_ptr<Request> pending_;
    bool active_=true,running_=false;
};
}
