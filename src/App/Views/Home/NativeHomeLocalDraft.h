#pragma once
#include "NativeHomeComposer.h"
#include "App/ViewModels/Native/GenerationDraft.h"
#include <functional>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace dreamscapes {struct ModelPreferences;}
namespace dreamscapes::home {
struct GenerationModelOption final {
    std::string id,name;
    bool image=false,video=false;
    vm::Value source;
    bool operator==(const GenerationModelOption &) const = default;
};
// Session-only generation input. No inference, document or storage service is owned here.
class NativeHomeLocalDraft final : public std::enable_shared_from_this<NativeHomeLocalDraft> {
  public:
    static std::shared_ptr<NativeHomeLocalDraft> create();
    ~NativeHomeLocalDraft();
    [[nodiscard]] std::shared_ptr<HomeComposerModel> model() const {return model_;}
    void setAdvancedHandler(std::function<ii::core::Result<void>(bool)>);
    void setSubmitHandler(std::function<ii::core::Result<void>(const HomeComposerSnapshot &)>,bool image,bool video);
    [[nodiscard]] ii::core::Result<void> setDefaultPrompts(const ModelPreferences &);
    [[nodiscard]] ii::core::Result<void> setModelCatalog(std::vector<GenerationModelOption>,std::string error={});
    [[nodiscard]] vm::Value modelSource(std::string_view id) const;
  private:
    NativeHomeLocalDraft();
    void start();
    [[nodiscard]] ii::core::Result<void> command(const HomeComposerCommand &);
    [[nodiscard]] ii::core::Result<void> synchronize();
    [[nodiscard]] ii::core::Result<void> synchronize(const vm::Value &);
    void reconcileModels();
    struct ModelSelection {
        std::string selected, preferred;
        HomeComposerChoiceList choices;
        bool overridden = false;
    };
    const std::thread::id owner_=std::this_thread::get_id();
    std::vector<GenerationModelOption> models_;
    std::string modelError_,submissionError_;
    ModelSelection imageModel_,videoModel_;
    vm::ComposerState draft_;
    std::shared_ptr<HomeComposerModel> model_;
    std::function<ii::core::Result<void>(bool)> advanced_;
    std::function<ii::core::Result<void>(const HomeComposerSnapshot &)> submit_;
    bool imageAvailable_=false,videoAvailable_=false;
};
}
