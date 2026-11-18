#include "App/Native/NativeGenerationRuntime.h"
#include <iiFileProvider>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace fs=std::filesystem;
void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace dreamscapes::native;
        iiFileProvider::native::TemporaryDirectory temp(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        const auto sdk=temp.path()/"SDK"/"iiLocalDiffusion",worker=sdk/"bin"/"iild-generate",alternate=temp.path()/"alternate";
        const auto configured=temp.path()/"configured"/"bin"/"iild-generate";
        fs::create_directories(worker.parent_path());iiFileProvider::native::File::write(worker,"fixture");
        fs::create_directories(configured.parent_path());iiFileProvider::native::File::write(configured,"fixture");
        iiFileProvider::native::File::write(alternate,"fixture");
        fs::permissions(worker,fs::perms::owner_exec,fs::perm_options::add);
        fs::permissions(configured,fs::perms::owner_exec,fs::perm_options::add);
        fs::permissions(alternate,fs::perms::owner_exec,fs::perm_options::add);
        auto command=findGenerationWorker({{},sdk,{},false});
        check(command && command->size()==1 && command->front()==worker.string(),"installed SDK worker was not discovered");
        command=findGenerationWorker({.sdkRoot=sdk,.configuredExecutable=configured,.requireConfiguredExecutable=false});
        check(command && command->size()==1 && command->front()==configured.string(),
            "the build-selected worker was replaced by the default SDK");
        command=findGenerationWorker({.explicitExecutable=alternate,.sdkRoot=sdk,.configuredExecutable=configured});
        check(command && command->size()==1 && command->front()==alternate.string(),"explicit worker did not take precedence");
        check(!findGenerationWorker({.explicitExecutable="relative",.sdkRoot=sdk,.configuredExecutable=configured}) &&
            !findGenerationWorker({.explicitExecutable=temp.path()/"missing",.sdkRoot=sdk,.configuredExecutable=configured}),
            "an invalid explicit worker was silently replaced");
        check(!findGenerationWorker({.sdkRoot=sdk,.configuredExecutable=temp.path()/"missing"}),
            "a missing build-selected worker silently fell back to the default SDK");
        command=findGenerationWorker({.sdkRoot=sdk,.configuredExecutable=temp.path()/"missing",
            .requireConfiguredExecutable=false});
        check(command && command->size()==1 && command->front()==worker.string(),
            "relocating the app made its SDK package hint mandatory");
        check(!findGenerationWorker({{},sdk,{},true}),"a Windows script without an interpreter was accepted");
        const auto python=temp.path()/"python.exe";iiFileProvider::native::File::write(python,"fixture");
        command=findGenerationWorker({{},sdk,python,true});
        check(command && command->size()==2 && command->front()==python.string() && command->back()==worker.string(),
            "Windows interpreter and worker were not represented as separate arguments");
        const auto exe=sdk/"bin"/"iild-generate.exe";iiFileProvider::native::File::write(exe,"fixture");
        command=findGenerationWorker({{},sdk,{},true});
        check(command && command->size()==1 && command->front()==exe.string(),"native Windows worker did not take precedence");
        ii::runtime::EventLoop loop;std::vector<ii::runtime::Task> tasks;
        std::optional<ii::core::Result<GenerationRuntimeDiscovery>> discovered;
        auto submit=[&](ii::runtime::Task task) {tasks.push_back(std::move(task));return true;};
        auto started=discoverGenerationRuntime(loop,submit,{{},sdk,{},false},temp.path(),
            [&](auto result) {discovered=std::move(result);});
        check(bool(started) && tasks.size()==1 && !discovered,"worker discovery blocked or completed on the view thread");
        auto task=std::move(tasks.front());tasks.clear();task();
        check(!discovered,"worker discovery delivered outside the owner event loop");
        (void)loop.poll();
        check(discovered && *discovered && (**discovered).command.front()==worker.string() &&
            (**discovered).stagingRoot==fs::canonical(temp.path()),"asynchronous worker discovery lost its command or staging root");
        discovered.reset();
        check(bool(discoverGenerationRuntime(loop,submit,{temp.path()/"missing",sdk,{},false},temp.path(),
            [&](auto result) {discovered=std::move(result);})),"failure discovery was not scheduled");
        task=std::move(tasks.front());tasks.clear();task();(void)loop.poll();
        check(discovered && !*discovered,"worker discovery hid a missing explicit executable");
        check(!discoverGenerationRuntime(loop,[](auto){return false;},{{},sdk,{},false},temp.path(),[](auto){}),
            "worker discovery ignored executor rejection");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
