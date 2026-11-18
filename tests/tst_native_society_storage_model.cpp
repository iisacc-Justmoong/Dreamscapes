#include "App/Native/NativeApplicationShell.h"
#include "App/Native/NativeSocietyStorageModel.h"
#include "App/Native/NativeSocietyModels.h"
#include "NativeAssets.h"
#include <iiFileProvider>
#include <iiJson>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
using namespace dreamscapes;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> result) {if (!result) throw std::runtime_error(result.error().message);}
void put(const fs::path &path,const vm::Value &value) {
    auto text=ii::json::stringify(value); check(bool(text),"fixture encoding failed");
    iiFileProvider::native::File::write(path,*text);
}
int main() {
    try {
        iiFileProvider::native::TemporaryDirectory temp(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temp.isValid(),"fixture unavailable");
        const auto root=temp.path()/"test.societycontainer",selection=temp.path()/"selection.json";
        fs::create_directories(root/"Files"/"Nested"); fs::create_directories(root/"Asset Library");
        fs::create_directories(root/"Generation History");
        fs::create_directories(root/"Models"/"Checkpoint"/"LTX");
        const std::string modelHeader=R"({"__metadata__":{"_class_name":"StableDiffusionPipeline"},"tensor":{}})";
        std::string modelBytes(8,'\0');for (unsigned i=0;i<8;++i) modelBytes[i]=char((std::uint64_t(modelHeader.size())>>(8*i))&255);
        iiFileProvider::native::File::write(root/"Models"/"Checkpoint"/"image.safetensors",modelBytes+modelHeader);
        put(root/"Models"/"Checkpoint"/"LTX"/"model_index.json",vm::Value(vm::Object{{"_class_name",vm::Value("LTX2Pipeline")}}));
        iiSharedCanvas::Document document; document.extent={4,3};
        check(bool(native::createCanvasFile(root/"Files"/"sample.iisc",document)),"canvas fixture failed");
        iiFileProvider::native::File::write(root/"Files"/"Nested"/"found.txt","sample");
        iiFileProvider::native::File::write(root/"Generation History"/"real.png","image fixture");
        constexpr auto id="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        vm::Array sections{
            vm::Value(vm::Object{{"id",vm::Value("files")},{"path",vm::Value("Files")}}),
            vm::Value(vm::Object{{"id",vm::Value("asset-library")},{"path",vm::Value("Asset Library")}}),
            vm::Value(vm::Object{{"id",vm::Value("generation-history")},{"path",vm::Value("Generation History")}}),
            vm::Value(vm::Object{{"id",vm::Value("models")},{"path",vm::Value("Models")}})};
        put(root/".society-drive.json",vm::Value(vm::Object{{"type",vm::Value("SocietyDrive")},
            {"schemaVersion",vm::Value(1)},{"identifier",vm::Value(id)},{"sections",vm::Value(sections)}}));
        put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value(id)}}));
        ii::runtime::EventLoop loop; std::vector<ii::runtime::Task> jobs;
        auto submit=[&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;};
        auto run=[&] {check(jobs.size()==1,"expected one disk operation"); auto task=std::move(jobs.front()); jobs.clear(); task(); (void)loop.poll();};
        auto runNext=[&] {check(!jobs.empty(),"expected a background operation");auto task=std::move(jobs.front());jobs.erase(jobs.begin());task();(void)loop.poll();};
        auto shell=native::NativeApplicationShell::create(native::queueReadySvg);
        good(shell->connectCanvasFiles(loop,submit));
        good(shell->connectSocietyStorage(loop,submit,selection));
        check(jobs.size()==4,"home did not request storage, recent files, models and history on workers");
        runNext();runNext();runNext();runNext();
        check(shell->composer()->snapshot.get().modelId=="Checkpoint/image.safetensors" &&
              shell->generation(false)->field("model")->enabled.get(),"local image model did not reach Home and Advanced");
        good(shell->composer()->dispatch(home::HomeComposerCommandKind::chooseMedia,"Video"));
        check(shell->composer()->snapshot.get().modelId=="Checkpoint/LTX" &&
              shell->generation(true)->field("model")->enabled.get(),"local LTX model did not reach Video");
        good(shell->composer()->dispatch(home::HomeComposerCommandKind::chooseMedia,"Image"));
        auto recentFiles=shell->recentFiles(); auto selectedFile=shell->selectedFile();
        std::string recentCanvas,recentText;
        for (const auto &row:recentFiles->rows.get()) {
            if (row.name=="sample.iisc") {recentCanvas=row.token;check(row.date!="Unavailable","Home hid the file's modification date");}
            if (row.name=="found.txt") recentText=row.token;
        }
        check(!recentText.empty(),"Home did not display a nested recent file");
        check(!recentCanvas.empty(),"Home did not display the real recent canvas");
        good(recentFiles->selectToken(recentCanvas));
        bool refreshFromSelection=false;
        auto refreshObserver=selectedFile->name.observe([&](const auto &name,std::uint64_t) {
            if (name=="found.txt" && !refreshFromSelection) {refreshFromSelection=true;good(shell->navigate("home"));}
        });
        good(recentFiles->selectToken(recentText));
        check(refreshFromSelection && jobs.size()==1 && !shell->workspace()->hasError.get(),
              "reentrant Home refresh interrupted recent-file selection or reported a spurious error");
        run();refreshObserver={};
        check(selectedFile->name.get()=="found.txt" && !selectedFile->canOpen.get(),
              "unsupported file type was presented as openable in the canvas editor");
        good(recentFiles->selectToken(recentCanvas));
        check(selectedFile->name.get()=="sample.iisc" && selectedFile->canOpen.get() && !selectedFile->previewSource.get().empty(),
              "selected-file panel did not receive the chosen real canvas");
        selectedFile->open();run();
        check(shell->documentSession()->hasDocument.get() && shell->documentSession()->title.get()=="sample.iisc",
              "the Home selected-file action did not open the real canvas");
        check(bool(native::createCanvasFile(root/"Files"/"Nested"/"new.iisc",document)),"new canvas fixture failed");
        good(shell->navigate("home"));run();
        check(std::ranges::any_of(recentFiles->rows.get(),[](const auto &row){return row.name=="new.iisc";}),
              "returning Home did not refresh recent files");
        auto recent=shell->recentGenerations();
        check(recent->rows.get().size()==1 && recent->rows.get()[0].name=="real.png" && !recent->rows.get()[0].preview.empty(),
              "home recent generations did not receive the actual local history and preview");
        good(recent->activate(recent->rows.get()[0].token));
        check(shell->historySession()->focusedIndex()==0 && jobs.size()==1,"a recent result did not open and focus history");
        run();good(shell->navigate("home"));run();
        iiFileProvider::native::File::write(root/"Generation History"/"another.png","second image");
        good(shell->navigate("history"));run();good(shell->navigate("home"));run();
        check(recent->rows.get().size()==2,"a new history file did not reach Home");
        good(recent->toggleMode());
        const auto first=recent->rows.get()[0].token,second=recent->rows.get()[1].token;
        good(recent->activate(first));good(recent->activate(second));recent->editSelection();
        check(shell->historySession()->selectedCount()==2 && jobs.size()==1,
              "home review did not transfer the selected real results to history");
        run();check(shell->historySession()->selectedCount()==2,"history refresh lost the transferred selection");
        good(shell->navigate("home"));run();
        auto model=shell->storageWorkspace();
        good(model->refresh());
        check(model->rows.get().empty() && jobs.size()==1,"storage read happened on the view thread");
        good(model->setQuery("found"));
        check(jobs.size()==1,"rapid requests were not coalesced");
        run();
        check(model->rows.get().empty() && jobs.size()==1,"an old response replaced newer search state");
        run();
        check(model->available.get() && model->rows.get().size()==1 && model->rows.get()[0]->name=="found.txt",
              "latest recursive search did not reach the view model");
        good(model->setQuery("")); run();
        check(model->rows.get().size()==2,"root entries did not reach the view model");
        std::shared_ptr<storage::Row> canvas;
        for (const auto &row:model->rows.get()) if (row->name=="sample.iisc") canvas=row;
        check(bool(canvas),"canvas missing from real listing");
        good(model->activate(canvas->key)); check(model->canOpen.get(),"local canvas was not openable");
        good(model->openSelected()); run();
        check(shell->documentSession()->hasDocument.get() && shell->documentSession()->title.get()=="sample.iisc",
              "storage row did not open the real canvas");
        good(shell->preferencesWorkspace()->select("Storage"));
        check(shell->preferencesWorkspace()->available.get() && !shell->preferencesWorkspace()->fields.get().empty(),
              "selected storage path did not reach preferences");
        shell->reviewSession()->replaceEntries({vm::Value(vm::Object{{"id",vm::Value("running")},
            {"imageSource",vm::Value("file:///current.png")},{"mediaType",vm::Value("Image")}})});
        good(shell->navigate("history"));
        check(shell->hasPageStatus.get() && jobs.size()==1 && shell->historySession()->totalCount()==2,
              "history did not start a separate background request");
        run();
        check(!shell->hasPageStatus.get() && shell->historySession()->totalCount()==2 && shell->reviewSession()->totalCount()==1,
              "history was not connected to its own real Society session");
        good(shell->navigate("history")); good(shell->navigate("home")); runNext();runNext();
        check(!shell->hasPageStatus.get(),"a history completion changed the status on another view");
        good(model->setQuery("late"));
        shell->disconnectSocietyStorage(); run();
        check(!model->available.get(),"closed provider accepted an outstanding result");
        check(recentFiles->showEmpty.get() && recentFiles->emptyMessage.get()=="Storage unavailable" && !selectedFile->hasFile.get(),
              "disconnecting storage retained a stale recent file or successful empty state");
        check(shell->composer()->snapshot.get().choices.models.empty() && !shell->composer()->snapshot.get().errorText.empty(),
              "disconnecting storage left its models selectable");
        shell->disconnectCanvasFiles();
        auto deniedWorkspace=storage::WorkspaceModel::create();
        auto denied=native::SocietyStorageModel::create(deniedWorkspace,loop,[](ii::runtime::Task){return false;},selection);
        bool deniedReported=false;
        denied->setListingHandler([&](const auto &result){deniedReported=!result;});
        good(deniedWorkspace->refresh());
        check(deniedReported && !deniedWorkspace->available.get(),"rejected model worker did not report failure to its consumer");
        denied->close();
        auto canceledWorkspace=storage::WorkspaceModel::create();
        std::vector<ii::runtime::Task> pendingModels;
        auto canceled=native::SocietyStorageModel::create(canceledWorkspace,loop,[&](ii::runtime::Task task){pendingModels.push_back(std::move(task));return true;},
            selection,{},[](const auto &path,const auto &,const auto &,const auto &,const auto &root,native::SocietyStorageModel::Canceled stop) {
                return native::browseSocietyModels(path,root,std::move(stop));
            });
        bool canceledDelivered=false;canceled->setListingHandler([&](const auto &){canceledDelivered=true;});
        good(canceledWorkspace->refresh());check(pendingModels.size()==1,"model scan was not dispatched");
        canceled->close();pendingModels.front()();(void)loop.poll();
        check(!canceledDelivered && !canceledWorkspace->available.get(),"closed model provider accepted a late result");
        auto failingWorkspace=storage::WorkspaceModel::create();std::vector<ii::runtime::Task> failingJobs;
        auto failing=native::SocietyStorageModel::create(failingWorkspace,loop,[&](ii::runtime::Task task){failingJobs.push_back(std::move(task));return true;},
            selection,{},[](const auto &,const auto &,const auto &,const auto &,const auto &,native::SocietyStorageModel::Canceled) -> ii::core::Result<native::SocietyListing> {
                throw std::runtime_error("fixture listing failure");
            });
        bool failureReported=false;failing->setListingHandler([&](const auto &result){failureReported=!result;});
        good(failingWorkspace->refresh());check(failingJobs.size()==1,"injected listing was not dispatched");
        failingJobs.front()();(void)loop.poll();
        check(failureReported && !failingWorkspace->available.get(),"a failed listing adapter escaped its worker or appeared available");
        failing->close();
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
