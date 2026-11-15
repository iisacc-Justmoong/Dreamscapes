#include "App/Views/Home/NativeHomeRecentGenerationsPresenter.h"
#include "App/Views/Home/NativeHomeRecentGenerationsSession.h"
#include "App/Views/Home/ImageSelectionModel.h"
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) { if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); } }
void settle() { for (int i = 0; i != 6; ++i) QCoreApplication::processEvents(); }
QVariant normalized(QVariant value) { return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value; }
class Surface final : public native::NativePresentationSurface {
  public:
    explicit Surface(Callbacks value) : callbacks(std::move(value)) {}
    bool start(QRectF rect) override { frame = rect; ++started; return true; }
    bool show(QRectF rect) override { frame = rect; ++shown; return true; }
    void hide() override { ++hidden; }
    void close() override { ++closing; }
    void restoreHostFocus() override { ++focused; }
    Callbacks callbacks; QRectF frame; int started = 0, shown = 0, hidden = 0, closing = 0, focused = 0;
};
void exercise() {
    QQmlEngine engine; QQmlComponent component(&engine);
    component.setData(R"QML(
        import QtQuick
        import QtQuick.Window
        Window {
            width: 800; height: 600
            QtObject { objectName: "failureDetailsModal"; property bool open: false }
            Item { objectName: "generationHistory"; signal fileRequested(var file); signal imagesRequested(var images); signal viewAllRequested() }
            Item {
                objectName: "generationHistory"; x: 20; y: 30; width: 700; height: 160
                property bool recentFixture: true
                property var files: [
                    {id: "a", name: "First", source: "https://example.invalid/a.png", opaque: {n: 42}},
                    {id: "b", name: "Second", source: "file:///b.png"},
                    {id: "v", name: "Video", source: "file:///v.mp4", mediaType: "Video"}
                ]
                property bool loading: false
                property string focusedKey: "b"
                property var opened: null
                property var edited: []
                property int browsed: 0
                signal fileRequested(var file)
                signal imagesRequested(var images)
                signal viewAllRequested()
                onFileRequested: function(file) { opened = file }
                onImagesRequested: function(images) { edited = images }
                onViewAllRequested: browsed++
            }
        }
    )QML", QUrl());
    auto root = std::unique_ptr<QObject>(component.create());
    if (!root) throw std::runtime_error(component.errorString().toStdString());
    auto *window = qobject_cast<QQuickWindow *>(root.get());
    QQuickItem *panel = nullptr;
    for (auto *candidate : root->findChildren<QQuickItem *>("generationHistory"))
        if (candidate->property("recentFixture").toBool()) panel = candidate;
    check(window && panel, "QML generation fixture missing");
    auto *selection = new ImageSelectionModel(panel);
    selection->setObjectName("generationHistorySelectionNativeModel");
    selection->setEntries(normalized(panel->property("files")).toList());
    std::shared_ptr<Surface> surface; std::shared_ptr<home::HomeRecentGenerationsModel> model;
    QPointer<home::NativeHomeRecentGenerationsSession> session;
    home::NativeHomeRecentGenerationsPresenter presenter(window, [&](auto source, auto owner, auto callbacks) {
        model = std::move(source); session = owner; surface = std::make_shared<Surface>(std::move(callbacks)); return surface;
    });
    check(presenter.valid(), "generation panel properties or signals not discovered");
    presenter.begin(); window->setVisible(true); settle();
    check(surface && session && model->rows.get().size() == 3 && model->focusedKey.get() == "b", "Home state did not reach native grid");
    surface->callbacks.ready(); settle();
    check(panel->opacity() == 0 && !panel->isEnabled(), "native generation panel did not appear");
    auto rows = model->rows.get(); good(model->activate(rows[0].token)); settle();
    check(normalized(panel->property("opened")).toMap().value("id").toString() == "a" && panel->property("focusedKey").toString() == "a",
        "native activation did not preserve original file and focus");
    good(model->toggleMode()); good(model->activate(rows[0].token)); good(model->activate(rows[1].token)); settle();
    check(selection->property("selectionMode").toBool() && normalized(selection->property("selectedKeys")).toStringList().size() == 2,
        "native selection did not reach the Home selection model");
    check(selection->selectedImages().size() == 2, "Home selection model did not resolve the original images");
    selection->setSelectedKeys({QStringLiteral("file:///b.png")}); settle();
    check(model->selectionMode.get() && model->selectedCount.get() == 1, "external Home selection was not reflected in the grid");
    selection->setSelectedKeys({QStringLiteral("https://example.invalid/a.png"), QStringLiteral("file:///b.png")}); settle();
    check(model->selectedCount.get() == 2, "Home selection restoration lost an image");
    model->editSelection(); settle();
    const auto edited = normalized(panel->property("edited")).toList();
    check(edited.size() == 2 && normalized(edited[0]).toMap().value("opaque").toMap().value("n").toInt() == 42,
        "edit did not receive original selected image objects");
    auto files = normalized(panel->property("files")).toList(); std::swap(files[0], files[1]);
    panel->setProperty("files", files); selection->setEntries(files); settle();
    check(model->rows.get()[0].token == rows[1].token && model->selectedCount.get() == 2,
        "Home reorder lost stable result identity or selection");
    auto old = surface;
    QEvent language(QEvent::LanguageChange); QCoreApplication::sendEvent(qApp, &language); settle();
    check(old->closing == 1 && panel->opacity() == 1, "language change did not retire the native grid");
    old->callbacks.closed(); settle();
    check(surface != old && model->selectionMode.get() && model->selectedCount.get() == 2,
        "native grid recreation lost the Home selection");
    surface->callbacks.ready(); settle();
    model->browse(); settle(); check(panel->property("browsed").toInt() == 1, "View all did not reach the original route");
    auto *modal = root->findChild<QObject *>("failureDetailsModal"); modal->setProperty("open", true); settle();
    check(panel->opacity() == 1 && surface->hidden, "modal did not uncover the QML panel");
    modal->setProperty("open", false); settle(); check(panel->opacity() == 0, "native grid did not return");
    bool finished = false; presenter.shutdown([&] { finished = true; });
    check(!finished && surface->closing == 1 && panel->opacity() == 1, "shutdown did not wait for native grid");
    auto retiring = surface; retiring->callbacks.closed(); settle(); check(finished && !session, "shutdown retained the session");
    std::shared_ptr<Surface> failedSurface;
    std::shared_ptr<home::HomeRecentGenerationsModel> failedModel;
    home::NativeHomeRecentGenerationsPresenter fallback(window, [&](auto source, auto, auto callbacks) {
        failedModel = std::move(source); failedSurface = std::make_shared<Surface>(std::move(callbacks)); return failedSurface;
    });
    fallback.begin(); settle(); check(bool(failedSurface), "fallback fixture did not start");
    failedSurface->callbacks.ready(); settle(); failedSurface->callbacks.failed(); settle();
    check(panel->opacity() == 1 && panel->isEnabled() && failedSurface->closing == 1,
        "native failure did not restore Home generation view");
    failedSurface->callbacks.closed(); settle();
    const auto before = panel->property("browsed").toInt(); failedModel->browse();
    check(panel->property("browsed").toInt() == before, "failed session retained Home commands");
    fallback.shutdown({});
}
}
int main(int argc, char **argv) { QGuiApplication app(argc, argv); try { exercise(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
