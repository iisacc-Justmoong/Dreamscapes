#include "App/Native/NativeApplicationShell.h"
#include "App/Native/NativeSocietyDaemon.h"
#include <iiSDKCore>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using namespace dreamscapes;
using namespace dreamscapes::native;

/// Fails the protocol fixture with diagnostics from the exact operation.
void check(bool ok, const std::string &message) {
    if (!ok) throw std::runtime_error(message);
}

/// Uses the same owner-thread completion path as the native app.
void runPending(ii::runtime::EventLoop &loop, std::vector<ii::runtime::Task> &tasks) {
    while (!tasks.empty()) {
        auto batch=std::exchange(tasks,{});
        for (auto &task:batch) task();
        (void)loop.poll();
    }
}

/// Exercises paging, latest state, remote-file preparation, and the app's
/// normal Home and Files bindings without launching a desktop window.
void run(const std::filesystem::path &broker, const std::filesystem::path &root) {
    auto access=societyDaemonStorage(broker,root);
    const auto files=access.read({"files","",""},{});
    check(bool(files),files?"":files.error().message);
    check(files->root==root && files->containerId=="d82fa87d-a754-44e4-a8af-fa60388d0abc","wrong daemon drive");
    const auto &aliases=vm::array(files->aliases);
    check(aliases.size()==66,"directory pages were lost or replaced by a direct disk scan");
    check(vm::text(vm::get(aliases.front(),"relativePath"))=="remote.png" &&
          !vm::boolean(vm::get(aliases.front(),"resident")),"remote item missing from the catalogue");
    check(!access.read({"files","../Models",""},{}) && !access.read({"files","","traversal"},[] {return true;}),
          "invalid or retired requests should be rejected before reaching the daemon");
    const auto other=societyDaemonStorage(broker,root.parent_path());
    check(!other.read({"files","",""},{}) && !societyDaemonStorage(broker/"absent",root).read({"files","",""},{}),
          "a missing daemon or unexpected drive must not fall back to local browsing");

    const auto changed=access.read({"asset-library","",""},{});
    check(!changed && changed.error().message.find("changed")!=std::string::npos,
          "a drive switch inside a paged read was admitted");

    ii::runtime::EventLoop loop;
    std::vector<ii::runtime::Task> tasks;
    auto submit=[&](ii::runtime::Task task){tasks.push_back(std::move(task)); return true;};
    auto shell=NativeApplicationShell::create("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"/>");
    const auto canvas=shell->connectCanvasFiles(loop,submit);
    check(bool(canvas),canvas?"":canvas.error().message);
    const auto bound=shell->connectSocietyStorage(loop,submit,access);
    check(bool(bound),bound?"":bound.error().message);
    runPending(loop,tasks);
    const auto workspace=shell->storageWorkspace();
    check(workspace->available.get() && workspace->rows.get().size()==66,
          "application Files was not fed by the daemon");
    check(shell->historySession()->totalCount()==1,"the application did not read daemon generation history");
    check(shell->composer()->snapshot.get().modelId=="tiny.safetensors",
          "the daemon's model catalogue did not reach QuickGenerate");
    const auto saved=root/"exported.png";
    auto save=ReviewFileSave::create(loop,submit);
    save->setPicker([&](auto,auto completion) -> ii::core::Result<void> {
        completion(ii::gui::FileDialogSelection{.paths={saved.string()}});
        return {};
    });
    check(bool(save->save(shell->historySession()->entryAt(0))),"daemon history did not expose the local original");
    runPending(loop,tasks);
    check(std::filesystem::exists(saved),"the daemon's file revision prevented an existing history original from being saved");
    check(!std::filesystem::exists(root/"Files"/"remote.png"),"listing implicitly materialized an original");
    const auto selected=workspace->activate("files/remote.png");
    check(bool(selected) && workspace->canOpen.get(),"remote image could not be selected for opening");
    check(bool(workspace->openSelected()),"could not activate the remote file in Dreamscapes");
    runPending(loop,tasks);
    check(std::filesystem::exists(root/"Files"/"remote.png") && shell->documentSession()->hasDocument.get(),
          "the daemon's prepared image did not reach the native canvas");
    shell->disconnectSocietyStorage();
    shell->disconnectCanvasFiles();
    save->close();
}
}

int main(int argc, char **argv) {
    try {
        if (argc!=3) throw std::runtime_error("Expected broker and fixture directory");
        run(argv[1],argv[2]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
