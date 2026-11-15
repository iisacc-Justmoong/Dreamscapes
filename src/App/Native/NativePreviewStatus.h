#pragma once
#include <cstdint>
#include <string_view>

namespace dreamscapes::native {
// A read-only companion to an image resolver. An implementation may expose
// its observable revision when queried inside an LVUI binding.
enum class PreviewStatus { loading, ready, failed };
class PreviewStatusSource {
  public:
    virtual ~PreviewStatusSource() = default;
    [[nodiscard]] virtual PreviewStatus previewStatus(std::string_view source,std::uint32_t width,
                                                      std::uint32_t height) const = 0;
};
} // namespace dreamscapes::native
