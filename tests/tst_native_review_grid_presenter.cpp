#include "App/Views/Result/NativeReviewGridPresenter.h"
#include "App/Views/Result/GenerationReviewModel.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <iostream>
#include <stdexcept>

class ResultRoot final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool queueOpen READ queueOpen WRITE setQueueOpen NOTIFY queueOpenChanged)
    Q_PROPERTY(bool inspectorOpen READ inspectorOpen WRITE setInspectorOpen NOTIFY inspectorOpenChanged)
  public:
    using QQuickItem::QQuickItem;
    bool queueOpen() const {return queue_;} bool inspectorOpen() const {return inspector_;}
    void setQueueOpen(bool value) {if (queue_!=value) {queue_=value; emit queueOpenChanged();}}
    void setInspectorOpen(bool value) {if (inspector_!=value) {inspector_=value; emit inspectorOpenChanged();}}
    Q_INVOKABLE void showEntry(QVariant value) {detail=value;}
    Q_INVOKABLE void editEntries(QVariant value) {edited=value.toList();}
    Q_INVOKABLE void compareImages(QVariant value) {compared=value.toList();}
    Q_INVOKABLE void saveImageToFile() {++files;}
    Q_INVOKABLE void saveImageToPhotos() {++photos;}
    QVariant detail; QVariantList edited,compared; int files=0,photos=0;
  signals: void queueOpenChanged(); void inspectorOpenChanged();
  private: bool queue_=false,inspector_=false;
};
class Gallery final : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(qreal originY READ originY CONSTANT)
  public:
    using QQuickItem::QQuickItem;
    qreal contentY() const {return y_;} qreal originY() const {return 3;}
    void setContentY(qreal value) {if (y_!=value) {y_=value; emit contentYChanged();}}
  signals: void contentYChanged();
  private: qreal y_=43;
};
class Modal final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY openChanged)
  public:
    using QObject::QObject;
    bool open() const {return open_;}
    void setOpen(bool value) {if (open_!=value) {open_=value; emit openChanged();}}
  signals: void openChanged();
  private: bool open_=false;
};
class Exporter final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy WRITE setBusy NOTIFY busyChanged)
    Q_PROPERTY(bool supported READ supported CONSTANT)
  public:
    using QObject::QObject;
    bool busy() const {return busy_;} bool supported() const {return true;}
    void setBusy(bool value) {if (busy_!=value) {busy_=value; emit busyChanged();}}
  signals: void busyChanged();
  private: bool busy_=false;
};
class Surface final : public dreamscapes::review::NativeReviewGridSurface {
  public:
    explicit Surface(Callbacks value) : callbacks(std::move(value)) {}
    bool start(QRectF frame) override {geometry=frame; ++started; return true;}
    bool show(QRectF frame) override {geometry=frame; ++shown; auto callback=onShow; if (callback) callback(); return true;}
    void hide() override {++hidden;}
    void close() override {++closing;}
    void restoreHostFocus() override {++focused;}
    std::optional<double> scrollOffset() const override {return offset;}
    bool setScrollOffset(double value) override {offset=value; ++scrolls; return true;}
    Callbacks callbacks; QRectF geometry; double offset=0; int started=0,shown=0,hidden=0,closing=0,focused=0,scrolls=0;
    std::function<void()> onShow;
};
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void settle() {for (int i=0;i!=8;++i) QCoreApplication::processEvents();}
QVariantMap entry(QString id,QString prompt) {return {{"id",id},{"mediaType","Image"},{"prompt",prompt},
    {"documentSource",QUrl("file:///"+id+".iisc")},{"imageSource",QUrl("file:///"+id+".png")}};}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    try {
        QQuickWindow window; window.resize(800,600);
        ResultRoot root(window.contentItem()); root.setObjectName("generationResult"); root.setSize({800,600});
        Gallery gallery(&root); gallery.setObjectName("resultGallery"); gallery.setPosition({20,30}); gallery.setSize({600,450});
        Exporter file(&root); file.setObjectName("imageFileExporter");
        Exporter photo(&root); photo.setObjectName("photoLibraryExporter");
        Modal modal(&window); modal.setObjectName("failureDetailsModal");
        GenerationReviewModel review; const auto a=entry("a","original"),b=entry("b","original");
        review.setEntries({a,b}); review.focusAt(0);
        std::shared_ptr<Surface> surface; std::shared_ptr<dreamscapes::review::ReviewGridModel> grid;
        dreamscapes::review::NativeReviewGridPresenter presenter(&window,&review,[&](auto model,auto callbacks) {
            grid=std::move(model); surface=std::make_shared<Surface>(std::move(callbacks)); return surface;
        });
        check(presenter.valid(),"grid presenter did not discover the existing result region");
        presenter.begin(); window.setVisible(true); settle();
        check(surface && surface->started==1,"grid surface was not prepared");
        surface->callbacks.ready(); settle();
        check(gallery.opacity()==0 && !gallery.isEnabled() && surface->offset==40,"native grid did not take over with the original scroll");
        auto model=grid->gallery(); auto session=review.nativeSession();
        session->selectAt(0); session->selectAt(1,false,true); model->edit();
        check(root.edited==QVariantList({a,b}) && root.edited[0].toMap().value("documentSource").metaType().id()==QMetaType::QUrl,
              "native edit did not receive original Qt result values");
        model->openDetail(model->items.get()[1]->token);
        check(root.detail.toMap()==b,"native detail did not open the original result");
        auto menu=model->contextMenu(); check(menu->open(session->keyAt(0)),"grid context did not open");
        review.focusAt(1);
        check(menu->invoke(dreamscapes::review::ReviewContextAction::saveFile) && root.files==1 && review.focusedEntry()==a,
              "native context save did not restore the clicked original");
        auto changed=a; changed["prompt"]="new"; review.setEntries({changed,b});
        photo.setBusy(true); settle();
        check(!menu->context.get().canSavePhotos,"photo exporter busy state did not update the open menu");
        check(menu->invoke(dreamscapes::review::ReviewContextAction::compare) && root.compared==QVariantList({a,b}),
              "context action did not retain the original menu snapshot");
        surface->offset=190; root.setQueueOpen(true); settle();
        check(gallery.opacity()==1 && gallery.isEnabled() && gallery.contentY()==193,"queue drawer did not restore the gallery and its scroll");
        root.setQueueOpen(false); settle();
        check(gallery.opacity()==0 && surface->offset==190,"native gallery did not resume at the retained scroll");
        modal.setOpen(true); settle(); check(gallery.opacity()==1,"modal did not hide the native grid");
        modal.setOpen(false); settle(); check(gallery.opacity()==0,"native grid did not return after modal dismissal");
        bool done=false; presenter.shutdown([&]{done=true;}); settle();
        check(!done && surface->closing==1 && gallery.opacity()==1,"grid shutdown did not restore the original view");
        surface->callbacks.closed(); settle(); check(done && !grid->active(),"grid shutdown did not retire its model");

        QQmlEngine engine; QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import QtQuick.Window
            Window {
                width: 800; height: 600
                Item {
                    objectName: "generationResult"; width: 800; height: 600
                    property bool queueOpen: false
                    property bool inspectorOpen: false
                    property var lastDetail
                    function showEntry(value) { lastDetail = value }
                    function editEntries(value) {}
                    function compareImages(value) {}
                    function saveImageToFile() {}
                    function saveImageToPhotos() {}
                    Item { objectName: "resultGallery"; width: 600; height: 450
                        property real contentY: 0; readonly property real originY: 0 }
                }
            }
        )QML",QUrl());
        auto qmlRoot=std::unique_ptr<QObject>(component.create());
        if (!qmlRoot) throw std::runtime_error(component.errorString().toStdString());
        auto *qmlWindow=qobject_cast<QQuickWindow *>(qmlRoot.get());
        auto *qmlResult=qmlRoot->findChild<QQuickItem *>("generationResult");
        check(qmlWindow && qmlResult,"QML grid fixture unavailable");
        std::shared_ptr<Surface> qmlSurface; std::shared_ptr<dreamscapes::review::ReviewGridModel> qmlGrid;
        auto qmlPresenter=std::make_unique<dreamscapes::review::NativeReviewGridPresenter>(qmlWindow,&review,[&](auto model,auto callbacks) {
            qmlGrid=std::move(model); qmlSurface=std::make_shared<Surface>(std::move(callbacks)); return qmlSurface;
        });
        check(qmlPresenter->valid(),"QML result methods were not discovered");
        qmlPresenter->begin(); qmlWindow->setVisible(true); settle();
        check(bool(qmlSurface),"QML grid surface did not start");
        qmlSurface->callbacks.ready(); settle();
        qmlGrid->gallery()->openDetail(qmlGrid->gallery()->items.get()[0]->token);
        check(qmlResult->property("lastDetail").toMap()==changed,"native detail did not invoke the QML result method");
        qmlPresenter->shutdown({}); qmlSurface->callbacks.closed(); settle(); qmlPresenter.reset();

        auto deleting=std::make_unique<dreamscapes::review::NativeReviewGridPresenter>(qmlWindow,&review,[&](auto,auto callbacks) {
            qmlSurface=std::make_shared<Surface>(std::move(callbacks)); return qmlSurface;
        });
        deleting->begin(); settle(); check(bool(qmlSurface),"reentrant grid fixture unavailable");
        qmlSurface->onShow=[&]{deleting.reset();};
        qmlSurface->callbacks.ready(); settle();
        check(!deleting,"native presenter did not survive reentrant surface retirement");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n'; return 1;}
}
#include "tst_native_review_grid_presenter.moc"
