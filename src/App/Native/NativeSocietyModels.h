#pragma once
#include "NativeSocietyStorage.h"
#include <functional>

namespace dreamscapes::native {
// Bounded local inventory and iiLocalDiffusion metadata inspection. Never
// reads model payloads, writes sidecars, starts inference or downloads models.
[[nodiscard]] ii::core::Result<SocietyListing> browseSocietyModels(const std::filesystem::path &selection,
    const std::filesystem::path &explicitRoot={},std::function<bool()> canceled={});
// Recheck the catalogue's captured container and every package member on a
// worker before using a model. No model bytes are changed or copied.
[[nodiscard]] ii::core::Result<std::filesystem::path> resolveSocietyModel(const std::filesystem::path &selection,
    const vm::Value &source,const std::filesystem::path &explicitRoot={},std::function<bool()> canceled={});
}
