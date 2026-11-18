#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace dreamscapes::native {
struct ResidentCommandResult { int code=1; std::string text; };
// Blocking CLI operation; never invoked by the GUI event loop.
ResidentCommandResult residentCommand(std::string_view prompt, const std::filesystem::path &broker = {});
}
