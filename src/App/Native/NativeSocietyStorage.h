#pragma once
#include "App/ViewModels/Native/ViewValue.h"
#include <iiSDKCore/Core/Result/Result.h>
#include <filesystem>
#include <cstddef>
#include <string>
#include <string_view>

namespace dreamscapes::native {
struct SocietyListing final {
    std::filesystem::path root;
    std::string containerId;
    vm::Value aliases;
};
// Performs bounded, read-only filesystem work. Call on a worker thread.
[[nodiscard]] ii::core::Result<SocietyListing> browseSociety(const std::filesystem::path &selection,
    const std::string &section,const std::string &directory,const std::string &query,
    const std::filesystem::path &explicitRoot={});
[[nodiscard]] ii::core::Result<SocietyListing> browseSocietyRecentFiles(const std::filesystem::path &selection,
    std::size_t limit,const std::filesystem::path &explicitRoot={});
[[nodiscard]] ii::core::Result<SocietyListing> browseSocietyModelEntries(const std::filesystem::path &selection,
    const std::filesystem::path &explicitRoot={});
[[nodiscard]] std::string societyFileUrl(std::string_view absolutePath);
}
