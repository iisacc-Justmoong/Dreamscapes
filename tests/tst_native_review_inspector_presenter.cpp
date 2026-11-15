#include "App/Views/Result/NativeReviewInspectorPresenter.h"
#include "App/Views/Result/GenerationReviewModel.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlComponent>
#include <QQmlEngine>
#include <algorithm>
#include <iostream>
#include <stdexcept>

class Inspector final : public QQuickItem {
    Q_OBJECT
  public: using QQuickItem::QQuickItem;
  signals: void previewRequested(); void saveRequested(); void playbackFailed(const QString &message);
};
class Player final : public dreamscapes::review::ReviewPlaybackPort {
  public:
    void setEvents(Events value) override {events=std::move(value);}
    ii::core::Result<void> load(std::string value,std::uint64_t generation) override {source=std::move(value); epoch=generation; ++loads; if (failLoad) return ii::core::fail(ii::core::ErrorCode::failed,"load failed"); return {};}
    ii::core::Result<void> play() override {++plays; return {};}
    ii::core::Result<void> pause() override {return {};}
    ii::core::Result<void> replay() override {return {};}
    ii::core::Result<void> stop() override {++stops; return {};}
    Events events; std::string source; std::uint64_t epoch=0; int loads=0,plays=0,stops=0; bool failLoad=false;
};
class LegacyPlayer final : public QObject {
    Q_OBJECT
  public: using QObject::QObject; int stops=0;
  public slots: void stop() {++stops;}
};
class Modal final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY openChanged)
  public:
    using QObject::QObject;
    bool open() const { return open_; }
    void setOpen(bool value) { if (open_!=value) { open_=value; emit openChanged(); } }
  signals: void openChanged();
  private: bool open_=false;
};
class Surface final : public dreamscapes::native::NativePresentationSurface {
  public:
    explicit Surface(Callbacks callbacks) : callbacks(std::move(callbacks)) {}
    bool start(QRectF frame) override { this->frame=frame; ++started; return true; }
    bool show(QRectF frame) override { this->frame=frame; ++shown; return true; }
    void hide() override { ++hidden; }
    void close() override { ++closing; }
    void restoreHostFocus() override { ++focused; }
    Callbacks callbacks; QRectF frame;
    int started=0,shown=0,hidden=0,closing=0,focused=0;
};
void check(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
void settle() { for (int i=0;i!=6;++i) QCoreApplication::processEvents(); }
QVariantMap result(QString id,QString type,QString prompt={}) {
    QVariantMap value{{"id",id},{"mediaType",type},{"imageSource",QUrl("file:///preview.png")},
        {"documentSource",QUrl("file:///original.iisc")},{"prompt",prompt},{"modelName","Fixture model"},
        {"width",1024},{"height",768},{"seed",123456},{"startedAt","2026-11-01T10:00:00.000Z"},
        {"finishedAt","2026-11-01T10:00:12.250Z"},
        {"advancedParameters",QVariantMap{{"steps",30},{"cfgScale",2.5}}}};
    if (type=="Video") value["mediaSource"]=QUrl("file:///"+id+".mp4");
    return value;
}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    try {
        QQuickWindow window; window.resize(800,600);
        GenerationReviewModel review;
        Inspector first(window.contentItem()); first.setSize({300,500});
        QQuickItem firstPreview(&first); firstPreview.setObjectName("reviewInspectorPreview"); firstPreview.setHeight(178);
        Inspector second(window.contentItem()); second.setPosition({320,0}); second.setSize({300,500}); second.setVisible(false);
        QQuickItem secondPreview(&second); secondPreview.setObjectName("reviewInspectorPreview"); secondPreview.setHeight(178);
        Modal modal(&window); modal.setObjectName("failureDetailsModal");
        review.setEntries({result("image","Image","prompt")}); review.focusAt(0);
        std::shared_ptr<Surface> surface; std::shared_ptr<dreamscapes::review::InspectorViewModel> model;
        dreamscapes::review::NativeReviewInspectorPresenter presenter(&window,&review,[&](auto source,auto,auto callbacks) {
            model=std::move(source); surface=std::make_shared<Surface>(std::move(callbacks)); return surface;
        });
        check(presenter.valid(),"presenter did not find both inspector panels");
        int previews=0,saves=0;
        QObject::connect(&first,&Inspector::previewRequested,[&]{++previews;});
        QObject::connect(&first,&Inspector::saveRequested,[&]{++saves;});
        presenter.begin(); window.setVisible(true); settle();
        check(surface && surface->started==1 && first.opacity()==1,"inspector surface was not created lazily");
        surface->callbacks.ready(); settle();
        check(surface->shown && first.opacity()==0 && !first.isEnabled(),"native inspector did not replace the image panel");
        for (const auto &entry : review.inspectorRows()) {
            const auto legacy=entry.toMap();
            const auto key=legacy.value("label").toString().toStdString();
            const auto fields=model->fields.get();
            const auto found=std::ranges::find_if(fields,[&](const auto &field){return field->key==key;});
            check(found!=fields.end() && (*found)->text.get()==legacy.value("value").toString().toStdString(),
                  "native inspector differs from the existing result metadata");
        }
        model->expand(); model->save();
        check(previews==1 && saves==1 && surface->focused==2,"inspector actions did not reach the original routes");
        modal.setOpen(true); settle();
        check(first.opacity()==1 && first.isEnabled(),"modal did not restore the original inspector");
        modal.setOpen(false); settle(); check(first.opacity()==0,"native inspector did not return after the modal");
        const auto fields=model->fields.get();
        const auto prompt=std::ranges::find_if(fields,[](const auto &field){return field->key=="Prompt";});
        check(prompt!=fields.end(),"prompt field missing");
        (*prompt)->wrappingFallbackChanged(true); settle();
        check(first.opacity()==1 && first.isEnabled(),"a live wrapping fallback did not restore the existing inspector");
        review.setEntries({result("image","Image","new prompt")}); review.focusAt(0); settle();
        check(first.opacity()==0,"native inspector did not recover after text changed");
        review.setEntries({result("video","Video")}); review.focusAt(0); settle();
        check(first.opacity()==1 && first.isEnabled() && surface->hidden>0,"video playback did not return to the existing inspector");
        review.setEntries({result("long","Image",QString(100000,'p'))}); review.focusAt(0); settle();
        check(first.opacity()==1 && first.isEnabled(),"long text without native soft wrapping displaced the existing inspector");
        review.setEntries({result("image","Image","prompt")}); review.focusAt(0); settle();
        check(first.opacity()==0 && !first.isEnabled(),"image inspector did not recover after the fallback");
        first.setVisible(false); second.setVisible(true); settle();
        check(first.opacity()==1 && second.opacity()==0 && surface->frame.x()==320,"native inspector did not follow the narrow panel");
        auto previous=surface;
        QEvent language(QEvent::LanguageChange); QCoreApplication::sendEvent(&app,&language); settle();
        check(previous->closing==1 && second.opacity()==1,"language change did not retire the translated inspector");
        previous->callbacks.closed(); settle();
        check(surface!=previous && surface->started==1,"language change did not recreate the inspector");
        surface->callbacks.ready(); settle();
        check(second.opacity()==0 && model->previewSource.get()=="file:///preview.png","language change lost the focused result");
        bool finished=false; presenter.shutdown([&]{finished=true;}); settle();
        check(!finished && surface->closing==1 && second.opacity()==1,"inspector shutdown did not wait for retirement");
        surface->callbacks.closed(); settle();
        check(finished && !model->active(),"inspector shutdown retained its model or commands");

        QQmlEngine engine; QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import QtQuick.Window
            Window {
                width: 800; height: 600
                Item {
                    objectName: "dynamicInspector"; width: 300; height: 500
                    property int previews: 0
                    signal previewRequested()
                    signal saveRequested()
                    onPreviewRequested: previews += 1
                    Item { objectName: "reviewInspectorPreview"; height: 178 }
                }
            }
        )QML",QUrl());
        auto qmlRoot=std::unique_ptr<QObject>(component.create());
        if (!qmlRoot) throw std::runtime_error(component.errorString().toStdString());
        auto *qmlWindow=qobject_cast<QQuickWindow *>(qmlRoot.get());
        auto *qmlPanel=qmlRoot->findChild<QQuickItem *>("dynamicInspector");
        check(qmlWindow && qmlPanel,"dynamic inspector fixture unavailable");
        std::shared_ptr<Surface> qmlSurface; std::shared_ptr<dreamscapes::review::InspectorViewModel> qmlModel;
        dreamscapes::review::NativeReviewInspectorPresenter qmlPresenter(qmlWindow,&review,[&](auto source,auto,auto callbacks) {
            qmlModel=std::move(source); qmlSurface=std::make_shared<Surface>(std::move(callbacks)); return qmlSurface;
        });
        check(qmlPresenter.valid(),"QML inspector signals were not discovered");
        qmlPresenter.begin(); qmlWindow->setVisible(true); settle();
        check(bool(qmlSurface),"QML inspector did not admit the native surface");
        qmlSurface->callbacks.ready(); settle(); qmlModel->expand();
        check(qmlPanel->property("previews").toInt()==1,"native preview did not reach the QML signal");
        qmlPresenter.shutdown({}); qmlSurface->callbacks.closed(); settle();

        QQuickWindow videoWindow; videoWindow.resize(800,600);
        Inspector videoPanel(videoWindow.contentItem()); videoPanel.setSize({300,500});
        QQuickItem videoPreview(&videoPanel); videoPreview.setObjectName("reviewInspectorPreview"); videoPreview.setHeight(178);
        LegacyPlayer legacy(&videoPreview); legacy.setObjectName("reviewInspectorPreviewPlayer");
        review.setEntries({result("clip1","Video")}); review.focusAt(0);
        std::shared_ptr<Surface> videoSurface; std::shared_ptr<Player> player;
        std::shared_ptr<dreamscapes::review::ReviewMediaModel> media;
        dreamscapes::review::NativeReviewInspectorPresenter videoPresenter(&videoWindow,&review,
            [&](auto,auto view,auto callbacks) {media=std::move(view); videoSurface=std::make_shared<Surface>(std::move(callbacks)); return videoSurface;},
            [&]() -> std::shared_ptr<dreamscapes::review::ReviewPlaybackPort> {player=std::make_shared<Player>(); return player;});
        QString playbackError;
        QObject::connect(&videoPanel,&Inspector::playbackFailed,[&](const QString &message){playbackError=message;});
        videoPresenter.begin(); videoWindow.setVisible(true); settle();
        check(player && videoSurface,"video backend or native surface was not created");
        videoSurface->callbacks.ready(); settle();
        check(videoPanel.opacity()==0 && legacy.stops>0 && media->controlsVisible.get() && player->loads==1 && player->source=="file:///clip1.mp4",
              "native video preview did not load its real source");
        const auto previousEpoch=player->epoch;
        check(bool(media->toggle()) && player->plays==1,"video play command was not routed");
        auto frame=ii::gui::rgba_image(1,1,{1,2,3,255}); check(bool(frame),"video frame fixture unavailable");
        player->events.state(previousEpoch,dreamscapes::review::ReviewPlaybackState::playing,0,1000);
        player->events.frame(previousEpoch,*frame); check(media->showFrame.get(),"video frame was not presented");
        review.setEntries({result("clip2","Video")}); review.focusAt(0); settle();
        check(player->epoch!=previousEpoch && player->source=="file:///clip2.mp4" && !media->showFrame.get(),
              "switching videos retained the previous frame or source");
        player->events.frame(previousEpoch,*frame); check(!media->showFrame.get(),"stale video frame reached the new result");
        player->events.error(player->epoch,"decode error"); settle();
        check(videoPanel.opacity()==1 && playbackError=="decode error","native playback failure did not restore and report through the old panel");
        player->failLoad=true; playbackError.clear();
        review.setEntries({result("clip3","Video")}); review.focusAt(0); settle();
        check(videoPanel.opacity()==1 && playbackError=="load failed",
              "synchronous load failure did not restore and report through the old panel");
        videoPresenter.shutdown({}); videoSurface->callbacks.closed(); settle();
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
#include "tst_native_review_inspector_presenter.moc"
