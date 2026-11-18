#pragma once
#include "NativeEditorFrame.h"
#include <array>
#include <cstdint>

namespace dreamscapes::editor {
enum class HistogramChannel {rgb,red,green,blue};
struct HistogramStatistics final {
    std::array<std::uint64_t,256> red{},green{},blue{};
    std::uint64_t pixels=0;
    double mean=0,standardDeviation=0;
    bool operator==(const HistogramStatistics &) const = default;
};

// A session-only projection of the rendered preview; it never edits the document.
class HistogramModel final : public std::enable_shared_from_this<HistogramModel> {
  public:
    static std::shared_ptr<HistogramModel> create(ii::runtime::EventLoop &,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>);
    ~HistogramModel();
    [[nodiscard]] ii::core::Result<void> setSource(std::shared_ptr<const ii::gui::RasterImage>);
    [[nodiscard]] ii::core::Result<void> setVisible(bool);
    [[nodiscard]] ii::core::Result<void> select(HistogramChannel);
    void close();
    ii::core::Observable<HistogramStatistics> statistics;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> plot;
    ii::core::Observable<std::string> summary,status;
    ii::core::Observable<bool> busy{false},rgbSelected{true},redSelected{false},greenSelected{false},blueSelected{false};
  private:
    struct Bridge;
    HistogramModel(ii::runtime::EventLoop::Sender,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>);
    void start();
    void schedule();
    void deliver();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    ii::runtime::EventLoop::Sender sender_;
    ii::runtime::TaskToken wake_;
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<const ii::gui::RasterImage> source_;
    std::array<std::shared_ptr<const ii::gui::RasterImage>,4> plots_{};
    std::uint64_t version_=0;
    HistogramChannel channel_=HistogramChannel::rgb;
    bool active_=true,visible_=false,inflight_=false;
};
}
