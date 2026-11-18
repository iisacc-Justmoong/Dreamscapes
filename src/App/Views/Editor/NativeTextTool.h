#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <iiGUI>
#include <functional>
#include <optional>
#include <thread>

namespace dreamscapes::editor {
struct TextDefaults final {
    std::string family,placeholder,colorText;
    double size=0,minimum=0,maximum=0,step=0;
    std::uint32_t color=0;
};
struct TextOutlineRequest final {
    std::string content,family;
    double size;
    std::uint32_t color;
    iiSharedCanvas::Point origin;
    double maxWidth,maxHeight;
};
using TextOutliner=std::function<ii::core::Result<std::vector<iiSharedCanvas::VectorPath>>(const TextOutlineRequest &)>;
[[nodiscard]] ii::core::Result<TextDefaults> parseTextDefaults(std::string_view);
[[nodiscard]] bool nativeTextOutlineAvailable() noexcept;
[[nodiscard]] ii::core::Result<std::vector<iiSharedCanvas::VectorPath>> nativeTextOutline(const TextOutlineRequest &);

class TextToolModel final : public std::enable_shared_from_this<TextToolModel> {
  public:
    static std::shared_ptr<TextToolModel> create(std::shared_ptr<native::DocumentSession>,TextDefaults);
    [[nodiscard]] ii::core::Result<void> setContent(std::string);
    [[nodiscard]] ii::core::Result<void> setFamily(std::string);
    [[nodiscard]] ii::core::Result<void> setFontSize(double);
    [[nodiscard]] ii::core::Result<void> setColor(std::string);
    void setOutliner(TextOutliner);
    void setVisible(bool);
    void setViewport(double,double);
    void setFrame(std::shared_ptr<const ii::gui::RasterImage>);
    void cancel();
    [[nodiscard]] ii::core::Result<bool> input(const ii::gui::WidgetInput &);
    const TextDefaults defaults;
    ii::core::Observable<std::string> content,family,colorText,status;
    ii::core::Observable<double> fontSize,width{1},height{1};
  private:
    struct Gesture final {
        std::uint64_t generation,revision,device;
        double x,y,left,top,displayWidth,displayHeight;
        iiSharedCanvas::CanvasRegion region;
    };
    TextToolModel(std::shared_ptr<native::DocumentSession>,TextDefaults);
    void start();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    TextOutliner outliner_;
    std::shared_ptr<const ii::gui::RasterImage> frame_;
    std::optional<Gesture> gesture_;
    ii::core::ObservableSubscription generationObserver_,revisionObserver_,frameObserver_;
    std::uint32_t color_;
    bool visible_=false;
};
}
