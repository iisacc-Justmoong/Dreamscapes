#pragma once
#include "NativeBrushQueue.h"
#include "App/Native/NativeDocumentSession.h"
#include <iiGUI>
#include <iiSDKCore>
#include <memory>
#include <optional>
#include <thread>

namespace dreamscapes::editor {
struct BrushCursor final {
    bool visible=false;double x=0,y=0,radius=0;
    bool operator==(const BrushCursor &) const = default;
};
class BrushToolModel final : public std::enable_shared_from_this<BrushToolModel> {
  public:
    static std::shared_ptr<BrushToolModel> create(std::shared_ptr<native::DocumentSession>,
        ii::runtime::EventLoop &,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>);
    ~BrushToolModel();
    void setVisible(bool);
    void setViewport(double,double);
    void setFrame(std::shared_ptr<const ii::gui::RasterImage>);
    [[nodiscard]] ii::core::Result<void> setBrush(const iiSharedCanvas::BitmapBrush &);
    [[nodiscard]] ii::core::Result<void> setSize(double);
    [[nodiscard]] ii::core::Result<void> setHardness(double);
    [[nodiscard]] ii::core::Result<void> setOpacity(double);
    [[nodiscard]] ii::core::Result<void> setFlow(double);
    [[nodiscard]] ii::core::Result<void> setColor(std::string);
    void sizeMoved(double value) {(void)setSize(value);}
    void hardnessMoved(double value) {(void)setHardness(value);}
    void opacityMoved(double value) {(void)setOpacity(value);}
    void flowMoved(double value) {(void)setFlow(value);}
    void colorEdited(std::string value) {(void)colorText.set(std::move(value));}
    void colorSubmitted() {(void)setColor(colorText.get());}
    [[nodiscard]] ii::core::Result<bool> input(const ii::gui::WidgetInput &);
    void acknowledgeFrame();
    void cancel();
    void close();
    ii::core::Observable<bool> available{false},busy{false},drawing{false};
    ii::core::Observable<double> size{8},hardness{100},opacity{100},flow{100},width{1},height{1};
    ii::core::Observable<std::string> colorText{"#000000"},status,availability;
    ii::core::Observable<BrushCursor> cursor;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> previewImage;
  private:
    struct Gesture;
    BrushToolModel(std::shared_ptr<native::DocumentSession>);
    void start(ii::runtime::EventLoop &,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>);
    void synchronize();
    void update(BrushQueueUpdate);
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    std::shared_ptr<BrushQueue> queue_;
    std::shared_ptr<const ii::gui::RasterImage> frame_;
    std::unique_ptr<Gesture> gesture_;
    iiSharedCanvas::BitmapBrush brush_;
    ii::core::ObservableSubscription generation_,revision_,selection_,frameObserver_;
    std::uint64_t request_=0,commitRevision_=0;
    bool visible_=false,active_=true,committing_=false,waitingFrame_=false;
};
}
