#include <iiFileProvider>
#include "ModelPreferences.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dreamscapes {
bool parseModelPreferences(std::string_view bytes, ModelPreferences &value, std::string &error)
{
    error.clear();
    if (bytes.size() > 1024 * 1024) { error = "Invalid generation preferences file."; return false; }
    std::string text(bytes);
    // The original Windows text-mode writer used CRLF throughout the file,
    // including inside quoted multiline prompts. Detect that format from its
    // header so files created on other systems retain literal CRLF in prompts.
    const auto headerEnd=text.find('\n');
    if (headerEnd!=std::string::npos && headerEnd>0 && text[headerEnd-1]=='\r') {
        std::string normalized; normalized.reserve(text.size());
        for (std::size_t i=0;i<text.size();++i) {
            if (text[i]=='\r' && i+1<text.size() && text[i+1]=='\n') continue;
            normalized.push_back(text[i]);
        }
        text=std::move(normalized);
    }
    std::istringstream input{std::move(text)};
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
    } else if (bytes.size() > 16 * 1024) {
        error = "Invalid generation preferences file."; return false;
    }
    input >> std::ws;
    if (!input.eof()) { error = "Invalid generation preferences file."; return false; }
    value = std::move(candidate);
    return true;
}
std::string serializeModelPreferences(const ModelPreferences &value)
{
    std::ostringstream output;
    output << "Dreamscapes generation preferences 2\n" << std::quoted(value.imageModel) << '\n'
           << std::quoted(value.videoModel) << '\n';
    for (const auto &prompts : value.prompts)
        output << std::quoted(prompts.prompt) << '\n' << std::quoted(prompts.negativePrompt) << '\n';
    return output.str();
}
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
    std::ifstream input(path, std::ios::binary);
    if (!input) { error = "Cannot read generation preferences."; return false; }
    std::string bytes(static_cast<std::size_t>(size),'\0');
    if (size && !input.read(bytes.data(),static_cast<std::streamsize>(size))) {
        error = "Cannot read generation preferences."; return false;
    }
    if (input.peek()!=std::char_traits<char>::eof() || input.bad()) {
        error = "Generation preferences changed while reading."; return false;
    }
    return parseModelPreferences(bytes,value,error);
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
    output << serializeModelPreferences(value);
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
