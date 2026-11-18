#pragma once
#include <Document/Document.h>
#include <iiSDKCore>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace dreamscapes::editor {
struct PreparedAudioEdit final {
    std::shared_ptr<const iiSharedCanvas::Document> source;
    std::shared_ptr<iiSharedCanvas::Document> candidate;
    std::uint64_t generation=0,revision=0;
    bool changed=false;
    std::string createdTrack;
};
[[nodiscard]] ii::core::Result<PreparedAudioEdit> prepareAudioTrackCreate(
    std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,std::function<bool()> cancelled={});
[[nodiscard]] ii::core::Result<PreparedAudioEdit> prepareAudioImport(
    std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,
    std::string_view track,std::int64_t firstFrame,iiSharedCanvas::AudioAsset,std::string name,std::function<bool()> cancelled={});
[[nodiscard]] ii::core::Result<PreparedAudioEdit> prepareAudioTrackPatch(
    std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,
    std::string_view track,std::optional<bool> muted,std::optional<double> gainDb,std::function<bool()> cancelled={});
[[nodiscard]] ii::core::Result<PreparedAudioEdit> prepareAudioClipTrim(
    std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,
    std::string_view track,std::string_view clip,std::int64_t first,std::int64_t end,std::function<bool()> cancelled={});
[[nodiscard]] ii::core::Result<PreparedAudioEdit> prepareAudioClipMove(
    std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,
    std::string_view track,std::string_view clip,std::int64_t first,std::function<bool()> cancelled={});
}
