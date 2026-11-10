#pragma once
#include <QCoreApplication>
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

// Normal Quit is held while registered owners retire their work. Every task
// starts without waiting and invokes its completion on this object's thread.
class ApplicationShutdownCoordinator final : public QObject {
public:
    using Task = std::function<void(std::function<void()>)>;
    explicit ApplicationShutdownCoordinator(QObject *parent = nullptr) : QObject(parent) {
        if (qApp) qApp->installEventFilter(this);
    }
    void add(Task task) { if (!m_closing && task) m_tasks.push_back(std::move(task)); }
    [[nodiscard]] bool isClosing() const noexcept { return m_closing; }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != qApp || event->type() != QEvent::Quit || m_ready) return false;
        if (m_closing) return true;
        m_closing = true;
        m_pending = m_tasks.size();
        if (!m_pending) { m_ready = true; return false; }
        const QPointer<ApplicationShutdownCoordinator> owner(this);
        for (const auto &task : m_tasks) {
            auto called = std::make_shared<std::atomic_bool>(false);
            task([owner, called] {
                if (!owner || called->exchange(true)) return;
                if (--owner->m_pending != 0) return;
                owner->m_ready = true;
                QTimer::singleShot(0, qApp, &QCoreApplication::quit);
            });
            if (!owner) return true;
        }
        return true;
    }
private:
    std::vector<Task> m_tasks;
    std::size_t m_pending = 0;
    bool m_closing = false, m_ready = false;
};
