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
    if (status || size > 16 * 1024) { error = "Invalid generation preferences file."; return false; }
    std::ifstream input(path);
    std::string header;
    ModelPreferences candidate;
    if (!std::getline(input, header) || header != "Dreamscapes generation models 1"
        || !(input >> std::quoted(candidate.imageModel) >> std::quoted(candidate.videoModel))) {
        error = "Cannot read generation preferences."; return false;
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
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), status);
    if (status) { error = "Cannot save generation preferences: " + status.message(); return false; }
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    output << "Dreamscapes generation models 1\n" << std::quoted(value.imageModel) << '\n'
           << std::quoted(value.videoModel) << '\n';
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, status);
        error = "Cannot save generation preferences."; return false;
    }
    std::filesystem::rename(temporary, path, status);
    if (status) {
        error = "Cannot save generation preferences: " + status.message();
        std::filesystem::remove(temporary, status);
        return false;
    }
    return true;
}
}
