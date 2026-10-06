#include "EditorProjectCodec.h"
#include <array>
#include <algorithm>
#include <stdexcept>

namespace dreamscapes {
namespace {
constexpr std::array<std::uint8_t, 8> Magic{'I','I','S','C','P','R','J',1};
constexpr auto Limit = iiSharedCanvas::SerializationLimits{}.maximumContainerBytes;
void number(std::vector<std::uint8_t> &out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}
class Reader {
public:
    std::span<const std::uint8_t> bytes;
    std::size_t offset = 0;
    std::span<const std::uint8_t> take(std::uint64_t count) {
        if (count > bytes.size() - offset) throw std::runtime_error("Truncated project.");
        const auto data = bytes.subspan(offset, static_cast<std::size_t>(count));
        offset += static_cast<std::size_t>(count);
        return data;
    }
    std::uint64_t number() {
        const auto data = take(8);
        std::uint64_t value = 0;
        for (int i = 0; i < 8; ++i) value |= std::uint64_t(data[i]) << (8 * i);
        return value;
    }
};
}
ProjectEncoding encodeProject(std::span<const ProjectPageView> pages, std::uint32_t active) {
    ProjectEncoding result;
    if (pages.empty() || pages.size() > MaximumProjectPages || active >= pages.size()) {
        result.error = "Invalid project canvas count or active canvas."; return result;
    }
    result.bytes.assign(Magic.begin(), Magic.end());
    number(result.bytes, pages.size()); number(result.bytes, active);
    for (const auto &page : pages) {
        if (!page.document || page.name.size() > 1048576) { result.error = "Invalid project page."; break; }
        auto native = iiSharedCanvas::encodeIisc(*page.document);
        if (!native.ok()) { result.error = native.error.message; break; }
        if (native.bytes.size() + page.name.size() + 16 > Limit - result.bytes.size()) {
            result.error = "Project exceeds the container limit."; break;
        }
        number(result.bytes, page.name.size());
        result.bytes.insert(result.bytes.end(), page.name.begin(), page.name.end());
        number(result.bytes, native.bytes.size());
        result.bytes.insert(result.bytes.end(), native.bytes.begin(), native.bytes.end());
    }
    if (!result.error.empty()) result.bytes.clear();
    return result;
}
ProjectDecoding decodeProject(std::span<const std::uint8_t> bytes) {
    ProjectDecoding result;
    try {
        if (bytes.size() > Limit) throw std::runtime_error("Project exceeds the container limit.");
        Reader input{bytes};
        const auto magic = input.take(Magic.size());
        if (!std::equal(magic.begin(), magic.end(), Magic.begin())) throw std::runtime_error("Unsupported project format.");
        const auto count = input.number(), active = input.number();
        if (count == 0 || count > MaximumProjectPages || active >= count) throw std::runtime_error("Invalid project canvas count or active canvas.");
        result.active = static_cast<std::uint32_t>(active);
        for (std::uint64_t i = 0; i < count; ++i) {
            const auto size = input.number();
            if (size > 1048576) throw std::runtime_error("Canvas name exceeds the limit.");
            const auto name = input.take(size);
            auto native = iiSharedCanvas::decodeIisc(input.take(input.number()));
            if (!native.ok()) throw std::runtime_error(native.error.message);
            result.pages.push_back({std::string(name.begin(), name.end()), std::move(native.document)});
        }
        if (input.offset != bytes.size()) throw std::runtime_error("Unexpected project trailing data.");
    } catch (const std::exception &e) { result.pages.clear(); result.error = e.what(); }
    return result;
}
}
