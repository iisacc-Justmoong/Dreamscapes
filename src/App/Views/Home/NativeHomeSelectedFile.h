#pragma once

#include "App/ViewModels/Native/ViewCatalog.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::home {
struct HomeSelectedFileStrings final {
    std::string heading{"Selected file"}, open{"Open in editor"}, download{"Download and open"};
    std::string empty{"Select a recent file"}, noFile{"No file selected"}, unavailable{"Unavailable"};
    std::string canvas{"Canvas"}, structure{"Structure"}, color{"Color"};
};

// Presentation and commands only. File reads and original-file ownership
// belong to the product adapter, outside the native view and model.
class HomeSelectedFileModel final : public std::enable_shared_from_this<HomeSelectedFileModel> {
  public:
    static std::shared_ptr<HomeSelectedFileModel> create(HomeSelectedFileStrings = {});
    ~HomeSelectedFileModel();
    HomeSelectedFileModel(const HomeSelectedFileModel &) = delete;
    HomeSelectedFileModel &operator=(const HomeSelectedFileModel &) = delete;

    [[nodiscard]] ii::core::Result<std::uint64_t> setFile(vm::Value file, std::string preview);
    [[nodiscard]] ii::core::Result<void> setMetadata(std::uint64_t generation, vm::Value metadata);
    void setOpenHandler(std::function<void()>);
    void open();
    void sizeChanged(double width, double height);
    void close();
    [[nodiscard]] vm::Value file() const;
    [[nodiscard]] bool active() const noexcept { return active_; }

    ii::core::Observable<std::string> heading, openLabel, name, path, dimensions, structure, color, status, previewSource;
    ii::core::Observable<std::string> canvasLabel, structureLabel, colorLabel;
    ii::core::Observable<bool> hasFile{false}, canOpen{false}, wide{true};
    ii::core::Observable<double> viewportWidth{0}, viewportHeight{0};

  private:
    explicit HomeSelectedFileModel(HomeSelectedFileStrings);
    void requireOwner() const;
    void publish();
    vm::Value query(vm::Read, std::vector<vm::Value>) const;
    const std::thread::id owner_ = std::this_thread::get_id();
    HomeSelectedFileStrings strings_;
    vm::ViewCatalog catalog_{{}};
    vm::Value file_{vm::Object{}}, metadata_{vm::Object{}};
    std::string preview_;
    std::function<void()> open_;
    std::uint64_t generation_ = 0;
    bool active_ = true, updating_ = false;
};
} // namespace dreamscapes::home

class NativeHomeSelectedFileBackend final {
  public:
    explicit NativeHomeSelectedFileBackend(std::shared_ptr<dreamscapes::home::HomeSelectedFileModel>);
    NativeHomeSelectedFileBackend();
    std::shared_ptr<dreamscapes::home::HomeSelectedFileModel> model;
    ii::core::Observable<std::string> &heading, &openLabel, &name, &path, &dimensions, &structure, &color, &status, &previewSource;
    ii::core::Observable<std::string> &canvasLabel, &structureLabel, &colorLabel;
    ii::core::Observable<bool> &hasFile, &canOpen, &wide;
    ii::core::Observable<double> &viewportWidth, &viewportHeight;
    void open() { model->open(); }
    void sizeChanged(double width, double height) { model->sizeChanged(width, height); }
};
