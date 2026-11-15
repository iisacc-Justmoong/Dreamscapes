#pragma once

#include "App/ViewModels/Native/ViewValue.h"
#include <iiSDKCore/Core/Property/Observable.h>
#include <iiSDKCore/Core/Result/Result.h>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::home {

// Inputs are supplied by the product's generation session, not by this view.
struct HomeQueueSnapshot final {
    vm::Value jobs{vm::Array{}};
    std::string statusText;
    std::string modelName;
    std::string deviceName{"Automatic"};
};

// Hosts can supply localized strings without teaching the view about Qt.
struct HomeQueueStrings final {
    std::string heading{"Generation queue"}, openQueue{"View queue"}, currentSession{"Current session"};
    std::string device{"Device"}, model{"Model"}, execution{"Execution"};
    std::string ready{"Ready to generate"}, generating{"Generating…"}, queued{"Queued"};
    std::string unavailable{"Unavailable"}, running{"Running"}, idle{"Idle"};
    std::string detail{"Generation progress and failures appear here."};
    std::function<std::string(std::size_t, std::size_t, std::size_t)> counts;
};

class HomeQueueModel final {
  public:
    explicit HomeQueueModel(HomeQueueStrings strings = {});
    [[nodiscard]] ii::core::Result<void> setSnapshot(const HomeQueueSnapshot &snapshot);
    void setOpenQueueHandler(std::function<void()> handler);
    void openQueue();

    ii::core::Observable<std::string> heading, openLabel, currentSession, deviceLabel, modelLabel, executionLabel;
    ii::core::Observable<std::string> status, summary, device, model, execution, detail;
    ii::core::Observable<bool> canOpenQueue{false};

  private:
    void requireOwner() const;
    const std::thread::id owner_ = std::this_thread::get_id();
    HomeQueueStrings strings_;
    std::function<void()> openQueue_;
    bool updating_ = false;
};
} // namespace dreamscapes::home

// The LVUI scope borrows the app-owned model; it does not own generation state.
class NativeHomeQueueBackend final {
  public:
    explicit NativeHomeQueueBackend(std::shared_ptr<dreamscapes::home::HomeQueueModel> source);
    NativeHomeQueueBackend();
    std::shared_ptr<dreamscapes::home::HomeQueueModel> model;
    ii::core::Observable<std::string> &heading, &openLabel, &currentSession, &deviceLabel, &modelLabel, &executionLabel;
    ii::core::Observable<std::string> &status, &summary, &device, &modelName, &execution, &detail;
    ii::core::Observable<bool> &canOpenQueue;
    void openQueue() { model->openQueue(); }
};
