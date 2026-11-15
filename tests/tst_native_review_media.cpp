#include "App/Views/Result/NativeReviewMedia.h"
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef DREAMSCAPES_REVIEW_MEDIA_WIDGET_TEST
#include "App/Views/Result/NativeReviewMediaView.h"
#include "App/Native/NativePreviewStatus.h"
#include "iiGUI/WidgetState.h"
#endif

using namespace dreamscapes::review;
namespace {
void check(bool value,const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> value) { if (!value) throw std::runtime_error(value.error().message); }
class Player final : public ReviewPlaybackPort {
  public:
    void setEvents(Events value) override { events=std::move(value); }
    ii::core::Result<void> load(std::string source,std::uint64_t generation) override {
        lastSource=std::move(source); epoch=generation; ++loads; return {};
    }
    ii::core::Result<void> play() override { ++plays; if (onPlay) onPlay(); if (failPlay) return ii::core::fail(ii::core::ErrorCode::failed,"play failed"); return {}; }
    ii::core::Result<void> pause() override { ++pauses; return {}; }
    ii::core::Result<void> replay() override { ++replays; if (failReplay) return ii::core::fail(ii::core::ErrorCode::failed,"replay failed"); return {}; }
    ii::core::Result<void> stop() override { ++stops; return {}; }
    Events events; std::string lastSource; std::uint64_t epoch=0;
    int loads=0,plays=0,pauses=0,replays=0,stops=0; bool failPlay=false,failReplay=false; std::function<void()> onPlay;
};
void exercise() {
    ReviewMediaStrings strings{"No result selected","Play","Pause","Replay",
        [](std::int64_t position,std::int64_t duration){return std::to_string(position)+"/"+std::to_string(duration);}};
    auto model=ReviewMediaModel::create(std::move(strings));
    auto player=std::make_shared<Player>(); std::string failure;
    model->setFailureHandler([&](std::string message){failure=std::move(message);});
    good(model->setViewport(300,178,22,4));
    check(model->previewHeight.get()==178,"image preview geometry reserved hidden controls");
    good(model->setPlayback(player));
    good(model->setResult(true,"file:///poster.png","file:///one.mp4"));
    check(player->loads==0 && !model->controlsVisible.get(),"hidden video initiated playback");
    good(model->setVisible(true));
    check(player->loads==1 && player->lastSource=="file:///one.mp4" && model->controlsVisible.get(),
          "visible video did not load its actual media source");
    check(model->previewHeight.get()==152,"video controls did not reserve their layout space");
    const auto first=player->epoch;
    player->failPlay=true;
    check(!model->toggle() && failure=="play failed" && model->hasError.get(),"synchronous play failure was not surfaced");
    player->failPlay=false; failure.clear();
    good(model->toggle()); check(player->plays==2,"play command did not reach the backend");
    player->events.state(first,ReviewPlaybackState::playing,1200,5000);
    check(model->playing.get() && model->playLabel.get()=="Pause" && model->timeText.get()=="1200/5000",
          "playback state was not reflected in the view");
    good(model->toggle());
    player->failReplay=true;
    check(!model->replay() && failure=="replay failed","synchronous replay failure was not surfaced");
    player->failReplay=false; failure.clear(); good(model->replay());
    check(player->pauses==1 && player->replays==2,"pause or replay command was lost");
    auto image=ii::gui::rgba_image(1,1,{4,3,2,255}); check(bool(image),"frame fixture unavailable");
    player->events.frame(first,*image);
    check(model->showFrame.get() && model->frame.get()==*image,"decoded frame did not replace the poster");
    good(model->setResult(true,"file:///poster2.png","file:///two.mp4"));
    const auto second=player->epoch;
    check(second!=first && player->loads==2 && !model->showFrame.get() && !model->frame.get(),
          "changing result retained an old frame");
    player->events.frame(first,*image); player->events.error(first,"stale error");
    check(!model->showFrame.get() && failure.empty(),"stale backend callbacks changed the current result");
    player->events.error(second,"decoder failed");
    check(failure=="decoder failed" && model->error.get()=="decoder failed","decoder failure was not surfaced");
    good(model->setVisible(false));
    check(player->stops>=1 && !model->controlsVisible.get(),"hidden video kept its playback controls");
    failure.clear(); player->events.error(second,"late error");
    check(failure.empty(),"hidden player accepted a late error");
    good(model->setResult(false,"file:///still.png",{})); good(model->setVisible(true));
    check(!model->controlsVisible.get() && model->previewSource.get()=="file:///still.png",
          "still image displayed video controls or lost its preview");
    bool wrongThread=false;
    std::thread other([&]{wrongThread=!model->setVisible(false).has_value();}); other.join();
    check(wrongThread,"media view accepted state changes from another thread");
    model->close(); check(!model->active(),"media view did not retire");
}
void reentrantChanges() {
    auto strings=[] {return ReviewMediaStrings{"No result selected","Play","Pause","Replay",
        [](std::int64_t,std::int64_t){return std::string{};}};};
    auto model=ReviewMediaModel::create(strings()); auto player=std::make_shared<Player>(); std::string failure;
    model->setFailureHandler([&](std::string message){failure=std::move(message);});
    good(model->setPlayback(player)); good(model->setResult(true,{},"file:///one.mp4")); good(model->setVisible(true));
    player->failPlay=true;
    player->onPlay=[&] {good(model->setResult(true,{},"file:///two.mp4"));};
    check(!model->toggle() && player->lastSource=="file:///two.mp4" && failure.empty(),
          "failure from a retired source reached the new result");
    player->onPlay={}; player->failPlay=false;
    auto observe=model->error.observe([&](const std::string &value,std::uint64_t) {
        if (value=="old error") good(model->setResult(true,{},"file:///three.mp4"));
    });
    player->events.error(player->epoch,"old error");
    check(player->lastSource=="file:///three.mp4" && failure.empty() && !model->hasError.get(),
          "an error notification contaminated a new source");
    model->close();

    auto retiring=ReviewMediaModel::create(strings()); auto other=std::make_shared<Player>();
    good(retiring->setPlayback(other)); good(retiring->setResult(true,{},"file:///video.mp4"));
    auto closeOnVisible=retiring->controlsVisible.observe([&](bool value,std::uint64_t){if (value) retiring->close();});
    check(!retiring->setVisible(true) && !retiring->active() && !retiring->controlsVisible.get() && !retiring->canControl.get(),
          "a retired media model reenabled controls during notification");

    auto switching=ReviewMediaModel::create(strings()); auto replacement=std::make_shared<Player>();
    good(switching->setPlayback(replacement)); good(switching->setResult(true,{},"file:///old.mp4"));
    auto switchOnVisible=switching->controlsVisible.observe([&](bool value,std::uint64_t) {
        if (value) good(switching->setResult(true,"file:///new.png","file:///new.mp4"));
    });
    check(!switching->setVisible(true) && replacement->loads==1 && replacement->lastSource=="file:///new.mp4",
          "a reentrant source change loaded the new video more than once");
    switching->close();
}
#ifdef DREAMSCAPES_REVIEW_MEDIA_WIDGET_TEST
template<class T> T take(ii::core::Result<T> value) {
    if (!value) { if (value.error().cause) std::rethrow_exception(value.error().cause); throw std::runtime_error(value.error().message); }
    return std::move(*value);
}
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state,std::string_view id) {
    for (const auto &node : take(state->snapshots()))
        if (node.id==id || node.id.ends_with(std::string("/")+std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Media widget not found: "+std::string(id));
}
class Preview final : public lvui::native::ImageResolver<ii::gui::RasterImage>,public dreamscapes::native::PreviewStatusSource {
  public:
    ii::core::Observable<dreamscapes::native::PreviewStatus> status{dreamscapes::native::PreviewStatus::loading};
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view,std::uint32_t,std::uint32_t) const override {
        return status.get()==dreamscapes::native::PreviewStatus::ready ? *ii::gui::rgba_image(1,1,{10,20,30,255}) : nullptr;
    }
    dreamscapes::native::PreviewStatus previewStatus(std::string_view,std::uint32_t,std::uint32_t) const override {return status.get();}
};
void widgets() {
    auto model=ReviewMediaModel::create({"No result selected","Play","Pause","Replay",
        [](std::int64_t a,std::int64_t b){return std::to_string(a)+"/"+std::to_string(b);},"Loading preview…","Preview unavailable"});
    auto player=std::make_shared<Player>(); good(model->setPlayback(player));
    auto preview=std::make_shared<Preview>();
    good(model->setResult(true,"file:///poster.png","file:///video.mp4")); good(model->setVisible(true));
    auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();});
    auto root=take(state->content_host({},"root",{320,178}));
    good(root.set_content([&](ii::gui::WidgetBuilder &builder){return buildReviewMedia(builder,{},model,preview);}));
    (void)take(state->drawing_snapshot({320,178}));
    check(take(find(state,"reviewMediaStatus").snapshot()).text=="Loading preview…",
          "pending image did not show the loading state");
    (void)preview->status.set(dreamscapes::native::PreviewStatus::failed);
    check(take(find(state,"reviewMediaStatus").snapshot()).text=="Preview unavailable",
          "failed image did not show the unavailable state");
    (void)preview->status.set(dreamscapes::native::PreviewStatus::ready);
    check(!take(find(state,"reviewMediaStatus").snapshot()).visible,
          "ready image retained the loading or failure state");
    good(model->setResult(true,{},"file:///video.mp4"));
    check(take(find(state,"reviewMediaStatus").snapshot()).text=="No result selected",
          "an absent preview was confused with a failed image");
    good(model->setResult(true,"file:///poster.png","file:///video.mp4"));
    good(find(state,"reviewMediaPlay").activate()); check(player->plays==1,"LVUI play control did not reach the player");
    player->events.state(player->epoch,ReviewPlaybackState::playing,1200,5000);
    check(take(find(state,"reviewMediaPlay").snapshot()).text=="Pause" &&
          take(find(state,"reviewMediaTime").snapshot()).text=="1200/5000", "LVUI media status did not update");
    auto frame=take(ii::gui::rgba_image(1,1,{255,0,0,255})); player->events.frame(player->epoch,frame);
    (void)preview->status.set(dreamscapes::native::PreviewStatus::failed);
    check(!take(find(state,"reviewMediaStatus").snapshot()).visible,"a poster failure hid an active video frame");
    const auto drawing=take(state->drawing_snapshot({320,178}));
    (void)find(state,"reviewMediaFrame");
    auto imageCount=drawing.images.size();
    for (const auto &layer : drawing.overlays) imageCount+=layer.images.size();
    for (const auto &scene : drawing.scenes) for (const auto &layer : scene.layers) imageCount+=layer.images.size();
    check(imageCount>0,"decoded frame did not reach the native drawing tree");
    good(find(state,"reviewMediaReplay").activate()); check(player->replays==1,"LVUI replay control was not routed");
    state->close(); model->close();
}
#endif
}
int main() { try { exercise(); reentrantChanges();
#ifdef DREAMSCAPES_REVIEW_MEDIA_WIDGET_TEST
    widgets();
#endif
} catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
