#include "NativeHomeProductSession.h"
#include "NativeHomeWorkspaceSources.h"
#include "NativeHomeWorkspaceView.h"
#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/GenerationController.h"
#include "App/ViewModels/GenerationProjection.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <iiSocietyContainer>
#include <iiGUI>
#include <QThread>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
NativeHomeProductSession::NativeHomeProductSession(WorkspaceSession *workspace, GenerationComposerModel *draft,
        NativeHomeProductOptions options, QObject *parent)
    : QObject(parent), workspace_(workspace), draft_(draft), navigate_(std::move(options.navigate)),
      failure_(std::move(options.failure)), deviceName_(std::move(options.deviceName)), idleStatus_(std::move(options.idleStatus)) {
    if (!workspace || !draft || workspace->thread() != QThread::currentThread() || draft->thread() != QThread::currentThread() ||
        thread() != QThread::currentThread() || draft->canvas() || !options.images || !options.referenceDetail ||
        !options.chooseFile || !navigate_)
        throw std::invalid_argument("Native Home requires an unbound draft, workspace, image service and host commands on their owner thread");
    const QPointer<NativeHomeProductSession> guard(this);
    inputs_ = std::make_unique<NativeHomeReferenceInputs>();
    draft->setCanvas(inputs_.get());
    NativeHomeComposerSession::Callbacks actions;
    actions.chooseAttachments = [guard]() -> ii::core::Result<void> {
        if (!guard || !guard->picker_) return ii::core::fail(ii::core::ErrorCode::closed, "Reference picker is unavailable");
        auto result = guard->picker_->choose();
        if (!result && guard && guard->workspace_) guard->workspace_->setGenerationRequestError(QString::fromStdString(result.error().message));
        if (!result && guard) guard->report(result.error());
        return result;
    };
    actions.removeAttachment = [guard](std::string key) -> ii::core::Result<void> {
        if (!guard || !guard->inputs_) return ii::core::fail(ii::core::ErrorCode::closed, "Reference inputs are unavailable");
        QPointer<NativeHomeReferenceInputs> inputs = guard->inputs_.get();
        if (inputs->removeAttachment(QString::fromStdString(key))) return {};
        return inputs ? ii::core::fail(ii::core::ErrorCode::invalid_argument, inputs->inputError().toStdString())
                      : ii::core::fail(ii::core::ErrorCode::closed, "Reference inputs are unavailable");
    };
    actions.paint = [guard]() -> ii::core::Result<void> {
        if (!guard || !guard->workspace_) return ii::core::fail(ii::core::ErrorCode::closed, "Workspace is unavailable");
        guard->workspace_->openNewCanvas(); return {};
    };
    actions.advanced = [guard](QVariantMap parameters) -> ii::core::Result<void> {
        if (!guard || !guard->workspace_ || !guard->draft_ || !guard->inputs_)
            return ii::core::fail(ii::core::ErrorCode::closed, "Workspace is unavailable");
        auto workspace = guard->workspace_; auto draft = guard->draft_; auto *inputs = guard->inputs_.get();
        if (workspace->advancedComposer(parameters, draft->mediaType(), draft->videoDuration(), draft->videoFps(), inputs->inputError())) return {};
        return ii::core::fail(ii::core::ErrorCode::failed, workspace ? workspace->generationRequestError().toStdString() : "Workspace is unavailable");
    };
    actions.failure = [guard](ii::core::Error error) { if (guard) guard->report(error); };
    composer_ = std::make_unique<NativeHomeComposerSession>(draft, std::move(options.choices), options.composerLabels.random, std::move(actions));
    references_ = std::make_unique<NativeHomeReferenceBinding>(inputs_.get(), draft, composer_.get(), std::move(options.referenceDetail));
    picker_ = std::make_unique<NativeHomeReferencePicker>(inputs_.get(), std::move(options.chooseFile), std::move(options.dialog),
        [guard](const ii::core::Error &error) { if (guard) guard->report(error); });
    content_ = std::make_unique<NativeHomeContentSession>(workspace, std::move(options.files), std::move(options.selectedFile),
                                                         std::move(options.generations), options.images);
    queue_ = std::make_unique<NativeHomeQueueSession>(workspace, std::move(options.queue), [guard] {
        if (guard && guard->workspace_) guard->workspace_->openGenerationReview();
    });
    sidebar_ = HomeSidebarModel::create(std::move(options.sidebar));
    if (auto selected = sidebar_->setSelected(std::move(options.initialSidebarKey)); !selected) throw std::invalid_argument(selected.error().message);
    sidebar_->setHandler([guard](std::string key) {
        if (!guard || !guard->active_ || !guard->sidebar_) return;
        auto selected = guard->sidebar_->setSelected(key);
        if (!guard) return;
        if (!selected) { guard->report(selected.error()); return; }
        auto navigate = guard->navigate_; if (navigate) navigate(std::move(key));
    });
    panel_ = createHomeComposerPanel(composer_->model(), std::move(options.composerLabels), options.layout, options.metrics, options.images);
    HomeWorkspaceComponents parts{sidebar_, homeComposerPanelSource(panel_), content_->recentFiles(), content_->selectedFile(),
        queue_->model(), content_->recentGenerations(), options.images, std::move(options.queueReadySvg),
        content_->recentFileImages(), content_->recentGenerationImages()};
    workspaceModel_ = HomeWorkspaceModel::create(homeWorkspaceSources(std::move(parts)), sidebar_, std::move(options.workspaceLabels),
        {.openFile = [guard] { if (guard && guard->workspace_) guard->workspace_->openHomeFileDialog(); },
         .newCanvas = [guard] { if (guard && guard->workspace_) guard->workspace_->openNewCanvas(); }});
    height_ = HomeComposerWorkspaceBinding::create(panel_, workspaceModel_, [guard](ii::core::Error error) { if (guard) guard->report(error); });
    source_ = ii::gui::compiled_view_source([model = workspaceModel_](ii::gui::WidgetBuilder &builder) {
        return buildHomeWorkspace(builder, {}, model);
    });
    connect(draft, &GenerationComposerModel::generateRequested, this, [guard](QString, QString, QString, int) {
        if (guard && guard->workspace_ && guard->draft_) (void)guard->workspace_->enqueueComposer(guard->draft_, guard->draft_);
    });
    connect(workspace, &WorkspaceSession::generationChanged, this, [guard] { if (guard) guard->rebindGeneration(); });
    connect(workspace, &WorkspaceSession::generationRequestErrorChanged, this, [guard] { if (guard) guard->updateError(); });
    connect(workspace, &QObject::destroyed, this, [guard] { if (guard) guard->retire(); });
    connect(draft, &QObject::destroyed, this, [guard] { if (guard) guard->retire(); });
    connect(queue_->projection(), &GenerationProjection::generationStatusTextChanged, this, [guard] { if (guard) guard->updateStatus(); });
    composerObservation_ = composer_->model()->snapshot.observe([guard](const auto &, std::uint64_t) { if (guard) guard->updateRuntimeNames(); });
    rebindGeneration(); updateRuntimeNames(); updateStatus();
}
NativeHomeProductSession::~NativeHomeProductSession() { retire(); }
void NativeHomeProductSession::report(const ii::core::Error &error) {
    if (!active_ || !failure_) return;
    auto callback = failure_; try { callback(error); } catch (...) {}
}
void NativeHomeProductSession::rebindGeneration() {
    if (!active_) return;
    QObject::disconnect(mirrorChanged_); QObject::disconnect(storageChanged_);
    QObject::disconnect(submissionQueued_); QObject::disconnect(generationDestroyed_);
    generation_ = workspace_ ? qobject_cast<GenerationController *>(workspace_->generation()) : nullptr;
    const QPointer<NativeHomeProductSession> guard(this);
    if (draft_) draft_->setGeneration(generation_);
    if (!guard) return;
    if (generation_) {
        mirrorChanged_ = connect(generation_->container(), &iiSocietyContainer::Container::stateChanged, this, [guard] { if (guard) guard->updateError(); });
        storageChanged_ = connect(generation_, &GenerationController::storageChanged, this, [guard] { if (guard) guard->updateError(); });
        submissionQueued_ = connect(generation_, &GenerationController::submissionQueued, this, [guard](const QStringList &ids) {
            if (guard && guard->active_ && guard->workspace_) guard->workspace_->submissionQueued(ids);
        });
        generationDestroyed_ = connect(generation_, &QObject::destroyed, this, [guard] { if (guard) { guard->generation_ = nullptr; guard->rebindGeneration(); } });
    }
    updateError();
}
void NativeHomeProductSession::updateError() {
    if (!active_ || !workspaceModel_) return;
    auto error = workspace_ ? workspace_->generationRequestError() : QString{};
    if (error.isEmpty() && generation_) error = generation_->container()->errorString();
    auto result = workspaceModel_->setError(error.toStdString()); if (!result) report(result.error());
}
void NativeHomeProductSession::updateStatus() {
    if (!active_ || !workspaceModel_ || !queue_) return;
    const auto value = queue_->projection()->generationStatusText().toStdString();
    auto result = workspaceModel_->setStatus(value.empty() ? idleStatus_ : value); if (!result) report(result.error());
}
void NativeHomeProductSession::updateRuntimeNames() {
    if (!active_ || !queue_ || !composer_) return;
    try { queue_->setRuntimeNames(QString::fromStdString(composer_->model()->snapshot.get().modelLabel), QString::fromStdString(deviceName_)); }
    catch (...) { report({ii::core::ErrorCode::failed, "Cannot update Home runtime names", std::current_exception()}); }
}
void NativeHomeProductSession::retire() {
    if (!active_) return;
    active_ = false; composerObservation_.reset(); QObject::disconnect(mirrorChanged_);
    QObject::disconnect(storageChanged_); QObject::disconnect(submissionQueued_); QObject::disconnect(generationDestroyed_);
    navigate_ = {}; failure_ = {};
    if (height_) try { height_->close(); } catch (...) {}
    if (sidebar_ && sidebar_->active()) try { sidebar_->close(); } catch (...) {}
    if (workspaceModel_ && workspaceModel_->active()) try { workspaceModel_->close(); } catch (...) {}
    if (panel_ && panel_->active()) try { panel_->close(); } catch (...) {}
    picker_.reset(); references_.reset(); content_.reset(); queue_.reset(); composer_.reset(); inputs_.reset();
}
} // namespace dreamscapes::home
