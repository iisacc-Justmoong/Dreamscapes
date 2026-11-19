#pragma once
#include "App/ViewModels/Native/ViewValue.h"
#include <iiSDKCore>
#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace dreamscapes::native {
struct SocietyListing final {
    std::filesystem::path root;
    std::string containerId;
    vm::Value aliases;
    // The manifest-defined section after validating its type and containment.
    std::filesystem::path sectionPath;
};

/// A directory request, a recursive inventory, or a destination identity check.
enum class SocietyListingKind { directory, recent, modelEntries, section };

/// A logical request in the selected Society drive. Zero limit means all
/// entries up to the provider's bound.
struct SocietyStorageQuery final {
    std::string section;
    std::string directory;
    std::string filter;
    SocietyListingKind kind = SocietyListingKind::directory;
    std::size_t limit = 0;
};

/// Worker-side access to a selected drive. Listings are plain view data and
/// materialization returns a local path for the existing media reader.
struct SocietyStorageAccess final {
    using Canceled = std::function<bool()>;
    using Read = std::function<ii::core::Result<SocietyListing>(const SocietyStorageQuery &, Canceled)>;
    using Materialize = std::function<ii::core::Result<std::filesystem::path>(const vm::Value &, Canceled)>;
    Read read;
    Materialize materialize;
};

/// Validates the manifest and native destination for a daemon-selected drive.
/// This reads a single manifest and never discovers a drive or lists its contents.
[[nodiscard]] ii::core::Result<SocietyListing> describeSocietySection(
    const std::filesystem::path &root, std::string_view section);

/// Wraps the on-device store for mobile and isolated filesystem tests.
[[nodiscard]] SocietyStorageAccess localSocietyStorage(
    std::filesystem::path selection, std::filesystem::path root = {});

/// Returns the native file stamp shared by history previews and original export.
[[nodiscard]] std::string societySourceRevision(std::filesystem::file_time_type modified, std::uintmax_t bytes);
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
