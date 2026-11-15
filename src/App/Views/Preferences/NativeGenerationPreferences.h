#pragma once
#include "NativePreferencesWorkspace.h"
#include <iiSDKCore/Runtime/EventLoop.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>

namespace dreamscapes {struct ModelPreferences;}
namespace dreamscapes::preferences {
// The path follows the previous application's platform configuration location.
[[nodiscard]] std::filesystem::path generationPreferencesPath();

// Owner-thread view projection; file reads and transactional writes run on the
// supplied executor. Accepted writes remain owned by the worker after close().
class GenerationPreferences final : public std::enable_shared_from_this<GenerationPreferences> {
  public:
    using Submit=std::function<bool(ii::runtime::Task)>;
    static std::shared_ptr<GenerationPreferences> create(std::shared_ptr<WorkspaceModel>,
        ii::runtime::EventLoop &,Submit,std::filesystem::path);
    ~GenerationPreferences();
    [[nodiscard]] ii::core::Result<void> reload();
    void setDefaultsHandler(std::function<void(const ModelPreferences &)>);
    void close();
    ii::core::Observable<bool> saving{false};
    ii::core::Observable<std::string> error;
  private:
    struct Bridge;
    GenerationPreferences(std::shared_ptr<WorkspaceModel>,ii::runtime::EventLoop::Sender,Submit,std::filesystem::path);
    void start();
    void deliver();
    [[nodiscard]] ii::core::Result<void> edit(std::string,std::string,std::string);
    void statusAll(const std::string &);
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<WorkspaceModel> workspace_;
    ii::runtime::EventLoop::Sender sender_;
    Submit submit_;
    std::filesystem::path path_;
    std::shared_ptr<Bridge> bridge_;
    std::function<void(const ModelPreferences &)> defaults_;
    ii::runtime::TaskToken wake_;
    bool active_=true;
};
}
