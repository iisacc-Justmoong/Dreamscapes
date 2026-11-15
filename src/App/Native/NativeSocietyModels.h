#pragma once
#include "NativeSocietyStorage.h"
#include <functional>

namespace dreamscapes::native {
// Bounded local inventory and iiLocalDiffusion metadata inspection. Never
// reads model payloads, writes sidecars, starts inference or downloads models.
[[nodiscard]] ii::core::Result<SocietyListing> browseSocietyModels(const std::filesystem::path &selection,
    const std::filesystem::path &explicitRoot={},std::function<bool()> canceled={});
}
