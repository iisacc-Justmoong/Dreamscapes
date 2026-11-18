#pragma once
#include "App/ViewModels/Native/CanvasDraft.h"
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::canvas {
class WorkspaceModel;
struct Category final {
    std::string id,name; int index=0;
    ii::core::Observable<bool> selected{false};
    std::weak_ptr<WorkspaceModel> owner;
    void choose();
};
struct Preset final {
    std::string id,name;
    vm::Value value;
    std::weak_ptr<WorkspaceModel> owner;
    void choose();
};
class WorkspaceModel final : public std::enable_shared_from_this<WorkspaceModel> {
  public:
    using CreateHandler=std::function<ii::core::Result<void>(vm::Value)>;
    static std::shared_ptr<WorkspaceModel> create(std::string_view catalog);
    [[nodiscard]] ii::core::Result<void> chooseCategory(int);
    [[nodiscard]] ii::core::Result<void> choosePreset(vm::Value);
    [[nodiscard]] ii::core::Result<void> search(std::string);
    [[nodiscard]] ii::core::Result<void> customSize();
    [[nodiscard]] ii::core::Result<void> edit(std::string_view key,std::string);
    [[nodiscard]] ii::core::Result<void> changeUnit(std::string);
    [[nodiscard]] ii::core::Result<void> createCanvas();
    void setCreateHandler(CreateHandler);
    void sizeChanged(double,double,double);
    ii::core::Observable<std::vector<std::shared_ptr<Category>>> categories;
    ii::core::Observable<std::vector<std::shared_ptr<Preset>>> presets;
    ii::core::Observable<std::string> widthText,heightText,ppiText,query,unit,background,selectionName,status;
    ii::core::Observable<bool> valid{false},canCreate{false};
    ii::core::Observable<double> width{1},height{1},listWidth{1},detailWidth{1};
  private:
    explicit WorkspaceModel(vm::Value);
    void start();
    void synchronize(const vm::Value &);
    [[nodiscard]] ii::core::Result<void> send(vm::CanvasDraftAction,std::string key={},vm::Value value={});
    [[nodiscard]] ii::core::Result<void> writable() const;
    vm::Value sections(int,std::string_view) const;
    const std::thread::id owner_=std::this_thread::get_id();
    vm::Value catalog_;
    vm::CanvasDraft draft_;
    std::optional<vm::Value> projectedSections_;
    CreateHandler create_;
};
}
