#include "App/Native/NativeReviewFileSave.h"
#include <iiFileProvider>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fs=std::filesystem;
using namespace dreamscapes;
using namespace dreamscapes::native;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
std::string revision(const fs::path &path) {
    return std::to_string(std::chrono::duration_cast<std::chrono::microseconds>(
        fs::last_write_time(path).time_since_epoch()).count())+"-"+std::to_string(fs::file_size(path));
}
vm::Value entry(const fs::path &path) {
    return vm::Value(vm::Object{{"path",vm::Value(path.string())},{"sourceRevision",vm::Value(revision(path))}});
}
int main() {
    try {
        iiFileProvider::native::TemporaryDirectory temp(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temp.isValid(),"cannot create a review save fixture");
        const auto source=temp.path()/"source.png", destination=temp.path()/"saved.png";
        iiFileProvider::native::File::write(source,"original-png-bytes");
        const auto original=entry(source);
        ii::runtime::EventLoop loop; std::vector<ii::runtime::Task> jobs;
        auto model=ReviewFileSave::create(loop,[&](ii::runtime::Task job) {jobs.push_back(std::move(job));return true;});
        ii::gui::FileDialogOptions options; ii::gui::FileDialogCompletion complete;
        model->setPicker([&](auto value,auto callback) {options=std::move(value);complete=std::move(callback);return ii::core::Result<void>{};});
        auto run=[&] {check(jobs.size()==1,"expected one worker job");auto job=std::move(jobs.front());jobs.clear();job();(void)loop.poll();};
        check(model->canSave(original),"a local original was not eligible");
        check(!model->canSave(vm::Value(vm::Object{{"imageSource",vm::Value("https://example.org/preview.png")}})),
              "a remote preview was treated as an original");
        check(!model->canSave(vm::Value(vm::Object{{"path",vm::Value("/tmp/unavailable.iisc")}})),
              "an unverified canvas archive was treated as a media original");
        good(model->save(original));
        check(model->busy.get() && jobs.empty() && options.mode==ii::gui::FileDialogMode::save &&
              options.filename=="source.png", "save did not open the native picker without filesystem work");
        complete(ii::gui::FileDialogSelection{.paths={destination.string()}});
        check(jobs.size()==1 && !fs::exists(destination),"copy ran on the view thread");
        run();
        check(!model->busy.get() && model->status.get()=="Result saved" &&
              iiFileProvider::native::File::read(destination)=="original-png-bytes" &&
              iiFileProvider::native::File::read(source)=="original-png-bytes", "the original was not copied safely");

        iiFileProvider::native::File::write(destination,"untouched-existing");
        good(model->save(original)); complete(ii::gui::FileDialogSelection{.paths={destination.string()}});run();
        check(!model->status.get().empty() && model->status.get()!="Result saved" &&
              iiFileProvider::native::File::read(destination)=="untouched-existing", "existing output was overwritten");
        good(model->save(original));complete(ii::gui::FileDialogSelection{.paths={source.string()}});run();
        check(model->status.get()!="Result saved" && iiFileProvider::native::File::read(source)=="original-png-bytes",
              "saving over the source changed the original");
        const auto noExtension=temp.path()/"extension-added";
        good(model->save(original));complete(ii::gui::FileDialogSelection{.paths={noExtension.string()}});run();
        check(iiFileProvider::native::File::read(temp.path()/"extension-added.png")=="original-png-bytes",
              "the original extension was not supplied to a destination without one");
        good(model->save(original)); complete(ii::gui::FileDialogSelection{.paths={(temp.path()/"wrong.jpg").string()}});
        check(jobs.empty() && !model->busy.get() && !fs::exists(temp.path()/"wrong.jpg"),"copy changed the file format");
        good(model->save(original)); complete(ii::gui::FileDialogSelection{.canceled=true});
        check(jobs.empty() && !model->busy.get(),"canceling the picker performed a save");

        iiFileProvider::native::File::write(source,"replacement-with-different-size");
        const auto staleDestination=temp.path()/"stale.png";
        good(model->save(original)); complete(ii::gui::FileDialogSelection{.paths={staleDestination.string()}});run();
        check(!fs::exists(staleDestination) && model->status.get()!="Result saved", "a stale source revision was saved");
        const auto fresh=entry(source);
        good(model->save(fresh));
        auto retired=complete; model->setPicker({}); retired(ii::gui::FileDialogSelection{.paths={(temp.path()/"retired.png").string()}});
        check(!model->busy.get() && jobs.empty(),"a retired picker callback started a copy");
        model->setPicker([&](auto,auto callback) {complete=std::move(callback);return ii::core::Result<void>{};});
        good(model->save(fresh)); complete(ii::gui::FileDialogSelection{.paths={(temp.path()/"closed.png").string()}});
        model->close();run();
        check(!fs::exists(temp.path()/"closed.png"),"closed save model published queued work");
        auto unavailable=ReviewFileSave::create(loop,[](ii::runtime::Task) {return false;});
        unavailable->setPicker([&](auto,auto callback) {complete=std::move(callback);return ii::core::Result<void>{};});
        good(unavailable->save(fresh));complete(ii::gui::FileDialogSelection{.paths={(temp.path()/"unavailable.png").string()}});
        check(!unavailable->busy.get() && !unavailable->status.get().empty() && !fs::exists(temp.path()/"unavailable.png"),
              "unavailable worker left the view busy or published a file");
        unavailable->close();
    } catch (const std::exception &error) {std::cerr << error.what() << '\n';return 1;}
}
