#include "App/Views/Publishing/NativePublishWorkspace.h"
#include "App/Views/Editor/NativeEditorFrame.h"
#include <Serialization/IiscCodec.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    void post(std::function<void()> task) override {tasks.push_back(std::move(task));}
    std::size_t maxConcurrency() const noexcept override {return 1;}
    void drain() {while (!tasks.empty()) {auto task=std::move(tasks.front());tasks.erase(tasks.begin());task();}}
    std::vector<std::function<void()>> tasks;
};

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        auto document=dreamscapes::native::DocumentSession::create();
        iiSharedCanvas::Document canvas; canvas.extent={8,8};canvas.timeline.frameCount=2;
        canvas.assets.emplace_back(iiSharedCanvas::RasterAsset{"pixels",makeRasterLayer(8,8,0xff123456)});
        canvas.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"paint","Paint"},iiSharedCanvas::StaticSource{"pixels"}});
        canvas.publicationMetadata=iiSharedCanvas::PublicationMetadata{.title="Inherited",.description="Original",
            .settings={{"vendor.option",std::string("kept")},
                       {"publish.destination.x.sequence",std::string("Thread")},
                       {"publish.destination.x.text",std::string("Destination text")}}};
        good(document->attach(canvas,"post.iisc"));
        auto publish=dreamscapes::publishing::WorkspaceModel::create(document);
        int saves=0;
        check(!publish->canSaveDraft.get() && !publish->saveDraft(),"unconnected draft save was enabled");
        publish->setDraftSaveHandler([&]()->ii::core::Result<void>{++saves;return {};});
        publish->setDraftSaveAvailable(true);
        check(publish->canSaveDraft.get(),"connected draft save was not enabled");
        good(publish->saveDraft());check(saves==1,"draft save did not reach its provider");
        publish->setDraftSaveAvailable(false);
        check(!publish->canSaveDraft.get() && !publish->saveDraft() && saves==1,"disabled draft save invoked its provider");
        ii::runtime::EventLoop loop;
        auto executor=std::make_shared<ManualExecutor>();
        auto renderer=dreamscapes::editor::FramePresenter::create(document,loop,executor);
        publish->setFramePresenter(renderer); publish->previewSizeChanged(128,128);
        const auto revision=document->revision.get();
        publish->setVisible(true); executor->drain(); (void)loop.poll();
        check(publish->hasFrame.get() && publish->renderedFrame.get() && !document->dirty.get() &&
              document->revision.get()==revision,"publish preview did not render the real canvas without editing it");
        publish->setVisible(false);
        check(!publish->targets.get().empty() && !publish->canPublish.get(),"unconnected publication appeared available");
        good(publish->select("x"));
        check(publish->platformName.get()=="X" && publish->maximumMedia.get()==4 && publish->threaded.get() &&
              !document->dirty.get(),"X policy or the saved thread setting was not restored");
        auto x=*std::ranges::find_if(publish->targets.get(),[](const auto &item){return item->id=="x";});
        auto facebook=*std::ranges::find_if(publish->targets.get(),[](const auto &item){return item->id=="facebook";});
        check(!x->included.get() && !facebook->included.get(),"an absent destination setting was invented");
        good(publish->setTargetIncluded("x",true));good(publish->setTargetIncluded("facebook",true));
        check(x->included.get() && facebook->included.get() && x->selected.get() && !facebook->selected.get() &&
              std::get<bool>(document->document()->publicationMetadata->settings.at("publish.destination.x.selected")) &&
              std::get<bool>(document->document()->publicationMetadata->settings.at("publish.destination.facebook.selected")) &&
              !publish->canPublish.get(),"independent destination choices did not update the document");
        good(publish->editTitle("Updated"));
        check(document->publicationTitle.get()=="Updated" && document->document()->publicationMetadata &&
              std::get<std::string>(document->document()->publicationMetadata->settings.at("vendor.option"))=="kept",
              "publication edit did not update shared IISC metadata");
        check(executor->tasks.empty(),"hidden publishing preview scheduled a canvas render");
        iiSocialIntegration::ReviewSource source; source.post.text="caption";
        source.post.media.resize(20); source.captions.resize(20);
        source.captions[0]="First canvas caption";
        auto frameSource=source;good(publish->setReviewSource(std::move(source)));
        good(document->seek(1));
        check(publish->plannedPosts.get()==0 && publish->planText.get()=="Media not prepared",
              "seeking retained media prepared from another canvas frame");
        good(document->seek(0));good(publish->setReviewSource(std::move(frameSource)));
        check(publish->plannedPosts.get()==5 && publish->plan().posts.size()==5 &&
              publish->plan().posts[1].replyTo==std::optional<std::size_t>{0} &&
              publish->plan().posts[0].post.text.find("Destination text")!=std::string::npos,
              "twenty X images were not planned as a five-post thread");
        auto titleField=publish->reviewField("title"),textField=publish->reviewField("text");
        auto countField=publish->reviewField("mediaPerPost"),captionField=publish->reviewField("includeCaptions");
        check(titleField && textField && countField && captionField && titleField->text.get()=="Updated" &&
              !titleField->overridden.get() && textField->text.get()=="Destination text" && textField->overridden.get(),
              "destination review fields did not reflect shared and overridden metadata");
        good(titleField->edit("X title"));
        check(publish->plan().posts[0].post.title=="X title" && document->publicationTitle.get()=="Updated",
              "destination title modified the common title or not the plan");
        good(titleField->reset());
        check(publish->plan().posts[0].post.title=="Updated" &&
              !document->document()->publicationMetadata->settings.contains("publish.destination.x.title"),
              "reset did not restore the shared title");
        const auto beforeReset=document->revision.get();
        good(titleField->reset());
        check(document->revision.get()==beforeReset,"resetting an inherited field modified the document");
        const auto choices=countField->options.get();
        const auto two=std::ranges::find(choices,std::string("2"));
        check(two!=choices.end(),"platform policy omitted an allowed attachment count");
        good(countField->select(int(two-choices.begin())));
        good(captionField->toggle(false));
        check(publish->plannedPosts.get()==10 &&
              publish->plan().posts[0].post.text.find("First canvas caption")==std::string::npos,
              "review field edits did not update grouping and captions");
        good(countField->reset());good(captionField->reset());
        check(publish->plannedPosts.get()==5 &&
              publish->plan().posts[0].post.text.find("First canvas caption")!=std::string::npos,
              "reset did not restore automatic grouping and captions");
        good(publish->select("facebook"));
        check(!textField->edit("stale change") &&
              std::get<std::string>(document->document()->publicationMetadata->settings.at("publish.destination.x.text"))=="Destination text",
              "a retired field edited a different destination");
        good(publish->select("x"));
        good(document->editPublicationSetting("publish.destination.x.mediaPerPost",std::string("2")));
        check(publish->plannedPosts.get()==10,"saved destination media grouping was ignored");
        good(document->editPublicationSetting("publish.destination.x.mediaPerPost",std::string("Automatic")));
        good(document->editPublicationSetting("publish.destination.x.includeCaptions",false));
        check(publish->plannedPosts.get()==5 && publish->plan().posts[0].post.text.find("First canvas caption")==std::string::npos,
              "saved destination caption setting was ignored");
        good(document->editPublicationSetting("publish.destination.x.includeCaptions",true));
        check(publish->plan().posts[0].post.text.find("First canvas caption")!=std::string::npos,
              "the canvas caption could not be restored");
        const auto visualRevision=document->visualRevision.get();
        good(publish->editTitle("Updated again"));
        check(publish->plannedPosts.get()==5 && publish->plan().posts[0].post.title=="Updated again" &&
              document->visualRevision.get()==visualRevision && executor->tasks.empty(),
              "metadata edit discarded prepared media or requested a canvas render");
        good(publish->setThreaded(false));
        check(!publish->threaded.get() && publish->plannedPosts.get()==5 &&
              !publish->plan().posts[1].replyTo &&
              std::get<std::string>(document->document()->publicationMetadata->settings.at("publish.destination.x.sequence"))=="Separate posts" &&
              document->visualRevision.get()==visualRevision,
              "thread option did not update the shared IISC metadata or retained stale planning");
        const auto serialized=iiSharedCanvas::encodeIisc(*document->document());
        check(serialized.ok(),"publication settings could not be encoded");
        const auto reopened=iiSharedCanvas::decodeIisc(serialized.bytes);
        check(reopened.ok() && std::get<std::string>(reopened.document.publicationMetadata->settings.at("publish.destination.x.sequence"))=="Separate posts" &&
              std::get<std::string>(reopened.document.publicationMetadata->settings.at("vendor.option"))=="kept",
              "publication settings or unknown metadata did not survive the IISC round trip");
        good(publish->select("facebook"));
        check(publish->plannedPosts.get()>0 && publish->plan().posts[0].post.text.find("Original")!=std::string::npos &&
              publish->plan().posts[0].post.text.find("Destination text")==std::string::npos,
              "the previous platform's text override leaked into another target");
        good(publish->select("x"));
        check(!publish->threaded.get(),"switching targets lost the saved platform setting");
        good(publish->setThreaded(true));
        check(publish->threaded.get() && publish->plan().posts[1].replyTo==std::optional<std::size_t>{0},
              "re-enabling thread did not restore the planned chain");
        bool redirected=false;
        auto redirect=publish->platformName.observe([&](const auto &name,std::uint64_t) {
            if (name=="Facebook" && !redirected) {redirected=true;good(publish->select("x"));}
        });
        good(publish->select("facebook"));
        bool selectedX=false,selectedFacebook=false;
        for (const auto &target:publish->targets.get()) {
            if (target->id=="x") selectedX=target->selected.get();
            if (target->id=="facebook") selectedFacebook=target->selected.get();
        }
        check(redirected && publish->platformName.get()=="X" && selectedX && !selectedFacebook &&
              publish->threaded.get(),"a reentrant target choice left inconsistent publication state");
        check(!publish->canPublish.get(),"review planning implicitly enabled an account or upload");
        check(!publish->select("unknown") && publish->platformName.get()=="X","unknown platform changed the selected target");
        good(document->setLayerOpacity("paint",0.5));
        check(publish->plannedPosts.get()==0 && publish->planText.get()=="Media not prepared",
              "a visual canvas change retained stale prepared publication media");
        auto previousDocumentField=publish->reviewField("title");
        iiSharedCanvas::Document other; other.extent={8,8};
        good(document->attach(other,"other.iisc"));
        check(publish->plannedPosts.get()==0 && publish->planText.get()=="Media not prepared" && !publish->hasFrame.get() &&
              publish->threaded.get() && !document->dirty.get(),
              "switching documents retained another document's prepared media");
        check(previousDocumentField && !previousDocumentField->edit("stale document") && !document->dirty.get(),
              "a field from the previous document edited the replacement");
        auto unknown=canvas;
        unknown.publicationMetadata->settings["publish.destination.x.sequence"]=false;
        unknown.publicationMetadata->settings["publish.destination.x.selected"]=std::string("unknown");
        good(document->attach(unknown,"unknown.iisc"));
        check(!x->selectionStatus.get().empty() && !document->dirty.get() &&
              std::get<std::string>(document->document()->publicationMetadata->settings.at("publish.destination.x.selected"))=="unknown",
              "an unrecognized destination selection was silently coerced");
        iiSocialIntegration::ReviewSource unknownSource;unknownSource.post.media.resize(1);
        good(publish->setReviewSource(std::move(unknownSource)));
        check(publish->planText.get()=="Saved publish structure is not recognized" &&
              std::holds_alternative<bool>(document->document()->publicationMetadata->settings.at("publish.destination.x.sequence")) &&
              !document->dirty.get(),"an unknown saved setting was silently replaced or used");
        auto invalidSequence=publish->reviewField("sequence");
        check(invalidSequence && !invalidSequence->status.get().empty() &&
              !invalidSequence->select(int(invalidSequence->options.get().size()-1)) && !document->dirty.get(),
              "an invalid saved sequence was silently selected or overwritten");
        good(publish->setTargetIncluded("x",true));
        check(x->included.get() && x->selectionStatus.get().empty() &&
              std::get<bool>(document->document()->publicationMetadata->settings.at("publish.destination.x.selected")),
              "an explicit destination choice did not repair the invalid selection");
        good(publish->setThreaded(true));
        check(publish->threaded.get() && publish->plannedPosts.get()==1 &&
              std::get<std::string>(document->document()->publicationMetadata->settings.at("publish.destination.x.sequence"))=="Thread",
              "an explicit thread selection did not replace the unknown setting");
        auto reply=publish->reviewField("replyControl"),disclosure=publish->reviewField("madeWithAi");
        check(reply && disclosure,"X-specific controls are missing");
        const auto replyChoices=reply->options.get();
        const auto following=std::ranges::find(replyChoices,std::string("following"));
        check(following!=replyChoices.end(),"X reply policy is missing");
        good(reply->select(int(following-replyChoices.begin())));good(disclosure->toggle(true));
        check(publish->plan().posts[0].post.replyControl=="following" && publish->plan().posts[0].post.madeWithAi,
              "X-specific review settings did not reach the publication plan");
        good(publish->select("mastodon"));
        auto warning=publish->reviewField("contentWarning"),sensitive=publish->reviewField("sensitive");
        check(warning && sensitive,"Mastodon-specific controls are missing");
        good(warning->edit("Sensitive illustration"));good(sensitive->toggle(true));
        check(publish->plan().posts[0].post.contentWarning=="Sensitive illustration" && publish->plan().posts[0].post.sensitive,
              "Mastodon-specific review settings did not reach the publication plan");
        iiSocialIntegration::ReviewSource video;video.post.title="Video";video.post.media.resize(1);
        video.post.media[0].kind=iiSocialIntegration::MediaKind::Video;
        good(publish->setReviewSource(std::move(video)));good(publish->select("youtube"));
        auto category=publish->reviewField("categoryId"),kids=publish->reviewField("madeForKids");
        auto synthetic=publish->reviewField("syntheticMedia"),notify=publish->reviewField("notifySubscribers");
        check(category && kids && synthetic && notify,"YouTube-specific controls are missing");
        good(category->edit("28"));
        const auto kidsChoices=kids->options.get();const auto no=std::ranges::find(kidsChoices,std::string("No"));
        check(no!=kidsChoices.end(),"YouTube audience choice is missing");
        good(kids->select(int(no-kidsChoices.begin())));good(synthetic->toggle(true));good(notify->toggle(false));
        check(!publish->plan().posts.empty() && publish->plan().posts[0].post.youtube.categoryId=="28" &&
              publish->plan().posts[0].post.youtube.madeForKids==std::optional<bool>{false} &&
              publish->plan().posts[0].post.youtube.containsSyntheticMedia==std::optional<bool>{true} &&
              !publish->plan().posts[0].post.youtube.notifySubscribers,
              "YouTube-specific review settings did not reach the publication plan");
        const auto optionsBytes=iiSharedCanvas::encodeIisc(*document->document());
        check(optionsBytes.ok(),"platform review settings could not be encoded");
        const auto optionsReloaded=iiSharedCanvas::decodeIisc(optionsBytes.bytes);
        check(optionsReloaded.ok() &&
              std::get<std::string>(optionsReloaded.document.publicationMetadata->settings.at("publish.destination.x.replyControl"))=="following" &&
              std::get<bool>(optionsReloaded.document.publicationMetadata->settings.at("publish.destination.mastodon.sensitive")) &&
              std::get<std::string>(optionsReloaded.document.publicationMetadata->settings.at("publish.destination.youtube.categoryId"))=="28" &&
              !std::get<bool>(optionsReloaded.document.publicationMetadata->settings.at("publish.destination.youtube.notifySubscribers")),
              "platform review settings did not survive an IISC round trip");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
