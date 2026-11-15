#include "App/Native/NativePanelRegion.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void settle() { for (int i = 0; i != 5; ++i) QCoreApplication::processEvents(); }
bool near(qreal a, qreal b) { return std::abs(a - b) < 0.01; }
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        QQuickWindow window;
        window.resize(800, 600);
        QQuickItem container(window.contentItem());
        container.setPosition({10, 20}); container.setSize({600, 400}); container.setClip(true);
        QQuickItem queue(&container);
        queue.setPosition({15, 25}); queue.setSize({260, 300});
        QRectF frame;
        int visible = 0, hidden = 0;
        bool accept = true;
        dreamscapes::native::NativePanelRegion region(&window, &queue, {
            .show = [&](QRectF next) { frame = next; ++visible; return accept; },
            .hide = [&] { ++hidden; }
        });
        region.setReady(true); settle();
        check(visible == 0 && queue.opacity() == 1 && queue.isEnabled(), "hidden window activated native region");
        window.setVisible(true); settle();
        check(visible > 0 && near(frame.x(), 25) && near(frame.y(), 45) &&
              near(frame.width(), 260) && near(frame.height(), 300), "native region frame differs from queue");
        check(queue.opacity() == 0 && !queue.isEnabled(), "legacy queue was not suppressed while native is visible");
        container.setX(30); settle();
        check(near(frame.x(), 45), "ancestor movement did not update the frame");
        region.setBlocked(true); settle();
        check(hidden > 0 && queue.opacity() == 1 && queue.isEnabled(), "overlay did not restore the legacy queue");
        region.setBlocked(false); settle();
        check(queue.opacity() == 0 && !queue.isEnabled(), "native queue did not return after overlay");
        container.setWidth(200); settle();
        check(queue.opacity() == 1 && queue.isEnabled(), "clipped queue incorrectly kept a native overlay");
        container.setWidth(600); settle();
        check(queue.opacity() == 0, "native queue did not return after clipping ended");
        container.setRotation(10); settle();
        check(queue.opacity() == 1 && queue.isEnabled(), "rotated queue incorrectly kept a native overlay");
        container.setRotation(0); settle();
        check(queue.opacity() == 0, "native queue did not return after rotation ended");
        accept = false;
        queue.setX(18); settle();
        check(queue.opacity() == 1 && queue.isEnabled(), "surface failure did not restore the legacy queue");
        accept = true;
        region.setReady(false); settle();
        region.setReady(true); settle();
        check(queue.opacity() == 0, "recovered surface did not reactivate");
        window.setVisible(false); settle();
        check(queue.opacity() == 1 && queue.isEnabled(), "hidden window did not restore the legacy queue");
        region.close(); settle();
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
