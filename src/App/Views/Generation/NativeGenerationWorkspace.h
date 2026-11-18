#pragma once
#include "App/ViewModels/Native/ViewValue.h"
#include "App/Views/Home/NativeHomeComposer.h"
#include <iiSDKCore>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dreamscapes::generation {
class GenerationWorkspaceModel;
struct GenerationField final {
    std::string key,label,section,chooseAccessibility,clearAccessibility;
    bool isToggle=false,isChoice=false,isText=false,isNumber=false,isFile=false,showSection=false;
    std::vector<std::string> options;
    ii::core::Observable<std::string> text,error,fileName;
    ii::core::Observable<bool> checked{false},invalid{false},enabled{true},hasFile{false},canChooseFile{false};
    ii::core::Observable<int> index{0};
    std::weak_ptr<GenerationWorkspaceModel> owner;
    void edited(std::string);
    void toggled(bool);
    void selected(int);
    void chooseFile();
    void clearFile();
};
class GenerationWorkspaceModel final : public std::enable_shared_from_this<GenerationWorkspaceModel> {
  public:
    using Field=std::shared_ptr<GenerationField>;
    static std::shared_ptr<GenerationWorkspaceModel> create(bool video,std::shared_ptr<home::HomeComposerModel>);
    ~GenerationWorkspaceModel();
    [[nodiscard]] Field field(std::string_view) const;
    [[nodiscard]] vm::Value parameters() const;
    [[nodiscard]] ii::core::Result<void> edit(std::string_view,std::string);
    [[nodiscard]] ii::core::Result<void> toggle(std::string_view,bool);
    [[nodiscard]] ii::core::Result<void> select(std::string_view,std::string);
    [[nodiscard]] ii::core::Result<void> selectModelAt(int);
    void setReferencePicker(std::function<ii::core::Result<void>(std::string_view)>);
    [[nodiscard]] ii::core::Result<void> chooseReference(std::string_view);
    [[nodiscard]] ii::core::Result<void> clearReference(std::string_view);
    [[nodiscard]] std::uint64_t referenceRevision() const;
    [[nodiscard]] ii::core::Result<void> adoptQuickSettings(const home::HomeComposerSnapshot &);
    void setSubmitHandler(std::function<ii::core::Result<void>(vm::Value)>);
    [[nodiscard]] ii::core::Result<void> submit();
    void sizeChanged(double,double,double maximumPanel,double minimumPreview);
    const bool video;
    ii::core::Observable<std::vector<Field>> fields;
    ii::core::Observable<bool> canGenerate{false};
    ii::core::Observable<std::string> serviceStatus{"Generation service unavailable"};
    ii::core::Observable<std::string> validationStatus;
    ii::core::Observable<double> viewportWidth{1},viewportHeight{1};
    ii::core::Observable<double> panelWidth{1},previewWidth{0};
    ii::core::Observable<bool> showPreview{false};
  private:
    struct Impl;
    GenerationWorkspaceModel(bool,std::shared_ptr<home::HomeComposerModel>);
    void start();
    void syncShared(const home::HomeComposerSnapshot &);
    void updateCanGenerate();
    void publishField(const Field &);
    [[nodiscard]] ii::core::Result<void> apply(std::string_view,const vm::Value &,const std::string *raw=nullptr);
    std::unique_ptr<Impl> impl_;
};
}
