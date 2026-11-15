#include "NativeReviewQueuePresenter.h"
#include "GenerationReviewModel.h"
#include "App/Native/NativePanelOverlays.h"
#include "App/Native/NativePanelRegion.h"
#include "NativeReviewQueueSession.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <QCoreApplication>
#include <QEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <algorithm>
#include <utility>

namespace dreamscapes::review {
namespace {
constexpr auto noActive = static_cast<std::size_t>(-1);
QueueStrings translatedStrings() {
    const auto view = [](const char *s) { return QCoreApplication::translate("ReviewQueue", s).toStdString(); };
    const auto state = [](const char *s) { return QCoreApplication::translate("dreamscapes::GenerationProjection", s).toStdString(); };
    QueueStrings s;
    s.heading = view("GENERATION"); s.allJobs = view("All jobs"); s.empty = view("No generation jobs");
    s.preview = view("Live preview"); s.cancel = view("Cancel job"); s.failure = view("Failure details");
    s.unavailable = view("Unavailable"); s.modelUnavailable = view("Model unavailable"); s.preparing = view("Preparing");
    s.running = state("Running"); s.queued = state("Queued"); s.completed = state("Completed");
    s.failed = state("Failed"); s.canceled = state("Canceled"); s.interrupted = state("Interrupted");
    s.connecting = state("Connecting"); s.downloading = state("Downloading");
    s.jobTitle = [](std::string_view id) {
        return QCoreApplication::translate("ReviewQueue", "Job %1")
            .arg(QString::fromUtf8(id.data(), qsizetype(id.size()))).toStdString();
    };
    s.count = [running = s.running, queued = s.queued, completed = s.completed, failed = s.failed,
               canceled = s.canceled](std::string_view label, int value) {
        const char *format = label == running ? "Running  %1" : label == queued ? "Queued  %1" :
            label == completed ? "Complete  %1" : label == failed ? "Failed  %1" :
            label == canceled ? "Canceled  %1" : nullptr;
        return format ? QCoreApplication::translate("ReviewQueue", format).arg(value).toStdString()
                      : std::string(label) + "  " + std::to_string(value);
    };
    s.outputCount = [](std::size_t n) { return QCoreApplication::translate("ReviewQueue", "%1 outputs").arg(qulonglong(n)).toStdString(); };
    s.progressDetail = [](std::string_view phase, int step, int total) {
        return QString::fromUtf8(phase.data(), qsizetype(phase.size())).append(
            QCoreApplication::translate("ReviewQueue", " · %1 / %2").arg(step).arg(total)).toStdString();
    };
    return s;
}
bool queueSignals(QQuickItem *item) {
    const auto *meta = item->metaObject();
    return meta->indexOfSignal("jobRequested(QString)") >= 0 && meta->indexOfSignal("cancelRequested(QString)") >= 0 &&
           meta->indexOfSignal("failureRequested(QString)") >= 0;
}
}
NativeReviewQueuePresenter::NativeReviewQueuePresenter(QQuickWindow *window, WorkspaceSession *workspace,
    GenerationReviewModel *review, Factory factory, QObject *parent)
    : QObject(parent), window_(window), workspace_(workspace), review_(review), factory_(std::move(factory)) {
    if (!window_ || !workspace_ || !review_ || !factory_ || window_->thread() != QThread::currentThread() ||
        workspace_->thread() != QThread::currentThread() || review_->thread() != QThread::currentThread()) return;
    for (auto *list : window_->findChildren<QQuickItem *>(QStringLiteral("reviewQueueList"))) {
        auto *panel = list->parentItem();
        while (panel && !queueSignals(panel)) panel = panel->parentItem();
        if (panel && std::none_of(queues_.begin(), queues_.end(), [panel](const auto &q) { return q == panel; }))
            queues_.push_back(panel);
    }
    if (queues_.empty()) return;
    for (std::size_t i = 0; i < queues_.size(); ++i) {
        regions_.push_back(std::make_unique<native::NativePanelRegion>(window_, queues_[i], native::NativePanelRegion::Surface{
            .show = [this, i](QRectF frame) { return show(i, frame); }, .hide = [this, i] { hide(i); }}));
    }
    overlays_ = std::make_unique<native::NativePanelOverlays>(window_, [this](bool blocked) {
        for (const auto &region : regions_) region->setBlocked(blocked);
    });
    connect(window_, &QObject::destroyed, this, [this] { shutdown({}); });
    connect(workspace_, &QObject::destroyed, this, [this] { shutdown({}); });
    connect(review_, &QObject::destroyed, this, [this] { shutdown({}); });
    if (qApp) qApp->installEventFilter(this);
    valid_ = true;
}
NativeReviewQueuePresenter::~NativeReviewQueuePresenter() {
    destroying_ = true;
    if (qApp) qApp->removeEventFilter(this);
    for (const auto &region : regions_) region->close();
    if (auto surface = surface_) surface->close();
    if (session_) try { session_->close(); } catch (...) {}
}
void NativeReviewQueuePresenter::begin() {
    if (!valid_ || started_ || stopping_) return;
    started_ = true;
    for (const auto &region : regions_) region->setReady(true);
}
bool NativeReviewQueuePresenter::show(std::size_t index, QRectF frame) {
    if (stopping_ || restarting_ || failed_ || !window_ || !workspace_ || !review_ || index >= queues_.size()) return false;
    if (!surface_) {
        try {
            const QPointer<NativeReviewQueuePresenter> self(this);
            session_ = std::make_unique<NativeReviewQueueSession>(workspace_, review_->nativeSession(), NativeReviewQueueActions{
                .cancel = [self](const QString &id) { if (self) self->dispatch("cancelRequested", id); },
                .failure = [self](const QString &id) { if (self) self->dispatch("failureRequested", id); },
                .selection = [self](const QString &id) { if (self) self->dispatch("jobRequested", id); }}, translatedStrings());
            surface_ = factory_(session_->model(), {.ready = [self] { if (self) self->ready(); },
                .failed = [self] { if (self) self->failed(); }, .closed = [self] { if (self) self->closed(); }});
            auto surface = surface_;
            if (!surface || !surface->start(frame)) { failed(); return false; }
        } catch (...) { failed(); }
        return false;
    }
    auto surface = surface_;
    if (!surfaceReady_ || !surface || !surface->show(frame)) return false;
    active_ = index;
    return true;
}
void NativeReviewQueuePresenter::hide(std::size_t index) {
    if (active_ != index) return;
    active_ = noActive;
    if (surface_) surface_->hide();
}
void NativeReviewQueuePresenter::dispatch(const char *method, const QString &id) {
    if (stopping_ || active_ >= queues_.size() || !queues_[active_]) return;
    const auto queue = queues_[active_];
    if (surface_) surface_->restoreHostFocus();
    (void)QMetaObject::invokeMethod(queue, method, Qt::DirectConnection, Q_ARG(QString, id));
}
void NativeReviewQueuePresenter::ready() {
    if (destroying_ || stopping_ || restarting_ || failed_) return;
    surfaceReady_ = true;
    for (const auto &region : regions_) { region->setReady(false); region->setReady(true); }
}
void NativeReviewQueuePresenter::failed() {
    if (destroying_ || failed_) return;
    failed_ = true; surfaceReady_ = false;
    for (const auto &region : regions_) region->setReady(false);
    if (auto surface = surface_) surface->close();
    else if (session_) { session_->close(); session_.reset(); }
}
void NativeReviewQueuePresenter::closed() {
    if (destroying_) return;
    surfaceReady_ = false; active_ = noActive;
    surface_.reset();
    if (session_) { session_->close(); session_.reset(); }
    if (stopping_) { finishShutdown(); return; }
    if (restarting_) {
        restarting_ = false; failed_ = false;
        for (const auto &region : regions_) region->setReady(true);
    } else {
        failed_ = true;
        for (const auto &region : regions_) region->setReady(false);
    }
}
void NativeReviewQueuePresenter::restartForLanguage() {
    if (!started_ || stopping_ || restarting_) return;
    restarting_ = true; surfaceReady_ = false;
    for (const auto &region : regions_) region->setReady(false);
    if (auto surface = surface_) surface->close();
    else {
        if (session_) { session_->close(); session_.reset(); }
        restarting_ = false; failed_ = false;
        for (const auto &region : regions_) region->setReady(true);
    }
}
bool NativeReviewQueuePresenter::eventFilter(QObject *watched, QEvent *event) {
    if (watched == qApp && event->type() == QEvent::LanguageChange) restartForLanguage();
    return QObject::eventFilter(watched, event);
}
void NativeReviewQueuePresenter::shutdown(std::function<void()> done) {
    if (done) completion_.push_back(std::move(done));
    if (stopping_) { if (!surface_) finishShutdown(); return; }
    stopping_ = true;
    for (const auto &region : regions_) region->close();
    overlays_.reset();
    if (auto surface = surface_) surface->close(); else finishShutdown();
}
void NativeReviewQueuePresenter::finishShutdown() {
    if (session_) { session_->close(); session_.reset(); }
    auto callbacks = std::move(completion_);
    for (auto &done : callbacks) if (done) done();
}
} // namespace dreamscapes::review
