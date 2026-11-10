#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>

// Value-owned tickets cover work even if the UI controller is destroyed.
class GenerationBackgroundWork final {
    struct State {
        std::mutex mutex;
        std::condition_variable idle;
        std::size_t count = 0;
        bool closing = false;
    };
public:
    class Ticket final {
    public:
        explicit Ticket(std::shared_ptr<State> state) : m_state(std::move(state)) {}
        ~Ticket() { finish(); }
        void finish() {
            auto state = std::move(m_state);
            if (!state) return;
            std::lock_guard lock(state->mutex);
            if (--state->count == 0) state->idle.notify_all();
        }
    private:
        std::shared_ptr<State> m_state;
    };
    std::shared_ptr<Ticket> acquire() {
        std::lock_guard lock(m_state->mutex);
        if (m_state->closing) return {};
        auto ticket = std::make_shared<Ticket>(m_state);
        ++m_state->count;
        return ticket;
    }
    void close() { std::lock_guard lock(m_state->mutex); m_state->closing = true; }
    void wait() {
        std::unique_lock lock(m_state->mutex);
        m_state->idle.wait(lock, [&] { return m_state->count == 0; });
    }
private:
    std::shared_ptr<State> m_state = std::make_shared<State>();
};
