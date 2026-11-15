#include "App/Views/Home/NativeHomeSelectedFilePresenter.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>
#include <iostream>
#include <stdexcept>

class SelectedFile final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QVariant file READ file WRITE setFile NOTIFY fileChanged)
  public:
    using QQuickItem::QQuickItem;
    QVariant file() const { return file_; }
    void setFile(QVariant file) { file_ = std::move(file); emit fileChanged(); }
  signals:
    void fileChanged();
    void fileRequested(QVariant file);
  private:
    QVariant file_ = QVariantMap{};
};
class Modal final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY openChanged)
  public:
    using QObject::QObject;
    bool open() const { return open_; }
    void setOpen(bool value) { if (open_ != value) { open_ = value; emit openChanged(); } }
  signals:
    void openChanged();
  private:
    bool open_ = false;
};
class Surface final : public dreamscapes::native::NativePresentationSurface {
  public:
    explicit Surface(Callbacks callbacks) : callbacks(std::move(callbacks)) {}
    bool start(QRectF value) override { frame = value; ++started; return true; }
    bool show(QRectF value) override { frame = value; ++shown; return true; }
    void hide() override { ++hidden; }
    void close() override { ++closing; }
    void restoreHostFocus() override { ++focused; }
    Callbacks callbacks;
    QRectF frame;
    int started = 0, shown = 0, hidden = 0, closing = 0, focused = 0;
};
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void settle() { for (int i = 0; i != 6; ++i) QCoreApplication::processEvents(); }
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    try {
        QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-selected-presenter-XXXXXX"));
        check(root.isValid(), "isolated fixture unavailable");
        QQuickWindow window; window.resize(800, 600);
        SelectedFile selected(window.contentItem()); selected.setObjectName("homeSelectedFile");
        selected.setPosition({25, 30}); selected.setSize({450, 400});
        const QVariantMap original{{"key", "file-1"}, {"name", "First.iisc"}, {"available", false},
            {"url", QUrl("https://example.invalid/file-1")}, {"opaque", QVariantMap{{"id", 42}}}};
        selected.setFile(original);
        Modal modal(&window); modal.setObjectName("failureDetailsModal");
        QVariant received;
        QObject::connect(&selected, &SelectedFile::fileRequested, [&](const QVariant &file) { received = file; });
        std::shared_ptr<Surface> surface;
        std::shared_ptr<dreamscapes::home::HomeSelectedFileModel> model;
        dreamscapes::home::NativeHomeSelectedFilePresenter presenter(&window, [&](auto source, auto callbacks) {
            model = std::move(source); surface = std::make_shared<Surface>(std::move(callbacks)); return surface;
        });
        check(presenter.valid(), "presenter did not discover the selected file panel");
        presenter.begin(); window.setVisible(true); settle();
        check(surface && surface->started == 1 && selected.opacity() == 1, "native surface was not created lazily");
        check(model->name.get() == "First.iisc" && model->openLabel.get() == "Download and open", "selected file was not supplied");
        surface->callbacks.ready(); settle();
        check(selected.opacity() == 0 && !selected.isEnabled() && surface->frame.x() == 25, "native panel was not presented");
        model->open();
        check(received.toMap() == original && received.toMap().value("url").metaType() == QMetaType::fromType<QUrl>() &&
            surface->focused == 1, "open did not deliver the original file object");
        const QVariantMap next{{"key", "file-2"}, {"name", "Second.iisc"}, {"available", true}};
        selected.setFile(next); settle();
        check(model->name.get() == "Second.iisc" && model->openLabel.get() == "Open in editor", "selection update was lost");
        modal.setOpen(true); settle(); check(selected.opacity() == 1 && selected.isEnabled(), "modal did not uncover selected file");
        modal.setOpen(false); settle(); check(selected.opacity() == 0, "native panel did not return after modal");
        auto previous = surface;
        QEvent languageChange(QEvent::LanguageChange); QCoreApplication::sendEvent(&app, &languageChange); settle();
        check(previous->closing == 1 && selected.opacity() == 1, "language change did not retire the surface");
        previous->callbacks.closed(); settle();
        check(surface != previous && model->name.get() == "Second.iisc", "translated surface lost the current file");
        surface->callbacks.ready(); settle();
        bool finished = false; presenter.shutdown([&] { finished = true; });
        check(!finished && surface->closing == 1 && selected.opacity() == 1, "shutdown did not retire surface asynchronously");
        auto retired = surface; retired->callbacks.closed(); settle();
        check(finished && !model->canOpen.get(), "shutdown left the open action attached");

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import QtQuick.Window
            Window {
                width: 800; height: 600
                Item {
                    objectName: "homeSelectedFile"; width: 450; height: 400
                    property var file: ({key: "qml-1", name: "QML selection", available: false})
                    property var requested: null
                    signal fileRequested(var file)
                    onFileRequested: function(value) { requested = value }
                }
            }
        )QML", QUrl());
        auto qmlRoot = std::unique_ptr<QObject>(component.create());
        if (!qmlRoot) throw std::runtime_error(component.errorString().toStdString());
        auto *qmlWindow = qobject_cast<QQuickWindow *>(qmlRoot.get());
        auto *qmlSelected = qmlRoot->findChild<QQuickItem *>("homeSelectedFile");
        check(qmlWindow && qmlSelected, "QML fixture did not create selected file panel");
        std::shared_ptr<Surface> qmlSurface;
        std::shared_ptr<dreamscapes::home::HomeSelectedFileModel> qmlModel;
        dreamscapes::home::NativeHomeSelectedFilePresenter qmlPresenter(qmlWindow, [&](auto source, auto callbacks) {
            qmlModel = std::move(source); qmlSurface = std::make_shared<Surface>(std::move(callbacks)); return qmlSurface;
        });
        check(qmlPresenter.valid(), "QML selection property or signal was not discovered");
        qmlPresenter.begin(); qmlWindow->setVisible(true); settle();
        check(qmlSurface && qmlModel->name.get() == "QML selection", "QML selection did not reach native model");
        qmlSurface->callbacks.ready(); settle();
        qmlModel->open(); settle();
        check(qmlSelected->property("requested").toMap().value("key").toString() == "qml-1", "QML open signal was not sent");
        qmlSelected->setProperty("file", QVariantMap{{"key", "qml-2"}, {"name", "QML changed"}}); settle();
        check(qmlModel->name.get() == "QML changed", "QML file update was not connected");
        qmlSurface->callbacks.failed(); settle();
        check(qmlSelected->opacity() == 1 && qmlSelected->isEnabled() && qmlSurface->closing == 1,
            "surface failure did not restore the original QML panel");
        qmlSurface->callbacks.closed(); settle();
        check(!qmlModel->canOpen.get(), "failed surface left its open handler active");
        qmlPresenter.shutdown({});
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
#include "tst_native_home_selected_file_presenter.moc"
