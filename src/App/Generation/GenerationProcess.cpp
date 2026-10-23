#include "GenerationProcess.h"
#include <iiSDKCore/Runtime/EventLoop.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include <QCoreApplication>
#include <QPointer>
#include <QScopeGuard>
#include <QTimer>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <map>
#include <vector>
#include <functional>
#include <variant>

namespace {
// Process loops use the existing native Runtime. The application owns their
// final drain, while removing an individual GUI owner remains non-joining.
class ProcessRuntime final {
public:
    ~ProcessRuntime() { shutdown(); }
    bool post(std::function<void()> run, std::function<void()> stop) {
        const std::lock_guard lock(m_mutex);
        if (m_closing) return false;
        const auto id = ++m_sequence;
        m_stops.emplace(id, std::move(stop));
        const auto submitted = m_workers.post([this, id, run = std::move(run)] {
            struct Retire { ProcessRuntime *owner; std::uint64_t id; ~Retire() { owner->complete(id); } } retire{this, id};
            run();
        });
        if (!submitted) m_stops.erase(id);
        return bool(submitted);
    }
    void shutdown() {
        const std::lock_guard shutdownLock(m_shutdownMutex);
        std::vector<std::function<void()>> stops;
        {
            const std::lock_guard lock(m_mutex);
            if (m_closing) return;
            m_closing = true;
            for (const auto &[id, stop] : m_stops) stops.push_back(stop);
        }
        for (const auto &stop : stops) stop();
        // Queued sessions also run their cancelled cleanup; no task is abandoned.
        m_workers.request_stop(ii::runtime::ShutdownMode::drain);
        (void)m_workers.join();
    }
private:
    void complete(std::uint64_t id) { const std::lock_guard lock(m_mutex); m_stops.erase(id); }
    ii::runtime::Runtime m_workers;
    std::mutex m_mutex, m_shutdownMutex;
    std::map<std::uint64_t, std::function<void()>> m_stops;
    std::uint64_t m_sequence = 0;
    bool m_closing = false;
};
ProcessRuntime &processRuntime() {
    static ProcessRuntime runtime;
    if (auto *application = QCoreApplication::instance()) {
        static std::once_flag hooked;
        std::call_once(hooked, [application] {
            const auto close = [target = &runtime] { target->shutdown(); };
            QObject::connect(application, &QCoreApplication::aboutToQuit, application, close, Qt::DirectConnection);
            QObject::connect(application, &QObject::destroyed, close);
        });
    }
    return runtime;
}
}

struct GenerationProcess::State : std::enable_shared_from_this<State> {
    using Loop = ii::runtime::EventLoop;
    struct Began { std::uint64_t pid; };
    struct Failed { ProcessError kind; std::string message; };
    struct Ended { ii::process::Exit exit; };
    struct Probe { std::uint64_t token; };
    using Event = std::variant<ii::core::Bytes, Began, Failed, Ended, Probe>;
    struct Frame { ii::core::Bytes bytes; std::size_t offset = 0; std::shared_ptr<void> lifetime; };
    static constexpr std::size_t capacity = 1024 * 1024, quantum = 64 * 1024;
    std::mutex mutex;
    std::deque<Event> events;
    std::size_t buffered = 0;
    bool notice = false, senderReady = false;
    std::atomic_bool stop{false}, closed{false}, done{false};
    std::atomic_uint64_t pid{0};
    Loop::Sender sender;
    std::deque<ii::runtime::Task> admission;
    std::function<void()> notify;
    std::shared_ptr<void> runtimeLifetime, inputLifetime, retirementLifetime;
    // Below this point all access belongs to the native dispatch worker.
    std::unique_ptr<Loop> loop;
    std::optional<ii::process::Process> process;
    ii::io::Channel input, output;
    ii::runtime::TaskToken readWatch, writeWatch;
    std::deque<Frame> writes;
    std::optional<ii::process::Exit> exit;
    bool eof = false, failed = false;
    std::optional<ii::runtime::Clock::time_point> exitedAt;

