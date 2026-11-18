#include "App/Views/Publishing/NativePublicationService.h"
#include "App/Views/Publishing/NativePublishWorkspace.h"
#include "App/Native/NativeApplicationShell.h"
#ifdef DREAMSCAPES_PUBLICATION_WIDGET_TEST
#include "App/Views/Publishing/NativePublishWorkspaceView.h"
#include <iiGUI/WidgetState.h>
#endif
#include <iiSDKCore>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition,const char *message) {if (!condition) throw std::runtime_error(message);}
void good(ii::core::Result<void> result) {if (!result) throw std::runtime_error(result.error().message);}
template<class T> T take(ii::core::Result<T> result) {if (!result) throw std::runtime_error(result.error().message);return std::move(*result);}
#ifdef DREAMSCAPES_PUBLICATION_WIDGET_TEST
ii::gui::Widget find(const std::shared_ptr<ii::gui::detail::WidgetState> &state,std::string_view id) {
    for (const auto &node:take(state->snapshots()))
        if (node.id==id || node.id.ends_with(std::string("/")+std::string(id))) return take(state->find(node.id));
    throw std::runtime_error("Missing publication widget");
}
#endif
struct FakeTransport final : iiSocialIntegration::Transport {
    int requests=0;
    iiSocialIntegration::Response send(const iiSocialIntegration::Request &request,std::stop_token) override {
        ++requests;
        if (request.url.ends_with("/api/v2/instance"))
            return {200,{},R"({"configuration":{"statuses":{"max_media_attachments":4,"max_characters":500}}})"};
        if (request.method=="POST" && request.url.ends_with("/api/v2/media")) {
            check(std::ranges::any_of(request.parts,[](const auto &part){return part.file.has_value();}),
                  "image bytes were not attached to the upload");
            return {200,{},R"({"id":"456"})"};
        }
        if (request.method=="POST" && request.url.ends_with("/api/v1/statuses"))
            return {200,{},R"({"id":"123","url":"https://example.social/@user/123"})"};
        throw std::runtime_error("Unexpected request");
    }
};
}
int main() {
    try {
        using namespace dreamscapes::publishing;
        using namespace iiSocialIntegration;
#if defined(_WIN32)
        _putenv_s("DREAMSCAPES_PUBLISH_MASTODON_ACCESS_TOKEN","fixture-env-token");
        _putenv_s("DREAMSCAPES_PUBLISH_MASTODON_SCOPES","write:statuses,write:media");
#else
        setenv("DREAMSCAPES_PUBLISH_MASTODON_ACCESS_TOKEN","fixture-env-token",1);
        setenv("DREAMSCAPES_PUBLISH_MASTODON_SCOPES","write:statuses,write:media",1);
#endif
        auto environmentAccounts=publicationAccountsFromEnvironment();
        check(environmentAccounts.contains("mastodon") && environmentAccounts.at("mastodon").scopes.size()==2 &&
              (!std::getenv("DREAMSCAPES_PUBLISH_MASTODON_ACCESS_TOKEN") ||
               !*std::getenv("DREAMSCAPES_PUBLISH_MASTODON_ACCESS_TOKEN")),
              "session grant was not loaded or remained available for child processes");
        ii::runtime::EventLoop loop;
        std::vector<ii::runtime::Task> tasks;
        auto transport=std::make_shared<FakeTransport>();
        auto service=PublicationService::create(loop,[&](ii::runtime::Task task){tasks.push_back(std::move(task));return true;},transport);
        Account account;account.accessToken="fixture-token";account.instanceUrl="https://example.social";account.scopes={"write:statuses","write:media"};
        good(service->configure("mastodon",account));
        check(service->hasAccount("mastodon") && !service->hasAccount("x"),"configured accounts were not tracked");
        ReviewSource source;source.post.text="Hello";
        auto plan=planPublication(Platform::Mastodon,source);
        check(plan.ready(),"test plan is invalid");
        auto lease=std::make_shared<int>(42);std::weak_ptr<int> retained=lease;
        good(service->submit({{"mastodon",plan}},lease));lease.reset();
        check(service->busy.get() && tasks.size()==1 && transport->requests==0,"publishing did not defer to the worker");
        check(!retained.expired(),"publication did not retain its media lease");
        tasks.front()();tasks.clear();(void)loop.poll();
        const auto result=service->outcomes.get();
        check(!service->busy.get() && result && result->size()==1 && result->front().published &&
              result->front().postId=="123" && transport->requests==3,"provider outcome did not reach the owner");
        check(retained.expired(),"completed publication retained its media lease");
        check(service->status.get().find("fixture-token")==std::string::npos,"credential appeared in status");
        Account missingScope=account;missingScope.scopes.clear();good(service->configure("mastodon",missingScope));
        good(service->submit({{"mastodon",plan}},{}));tasks.front()();tasks.clear();(void)loop.poll();
        check(transport->requests==3 && !service->outcomes.get()->front().published,
              "invalid grant reached the network or appeared successful");
        service->close();
        auto shell=dreamscapes::native::NativeApplicationShell::create("<svg xmlns=\"http://www.w3.org/2000/svg\"/>");
        good(shell->connectPublication(loop,[&](ii::runtime::Task task){tasks.push_back(std::move(task));return true;},
                                       {{"mastodon",account}},transport));
        auto connected=shell->publicationService();auto document=shell->documentSession();
        iiSharedCanvas::Document canvas;canvas.extent={8,8};
        canvas.publicationMetadata=iiSharedCanvas::PublicationMetadata{.description="Hello"};
        good(document->attach(canvas,"publish.iisc"));
        auto workspace=shell->publishingWorkspace();
        auto imageSource=source;
        const auto image=std::filesystem::path(__FILE__).parent_path().parent_path()/"resources/Appicon/Artboard 1.png";
        check(std::filesystem::is_regular_file(image),"publication image fixture is missing");
        imageSource.post.media.push_back({MediaKind::Image,image,{},"image/png","Canvas"});
        good(workspace->setTargetIncluded("mastodon",true));good(workspace->setReviewSource(imageSource));
        check(workspace->canPublish.get(),"configured selected target did not enable Publish");
        const auto revision=document->revision.get();
#ifdef DREAMSCAPES_PUBLICATION_WIDGET_TEST
        ii::gui::detail::WidgetFrameHooks hooks;hooks.events=[&]() -> ii::core::Result<ii::runtime::EventLoop *> {return &loop;};
        auto state=std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task){task();},ii::gui::WidgetAppearance{},hooks);
        auto root=take(state->content_host({},"root",{1200,900}));
        good(root.set_content([&](ii::gui::WidgetBuilder &builder){return buildWorkspace(builder,{},workspace);}));
        (void)take(state->drawing_snapshot({1200,900}));
        good(find(state,"publishAction").activate());
#else
        good(workspace->publish());
#endif
        check(!workspace->canPublish.get() && tasks.size()==1,"Publish did not start the worker");
        tasks.front()();tasks.clear();(void)loop.poll();
        auto row=std::ranges::find_if(workspace->targets.get(),[](const auto &item){return item->id=="mastodon";});
        check(row!=workspace->targets.get().end() && (*row)->resultStatus.get()=="Published" &&
              document->revision.get()==revision && transport->requests==7,
              "workspace did not present the provider result or modified the IISC");
        good(workspace->setTargetIncluded("x",true));
        check(!workspace->canPublish.get(),"a target without a grant was enabled");
        Account x;x.accessToken="fixture-x";x.allowBillableRequests=true;
        good(connected->configure("x",x));check(workspace->canPublish.get(),"configured batch did not enable Publish");
        good(workspace->publish());tasks.front()();tasks.clear();(void)loop.poll();
        check(transport->requests==7 && connected->outcomes.get()->size()==2,
              "invalid later target did not prevent the entire batch from contacting providers");
#ifdef DREAMSCAPES_PUBLICATION_WIDGET_TEST
        state->close();
#endif
        shell->disconnectPublication();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
