#pragma once
#include <iiSharedCanvas/Serialization/IiscCodec.h>

namespace dreamscapes {
struct ProjectPage { std::string name; iiSharedCanvas::Document document; };
struct ProjectPageView { std::string name; const iiSharedCanvas::Document *document; };
struct ProjectEncoding { std::vector<std::uint8_t> bytes; std::string error; };
struct ProjectDecoding { std::vector<ProjectPage> pages; std::uint32_t active = 0; std::string error; };
inline constexpr std::uint32_t MaximumProjectPages = 1000;
ProjectEncoding encodeProject(std::span<const ProjectPageView> pages, std::uint32_t active);
ProjectDecoding decodeProject(std::span<const std::uint8_t> bytes);
}
