#pragma once
#include "ViewValue.h"
#include <charconv>
#include <limits>
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
enum class NumberInputKind { number, percentage, frame };
struct NumberInput final { NumberInputKind kind; std::string text; };
struct NumberInputReply final { bool accepted = false; double value = 0; };
class NumberInputs final {
  public:
    NumberInputs() : queries_([](const NumberInput &request) -> NumberInputReply {
        if (request.kind == NumberInputKind::frame) {
            auto input = trim(request.text);
            for (const auto prefix : {"Source ", "In ", "Out ", "Duration "})
                if (input.starts_with(prefix)) { input = trim(input.substr(std::string_view(prefix).size())); break; }
            if (input.starts_with('F') || input.starts_with('f')) input.erase(input.begin());
            if (input.ends_with('f')) input.pop_back();
            input = trim(input);
            if (input.starts_with('+')) input.erase(input.begin());
            int value = 0;
            const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
            return {parsed.ec == std::errc{} && parsed.ptr == input.data() + input.size() && !input.empty(),
                    double(value)};
        }
        const Value field = request.kind == NumberInputKind::percentage
            ? Value(Object{{"min",Value(0)},{"max",Value(100)},{"unit",Value("%")}}) : Value(Object{});
        const auto parsed = parseNumber(field, request.text, true);
        return {parsed.valid, parsed.valid ? number(parsed.value) : 0};
    }) {}
    const ii::runtime::QueryPort<NumberInput, NumberInputReply> &queries() const { return queries_; }
  private:
    ii::runtime::QueryPort<NumberInput, NumberInputReply> queries_;
};
} // namespace dreamscapes::vm
