#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace dreamscapes::native {
struct ApplicationCommandResult final {int code=0; std::string text; std::optional<std::string> initialView;};
// An empty argument sequence means the GUI should start.
[[nodiscard]] std::optional<ApplicationCommandResult> applicationCommand(std::span<const std::string_view>);
}
