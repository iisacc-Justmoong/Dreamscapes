#include <iiFileProviderStorage.h>
#include "ModelPreferences.h"
#include <fstream>
#include <iomanip>

namespace dreamscapes {
bool readModelPreferences(const std::filesystem::path &path, ModelPreferences &value, std::string &error)
{
    error.clear();
    if (path.empty()) return true;
    std::error_code status;
    const bool exists = std::filesystem::exists(path, status);
    if (status) { error = "Cannot read generation preferences: " + status.message(); return false; }
    if (!exists) return true;
    const auto size = std::filesystem::file_size(path, status);
    if (status || size > 1024 * 1024) { error = "Invalid generation preferences file."; return false; }
    std::ifstream input(path);
    std::string header;
    ModelPreferences candidate;
    if (!std::getline(input, header)
        || (header != "Dreamscapes generation models 1" && header != "Dreamscapes generation preferences 2")
        || !(input >> std::quoted(candidate.imageModel) >> std::quoted(candidate.videoModel))) {
        error = "Cannot read generation preferences."; return false;
    }
    if (header == "Dreamscapes generation preferences 2") {
        for (auto &prompts : candidate.prompts) {
            if (!(input >> std::quoted(prompts.prompt) >> std::quoted(prompts.negativePrompt))) {
                error = "Cannot read generation preferences."; return false;
            }
        }
    } else if (size > 16 * 1024) {
        error = "Invalid generation preferences file."; return false;
    }
    input >> std::ws;
    if (!input.eof()) { error = "Invalid generation preferences file."; return false; }
    value = std::move(candidate);
    return true;
}

bool writeModelPreferences(const std::filesystem::path &path, const ModelPreferences &value, std::string &error)
{
    error.clear();
    if (path.empty()) return true;
    std::error_code status;
    if (!path.parent_path().empty()) iiFileProvider::native::createDirectories(path.parent_path(), status);
    if (status) { error = "Cannot save generation preferences: " + status.message(); return false; }
    auto temporary = path;
    temporary += ".tmp";
    iiFileProvider::native::OutputFile output(temporary, std::ios::trunc);
    output << "Dreamscapes generation preferences 2\n" << std::quoted(value.imageModel) << '\n'
           << std::quoted(value.videoModel) << '\n';
    for (const auto &prompts : value.prompts)
        output << std::quoted(prompts.prompt) << '\n' << std::quoted(prompts.negativePrompt) << '\n';
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, status);
        error = "Cannot save generation preferences."; return false;
    }
    iiFileProvider::native::renameFile(temporary, path, status);
    if (status) {
        error = "Cannot save generation preferences: " + status.message();
        std::filesystem::remove(temporary, status);
        return false;
    }
    return true;
}
}
