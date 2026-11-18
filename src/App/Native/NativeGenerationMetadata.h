#pragma once
#include <iiSDKCore>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace dreamscapes::native {
// Preserve the PNG pixels and unrelated chunks while replacing this product's
// UTF-8 generation recipe. The input and every chunk checksum are validated.
[[nodiscard]] ii::core::Result<std::vector<std::uint8_t>> addGenerationRecipe(
    std::span<const std::uint8_t> png,std::string_view json);
}
