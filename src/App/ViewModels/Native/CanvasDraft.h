#pragma once
#include "ViewValue.h"
#include <functional>
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
enum class CanvasDraftAction { write, choose, chooseCategory, search, customSize, begin, changeUnit, create };
struct CanvasDraftCommand {
    CanvasDraftAction action;
    std::string key;
    Value value;
};
struct CanvasDraftReply {
    Value presentation;
    std::optional<Value> created;
};
class CanvasDraft final {
  public:
    using Sections = std::function<Value(int, std::string_view)>;
    using Specification = std::function<Value(double, double, std::string_view, double, std::string_view)>;
    using Convert = std::function<double(double, std::string_view, std::string_view, double)>;
    CanvasDraft(Sections, Specification, Convert);
    const ii::runtime::QueryPort<CanvasDraftCommand, CanvasDraftReply> &commands() const {
        return commands_;
    }
    Value presentation() const;

  private:
    CanvasDraftReply execute(const CanvasDraftCommand &);
    void choose(const Value &);
    Value sections() const;
    Object values_;
    Sections sections_;
    Specification specification_;
    Convert convert_;
    mutable int cachedCategory_ = -1;
    mutable std::string cachedQuery_;
    mutable Value cachedSections_{Array{}};
    ii::runtime::QueryPort<CanvasDraftCommand, CanvasDraftReply> commands_;
};
} // namespace dreamscapes::vm
