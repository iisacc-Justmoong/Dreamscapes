#include "../src/App/Publishing/SocialPublishingController.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QThread>
#include <QPointer>
#include <QScopeGuard>
#include <atomic>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace iiSocialPublish;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool wait(std::function<bool()> predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < 5000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); }
    return predicate();
}
struct Fake final : Transport {
    std::atomic<unsigned> calls{0}, failAt{0};
    std::atomic<bool> hold{false}, ambiguous{false}, onWorker{false};
    Response send(const Request& request, std::stop_token stop) override {
        ++calls;
        onWorker = QThread::currentThread() != QCoreApplication::instance()->thread();
        while (hold && !stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (stop.stop_requested()) return {0, {}, {}, true, true, false};
        if (ambiguous || calls==failAt) return {0, {}, {}, true, false, true};
        if (request.url != "https://api.x.com/2/tweets") return {404};
        return {201, {}, "{\"data\":{\"id\":\""+std::to_string(1000+calls.load())+"\"}}"};
    }
};
Account grant() {
    Account a; a.accessToken = "must-never-appear-in-snapshots"; a.allowBillableRequests = true;
    a.scopes = {"tweet.read", "tweet.write", "users.read"}; return a;
}
QVariantMap payload(const QString& key) { return {{"text", "A fixture post"}, {"idempotencyKey", key}}; }
void destinationSequences() {
    ReviewSource source; source.post.text=std::string(700,'a'); source.post.idempotencyKey="thread-intent";
    auto plan=planPublication(Platform::X,source);
    check(plan.posts.size()==3,"Long text fixture must produce three steps");
    auto fake=std::make_shared<Fake>(); SocialPublishingController backend(fake);
    auto account=grant(); account.allowSelfThreadReplies=true; backend.configureAccount("x",Platform::X,account);
    check(backend.accounts().size()==1 && !QJsonDocument::fromVariant(backend.accounts()).toJson().contains("must-never"),"Account snapshot leaked credentials");
    const auto job=backend.submitPlan("x",plan);
    check(!job.isEmpty() && backend.submitPlan("x",plan)==job,"Sequence intent duplicated");
    check(wait([&] { return backend.pendingCount()==0; }),"Sequence did not finish");
    auto result=backend.jobs().last().toMap();
    check(result.value("state")=="published" && result.value("result").toMap().value("completed").toList().size()==3 && fake->calls==3,"Sequence outcomes were not delivered");
    fake=std::make_shared<Fake>(); fake->failAt=2; SocialPublishingController partial(fake);
    partial.configureAccount("x",Platform::X,account);
    const auto partialJob=partial.submitPlan("x",plan);
    check(wait([&] { return partial.pendingCount()==0; }),"Partial sequence did not stop");
    result=partial.jobs().last().toMap();
    check(result.value("state")=="needs-reconciliation" && result.value("result").toMap().value("next").toInt()==1 && fake->calls==2,"Partial sequence lost reconciliation state");
    partial.clearFinished();
    check(partial.submitPlan("x",plan)==partialJob && fake->calls==2,"An unknown sequence was resubmitted");
}
void run() {
    auto fake = std::make_shared<Fake>(); SocialPublishingController backend(fake);
    check(backend.providers().size() == 11 && fake->calls == 0, "Startup should expose providers without API traffic");
    check(backend.submit("missing", payload("missing")).isEmpty(), "Missing account accepted");
    backend.configureAccount("x-account", Platform::X, grant());
    std::atomic<unsigned> finished{0}; bool signalsOnGui = true;
    QObject::connect(&backend, &SocialPublishingController::jobFinished, &backend, [&](auto, auto) {
        signalsOnGui &= QThread::currentThread() == QCoreApplication::instance()->thread(); ++finished;
    });
    auto first = backend.submit("x-account", payload("stable"));
    check(!first.isEmpty(), "Valid job was not queued");
    check(backend.submit("x-account", payload("stable")) == first, "Duplicate intent queued twice");
    auto conflicting = payload("stable"); conflicting["text"] = "Different content";
    check(backend.submit("x-account", conflicting).isEmpty(), "Idempotency key reused for a different post");
    check(wait([&] { return backend.pendingCount() == 0; }), "Publication did not complete");
    check(fake->calls == 1 && fake->onWorker && finished == 1 && signalsOnGui, "Worker isolation or completion delivery failed");
    check(backend.jobs().first().toMap().value("state") == "published", "Published state missing");
    const auto snapshots = QJsonDocument::fromVariant(backend.jobs()).toJson();
    check(!snapshots.contains("must-never-appear") && !snapshots.contains("accessToken"), "Credentials leaked to snapshots");
    check(backend.submit("x-account", {{"text", "x"}, {"accidentalOption", true}}).isEmpty(), "Unknown post parameter silently ignored");
    fake->hold = true;
    auto active = backend.submit("x-account", payload("cancel-active"));
    check(wait([&] { return fake->calls == 2; }), "Worker did not start blocked request");
    auto queued = backend.submit("x-account", payload("cancel-queued"));
    check(backend.cancel(queued), "Queued cancellation rejected");
    check(backend.pendingCount() == 1, "Queued cancellation is waiting for an unrelated active request");
    check(backend.jobs().last().toMap().value("state") == "cancelled", "Queued cancellation has no immediate terminal outcome");
    check(!backend.cancel(queued), "Cancelled queue job accepted a second cancellation");
    check(backend.cancel(active), "Active cancellation rejected");
    check(wait([&] { return backend.pendingCount() == 0; }), "Cancellation did not drain queue");
    check(fake->calls == 2, "Cancelled queued job still reached the provider");
    fake->hold = false; fake->ambiguous = true;
    auto uncertain = backend.submit("x-account", payload("uncertain"));
    check(wait([&] { return backend.pendingCount() == 0; }), "Uncertain job did not finish");
    check(backend.jobs().last().toMap().value("state") == "needs-reconciliation", "Lost publication response incorrectly marked retryable");
    check(backend.submit("x-account", payload("uncertain")) == uncertain && fake->calls == 3, "Uncertain publication automatically repeated");
    fake->ambiguous = false; fake->hold = true;
    backend.submit("x-account", payload("disconnect"));
    check(wait([&] { return fake->calls == 4; }), "Disconnect fixture did not start");
    backend.disconnectAccount("x-account");
    check(wait([&] { return backend.pendingCount() == 0; }), "Disconnect did not cancel in-flight publishing");
    check(backend.submit("x-account", payload("after-disconnect")).isEmpty(), "Disconnected account still publishes");
    backend.clearFinished();
    check(backend.jobs().size() == 1 && backend.jobs().first().toMap().value("state") == "needs-reconciliation", "Clearing history must retain uncertain publication deduplication");
    backend.configureAccount("pixiv", Platform::Pixiv, {});
    backend.submit("pixiv", payload("unsupported"));
    check(wait([&] { return backend.pendingCount() == 0; }), "Unsupported job did not finish");
    check(backend.jobs().last().toMap().value("result").toMap().value("error") == "unsupported" && fake->calls == 4, "Unsupported platform made a request");
    // Destroying the backend requests cancellation of a running request.
    auto closingFake = std::make_shared<Fake>(); closingFake->hold = true;
    {
        SocialPublishingController closing(closingFake); closing.configureAccount("x", Platform::X, grant());
        closing.submit("x", payload("shutdown"));
        check(wait([&] { return closingFake->calls == 1; }), "Shutdown fixture did not start");
    }
    auto isolatedFake = std::make_shared<Fake>(); isolatedFake->hold = true;
    {
        SocialPublishingController isolated(isolatedFake);
        isolated.configureAccount("first", Platform::X, grant());
        isolated.configureAccount("second", Platform::X, grant());
        auto running = isolated.submit("first", payload("blocking-first"));
        check(wait([&] { return isolatedFake->calls == 1; }), "Unrelated active fixture did not start");
        isolated.submit("second", payload("disconnect-queued"));
        bool reentrantOutcome = false;
        QObject::connect(&isolated, &SocialPublishingController::jobFinished, &isolated, [&](auto, auto) {
            // Completion observers may call back into the queue; never emit under its lock.
            reentrantOutcome = !isolated.cancel("already-absent");
        });
        isolated.disconnectAccount("second");
        check(isolated.pendingCount() == 1, "Account disconnect left its queued work pending");
        check(reentrantOutcome && isolated.jobs().last().toMap().value("state") == "cancelled", "Disconnected queue completion was not delivered");
        check(isolatedFake->calls == 1, "Disconnect executed a cancelled request");
        check(isolated.cancel(running), "Unrelated running job was lost");
        check(wait([&] { return isolated.pendingCount() == 0; }), "Unrelated job did not finish cancellation");
    }
}

struct StalledTransfer {
    std::counting_semaphore<4> release{0}, viewClosed{0};
    std::atomic<unsigned> calls{0};
    std::atomic<bool> stopped{false}, destroyed{false};
};
struct StalledTransport final : Transport {
    explicit StalledTransport(std::shared_ptr<StalledTransfer> state) : state(std::move(state)) {}
    ~StalledTransport() override { state->destroyed = true; }
    Response send(const Request&, std::stop_token stop) override {
        ++state->calls;
        // Model a provider phase that does not observe cancellation until it returns.
        state->release.try_acquire_for(std::chrono::seconds(3));
        state->stopped = stop.stop_requested();
        return {0, {}, {}, true, stop.stop_requested(), false};
    }
    std::shared_ptr<StalledTransfer> state;
};
void stalledTransferDoesNotHoldTheViewOpen() {
    const auto state = std::make_shared<StalledTransfer>();
    const auto release = qScopeGuard([state] { state->release.release(); state->viewClosed.release(); });
    auto backend = std::make_unique<SocialPublishingController>(std::make_shared<StalledTransport>(state));
    backend->configureAccount("x", Platform::X, grant());
    check(!backend->submit("x", payload("stalled-active")).isEmpty(), "Stalled intent was rejected");
    check(wait([&] { return state->calls == 1; }), "Stalled transfer never entered the provider");
    check(!backend->submit("x", payload("stalled-queued")).isEmpty(), "Queued intent was rejected");
    std::atomic<bool> forcedRelease{false};
    std::jthread escape([state, &forcedRelease] {
        if (!state->viewClosed.try_acquire_for(std::chrono::milliseconds(750))) forcedRelease = true;
        state->release.release();
    });
    QElapsedTimer timer; timer.start(); backend.reset(); const auto elapsed = timer.nsecsElapsed();
    const bool retainedUntilTransferReturned = !state->destroyed;
    state->viewClosed.release(); escape.join();
    check(wait([&] { return state->destroyed.load(); }), "The completed worker retained its transport");
    std::cout << "OBSERVATION close-ns=" << elapsed << " forced-release=" << forcedRelease
              << " retained=" << retainedUntilTransferReturned << " calls=" << state->calls << '\n';
    check(!forcedRelease, "View destruction waited for the stalled provider to return");
    check(retainedUntilTransferReturned, "Closing the view invalidated the active transport");
    check(state->stopped && state->calls == 1, "Shutdown executed a queued job or lost active cancellation");
}
void completionPrecedesItsPresentationNotification() {
    auto fake = std::make_shared<Fake>(); SocialPublishingController backend(fake);
    backend.configureAccount("x", Platform::X, grant()); QStringList order;
    QObject::connect(&backend, &SocialPublishingController::jobFinished, &backend, [&] { order.append("finished"); });
    QObject::connect(&backend, &SocialPublishingController::changed, &backend, [&] {
        if (!backend.jobs().isEmpty() && backend.pendingCount() == 0) order.append("changed");
    });
    check(!backend.submit("x", payload("completion-order")).isEmpty(), "Completion intent was rejected");
    check(wait([&] { return order.size() == 2; }), "Completion notifications were not delivered");
    check(order == QStringList({"finished", "changed"}), "Presentation notification preceded the accepted outcome");
}
void duplicateReturnsItsAcceptedIdEvenWhenHistoryChanges() {
    auto fake = std::make_shared<Fake>(); SocialPublishingController backend(fake);
    backend.configureAccount("x", Platform::X, grant());
    const auto first = backend.submit("x", payload("dedup-history"));
    check(wait([&] { return backend.pendingCount() == 0; }), "First dedup fixture did not finish");
    bool replace = true; QString replacement;
    QObject::connect(&backend, &SocialPublishingController::changed, &backend, [&] {
        if (!replace) return; replace = false; backend.clearFinished();
        replacement = backend.submit("x", payload("replacement"));
    });
    const auto repeated = backend.submit("x", payload("dedup-history"));
    check(wait([&] { return backend.pendingCount() == 0; }), "Replacement did not finish");
    check(!replacement.isEmpty() && replacement != first, "History fixture did not replace the completed row");
    check(repeated == first, "A duplicate returned an ID from a reentrant history replacement");
    check(fake->calls == 2, "Metadata observation repeated a publication");
}
void completionObserverMayCloseTheController() {
    auto fake = std::make_shared<Fake>(); QPointer<SocialPublishingController> observed;
    auto backend = std::make_unique<SocialPublishingController>(fake); observed = backend.get();
    backend->configureAccount("x", Platform::X, grant()); unsigned outcomes = 0;
    QObject::connect(backend.get(), &SocialPublishingController::jobFinished, qApp, [&] { ++outcomes; });
    QObject::connect(backend.get(), &SocialPublishingController::changed, qApp, [&] {
        if (backend && !backend->jobs().isEmpty() && backend->pendingCount() == 0) backend.reset();
    });
    check(!backend->submit("x", payload("terminal-close")).isEmpty(), "Terminal close fixture was rejected");
    check(wait([&] { return observed.isNull(); }), "Terminal observer did not close its controller");
    check(outcomes == 1 && fake->calls == 1, "Closing presentation lost or duplicated the accepted outcome");
}
void accountRetirementMayCloseTheController(bool replacing) {
    auto fake = std::make_shared<Fake>(); fake->hold = true;
    auto backend = std::make_unique<SocialPublishingController>(fake); QPointer<SocialPublishingController> observed(backend.get());
    backend->configureAccount("active", Platform::X, grant()); backend->configureAccount("retired", Platform::X, grant());
    backend->submit("active", payload("held-account")); check(wait([&] { return fake->calls == 1; }), "Held account did not start");
    backend->submit("retired", payload("retire-one")); backend->submit("retired", payload("retire-two")); unsigned outcomes = 0;
    QObject::connect(backend.get(), &SocialPublishingController::jobFinished, qApp, [&] { ++outcomes; backend.reset(); });
    if (replacing) backend->configureAccount("retired", Platform::X, grant());
    else backend->disconnectAccount("retired");
    check(observed.isNull() && outcomes == 1 && fake->calls == 1, "Account retirement used a closed controller or executed a retired job");
    QCoreApplication::processEvents();
}
void accountReplacementRetainsTheLatestIntent(bool disconnect, bool independent) {
    auto fake = std::make_shared<Fake>(); fake->hold = true; SocialPublishingController backend(fake);
    backend.configureAccount("active", Platform::X, grant()); backend.configureAccount("retired", Platform::X, grant());
    const auto active = backend.submit("active", payload("held-account"));
    check(wait([&] { return fake->calls == 1; }), "Held account did not start");
    backend.submit("retired", payload("retire-one")); bool replaced = false;
    QObject::connect(&backend, &SocialPublishingController::jobFinished, &backend, [&](const QString&, const QVariantMap&) {
        if (replaced) return; replaced = true;
        if (disconnect) backend.disconnectAccount("retired");
        else backend.configureAccount(independent ? "independent" : "retired", Platform::Pixiv, {});
    });
    backend.configureAccount("retired", Platform::X, grant());
    const auto next = backend.submit("retired", payload("latest-account"));
    if (disconnect) check(next.isEmpty(), "An older configuration restored a disconnected account");
    else check(!next.isEmpty(), "The latest configured account was lost");
    check(backend.cancel(active), "The unrelated active job was lost"); fake->hold = false;
    check(wait([&] { return backend.pendingCount() == 0; }), "Account replacement queue did not drain");
    check(fake->calls == (independent ? 2U : 1U), "An older or unrelated account configuration replaced the current intent");
    if (!disconnect && !independent)
        check(backend.jobs().last().toMap().value("result").toMap().value("error") == "unsupported", "Latest platform configuration was overwritten");
}
struct StopCallbackTransport final : Transport {
    std::atomic<bool> registered{false}, finished{false};
    std::function<void()> callback;
    Response send(const Request&, std::stop_token stop) override {
        std::stop_callback onStop(stop, [this] { callback(); }); registered = true;
        while (!stop.stop_requested()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        finished = true; return {0, {}, {}, true, true, false};
    }
};
void cancellationCallbacksCanObserveTheQueue(const QString& action) {
    auto transport = std::make_shared<StopCallbackTransport>();
    auto backend = std::make_unique<SocialPublishingController>(transport);
    backend->configureAccount("x", Platform::X, grant()); unsigned callbacks = 0; bool guiThread = false;
    auto* source = backend.get();
    transport->callback = [&, source] {
        ++callbacks; guiThread = QThread::currentThread() == qApp->thread();
        check(!source->cancel("absent"), "Stop callback found an absent job");
    };
    const auto active = backend->submit("x", payload("stop-callback"));
    check(wait([&] { return transport->registered.load(); }), "Stop callback was not registered");
    if (action == "cancel") check(backend->cancel(active), "Active cancellation was rejected");
    else if (action == "disconnect") backend->disconnectAccount("x");
    else backend.reset();
    check(wait([&] { return transport->finished.load(); }), "Stop callback blocked queue cancellation");
    check(callbacks == 1 && guiThread, "Cancellation callback did not stay with its GUI caller");
    if (backend) check(wait([&] { return backend->pendingCount() == 0; }), "Stop callback left an active job");
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const std::vector<std::pair<const char*, std::function<void()>>> cases{
        {"destination-sequences", destinationSequences},
        {"existing-contracts", run}, {"stalled-close", stalledTransferDoesNotHoldTheViewOpen},
        {"completion-order", completionPrecedesItsPresentationNotification},
        {"dedup-history", duplicateReturnsItsAcceptedIdEvenWhenHistoryChanges},
        {"terminal-close", completionObserverMayCloseTheController},
        {"disconnect-close", [] { accountRetirementMayCloseTheController(false); }},
        {"configure-close", [] { accountRetirementMayCloseTheController(true); }},
        {"configure-latest", [] { accountReplacementRetainsTheLatestIntent(false, false); }},
        {"configure-disconnected", [] { accountReplacementRetainsTheLatestIntent(true, false); }},
        {"configure-independent", [] { accountReplacementRetainsTheLatestIntent(false, true); }},
        {"cancel-stop-callback", [] { cancellationCallbacksCanObserveTheQueue("cancel"); }},
        {"disconnect-stop-callback", [] { cancellationCallbacksCanObserveTheQueue("disconnect"); }},
        {"close-stop-callback", [] { cancellationCallbacksCanObserveTheQueue("close"); }}
    };
    const QString selected = argc == 3 && QString::fromLocal8Bit(argv[1]) == "--case" ? QString::fromLocal8Bit(argv[2]) : QString{};
    bool found = selected.isEmpty(); unsigned failed = 0;
    for (const auto& [name, test] : cases) {
        if (!selected.isEmpty() && selected != name) continue; found = true;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    if (!found || failed) return 1;
    std::cout << "Social publishing backend: passed\n";
}
