#include "App/Views/Home/NativeHomeQueuePresenter.h"
#include "App/Generation/GenerationController.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <iostream>
#include <stdexcept>

class HomeQueue final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString modelName READ modelName WRITE setModelName NOTIFY modelNameChanged)
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
  public:
    using QQuickItem::QQuickItem;
    QString modelName() const { return model_; }
    QString deviceName() const { return device_; }
    void setModelName(QString value) { model_ = std::move(value); emit modelNameChanged(); }
    void setDeviceName(QString value) { device_ = std::move(value); emit deviceNameChanged(); }
  signals:
    void modelNameChanged();
    void deviceNameChanged();
    void viewQueueRequested();
  private:
    QString model_, device_ = "Automatic";
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
        QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-home-presenter-XXXXXX"));
        check(root.isValid(), "isolated fixture unavailable");
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.ini").toUtf8());
        GenerationRuntime runtime; runtime.modelPreferencesFile = root.filePath("models.conf");
        runtime.failureReportsDirectory = root.path();
        GenerationController controller(runtime); controller.setForeground(false);
        dreamscapes::WorkspaceSession workspace; workspace.setGeneration(&controller);
        QQuickWindow window; window.resize(800, 600);
        HomeQueue queue(window.contentItem()); queue.setObjectName("homeQueueBar");
        queue.setPosition({25, 30}); queue.setSize({450, 160});
        queue.setModelName("model A"); queue.setDeviceName("device A");
        Modal modal(&window); modal.setObjectName("failureDetailsModal");
        int opened = 0; QObject::connect(&queue, &HomeQueue::viewQueueRequested, [&] { ++opened; });
        std::shared_ptr<Surface> surface;
        std::shared_ptr<dreamscapes::home::HomeQueueModel> model;
        dreamscapes::home::NativeHomeQueuePresenter presenter(&window, &workspace,
            [&](auto source, auto callbacks) {
                model = std::move(source); surface = std::make_shared<Surface>(std::move(callbacks)); return surface;
            });
        check(presenter.valid(), "presenter did not discover the home queue");
        presenter.begin(); window.setVisible(true); settle();
        check(surface && surface->started == 1 && queue.opacity() == 1, "home surface was not created lazily");
        check(model->model.get() == "model A" && model->device.get() == "device A", "runtime names were not supplied");
        surface->callbacks.ready(); settle();
        check(queue.opacity() == 0 && !queue.isEnabled() && surface->frame.x() == 25, "home surface was not presented");
        queue.setModelName("model B"); queue.setDeviceName("device B"); settle();
        check(model->model.get() == "model B" && model->device.get() == "device B", "runtime name updates were lost");
        model->openQueue();
        check(opened == 1 && surface->focused == 1, "view queue did not reach the existing navigation action");
        modal.setOpen(true); settle(); check(queue.opacity() == 1 && queue.isEnabled(), "modal did not uncover home queue");
        modal.setOpen(false); settle(); check(queue.opacity() == 0, "home surface did not return after modal");
        auto previous = surface;
        QEvent languageChange(QEvent::LanguageChange); QCoreApplication::sendEvent(&app, &languageChange); settle();
        check(previous->closing == 1 && queue.opacity() == 1, "language change did not retire the surface");
        previous->callbacks.closed(); settle();
        check(surface != previous && model->model.get() == "model B", "translated surface lost runtime names");
        surface->callbacks.ready(); settle();
        bool finished = false; presenter.shutdown([&] { finished = true; });
        check(!finished && surface->closing == 1 && queue.opacity() == 1, "shutdown did not retire the surface asynchronously");
        auto retired = surface; retired->callbacks.closed(); settle();
        check(finished && !model->canOpenQueue.get(), "shutdown left home navigation attached");

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import QtQuick.Window
            Window {
                width: 800; height: 600
                Item {
                    objectName: "homeQueueBar"; width: 450; height: 160
                    property string modelName: "QML model"
                    property string deviceName: "QML device"
                    property int requested: 0
                    signal viewQueueRequested()
                    onViewQueueRequested: requested++
                }
            }
        )QML", QUrl());
        auto qmlRoot = std::unique_ptr<QObject>(component.create());
        if (!qmlRoot) throw std::runtime_error(component.errorString().toStdString());
        auto *qmlWindow = qobject_cast<QQuickWindow *>(qmlRoot.get());
        auto *qmlQueue = qmlRoot->findChild<QQuickItem *>("homeQueueBar");
        check(qmlWindow && qmlQueue, "QML fixture did not create its queue");
        std::shared_ptr<Surface> qmlSurface;
        std::shared_ptr<dreamscapes::home::HomeQueueModel> qmlModel;
        dreamscapes::home::NativeHomeQueuePresenter qmlPresenter(qmlWindow, &workspace,
            [&](auto source, auto callbacks) {
                qmlModel = std::move(source); qmlSurface = std::make_shared<Surface>(std::move(callbacks)); return qmlSurface;
            });
        check(qmlPresenter.valid(), "QML home queue properties or signal were not discovered");
        qmlPresenter.begin(); qmlWindow->setVisible(true); settle();
        check(bool(qmlSurface) && qmlModel->model.get() == "QML model", "QML model name did not reach the view model");
        qmlSurface->callbacks.ready(); settle();
        qmlQueue->setProperty("modelName", "QML changed"); settle();
        qmlModel->openQueue();
        check(qmlModel->model.get() == "QML changed" && qmlQueue->property("requested").toInt() == 1,
              "native home action or QML property change was not connected");
        qmlSurface->callbacks.failed(); settle();
        check(qmlQueue->opacity() == 1 && qmlQueue->isEnabled() && qmlSurface->closing == 1,
              "surface failure did not restore the original QML queue");
        qmlSurface->callbacks.closed(); settle();
        qmlModel->openQueue();
        check(qmlQueue->property("requested").toInt() == 1 && !qmlModel->canOpenQueue.get(),
              "a failed native surface left its navigation handler active");
        qmlPresenter.shutdown({});
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
#include "tst_native_home_queue_presenter.moc"
