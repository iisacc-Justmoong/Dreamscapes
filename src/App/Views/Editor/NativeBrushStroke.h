#pragma once
#include <Bitmap/BitmapEditor.h>
#include <Document/Document.h>
#include <iiSDKCore>
#include <cstdint>
#include <memory>
#include <string>

namespace dreamscapes::editor {
struct PreparedBrushStroke final {
    std::shared_ptr<const iiSharedCanvas::Document> source;
    std::shared_ptr<iiSharedCanvas::Document> candidate;
    std::uint64_t generation=0,revision=0;
    std::string layerId;
    iiSharedCanvas::FrameIndex frame=0;
};

// Owned by one worker. It never mutates the source snapshot or a live session.
// Samples are asset-local pixels. The caller maps the displayed canvas to the
// selected layer's coordinate system before crossing this interface.
class BrushStroke final {
  public:
    static ii::core::Result<std::unique_ptr<BrushStroke>> create(
        std::shared_ptr<const iiSharedCanvas::Document>,std::uint64_t generation,std::uint64_t revision,
        std::string layerId,iiSharedCanvas::FrameIndex,const iiSharedCanvas::BitmapBrush &);
    ~BrushStroke();
    [[nodiscard]] ii::core::Result<void> begin(const StrokePoint &);
    [[nodiscard]] ii::core::Result<void> move(const StrokePoint &);
    [[nodiscard]] ii::core::Result<PreparedBrushStroke> finish(const StrokePoint &);
    void cancel() noexcept;
    [[nodiscard]] const iiSharedCanvas::Document &document() const {return *candidate_;}
  private:
    BrushStroke()=default;
    std::shared_ptr<const iiSharedCanvas::Document> source_;
    std::shared_ptr<iiSharedCanvas::Document> candidate_;
    std::unique_ptr<iiSharedCanvas::BitmapEditor> bitmap_;
    std::uint64_t generation_=0,revision_=0;
    std::string layerId_,sourceAsset_,targetAsset_;
    iiSharedCanvas::FrameIndex frame_=0;
    bool active_=false,finished_=false;
};
}
