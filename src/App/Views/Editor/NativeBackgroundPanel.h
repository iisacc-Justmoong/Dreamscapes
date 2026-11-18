#pragma once
#include "NativeBackgroundColor.h"
#include "NativeEditorFrame.h"
#include <iiGUI>
#include <iiSDKCore>
#include <functional>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::editor {
class BackgroundPanelModel;
struct BackgroundChannel final {
    std::string key,label;
    iiColorManager::Channel channel{};
    double minimum=0,maximum=1,step=1;
    ii::core::Observable<double> value{0};
    ii::core::Observable<std::string> text;
    std::weak_ptr<BackgroundPanelModel> owner;
    [[nodiscard]] ii::core::Result<void> setValue(double);
    void moved(double);
    void edited(std::string);
    void submitted();
};
class BackgroundPanelModel final : public std::enable_shared_from_this<BackgroundPanelModel> {
  public:
    static std::shared_ptr<BackgroundPanelModel> create(std::shared_ptr<native::DocumentSession>,
        ii::runtime::EventLoop &,std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,std::string_view defaults);
    ~BackgroundPanelModel();
    void setVisible(bool);
    void sizeChanged(double,double,double,double,double,double);
    void setMaskAction(std::function<void()> action) {showMask_=std::move(action);}
    void showMask() {if (showMask_) showMask_();}
    void showColor();
    void showReplace();
    void setImagePickHandler(std::function<ii::core::Result<void>()>);
    [[nodiscard]] ii::core::Result<void> chooseImage();
    [[nodiscard]] ii::core::Result<void> setImagePath(std::filesystem::path);
    [[nodiscard]] ii::core::Result<void> selectImageFit(int);
    [[nodiscard]] ii::core::Result<void> applyImage();
    void reportError(std::string);
    [[nodiscard]] ii::core::Result<void> selectMode(int);
    [[nodiscard]] ii::core::Result<void> selectFill(int);
    [[nodiscard]] ii::core::Result<void> selectStop(int);
    [[nodiscard]] ii::core::Result<void> selectPreset(int);
    [[nodiscard]] ii::core::Result<void> selectSwatch(int);
    [[nodiscard]] ii::core::Result<void> selectRecent(int);
    [[nodiscard]] ii::core::Result<void> editHex(std::string);
    [[nodiscard]] ii::core::Result<void> commitHex();
    [[nodiscard]] ii::core::Result<void> setChannel(const BackgroundChannel *,double);
    [[nodiscard]] ii::core::Result<void> setSurface(bool rail,double x,double y);
    [[nodiscard]] ii::core::Result<void> setAlpha(double);
    [[nodiscard]] ii::core::Result<void> setOpacity(double);
    [[nodiscard]] ii::core::Result<void> setAngle(double);
    [[nodiscard]] ii::core::Result<void> setPerceptual(bool);
    [[nodiscard]] ii::core::Result<void> setStopPosition(double);
    [[nodiscard]] ii::core::Result<void> addStop();
    [[nodiscard]] ii::core::Result<void> removeStop();
    [[nodiscard]] ii::core::Result<void> apply();
    [[nodiscard]] iiColorManager::Point surfacePosition(bool rail) const;
    void close();
    std::vector<std::string> modeNames,fillNames{"Solid","Linear gradient","Radial gradient"},presetNames{"Select preset"},swatchNames{"Select swatch"};
    std::vector<std::string> imageFits{"Fill","Fit","Stretch"};
    ii::core::Observable<std::vector<std::shared_ptr<BackgroundChannel>>> channels,allChannels;
    ii::core::Observable<std::vector<std::string>> stopNames;
    ii::core::Observable<std::vector<std::string>> recentNames{std::vector<std::string>{"Recent colors"}};
    ii::core::Observable<int> modeIndex{0},fillIndex{0},stopIndex{0};
    ii::core::Observable<int> imageFit{0};
    ii::core::Observable<bool> canApply{false},busy{false},hasPlane{false},gradient{false},perceptual{true},canRemoveStop{false};
    ii::core::Observable<bool> replaceMode{false},canChooseImage{false},canApplyImage{false};
    ii::core::Observable<double> alpha{100},opacity{100},angle{90},stopPosition{0};
    ii::core::Observable<double> width{1},height{1},contentWidth{1},planeWidth{1},railWidth{1},planeHeight{1},channelListHeight{1},allChannelListHeight{1},channelSliderWidth{1},markerSize{1};
    ii::core::Observable<std::string> hex,status,availability,alphaText,opacityText,angleText,stopPositionText,imageName{"No image selected"};
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> planeImage,railImage;
    ii::core::Observable<std::uint64_t> surfaceVersion{0};
  private:
    struct Bridge;
    struct Preset {std::string name;std::vector<iiColorManager::Rgba> colors;};
    BackgroundPanelModel(std::shared_ptr<native::DocumentSession>,ii::runtime::EventLoop::Sender,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>,std::string_view);
    void start();
    void resetDraft();
    void projectPicker(bool rebuildChannels);
    void projectStops();
    void updateAvailability();
    void requestSurface();
    void scheduleSurface();
    void deliver();
    [[nodiscard]] ii::core::Result<void> writable() const;
    [[nodiscard]] iiColorManager::Surface surface(bool rail) const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::runtime::EventLoop::Sender sender_;
    ii::runtime::TaskToken wake_;
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<std::atomic_bool> fillCancellation_;
    ii::core::ObservableSubscription documentObserver_,revisionObserver_;
    iiColorManager::Picker picker_;
    BackgroundFillSpec defaults_,draft_;
    iiColorManager::Mode defaultMode_=iiColorManager::Mode::HueCube;
    std::vector<Preset> presets_;
    std::vector<iiColorManager::Rgba> swatches_;
    std::vector<iiColorManager::Rgba> recent_;
    iiColorManager::Rgba submittedColor_;
    std::function<void()> showMask_;
    std::function<ii::core::Result<void>()> pickImage_;
    std::filesystem::path imagePath_;
    bool submittedImage_=false;
    std::uint64_t requestedSurface_=0;
    double rowHeight_=1;
    bool visible_=false,active_=true,surfaceInFlight_=false,hexPending_=false;
};
}
