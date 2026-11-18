#include "App/Native/NativeSocietyStorage.h"
#include <iiFileProvider>
#include <iiJson>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iostream>
#include <stdexcept>

namespace fs=std::filesystem;
using namespace dreamscapes;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void put(const fs::path &path,const vm::Value &value) {
    auto encoded=ii::json::stringify(value); check(bool(encoded),"could not encode fixture");
    iiFileProvider::native::File::write(path,*encoded);
}
int main() {
    try {
        iiFileProvider::native::TemporaryDirectory temp(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temp.isValid(),"fixture unavailable");
        const auto root=temp.path()/"test.societycontainer",selection=temp.path()/"selection.json";
        fs::create_directories(root/"Files"/"Nested");
        fs::create_directories(root/"Files"/"Empty");
        fs::create_directories(root/"Asset Library");
        fs::create_directories(root/"Generation History");
        iiFileProvider::native::File::write(root/"Files"/"Alpha.iisc","fixture");
        iiFileProvider::native::File::write(root/"Files"/"Nested"/"Beta.iisc","fixture");
        std::error_code ec; fs::create_symlink(root/"Files"/"Alpha.iisc",root/"Files"/"link.iisc",ec);
        constexpr auto id="aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee";
        vm::Array sections{
            vm::Value(vm::Object{{"id",vm::Value("files")},{"path",vm::Value("Files")}}),
            vm::Value(vm::Object{{"id",vm::Value("asset-library")},{"path",vm::Value("Asset Library")}}),
            vm::Value(vm::Object{{"id",vm::Value("generation-history")},{"path",vm::Value("Generation History")}})};
        auto manifest=[&](bool ready) {put(root/".society-drive.json",vm::Value(vm::Object{{"type",vm::Value("SocietyDrive")},
            {"schemaVersion",vm::Value(1)},{"identifier",vm::Value(id)},{"replicaReady",vm::Value(ready)},{"sections",vm::Value(sections)}}));};
        manifest(true);
        put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value(id)}}));
        auto result=native::browseSociety(selection,"files","","");
        check(bool(result),"valid local container did not open");
        check(result->root==root && result->sectionPath==root/"Files" && vm::array(result->aliases).size()==3,
              "root listing lost its verified section path, files or empty directories, or included symlinks");
        check(!native::browseSociety(selection,"files","","",root/"other"),
              "a conflicting explicit root silently opened the selected container");
        check(bool(native::browseSociety(selection,"files","","",root)),
              "an explicit root matching the selected container was rejected");
        auto nested=native::browseSociety(selection,"files","Nested","");
        check(nested && vm::array(nested->aliases).size()==1 &&
              vm::text(vm::get(vm::array(nested->aliases).front(),"relativePath"))=="Nested/Beta.iisc",
              "folder listing did not preserve relative path");
        auto search=native::browseSociety(selection,"files","","beta");
        check(search && vm::array(search->aliases).size()==1 &&
              vm::text(vm::get(vm::array(search->aliases).front(),"name"))=="Beta.iisc",
              "recursive search did not find nested file");
        std::tm day{};day.tm_year=124;day.tm_mon=0;day.tm_mday=15;day.tm_hour=23;day.tm_min=59;day.tm_sec=59;day.tm_isdst=-1;
        const auto localInstant=std::mktime(&day);check(localInstant!=std::time_t(-1),"cannot prepare a local date fixture");
        const auto systemTime=std::chrono::system_clock::from_time_t(localInstant)+std::chrono::milliseconds(900);
#ifdef _MSC_VER
        const auto fileTime=std::chrono::clock_cast<fs::file_time_type::clock>(systemTime);
#else
        const auto fileTime=fs::file_time_type::clock::from_sys(systemTime);
#endif
        fs::last_write_time(root/"Files"/"Alpha.iisc",fileTime-std::chrono::hours(24));
        fs::last_write_time(root/"Files"/"Nested"/"Beta.iisc",fileTime);
        auto recent=native::browseSocietyRecentFiles(selection,1);
        check(recent && vm::array(recent->aliases).size()==1 &&
              vm::text(vm::get(vm::array(recent->aliases).front(),"relativePath"))=="Nested/Beta.iisc",
              "recent files did not find and order the newest nested local file");
        check(vm::text(vm::get(vm::array(recent->aliases).front(),"dateText"))=="2024-01-15",
              "recent file modification date was omitted or rounded into the next day");
        recent=native::browseSocietyRecentFiles(selection,20);
        check(recent && vm::array(recent->aliases).size()==2,"recent files included directories or redirected entries");
        check(!native::browseSociety(selection,"files","../Asset Library",""),"directory traversal was accepted");
        check(!native::browseSociety(selection,"unknown","",""),"unknown section was accepted");
        check(!native::browseSociety("selection.json","files","",""),"relative selection path was accepted");
        manifest(false);
        check(!native::browseSociety(selection,"files","",""),"unready replica appeared available");
        manifest(true);
        sections[0]=vm::Value(vm::Object{{"id",vm::Value("files")},{"path",vm::Value("../")}});
        manifest(true);
        check(!native::browseSociety(selection,"files","",""),"unsafe manifest section path was accepted");
        sections[0]=vm::Value(vm::Object{{"id",vm::Value("files")},{"path",vm::Value("Files")}});
        manifest(true);
        put(selection,vm::Value(vm::Object{{"schemaVersion",vm::Value(1)},{"path",vm::Value(root.string())},{"containerId",vm::Value("bbbbbbbb-cccc-4ddd-8eee-ffffffffffff")}}));
        check(!native::browseSociety(selection,"files","",""),"wrong container identity was accepted");
        check(fs::file_size(root/"Files"/"Alpha.iisc")==7,"browsing modified fixture content");
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
