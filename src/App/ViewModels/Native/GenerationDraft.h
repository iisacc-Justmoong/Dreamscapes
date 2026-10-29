#pragma once
#include "ViewValue.h"
#include <functional>
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
struct ValidityEdit {
    std::string key;
    bool valid = true, clear = false, clearCollections = false;
};
class InputValidity final {
  public:
    InputValidity()
        : commands_([this](const ValidityEdit &edit) {
              if (edit.clear)
                  fields_.clear();
              if (edit.clearCollections)
                  std::erase_if(fields_, [](const auto &p) {
                      return p.first.starts_with("control.") || p.first.starts_with("lora.");
                  });
              if (!edit.key.empty()) {
                  if (edit.valid)
                      fields_.erase(edit.key);
                  else
                      fields_[edit.key] = Value(true);
              }
              return Value(fields_);
          }) {}
    const ii::runtime::QueryPort<ValidityEdit, Value> &commands() const {
        return commands_;
    }
    bool invalid() const {
        return !fields_.empty();
    }

  private:
    Object fields_;
    ii::runtime::QueryPort<ValidityEdit, Value> commands_;
};
Value videoDefaults(const Value &prompts);
bool videoCameraValid(const Value &parameters);
std::string aspectRatio(double width, double height);
struct VideoEdit {
    std::string key;
    Value value;
};
struct VideoEditPlan {
    bool valid = false;
    Value values, invalidInputs;
    bool resize = false;
};
VideoEditPlan planVideoEdit(const Value &values, const Value &invalid, const VideoEdit &edit);
class VideoDraft final {
  public:
    using Resize = std::function<bool(int frames, double fps)>;
    explicit VideoDraft(Resize resize)
        : values_(videoDefaults({})), resize_(std::move(resize)), edits_([this](const VideoEdit &edit) {
              auto plan = planVideoEdit(values_, invalid_, edit);
              if (plan.valid && plan.resize && resize_ &&
                  !resize_(static_cast<int>(number(get(plan.values, "duration")) * number(get(plan.values, "fps"))),
                           number(get(plan.values, "fps")))) {
                  plan.valid = false;
                  auto invalid = object(plan.invalidInputs);
                  invalid[edit.key] = Value(true);
                  plan.invalidInputs = Value(invalid);
              }
              invalid_ = plan.invalidInputs;
              if (plan.valid)
                  values_ = plan.values;
              return plan.valid;
          }) {}
    const ii::runtime::QueryPort<VideoEdit, bool> &edits() const {
        return edits_;
    }
    Value values() const {
        return values_;
    }
    Value invalidInputs() const {
        return invalid_;
    }
    bool invalid() const {
        return !object(invalid_).empty() || !videoCameraValid(values_);
    }
    void replace(Value values, bool clearValidity = true) {
        values_ = std::move(values);
        if (clearValidity)
            invalid_ = Value(Object{});
    }
    void restoreComposition(int frames, double fps);

  private:
    Value values_, invalid_{Object{}};
    Resize resize_;
    ii::runtime::QueryPort<VideoEdit, bool> edits_;
};
struct ComposerEdit {
    std::string key;
    Value value;
};
class ComposerState final {
  public:
    ComposerState();
    const ii::runtime::QueryPort<ComposerEdit, Value> &edits() const {
        return edits_;
    }
    Value presentation() const;

  private:
    void applyDefaults();
    Object values_;
    ii::runtime::QueryPort<ComposerEdit, Value> edits_;
};
Value imageSize(std::string_view ratio, int shorter);
} // namespace dreamscapes::vm
