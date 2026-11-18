#include "App/Views/Publishing/NativePublishMedia.h"
#include "App/Views/Publishing/NativePublishWorkspace.h"
#include "App/Native/NativeDocumentSession.h"
#include <iiFileProvider>
#include <filesystem>
#include <iostream>
#include <stdexcept>

class ManualExecutor final : public iiSharedCanvas::FrameRenderExecutor {
  public:
    void post(std::function<void()> task) override {tasks.push_back(std::move(task));}
    std::size_t maxConcurrency() const noexcept override {return 1;}
    std::vector<std::function<void()>> tasks;
};
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        iiFileProvider::native::TemporaryDirectory fixture(std::filesystem::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(fixture.isValid(),"cannot create publication model fixture");
        ii::runtime::EventLoop loop;auto worker=std::make_shared<ManualExecutor>();
        auto document=native::DocumentSession::create();iiSharedCanvas::Document canvas;canvas.extent={2,2};canvas.timeline.frameCount=2;
        canvas.assets.emplace_back(iiSharedCanvas::RasterAsset{"pixel",makeRasterLayer(2,2,0xff123456)});
        canvas.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"paint","Paint"},iiSharedCanvas::StaticSource{"pixel"}});
        good(document->attach(canvas,"publication.iisc"));const auto initial=document->snapshot();
        auto model=publishing::PublicationMediaModel::create(document,loop,worker,fixture.path());
        check(worker->tasks.empty(),"hidden publication media prepared work");
        model->setVisible(true);check(model->busy.get() && worker->tasks.size()==1 && document->snapshot()==initial,
                                      "publication preparation did not start on a worker");
        auto step=[&] {check(!worker->tasks.empty(),"missing publication worker");auto task=std::move(worker->tasks.front());
            worker->tasks.erase(worker->tasks.begin());task();(void)loop.poll();};
        step();check(model->prepared.get() && !model->busy.get() && !document->dirty.get(),"publication media did not arrive cleanly");
        const auto first=model->prepared.get();const auto path=first->source.post.media.front().localFile;
        check(std::filesystem::is_regular_file(path),"publication stage was unavailable");
        auto workspace=publishing::WorkspaceModel::create(document);good(workspace->select("x"));
        workspace->setVisible(true);
        bool detached=false;
        auto reentrant=workspace->plannedPosts.observe([&](auto count,std::uint64_t) {
            if (count==1 && !detached) {detached=true;workspace->setMediaModel({});}
        });
        workspace->setMediaModel(model);
        check(detached && workspace->plannedPosts.get()==0 && workspace->planText.get()=="Media not prepared",
              "a reentrant detach left stale publication state");
        reentrant={};
        model->setVisible(true);check(worker->tasks.size()==1,"reattaching did not prepare the publication again");
        step();workspace->setMediaModel(model);
        const auto reattached=model->prepared.get();const auto reattachedPath=reattached->source.post.media.front().localFile;
        check(workspace->plannedPosts.get()==1 && workspace->plan().posts.front().post.media.front().localFile==reattachedPath,
              "the publishing viewmodel did not receive its actual prepared canvas");
        good(document->editPublication("title","Updated title"));
        check(model->prepared.get()==reattached && worker->tasks.empty() && workspace->plan().posts.front().post.title=="Updated title",
              "a title edit rerendered media or failed to update the publication plan");
        good(document->seek(1));check(!model->prepared.get() && model->busy.get(),"seeking retained old-frame media");
        check(workspace->plannedPosts.get()==0,"the publishing view retained a stale frame plan");
        good(document->seek(0));check(worker->tasks.size()==1,"rapid seek started an unbounded number of workers");
        step();check(!model->prepared.get() && worker->tasks.size()==1,"stale frame work was accepted or the latest frame was not scheduled");
        step();check(model->prepared.get() && model->prepared.get()->frame==0 && workspace->plannedPosts.get()==1,
                     "the latest frame did not win publication preparation");
        workspace->setVisible(false);check(!model->prepared.get() && workspace->plannedPosts.get()==0,
                                           "hiding publication retained its prepared view state");
        workspace->setMediaModel({});model->close();
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
