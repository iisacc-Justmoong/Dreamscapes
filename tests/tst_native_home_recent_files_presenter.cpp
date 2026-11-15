#include "App/Views/Home/NativeHomeRecentFilesPresenter.h"
#include "App/Views/Home/NativeHomeRecentFilesSession.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrl>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) {
    if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); }
}
void settle() { for (int i = 0; i != 6; ++i) QCoreApplication::processEvents(); }
class Surface final : public native::NativePresentationSurface {
  public:
    explicit Surface(Callbacks callbacks) : callbacks(std::move(callbacks)) {}
    bool start(QRectF rect) override { frame = rect; ++started; return true; }
    bool show(QRectF rect) override { frame = rect; ++shown; return true; }
    void hide() override { ++hidden; }
    void close() override { ++closing; }
    void restoreHostFocus() override { ++focused; }
    Callbacks callbacks; QRectF frame;
    int started = 0, shown = 0, hidden = 0, closing = 0, focused = 0;
};
void exercise() {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Window
        Window {
            width: 800; height: 600
            QtObject { objectName: "failureDetailsModal"; property bool open: false }
            Item {
                objectName: "desktopRecentFiles"; x: 20; y: 30; width: 700; height: 450
                property var files: [
                    {id: "a", name: "First.iisc", source: "https://example.invalid/a"},
                    {id: "b", name: "Second.mp4", source: "https://example.invalid/b"}
                ]
                property bool loading: false
                property string selectedKey: "b"
                property string typeFilter: "All types"
                property var opened: null
                property int browsed: 0
                signal fileRequested(var file)
                signal viewAllRequested()
                onFileRequested: function(file) { opened = file }
                onViewAllRequested: browsed++
            }
        }
    )QML", QUrl());
    auto root = std::unique_ptr<QObject>(component.create());
    if (!root) throw std::runtime_error(component.errorString().toStdString());
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    auto *panel = root->findChild<QQuickItem *>("desktopRecentFiles");
    check(window && panel, "QML fixture did not create recent files panel");
    std::shared_ptr<Surface> surface;
    std::shared_ptr<home::HomeRecentFilesModel> model;
    QPointer<home::NativeHomeRecentFilesSession> session;
    home::NativeHomeRecentFilesPresenter presenter(window, [&](auto source, auto owner, auto callbacks) {
        model = std::move(source); session = owner;
        surface = std::make_shared<Surface>(std::move(callbacks)); return surface;
    });
    check(presenter.valid(), "recent panel properties or signals not discovered");
    presenter.begin(); window->setVisible(true); settle();
    check(surface && session && model->rows.get().size() == 2 && model->selectedKey.get() == "b", "QML files or selection not projected");
    check(panel->opacity() == 1 && surface->started == 1, "native surface did not start safely");
    surface->callbacks.ready(); settle();
    check(panel->opacity() == 0 && !panel->isEnabled() && surface->frame.x() == 20, "native surface did not replace original panel");
    auto rows = model->rows.get();
    good(model->selectToken(rows[0].token)); settle();
    check(panel->property("selectedKey").toString() == "a", "native selection did not update the home view");
    good(model->openToken(rows[0].token)); settle();
    check(panel->property("opened").toMap().value("id").toString() == "a" && surface->focused == 1,
        "open did not deliver original QML file");
    good(model->filterAt(3)); settle();
    check(panel->property("typeFilter").toString() == "Video" && model->selectedKey.get() == "a",
        "filter did not reach home or preserve selection");
    model->browse(); settle(); check(panel->property("browsed").toInt() == 1, "view all did not reach home");
    panel->setProperty("typeFilter", "All types"); panel->setProperty("selectedKey", "b"); settle();
    check(model->selectedKey.get() == "b" && model->rows.get().size() == 2, "home changes were not reflected");
    auto *modal = root->findChild<QObject *>("failureDetailsModal");
    check(modal, "fixture modal not found");
    modal->setProperty("open", true); settle();
    check(panel->opacity() == 1 && panel->isEnabled() && surface->hidden, "modal did not uncover original panel");
    modal->setProperty("open", false); settle();
    check(panel->opacity() == 0 && !panel->isEnabled(), "native panel did not return after modal");
    auto previous = surface;
    QEvent languageChange(QEvent::LanguageChange); QCoreApplication::sendEvent(qApp, &languageChange); settle();
    check(previous->closing == 1 && panel->opacity() == 1, "language change did not retire panel");
    previous->callbacks.closed(); settle();
    check(surface != previous && model->selectedKey.get() == "b", "panel recreation lost selection");
    surface->callbacks.ready(); settle();
    bool finished = false; presenter.shutdown([&] { finished = true; });
    check(!finished && surface->closing == 1 && panel->opacity() == 1, "shutdown did not wait for surface");
    auto retiring = surface; retiring->callbacks.closed(); settle();
    check(finished && !session, "shutdown retained recent files session");
    std::shared_ptr<Surface> failedSurface;
    std::shared_ptr<home::HomeRecentFilesModel> failedModel;
    home::NativeHomeRecentFilesPresenter fallback(window, [&](auto source, auto, auto callbacks) {
        failedModel = std::move(source); failedSurface = std::make_shared<Surface>(std::move(callbacks)); return failedSurface;
    });
    fallback.begin(); settle();
    check(bool(failedSurface), "fallback fixture did not start");
    failedSurface->callbacks.ready(); settle();
    failedSurface->callbacks.failed(); settle();
    check(panel->opacity() == 1 && panel->isEnabled() && failedSurface->closing == 1,
        "native failure did not restore the original panel");
    failedSurface->callbacks.closed(); settle();
    const auto before = panel->property("browsed").toInt(); failedModel->browse();
    check(panel->property("browsed").toInt() == before, "failed session left its actions attached");
    fallback.shutdown({});
}
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
