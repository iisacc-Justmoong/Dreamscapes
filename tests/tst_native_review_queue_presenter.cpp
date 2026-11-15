#include "App/Views/Result/NativeReviewQueuePresenter.h"
#include "App/Views/Result/GenerationReviewModel.h"
#include "App/ViewModels/WorkspaceSession.h"
#include "App/Generation/GenerationController.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QQmlComponent>
#include <QQmlEngine>
#include <iostream>
#include <stdexcept>

class Queue final : public QQuickItem {
    Q_OBJECT
  public:
    using QQuickItem::QQuickItem;
  signals:
    void jobRequested(const QString &id);
    void cancelRequested(const QString &id);
    void failureRequested(const QString &id);
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
class Surface final : public dreamscapes::review::NativeReviewQueuePresentationSurface {
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
        QQuickWindow window; window.resize(800, 600);
        QTemporaryDir root(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-review-presenter-XXXXXX"));
        check(root.isValid(), "isolated fixture unavailable");
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", root.filePath("settings.ini").toUtf8());
        GenerationRuntime runtime;
        runtime.modelPreferencesFile = root.filePath("models.conf"); runtime.failureReportsDirectory = root.path();
        GenerationController controller(runtime); controller.setForeground(false);
        dreamscapes::WorkspaceSession workspace;
        workspace.setGeneration(&controller);
        GenerationReviewModel review;
        Queue first(window.contentItem()); first.setSize({250, 400});
        QQuickItem list1(&first); list1.setObjectName("reviewQueueList");
        Queue second(window.contentItem()); second.setPosition({270, 0}); second.setSize({300, 400}); second.setVisible(false);
        QQuickItem list2(&second); list2.setObjectName("reviewQueueList");
        Modal modal(&window); modal.setObjectName("failureDetailsModal");
        std::shared_ptr<Surface> surface;
        std::shared_ptr<dreamscapes::review::QueueModel> model;
        dreamscapes::review::NativeReviewQueuePresenter presenter(&window, &workspace, &review,
            [&](auto source, auto callbacks) {
                model = std::move(source); surface = std::make_shared<Surface>(std::move(callbacks)); return surface;
            });
        check(presenter.valid(), "presenter did not discover both queues");
        int selections = 0, cancellations = 0, failures = 0; QString selected, canceled, failed;
        QObject::connect(&first, &Queue::jobRequested, [&](const QString &id) { ++selections; selected = id; });
        QObject::connect(&first, &Queue::cancelRequested, [&](const QString &id) { ++cancellations; canceled = id; });
        QObject::connect(&first, &Queue::failureRequested, [&](const QString &id) { ++failures; failed = id; });
        presenter.begin(); window.setVisible(true); settle();
        check(surface && surface->started == 1 && first.opacity() == 1, "surface was not created lazily");
        surface->callbacks.ready(); settle();
        check(surface->shown && first.opacity() == 0 && !first.isEnabled(), "ready surface did not replace the queue");
        model->setSelectionEnabled(true);
        model->showAllJobs();
        check(selections == 1 && selected.isEmpty() && surface->focused == 1, "native selection did not return focus and use the existing signal");
        using dreamscapes::vm::Value;
        using dreamscapes::vm::Array;
        using dreamscapes::vm::Object;
        check(bool(model->setSnapshot({.jobs = Value(Array{
            Value(Object{{"id", Value("running")}, {"state", Value("running")}}),
            Value(Object{{"id", Value("failed")}, {"state", Value("failed")}})})})), "fixture jobs were rejected");
        model->items.get()[0]->select(); model->items.get()[0]->cancel(); model->items.get()[1]->inspectFailure();
        check(selections == 2 && selected == "running" && review.jobFilter() == "running" &&
              cancellations == 1 && canceled == "running" && failures == 1 && failed == "failed",
              "native row commands did not reach the existing queue actions");
        modal.setOpen(true); settle();
        check(first.opacity() == 1 && first.isEnabled() && surface->hidden, "modal did not restore the original queue");
        modal.setOpen(false); settle(); check(first.opacity() == 0, "queue did not return after the modal");
        first.setVisible(false); second.setVisible(true); settle();
        check(second.opacity() == 0 && first.opacity() == 1 && surface->frame.x() == 270,
              "native surface did not follow the narrow queue");
        auto previous = surface;
        QEvent languageChange(QEvent::LanguageChange);
        QCoreApplication::sendEvent(&app, &languageChange); settle();
        check(previous->closing == 1 && second.opacity() == 1, "language change did not retire the previous translated view");
        previous->callbacks.closed(); settle();
        check(surface != previous && surface->started == 1, "language change did not build a new translated view");
        surface->callbacks.ready(); settle();
        check(second.opacity() == 0 && review.jobFilter() == "running", "language change lost the shared review session");
        bool finished = false; presenter.shutdown([&] { finished = true; }); settle();
        check(!finished && surface->closing == 1 && second.opacity() == 1, "shutdown did not wait for native retirement");
        auto retired = surface; retired->callbacks.closed(); settle();
        check(finished && !model->active(), "shutdown did not retire the review session");

        // Exercise the same discovery and signal invocation against actual
        // dynamically generated QML metaobjects, not just a C++ test item.
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import QtQuick.Window
            Window {
                width: 800; height: 600
                Item {
                    objectName: "dynamicQueue"; width: 260; height: 400
                    property string lastSelected: "initial"
                    signal jobRequested(string id)
                    signal cancelRequested(string id)
                    signal failureRequested(string id)
                    onJobRequested: function(id) { lastSelected = id }
                    ListView { objectName: "reviewQueueList" }
                }
            }
        )QML", QUrl());
        auto qmlRoot = std::unique_ptr<QObject>(component.create());
        if (!qmlRoot) throw std::runtime_error(component.errorString().toStdString());
        auto *qmlWindow = qobject_cast<QQuickWindow *>(qmlRoot.get());
        auto *qmlQueue = qmlRoot->findChild<QQuickItem *>("dynamicQueue");
        check(qmlWindow && qmlQueue, "QML fixture did not create its queue");
        std::shared_ptr<Surface> qmlSurface;
        std::shared_ptr<dreamscapes::review::QueueModel> qmlModel;
        dreamscapes::review::NativeReviewQueuePresenter qmlPresenter(qmlWindow, &workspace, &review,
            [&](auto source, auto callbacks) {
                qmlModel = std::move(source); qmlSurface = std::make_shared<Surface>(std::move(callbacks)); return qmlSurface;
            });
        check(qmlPresenter.valid(), "QML queue signal signatures were not discovered");
        qmlPresenter.begin(); qmlWindow->setVisible(true); settle();
        check(bool(qmlSurface), "QML queue did not admit a native surface");
        qmlSurface->callbacks.ready(); settle();
        qmlModel->showAllJobs();
        check(qmlQueue->property("lastSelected").toString().isEmpty() && review.jobFilter().isEmpty(),
              "native action did not reach the QML signal and shared review filter");
        qmlPresenter.shutdown({}); qmlSurface->callbacks.closed(); settle();
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
#include "tst_native_review_queue_presenter.moc"