    bool post(ii::runtime::Task task) {
        Loop::Sender endpoint;
        {
            const std::lock_guard lock(mutex);
            if (done || closed) return false;
            if (!senderReady) { if (admission.size() >= 64) return false; admission.push_back(std::move(task)); return true; }
            endpoint = sender;
        }
        return bool(endpoint.post(std::move(task), ii::runtime::Priority::high));
    }
    void push(Event event) {
        bool wake = false;
        {
            const std::lock_guard lock(mutex);
            if (closed) return;
            if (const auto *bytes = std::get_if<ii::core::Bytes>(&event)) buffered += bytes->size();
            events.push_back(std::move(event));
            if (!notice) notice = wake = true;
        }
        if (wake) notify();
    }
    std::optional<Event> take() {
        const std::lock_guard lock(mutex);
        if (events.empty()) return {};
        auto event = std::move(events.front()); events.pop_front();
        if (const auto *bytes = std::get_if<ii::core::Bytes>(&event)) buffered -= bytes->size();
        return event;
    }
    void nextNotice() {
        bool wake = false;
        { const std::lock_guard lock(mutex); notice = false; if (!closed && !events.empty()) notice = wake = true; }
        if (wake) notify();
    }
    bool full() { const std::lock_guard lock(mutex); return buffered >= capacity; }
    void fail(ProcessError kind, std::string message) {
        if (failed) return;
        failed = true; push(Failed{kind, std::move(message)}); stop = true;
    }
    void armRead() {
        if (eof || closed || readWatch.pending() || full()) return;
        const auto weak = weak_from_this();
        auto submission = loop->watch(output, ii::io::Interest::readable, [weak](auto ready) {
            if (const auto self = weak.lock()) {
                self->readWatch = {};
                if (!ready) { if (!self->closed) self->fail(ReadError, "Cannot read the inference process output."); }
                else self->drain();
                self->control();
            }
        });
        if (!submission) fail(ReadError, "Cannot watch the inference process output.");
        else readWatch = submission.token;
    }
    void drain() {
        if (closed || eof) return;
        std::array<std::byte, 16384> buffer{};
        std::size_t transferred = 0;
        while (transferred < quantum && !full()) {
            const auto read = output.read(buffer);
            if (!read) {
                if (read.error().code != ii::io::ErrorCode::would_block)
                    fail(ReadError, "Inference output read failed: " + read.error().native.message());
                break;
            }
            if (read->bytes) {
                transferred += read->bytes;
                push(ii::core::Bytes(buffer.begin(), buffer.begin() + read->bytes));
            }
            if (read->eof) { eof = true; break; }
            if (!read->bytes) break;
        }
        if (!eof) armRead();
    }
    void pumpInput() {
        if (closed || stop || exit || writes.empty()) return;
        std::size_t transferred = 0;
        while (!writes.empty() && transferred < quantum) {
            auto &frame = writes.front();
            const auto bytes = std::span<const std::byte>(frame.bytes).subspan(frame.offset);
            const auto written = input.write(bytes);
            if (!written) {
                if (written.error().code == ii::io::ErrorCode::would_block) break;
                fail(WriteError, "Cannot send the inference request: " + written.error().native.message()); return;
            }
            frame.offset += *written; transferred += *written;
            if (frame.offset == frame.bytes.size()) writes.pop_front();
            if (!*written) break;
        }
        if (!writes.empty() && !writeWatch.pending()) {
            const auto weak = weak_from_this();
            const auto submission = loop->watch(input, ii::io::Interest::writable, [weak](auto ready) {
                if (const auto self = weak.lock()) {
                    self->writeWatch = {};
                    if (ready) self->pumpInput();
                    else self->fail(WriteError, "The inference process stopped accepting input.");
                    self->control();
                }
            });
            if (!submission) fail(WriteError, "Cannot watch the inference request pipe.");
            else writeWatch = submission.token;
        }
    }
    void control() {
        if (!process) { loop->request_stop(); return; }
        if (stop && !exit) {
            const auto cancelled = process->cancel();
            if (!cancelled) { fail(UnknownError, cancelled.error().message); loop->request_stop(); return; }
            else exit = *cancelled;
        } else if (!exit) {
            const auto status = process->poll();
            if (!status) { fail(UnknownError, status.error().message); loop->request_stop(); return; }
            else exit = *status;
        }
        if (closed) { loop->request_stop(); return; }
        if (exit) {
            if (!exitedAt) exitedAt = ii::runtime::Clock::now();
            if (eof) { loop->request_stop(); return; }
            if (ii::runtime::Clock::now() - *exitedAt > std::chrono::seconds(2) && !full()) {
                fail(ReadError, "The inference output pipe remained open after process exit.");
                loop->request_stop(); return;
            }
        }
        armRead();
    }
    void run(ii::process::Launch launch) {
        try {
            loop = std::make_unique<Loop>(128);
            {
                std::deque<ii::runtime::Task> pending;
                { const std::lock_guard lock(mutex); sender = loop->sender(); senderReady = true; pending.swap(admission); }
                for (auto &task : pending) if (!loop->post(std::move(task), ii::runtime::Priority::high)) stop = true;
            }
            if (stop) exit = ii::process::Exit{ii::process::ExitReason::cancelled};
            else {
                auto child = ii::process::Process::start(launch);
                if (!child) push(Failed{FailedToStart, child.error().message});
                else {
                    process = std::move(*child); pid = process->id(); input = process->input(); output = process->output();
                    push(Began{pid.load()});
                    const auto weak = weak_from_this();
                    const auto poll = loop->every(std::chrono::milliseconds(50), [weak] { if (const auto self = weak.lock()) self->control(); });
                    if (!poll) fail(UnknownError, "Cannot observe the inference process lifetime.");
                    armRead(); control(); loop->run();
                }
            }
        } catch (const std::exception &error) { push(Failed{process ? UnknownError : FailedToStart, error.what()}); }
        catch (...) { push(Failed{process ? UnknownError : FailedToStart, "The native process worker stopped unexpectedly."}); }
        if (loop) loop->request_stop();
        if (process && !exit) { const auto result = process->cancel(); if (result) exit = *result; else push(Failed{UnknownError, result.error().message}); }
        input = {}; output = {}; process.reset(); loop.reset(); writes.clear();
        std::shared_ptr<void> runtime, active, retired;
        {
            const std::lock_guard lock(mutex);
            done = true; senderReady = false; admission.clear();
            runtime = std::move(runtimeLifetime); active = std::move(inputLifetime); retired = std::move(retirementLifetime);
        }
        // Release file captures after the direct child and its owned tree have
        // been retired, before posting the terminal UI outcome.
        active.reset(); retired.reset(); runtime.reset();
        if (exit) push(Ended{*exit});
    }
};

