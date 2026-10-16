#pragma once

#include <filesystem>
#include <array>
#include <string>
#include <string_view>

namespace dreamscapes {
inline constexpr std::array<std::string_view, 4> generationPromptCategories{"Image", "Video", "Audio", "Agents"};
struct GenerationPrompts {
    std::string prompt;
    std::string negativePrompt;
    bool operator==(const GenerationPrompts &) const = default;
};
struct ModelPreferences {
    std::string imageModel;
    std::string videoModel;
    std::array<GenerationPrompts, 4> prompts{};
    bool operator==(const ModelPreferences &) const = default;
};

// Empty paths provide an in-memory configuration for isolated runtime fixtures.
bool readModelPreferences(const std::filesystem::path &path, ModelPreferences &value, std::string &error);
bool writeModelPreferences(const std::filesystem::path &path, const ModelPreferences &value, std::string &error);
}
