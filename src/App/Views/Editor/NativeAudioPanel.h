#pragma once
#include "NativeAudioTrack.h"
#include "NativeEditorFrame.h"
#include <iiGUI>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace dreamscapes::editor {
class AudioPanelModel final : public std::enable_shared_from_this<AudioPanelModel> {
  public:
    using PickHandler=std::function<ii::core::Result<void>()>;
    static std::shared_ptr<AudioPanelModel> create(std::shared_ptr<native::DocumentSession>,ii::runtime::EventLoop &,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>);
    ~AudioPanelModel();
    void setVisible(bool);
    void showImport();
    void showTrim();
    void showMix();
    void sizeChanged(double,double,double,double);
    void setPickHandler(PickHandler);
    [[nodiscard]] ii::core::Result<void> chooseFile();
    [[nodiscard]] ii::core::Result<void> selectTrack(int);
    [[nodiscard]] ii::core::Result<void> selectClip(int);
    [[nodiscard]] ii::core::Result<void> createTrack();
    [[nodiscard]] ii::core::Result<void> importFile(std::filesystem::path);
    [[nodiscard]] ii::core::Result<void> setMutedDraft(bool);
    [[nodiscard]] ii::core::Result<void> editGain(std::string);
    [[nodiscard]] ii::core::Result<void> setGainDraft(double);
    [[nodiscard]] ii::core::Result<void> applyTrack();
    [[nodiscard]] ii::core::Result<void> resetTrack();
    [[nodiscard]] ii::core::Result<void> editTrim(std::string);
    [[nodiscard]] ii::core::Result<void> applyTrim();
    [[nodiscard]] ii::core::Result<void> resetTrim();
    [[nodiscard]] ii::core::Result<void> editTimelineClip(std::string track,std::string clip,
        ii::gui::EditorTimelineGesture,std::int64_t first,std::int64_t end);
    void reportError(std::string);
    [[nodiscard]] std::string selectedTrackId() const {return selected_;}
    void close();
    ii::core::Observable<std::vector<std::string>> trackNames{std::vector<std::string>{"Select track"}};
    ii::core::Observable<std::vector<std::string>> clipNames{std::vector<std::string>{"Select clip"}};
    ii::core::Observable<int> selectedTrack{0};
    ii::core::Observable<int> selectedClip{0};
    ii::core::Observable<bool> busy{false},canCreate{false},canChoose{false},canApply{false},editable{false},clipEditable{false},muted{false},importMode{true},trimMode{false},mixMode{false},canTrim{false};
    ii::core::Observable<std::string> gainText,summary,status,trimRange;
    static constexpr double minimumGain=-96,maximumGain=24;
    ii::core::Observable<double> width{1},height{1},contentWidth{1},fieldWidth{1},gainValue{0};
  private:
    struct Bridge;
    enum class Job {create,patch,import,trim,timeline};
    using Operation=std::function<ii::core::Result<PreparedAudioEdit>(std::shared_ptr<std::atomic_bool>)>;
    AudioPanelModel(std::shared_ptr<native::DocumentSession>,ii::runtime::EventLoop::Sender,
        std::shared_ptr<iiSharedCanvas::FrameRenderExecutor>);
    void start();
    void project(bool forceDraft=false);
    void updateAvailability();
    void projectTrim(const iiSharedCanvas::Document *,bool);
    [[nodiscard]] std::optional<std::pair<std::int64_t,std::int64_t>> trimFrames() const;
    [[nodiscard]] ii::core::Result<void> run(Job,Operation,bool requireVisible=true);
    void deliver();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    ii::runtime::EventLoop::Sender sender_;
    ii::runtime::TaskToken wake_;
    std::shared_ptr<iiSharedCanvas::FrameRenderExecutor> executor_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<std::atomic_bool> cancellation_;
    ii::core::ObservableSubscription generationObserver_,revisionObserver_;
    std::vector<std::string> trackIds_;
    std::vector<std::string> clipIds_;
    PickHandler pick_;
    std::string selected_,selectedClip_,jobSelection_;
    std::optional<iiSharedCanvas::AudioClip> baselineClip_;
    bool trimDirty_=false,trimConflict_=false;
    double baselineGain_=0,draftGain_=0;
    bool baselineMuted_=false,draftValid_=true,draftDirty_=false,conflict_=false;
    bool visible_=false,active_=true,projecting_=false,projectionPending_=false,forcePending_=false,trackForcePending_=false,trimForcePending_=false;
    Job job_=Job::create;
};
}
