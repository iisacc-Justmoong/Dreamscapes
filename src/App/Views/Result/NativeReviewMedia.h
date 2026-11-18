#pragma once
#include <iiGUI>
#include <iiSDKCore>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace dreamscapes::review {
enum class ReviewPlaybackState { stopped, playing, paused };
class ReviewPlaybackPort {
  public:
    struct Events final {
        std::function<void(std::uint64_t,ReviewPlaybackState,std::int64_t,std::int64_t)> state;
        std::function<void(std::uint64_t,std::shared_ptr<const ii::gui::RasterImage>)> frame;
        std::function<void(std::uint64_t,std::string)> error;
    };
    virtual ~ReviewPlaybackPort() = default;
    virtual void setEvents(Events) = 0;
    [[nodiscard]] virtual ii::core::Result<void> load(std::string source,std::uint64_t generation) = 0;
    [[nodiscard]] virtual ii::core::Result<void> play() = 0;
    [[nodiscard]] virtual ii::core::Result<void> pause() = 0;
    [[nodiscard]] virtual ii::core::Result<void> replay() = 0;
    [[nodiscard]] virtual ii::core::Result<void> stop() = 0;
    [[nodiscard]] virtual ii::core::Result<void> setViewport(double,double) { return {}; }
};
struct ReviewMediaStrings final {
    std::string noResult,play,pause,replay;
    std::function<std::string(std::int64_t,std::int64_t)> time;
    std::string loading,unavailable;
};

// Only presentation and transient playback state live here; the host owns the
// decoder, audio output and original media, which are never modified by this view.
class ReviewMediaModel final : public std::enable_shared_from_this<ReviewMediaModel> {
  public:
    static std::shared_ptr<ReviewMediaModel> create(ReviewMediaStrings);
    ~ReviewMediaModel();
    [[nodiscard]] ii::core::Result<void> setPlayback(std::shared_ptr<ReviewPlaybackPort>);
    [[nodiscard]] ii::core::Result<void> setResult(bool video,std::string preview,std::string media);
    [[nodiscard]] ii::core::Result<void> setVisible(bool);
    [[nodiscard]] ii::core::Result<void> setViewport(double width,double height,double controlsHeight,double gap);
    [[nodiscard]] ii::core::Result<void> toggle();
    [[nodiscard]] ii::core::Result<void> replay();
    void setFailureHandler(std::function<void(std::string)>);
    void close();
    [[nodiscard]] bool active() const noexcept { return active_; }

    const ReviewMediaStrings strings;
    ii::core::Observable<std::string> previewSource,playLabel,timeText,error;
    ii::core::Observable<bool> controlsVisible{false},canControl{false},playing{false},showFrame{false},noPreview{true},hasError{false};
    ii::core::Observable<double> viewportWidth{1},viewportHeight{1},previewHeight{1};
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> frame;
  private:
    explicit ReviewMediaModel(ReviewMediaStrings);
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] ii::core::Result<void> replaceSource();
    void bindEvents();
    void acceptState(std::uint64_t,ReviewPlaybackState,std::int64_t,std::int64_t);
    void acceptFrame(std::uint64_t,std::shared_ptr<const ii::gui::RasterImage>);
    void acceptError(std::uint64_t,std::string);
    void updateVisibility();
    void updateGeometry();
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<ReviewPlaybackPort> playback_;
    std::function<void(std::string)> failure_;
    std::string media_;
    std::uint64_t generation_=0;
    ReviewPlaybackState state_=ReviewPlaybackState::stopped;
    double controlsHeight_=0,gap_=0;
    bool video_=false,visible_=false,active_=true;
};
} // namespace dreamscapes::review
