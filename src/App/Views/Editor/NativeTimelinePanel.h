#pragma once
#include "App/Native/NativeDocumentSession.h"
#include <iiGUI/NativeEditorTimelineView.h>
#include <iiSDKCore/Core/Property/Observable.h>
#include <memory>
#include <optional>
#include <thread>

namespace dreamscapes::editor {
// Projection and interaction state only. The shared document remains the sole
// owner of layer content and file edits.
class TimelinePanelModel final : public std::enable_shared_from_this<TimelinePanelModel> {
  public:
    static std::shared_ptr<TimelinePanelModel> create(std::shared_ptr<native::DocumentSession>,double rowHeight);
    [[nodiscard]] ii::core::Result<ii::gui::EditorTimelineProjection> project(const ii::gui::EditorTimelineQuery &) const;
    [[nodiscard]] ii::core::Result<void> input(const ii::gui::EditorTimelineEvent &);
    void sizeChanged(double,double);
    void viewportChanged(const ii::gui::EditorTimelineOptions &);
    ii::core::Observable<ii::gui::EditorTimelineOptions> options;
    ii::core::Observable<double> width{1},height{1};
    ii::core::Observable<std::string> status;
  private:
    struct Gesture final {
        ii::gui::EditorTimelineGesture kind;
        std::string clip,layer;
        std::size_t index=0;
        std::uint64_t generation=0,revision=0;
        std::int64_t first=0,end=0,initial=0,previewFirst=0,previewEnd=0,minOffset=0,maxOffset=0;
        std::int64_t keyOriginal=0,previewKey=0;
        native::LayerKeyKind keyKind=native::LayerKeyKind::source;
    };
    TimelinePanelModel(std::shared_ptr<native::DocumentSession>,double);
    void start();
    void synchronize(bool projectionChanged);
    const std::thread::id owner_=std::this_thread::get_id();
    std::shared_ptr<native::DocumentSession> document_;
    double rowHeight_;
    std::uint64_t projectionRevision_=0;
    ii::core::ObservableSubscription frameObserver_,revisionObserver_,generationObserver_,selectionObserver_;
    std::optional<Gesture> gesture_;
    bool synchronizing_=false,pending_=false,pendingProjection_=false;
};
}
