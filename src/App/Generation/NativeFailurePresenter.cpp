#include "NativeFailurePresenter.h"
#include "GenerationController.h"
#include "NativeFailureDetailsBridge.h"
#include "App/Views/Failure/NativeFailureDetailsView.h"
#include <iiGUI/Host.h>
#include <iiGUI/NativeWidgets.h>
#include <iiSDKCore/Core/Property/ObservableSubscription.h>
#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <utility>
#include <vector>

namespace dreamscapes::failure {
namespace {
FailureStrings localizedStrings() {
    auto translated = [](const char *source) { return QCoreApplication::translate("FailureDetails", source); };
    auto withoutLeadingBreaks = [](QString value) {
        if (value.startsWith("\n\n")) value.remove(0, 2);
        return value.toStdString();
    };
    FailureStrings strings;
    strings.title = translated("Failure explanation").toStdString();
    strings.close = translated("Close").toStdString();
    strings.copy = translated("Copy report").toStdString();
    auto pending = translated("Agent analysis is running. Captured evidence is available below.\n\n");
    if (pending.endsWith("\n\n")) pending.chop(2);
    strings.pending = pending.toStdString();
    strings.agentStatus = withoutLeadingBreaks(translated("\n\nAgent status: "));
    strings.persistenceError = withoutLeadingBreaks(translated("\n\nReport could not be saved: "));
    strings.reportFile = withoutLeadingBreaks(translated("\n\nReport file: "));
    return strings;
}
}
struct NativeFailurePresenter::Session final : std::enable_shared_from_this<Session> {
    enum class End { parentClosed, dismissed, fallback, shutdown };
    QPointer<NativeFailurePresenter> owner;
    QString jobId;
    std::shared_ptr<FailureDetailsModel> model;
    std::unique_ptr<NativeFailureDetailsBridge> bridge;
    std::unique_ptr<ii::gui::Host> host;
    ii::core::ObservableSubscription opened, wrapped;
    std::vector<std::function<void()>> shutdownCallbacks;
    End end = End::parentClosed;
    bool closing = false;

    void close(End reason) {
        if (reason == End::shutdown || !closing) end = reason;
        if (closing || !host) return;
        closing = true;
        const auto weak = weak_from_this();
        auto closeOnHost = [weak] {
            if (auto session = weak.lock(); session && session->host)
                (void)session->host->request_close();
        };
        // Dismissal may originate inside a widget callback. Retire the surface
        // only after that callback returns to its event loop.
        if (!host->executor().post(closeOnHost)) QTimer::singleShot(0, std::move(closeOnHost));
    }
};

NativeFailurePresenter::NativeFailurePresenter(QObject *parent) : QObject(parent) {}
NativeFailurePresenter::~NativeFailurePresenter() {
    if (session_) { session_->owner = nullptr; session_->close(Session::End::shutdown); }
}

bool NativeFailurePresenter::open(QObject *controller, QObject *window, const QString &jobId) {
    auto *generation = qobject_cast<GenerationController *>(controller);
    auto *target = qobject_cast<QWindow *>(window);
    if (!generation || !target || !target->isVisible()) return false;
    return openOnNativeParent(generation, reinterpret_cast<void *>(target->winId()), jobId);
}

bool NativeFailurePresenter::openOnNativeParent(GenerationController *controller, void *nativeParent, const QString &jobId) {
    if (QThread::currentThread() != thread() || !controller || controller->thread() != thread() ||
        !nativeParent || jobId.isEmpty()) return false;
    if (session_) return !session_->closing && session_->jobId == jobId;
    try {
        auto session = std::make_shared<Session>();
        session->owner = this; session->jobId = jobId;
        session->model = std::make_shared<FailureDetailsModel>(localizedStrings());
        session->bridge = std::make_unique<NativeFailureDetailsBridge>(controller, session->model);
        if (!session->bridge->openReport(jobId) || !session->model->usesWrappedText.get()) return false;
        session->host = std::make_unique<ii::gui::Host>();
        const std::weak_ptr<Session> weak = session;
        session->opened = session->model->open.observe([weak](bool isOpen, std::uint64_t) {
            if (!isOpen) if (auto active = weak.lock()) active->close(Session::End::dismissed);
        });
        session->wrapped = session->model->usesWrappedText.observe([weak](bool wraps, std::uint64_t) {
            if (!wraps) if (auto active = weak.lock()) active->close(Session::End::fallback);
        });
        session_ = session;
        auto attached = session->host->attach_external(nativeParent, {
            .opened = [weak] {
                auto active = weak.lock();
                if (!active || active->closing || !active->host) return;
                const auto snapshot = active->host->snapshot();
                if (!snapshot || !active->model->setViewport(snapshot->size.width, snapshot->size.height)) {
                    active->close(Session::End::fallback); return;
                }
                auto mounted = active->host->mount_widgets([model = active->model](ii::gui::WidgetBuilder &builder) {
                    return buildFailureDetails(builder, {}, model);
                });
                if (!mounted) active->close(Session::End::fallback);
            },
            .resized = [weak](ii::gui::Size size) {
                if (auto active = weak.lock(); active && !active->closing)
                    if (!active->model->setViewport(size.width, size.height)) active->close(Session::End::fallback);
            },
            .closed = [session] {
                session->opened.reset(); session->wrapped.reset();
                session->host.reset(); session->bridge.reset();
                const auto owner = session->owner;
                if (owner && owner->session_ == session) owner->session_.reset();
                if (owner && session->end == Session::End::dismissed) emit owner->dismissed(session->jobId);
                if (owner && session->end == Session::End::fallback) emit owner->fallbackRequired(session->jobId);
                auto callbacks = std::move(session->shutdownCallbacks);
                for (auto &callback : callbacks) if (callback) callback();
            }
        });
        if (!attached) { session_.reset(); return false; }
        return true;
    } catch (...) {
        // Failure before a successful attach is a recoverable legacy-view path.
        // All attached-host callbacks arrange their own asynchronous retirement.
        session_.reset();
        return false;
    }
}

std::shared_ptr<FailureDetailsModel> NativeFailurePresenter::activeModel() const {
    return session_ ? session_->model : nullptr;
}
void NativeFailurePresenter::shutdown(std::function<void()> done) {
    if (!session_) { if (done) done(); return; }
    session_->shutdownCallbacks.push_back(std::move(done));
    session_->close(Session::End::shutdown);
}
} // namespace dreamscapes::failure
