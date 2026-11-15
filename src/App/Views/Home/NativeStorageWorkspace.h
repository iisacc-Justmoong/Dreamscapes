#pragma once
#include "App/ViewModels/Native/StorageProjection.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::storage {
class WorkspaceModel;
struct Row final {
    std::string key,name,path;
    bool directory=false;
    ii::core::Observable<bool> selected{false};
    std::weak_ptr<WorkspaceModel> owner;
    void activate();
};
class WorkspaceModel final : public std::enable_shared_from_this<WorkspaceModel> {
  public:
    static std::shared_ptr<WorkspaceModel> create();
    [[nodiscard]] ii::core::Result<void> setSnapshot(vm::Value aliases);
    [[nodiscard]] ii::core::Result<void> setSection(std::string section);
    [[nodiscard]] ii::core::Result<void> setQuery(std::string query);
    [[nodiscard]] ii::core::Result<void> activate(std::string_view key);
    [[nodiscard]] ii::core::Result<void> up();
    [[nodiscard]] ii::core::Result<void> openSelected();
    using BrowseHandler=std::function<void(std::string,std::string,std::string)>;
    void setBrowseHandler(BrowseHandler);
    [[nodiscard]] ii::core::Result<void> refresh();
    void setUnavailable(std::string message);
    void setOpenHandler(std::function<void(vm::Value)> handler,std::function<bool(const vm::Value &)> canOpen={});
    void sizeChanged(double width,double height);
    ii::core::Observable<std::vector<std::shared_ptr<Row>>> rows;
    ii::core::Observable<bool> available{false},loading{false},canOpen{false},canUp{false};
    ii::core::Observable<std::string> selectedKey,query,directory,status{"Storage unavailable"},heading{"Files"};
    ii::core::Observable<double> width{1},height{1};
  private:
    WorkspaceModel()=default;
    [[nodiscard]] ii::core::Result<void> writable() const;
    void project();
    void refreshOpen();
    const std::thread::id owner_=std::this_thread::get_id();
    vm::StorageSnapshot snapshot_;
    vm::StorageProjector projector_;
    std::function<void(vm::Value)> open_;
    std::function<bool(const vm::Value &)> canOpenEntry_;
    BrowseHandler browse_;
};
}
