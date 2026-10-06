#pragma once

#include <filesystem>
#include <string>

namespace dreamscapes {
struct ModelPreferences {
    std::string imageModel;
    std::string videoModel;
    bool operator==(const ModelPreferences &) const = default;
};

// Empty paths provide an in-memory configuration for isolated runtime fixtures.
bool readModelPreferences(const std::filesystem::path &path, ModelPreferences &value, std::string &error);
bool writeModelPreferences(const std::filesystem::path &path, const ModelPreferences &value, std::string &error);
}
