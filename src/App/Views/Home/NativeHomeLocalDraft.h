#pragma once
#include "NativeHomeComposer.h"
#include "App/ViewModels/Native/GenerationDraft.h"
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace dreamscapes {struct ModelPreferences;}
namespace dreamscapes::home {
struct GenerationModelOption final {
    std::string id,name;
    bool image=false,video=false;
    bool operator==(const GenerationModelOption &) const = default;
};
// Session-only generation input. No inference, document or storage service is owned here.
class NativeHomeLocalDraft final : public std::enable_shared_from_this<NativeHomeLocalDraft> {
  public:
    static std::shared_ptr<NativeHomeLocalDraft> create();
    ~NativeHomeLocalDraft();
    [[nodiscard]] std::shared_ptr<HomeComposerModel> model() const {return model_;}
    void setAdvancedHandler(std::function<ii::core::Result<void>(bool)>);
    [[nodiscard]] ii::core::Result<void> setDefaultPrompts(const ModelPreferences &);
    [[nodiscard]] ii::core::Result<void> setModelCatalog(std::vector<GenerationModelOption>,std::string error={});
  private:
    NativeHomeLocalDraft();
    void start();
    [[nodiscard]] ii::core::Result<void> command(const HomeComposerCommand &);
    [[nodiscard]] ii::core::Result<void> synchronize();
    void reconcileModels();
    struct ModelSelection {std::string selected,preferred;bool overridden=false;};
    const std::thread::id owner_=std::this_thread::get_id();
    std::vector<GenerationModelOption> models_;
    std::string modelError_;
    ModelSelection imageModel_,videoModel_;
    vm::ComposerState draft_;
    std::shared_ptr<HomeComposerModel> model_;
    std::function<ii::core::Result<void>(bool)> advanced_;
};
}
