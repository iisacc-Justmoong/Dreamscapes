#include "App/Native/NativeSocietyHistory.h"
#ifdef DREAMSCAPES_HISTORY_IMAGE_TEST
#include "App/Native/NativeFileImages.h"
#include <iiSDKCore/Runtime/Runtime.h>
#include <thread>
#endif
#include <iiFileProviderNative.h>
#include <iiJson/Json.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace fs=std::filesystem;
using namespace dreamscapes;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
void put(const fs::path &path,const vm::Value &value) {
    auto text=ii::json::stringify(value); check(bool(text),"fixture encoding failed");
    iiFileProvider::native::File::write(path,*text);
}
int main() {
    try {
        iiFileProvider::native::TemporaryDirectory temp(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temp.isValid(),"fixture unavailable");
        const auto root=temp.path()/"history.societycontainer", selection=temp.path()/"selection.json";
        const auto history=root/"Generation History";
        fs::create_directories(history/"nested");
        iiFileProvider::native::File::write(history/"older.png","older");
#ifdef DREAMSCAPES_HISTORY_IMAGE_TEST
        fs::copy_file(fs::path(DREAMSCAPES_TEST_RESOURCES)/"Appicon/generated/android/res/mipmap-mdpi/ic_launcher.png",history/"new #1.PNG");
#else
        iiFileProvider::native::File::write(history/"new #1.PNG","new");
#endif
        iiFileProvider::native::File::write(history/"clip.mp4","video");
        iiFileProvider::native::File::write(history/"ignored.txt","other");
        iiFileProvider::native::File::write(history/".hidden.png","hidden");
        iiFileProvider::native::File::write(history/"nested"/"hidden.png","nested");
        std::error_code ec; fs::create_symlink(history/"older.png",history/"redirect.png",ec);
        const auto now=fs::file_time_type::clock::now();
        fs::last_write_time(history/"older.png",now-std::chrono::hours(2));
        fs::last_write_time(history/"new #1.PNG",now-std::chrono::hours(1));
        fs::last_write_time(history/"clip.mp4",now);
        constexpr auto id="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        vm::Array sections{vm::Value(vm::Object{{"id",vm::Value("generation-history")},{"path",vm::Value("Generation History")}})};
        auto manifest=[&](bool ready) {put(root/".society-drive.json",vm::Value(vm::Object{{"type",vm::Value("SocietyDrive")},
            {"schemaVersion",vm::Value(1)},{"identifier",vm::Value(id)},{"replicaReady",vm::Value(ready)},{"sections",vm::Value(sections)}}));};
        manifest(true);
        put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value(id)}}));
        ii::runtime::EventLoop loop; std::vector<ii::runtime::Task> jobs;
        auto submit=[&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;};
        auto run=[&] {check(jobs.size()==1,"expected one background history operation"); auto task=std::move(jobs.front()); jobs.clear(); task(); (void)loop.poll();};
        auto session=std::make_shared<review::Session>();
        auto historyModel=native::SocietyHistoryModel::create(session,loop,submit,selection);
        bool refreshDuringNotification=false;
        auto onAvailable=historyModel->available.observe([&](bool value,std::uint64_t) {
            if (value && !refreshDuringNotification) {refreshDuringNotification=true;good(historyModel->refresh());}
        });
        good(historyModel->refresh(2));
        check(historyModel->loading.get() && jobs.size()==1 && session->totalCount()==0,"history read happened on the view thread");
        run();
        check(session->totalCount()==2 && refreshDuringNotification && historyModel->loading.get() && jobs.size()==1 && !historyModel->status.get().empty(),
              "a newer refresh was overwritten by an earlier completion notification");
        run();
        check(historyModel->available.get() && !historyModel->loading.get() && session->totalCount()==3,
              "history did not contain exactly the actual local media");
        auto video=session->entryAt(0),image=session->entryAt(1);
        check(vm::text(vm::get(video,"mediaType"))=="Video" && vm::text(vm::get(video,"name"))=="clip.mp4" &&
              vm::text(vm::get(video,"thumbnailSource")).empty(),"history video or unavailable preview was misrepresented");
        check(vm::text(vm::get(image,"name"))=="new #1.PNG" && vm::text(vm::get(image,"mediaType"))=="Image" &&
              vm::text(vm::get(image,"thumbnailSource")).find("new%20%231.PNG")!=std::string::npos,
              "newest image or encoded preview path is incorrect");
        check(session->rowAt(1) && session->rowAt(1)->title=="new #1.PNG","history hid the actual file name");
#ifdef DREAMSCAPES_HISTORY_IMAGE_TEST
        {
            ii::runtime::Runtime workers({.workers=1,.capacity=8});
            native::NativeFileImages images(loop,workers);
            const auto source=vm::text(vm::get(image,"thumbnailSource"));
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            auto pixels=images.image(source,48,48);
            while (!pixels && std::chrono::steady_clock::now()<deadline) {
                (void)loop.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
                pixels=images.image(source,48,48);
            }
            check(bool(pixels),"the actual image loader could not decode a Society path containing spaces and #");
        }
#endif
        check(!vm::has(image,"modelName") && !vm::has(image,"prompt"),"history invented generation metadata");
        session->selectAt(1); const auto selected=session->selectedEntries();
        good(historyModel->refresh()); good(historyModel->refresh());
        check(jobs.size()==1,"history requests were not coalesced");
        run(); check(jobs.size()==1 && historyModel->loading.get(),"stale history completion became current");
        run(); check(session->selectedEntries()==selected,"history refresh lost the user's selection");
        bool retried=false;
        auto onFailure=historyModel->available.observe([&](bool value,std::uint64_t) {
            if (!value && !retried) {retried=true;manifest(true);good(historyModel->refresh());}
        });
        manifest(false); good(historyModel->refresh()); run();
        check(retried && historyModel->loading.get() && jobs.size()==1,
              "a retry requested during a failure notification was cleared by the older failure");
        run(); check(historyModel->available.get() && session->totalCount()==3,"retry did not restore the real history");
        onFailure={};
        manifest(false); good(historyModel->refresh()); run();
        check(!historyModel->available.get() && !historyModel->loading.get() && session->totalCount()==0 &&
              !historyModel->status.get().empty(),"unready history appeared as an empty successful list");
        manifest(true); good(historyModel->refresh()); historyModel->close(); run();
        check(!historyModel->available.get() && session->totalCount()==0,"closed history accepted a late result");
        check(iiFileProvider::native::File::read(history/"older.png",32)=="older","history mutated source media");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