GenerationProcess::GenerationProcess(QObject *parent) : QObject(parent) {}
GenerationProcess::~GenerationProcess() { retire(); }
quint64 GenerationProcess::processId() const { return d ? d->pid.load() : 0; }
void GenerationProcess::start(const QString &program, const QStringList &arguments, std::shared_ptr<void> lifetime) {
    if (m_status != NotRunning) return;
    ii::process::Launch launch; launch.arguments.push_back(program.toStdString());
    for (const auto &argument : arguments) launch.arguments.push_back(argument.toStdString());
    launch.working_directory = std::filesystem::path(m_directory.toStdU16String());
    launch.environment = m_environment; launch.error = ii::process::ErrorOutput::merge_output;
    auto state = std::make_shared<State>(); state->runtimeLifetime = std::move(lifetime);
    const QPointer<GenerationProcess> owner(this);
    auto dispatch = std::shared_ptr<QObject>(new QObject, [](QObject *object) { object->deleteLater(); });
    const std::weak_ptr<QObject> target = dispatch; const std::weak_ptr<State> weak = state;
    state->notify = [owner, target, weak] {
        if (const auto node = target.lock()) QMetaObject::invokeMethod(node.get(), [owner, weak] {
            if (const auto state = weak.lock()) { if (owner) owner->synchronize(state); }
        }, Qt::QueuedConnection);
    };
    d = state; m_dispatch = dispatch; m_error.clear(); m_status = Starting;
    try {
        const auto accepted = processRuntime().post(
            [state, dispatch, launch = std::move(launch)]() mutable { state->run(std::move(launch)); },
            [weak] { if (const auto state = weak.lock()) { state->stop = true; state->closed = true; } });
        if (!accepted) { state->done = true; state->push(State::Failed{FailedToStart, "The application process runtime is shutting down or full."}); }
    }
    catch (const std::exception &error) { state->done = true; state->push(State::Failed{FailedToStart, error.what()}); }
}
qint64 GenerationProcess::write(const QByteArray &data, std::shared_ptr<void> lifetime) {
    const auto state = d;
    if (!state || state->done || state->stop || state->closed || data.size() > 16 * 1024 * 1024) return -1;
    State::Frame frame; frame.bytes.resize(std::size_t(data.size()));
    std::copy_n(reinterpret_cast<const std::byte *>(data.constData()), frame.bytes.size(), frame.bytes.begin()); frame.lifetime = lifetime;
    const std::weak_ptr<State> weak = state;
    if (!state->post([weak, frame = std::move(frame)]() mutable {
        if (const auto self = weak.lock()) {
            { const std::lock_guard lock(self->mutex); self->inputLifetime = frame.lifetime; }
            self->writes.push_back(std::move(frame)); self->pumpInput();
        }
    })) return -1;
    return data.size();
}
void GenerationProcess::releaseInput() {
    if (const auto state = d) { std::shared_ptr<void> retired; { const std::lock_guard lock(state->mutex); retired = std::move(state->inputLifetime); } }
}
void GenerationProcess::probeOutput(quint64 token) {
    const auto state = d; if (!state) { emit outputProbed(token); return; }
    const std::weak_ptr<State> weak = state;
    if (!state->post([weak, token] { if (const auto self = weak.lock()) { self->drain(); self->push(State::Probe{token}); } })) {
        // A separate GUI callback could overtake the next bounded output turn.
        // Retain the barrier in the same FIFO even after the worker has ended.
        if (state->done) state->push(State::Probe{token});
        else if (!state->closed) QTimer::singleShot(1, this, [this, token] { probeOutput(token); });
    }
}
void GenerationProcess::cancel() {
    if (const auto state = d) {
        state->stop = true; const std::weak_ptr<State> weak = state;
        (void)state->post([weak] { if (const auto self = weak.lock()) self->control(); });
    }
}
void GenerationProcess::retire(std::shared_ptr<void> files) {
    const auto state = d; if (!state) return;
    { const std::lock_guard lock(state->mutex); if (!state->done && files) state->retirementLifetime = std::move(files); }
    cancel(); state->closed = true;
    // Stop posts remain accepted before the delivery gate closes. The periodic
    // native lifetime probe also observes stop if admission was already closed.
}
void GenerationProcess::synchronize(const std::shared_ptr<State> &state) {
    if (d != state || m_delivering || state->closed) return;
    const QPointer<GenerationProcess> owner(this); m_delivering = true;
    const auto finish = qScopeGuard([owner, state] { if (owner) owner->m_delivering = false; state->nextNotice(); });
    std::size_t bytes = 0;
    while (bytes < State::quantum) {
        auto event = state->take(); if (!event) break;
        if (const auto *data = std::get_if<ii::core::Bytes>(&*event)) {
            bytes += data->size(); emit outputAvailable(QByteArray(reinterpret_cast<const char *>(data->data()), qsizetype(data->size())));
        } else if (std::holds_alternative<State::Began>(*event)) { m_status = Running; emit started(); }
        else if (const auto *failure = std::get_if<State::Failed>(&*event)) {
            m_error = QString::fromUtf8(failure->message); if (failure->kind == FailedToStart) m_status = NotRunning;
            emit errorOccurred(failure->kind);
        } else if (const auto *ended = std::get_if<State::Ended>(&*event)) {
            m_status = NotRunning; const bool crash = ended->exit.reason == ii::process::ExitReason::signaled;
            emit finished(crash ? ended->exit.signal : qint64(ended->exit.code), crash ? CrashExit : NormalExit);
        } else if (const auto *probe = std::get_if<State::Probe>(&*event)) emit outputProbed(probe->token);
        if (!owner || d != state || state->closed) return;
    }
    const std::weak_ptr<State> weak = state;
    (void)state->post([weak] { if (const auto self = weak.lock()) self->armRead(); });
}
