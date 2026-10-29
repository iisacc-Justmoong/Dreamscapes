#pragma once
#include "ViewValue.h"
#include <cstdio>
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
struct PhotoSnapshot {
    Value settings, targets, devices;
    double recordingMilliseconds = 0;
    std::string deviceId;
};
class PhotoPresentation final {
  public:
    PhotoPresentation()
        : queries_([](const PhotoSnapshot &input) {
              Array targets;
              Value selected;
              const auto kind = text(get(input.settings, "captureScope")) == "Window" ? "Window" : "Screen";
              for (const auto &target : array(input.targets))
                  if (text(get(target, "kind")) == kind)
                      targets.push_back(target);
              for (const auto &target : targets)
                  if (get(target, "id") == get(input.settings, "captureTarget")) {
                      selected = target;
                      break;
                  }
              if (selected.kind() == ii::core::ValueKind::null && !targets.empty())
                  selected = targets.front();
              auto settings = object(input.settings);
              settings["captureTarget"] = Value(text(get(selected, "id")));
              const auto seconds = static_cast<long long>(std::max(0., std::floor(input.recordingMilliseconds / 1000)));
              char time[48];
              std::snprintf(time, sizeof(time), "%02lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60,
                            seconds % 60);
              auto deviceId = text(get(input.settings, "cameraDevice"));
              if (deviceId.empty())
                  deviceId = input.deviceId;
              const auto angle = text(get(input.settings,"cameraAngle"));
              const int orientation = angle == "90°" ? 90 : angle == "180°" ? 180 : angle == "270°" ? 270 : 0;
              std::string label = "No camera connected";
              for (const auto &device : array(input.devices))
                  if (text(get(device, "id")) == deviceId) {
                      label = text(get(device, "label"));
                      break;
                  }
              return Value(Object{{"targets", Value(std::move(targets))},
                                  {"captureTarget", selected},
                                  {"captureSettings", Value(std::move(settings))},
                                  {"recordingTime", Value(time)},
                                  {"orientation",Value(orientation)},
                                  {"deviceLabel", Value(std::move(label))}});
          }) {}
    const ii::runtime::QueryPort<PhotoSnapshot, Value> &queries() const {
        return queries_;
    }

  private:
    ii::runtime::QueryPort<PhotoSnapshot, Value> queries_;
};
} // namespace dreamscapes::vm
