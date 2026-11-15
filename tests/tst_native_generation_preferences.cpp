#include "App/Views/Preferences/NativeGenerationPreferences.h"
#include "App/Generation/ModelPreferences.h"
#include "App/Native/NativeApplicationShell.h"
#include "NativeAssets.h"
#include <iiFileProviderNative.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> result) {if (!result) throw std::runtime_error(result.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        namespace fs=std::filesystem;
        iiFileProvider::native::TemporaryDirectory temporary(fs::canonical(DREAMSCAPES_TEST_RESOURCES));
        check(temporary.isValid(),"cannot create preferences fixture");
        const auto path=temporary.path()/"generation-models.conf";
        ModelPreferences initial; initial.imageModel="image-model"; initial.videoModel="video-model";
        initial.prompts[0]={"original","original negative"}; initial.prompts[1]={"video","video negative"};
        std::string error;
        ModelPreferences windows;
        check(parseModelPreferences("Dreamscapes generation preferences 2\r\n\"image\"\r\n\"video\"\r\n"
                                    "\"first\r\nsecond\"\r\n\"\"\r\n\"\"\r\n\"\"\r\n"
                                    "\"\"\r\n\"\"\r\n\"\"\r\n\"\"\r\n",windows,error) &&
              windows.prompts[0].prompt=="first\nsecond",
              "Windows settings did not preserve the legacy text-mode behavior");
        auto literal=initial; literal.prompts[0].prompt="first\r\nsecond";
        ModelPreferences roundtrip;
        check(parseModelPreferences(serializeModelPreferences(literal),roundtrip,error) && roundtrip==literal,
              "a literal CRLF in an LF-format prompt was altered");
        check(writeModelPreferences(path,initial,error),"cannot write preferences fixture");
        auto workspace=preferences::WorkspaceModel::create({{"Image","Image"},{"Video","Video"},
                                                            {"Audio","Audio"},{"Agents","Agents"}});
        ii::runtime::EventLoop loop; std::vector<ii::runtime::Task> jobs;
        auto service=preferences::GenerationPreferences::create(workspace,loop,
            [&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;},path);
        ModelPreferences applied;
        service->setDefaultsHandler([&](const ModelPreferences &value){applied=value;});
        auto run=[&] {check(jobs.size()==1,"expected one preferences worker");
            auto task=std::move(jobs.front()); jobs.clear(); task(); (void)loop.poll();};
        good(workspace->select("Image"));
        check(!workspace->available.get() && jobs.size()==1,"preferences read ran on the view thread");
        run();
        check(workspace->available.get() && workspace->fields.get().size()==3 &&
              workspace->fields.get()[0]->value.get()=="image-model" && !workspace->fields.get()[0]->editable.get() && applied==initial,
              "saved image preferences did not reach the view or unvalidated model was editable");
        good(workspace->setChoices("Image","model",{{"image-model","Image model"},{"image-next","Image next"}},true));
        auto modelChoice=workspace->fields.get()[0];
        check(modelChoice->isChoice && modelChoice->editable.get() && modelChoice->options.size()==3 &&
              modelChoice->index.get()==1,"eligible image models were not projected to the preference selector");
        check(!modelChoice->edit("not-in-catalog") && jobs.empty(),"an unavailable model was accepted as a default");
        good(modelChoice->select(2)); run();
        initial.imageModel="image-next";
        ModelPreferences modelDisk;
        check(readModelPreferences(path,modelDisk,error) && modelDisk==initial && applied==initial,
              "the selected default model was not saved or applied");
        auto oldChoice=modelChoice;
        auto promptField=workspace->fields.get()[1];
        good(workspace->setChoices("Image","model",{},false));
        modelChoice=workspace->fields.get()[0];
        check(!modelChoice->editable.get() && modelChoice->value.get()=="image-next" &&
              workspace->fields.get()[1]==promptField && !oldChoice->select(1) && jobs.empty(),
              "an unavailable catalogue removed the saved default");
        good(workspace->setChoices("Image","model",{{"image-model","Image model"}},true));
        modelChoice=workspace->fields.get()[0];
        check(modelChoice->editable.get() && modelChoice->options[modelChoice->index.get()]=="Unavailable saved model" &&
              modelChoice->value.get()=="image-next","a missing model was silently replaced");
        good(workspace->setChoices("Image","model",{{"image-model","Image model"},{"image-next","Image next"}},true));
        good(workspace->setChoices("Video","model",{{"video-model","Video model"}},true));
        good(workspace->select("Video"));
        check(!workspace->fields.get()[0]->select(2) && jobs.empty(),"video accepted an image-only choice");
        good(workspace->select("Image"));
        good(workspace->fields.get()[1]->edit("새 기본 프롬프트"));
        good(workspace->fields.get()[2]->edit("negative changed"));
        check(jobs.size()==1 && service->saving.get(),"preference writes were not coalesced on a worker");
        ModelPreferences disk; check(readModelPreferences(path,disk,error) && disk==initial,
                                     "preference edit wrote on the view thread");
        run();
        check(readModelPreferences(path,disk,error) && disk.prompts[0].prompt=="새 기본 프롬프트" &&
              disk.prompts[0].negativePrompt=="negative changed" && disk.videoModel=="video-model" &&
              disk.prompts[1]==initial.prompts[1] && applied==disk && !service->saving.get(),
              "worker did not persist both fields while preserving unrelated settings");
        bool redirected=false;
        auto observer=service->saving.observe([&](bool value,std::uint64_t) {
            if (!value && !redirected) {redirected=true;good(workspace->fields.get()[1]->edit("latest"));}
        });
        good(workspace->fields.get()[1]->edit("first write")); run();
        check(redirected && service->saving.get() && jobs.size()==1 &&
              workspace->status.get()=="Saving generation preferences…" && applied.prompts[0].prompt!="first write",
              "reentrant editing showed an old write as the current saved state");
        run();
        check(readModelPreferences(path,disk,error) && disk.prompts[0].prompt=="latest" && applied==disk &&
              !service->saving.get(),"the reentrant edit did not become the saved default");
        check(!workspace->fields.get()[1]->edit(std::string(32001,'x')) && jobs.empty(),
              "an oversized prompt was accepted");
        check(!workspace->fields.get()[1]->edit(std::string("\xc0\xaf",2)) && jobs.empty(),
              "malformed UTF-8 was accepted");
        auto external=disk; external.prompts[1].prompt="external";
        check(writeModelPreferences(path,external,error),"cannot write external preferences fixture");
        good(workspace->fields.get()[1]->edit("unsaved local")); run();
        check(!service->error.get().empty() && workspace->fields.get()[1]->value.get()=="unsaved local" &&
              readModelPreferences(path,disk,error) && disk==external,
              "external edit was overwritten or the failed draft was discarded");
        good(service->reload()); run();
        good(workspace->select("Video"));
        check(workspace->fields.get()[1]->value.get()=="external","reload did not adopt the external file");
        good(workspace->select("Image"));
        good(workspace->fields.get()[1]->edit("drain on close"));
        service->close(); run();
        check(readModelPreferences(path,disk,error) && disk.prompts[0].prompt=="drain on close",
              "closing dropped an accepted pending preference write");
        check(!workspace->fields.get()[1]->editable.get(),"closed preferences remained editable");
        auto shell=native::NativeApplicationShell::create(native::queueReadySvg);
        good(shell->connectGenerationPreferences(loop,
            [&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;},path));
        run();
        check(shell->composer()->snapshot.get().prompt=="drain on close",
              "the application shell did not connect saved defaults to the generation draft");
        good(shell->preferencesWorkspace()->select("Image"));
        good(shell->preferencesWorkspace()->fields.get()[1]->edit("connected default")); run();
        check(shell->composer()->snapshot.get().prompt=="connected default",
              "saved preferences did not update the unedited application draft");
        good(shell->composer()->dispatch(home::HomeComposerCommandKind::editPrompt,"personal draft"));
        good(shell->preferencesWorkspace()->fields.get()[1]->edit("another default")); run();
        check(shell->composer()->snapshot.get().prompt=="personal draft",
              "a saved default overwrote the application's active prompt");
        shell->disconnectGenerationPreferences();
        const auto malformed=temporary.path()/"malformed.conf";
        iiFileProvider::native::File::write(malformed,"invalid");
        auto invalidWorkspace=preferences::WorkspaceModel::create({{"Image","Image"},{"Video","Video"},
                                                                   {"Audio","Audio"},{"Agents","Agents"}});
        auto invalid=preferences::GenerationPreferences::create(invalidWorkspace,loop,
            [&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;},malformed);
        run();
        check(!invalid->error.get().empty() && !invalidWorkspace->available.get() &&
              iiFileProvider::native::File::read(malformed)=="invalid",
              "malformed preferences were hidden or overwritten");
        invalid->close();
        const auto nearLimit=temporary.path()/"near-limit.conf";
        ModelPreferences large; large.imageModel=std::string(1048000,'m');
        check(writeModelPreferences(nearLimit,large,error),"cannot create size-limit fixture");
        auto limitedWorkspace=preferences::WorkspaceModel::create({{"Image","Image"},{"Video","Video"},
                                                                    {"Audio","Audio"},{"Agents","Agents"}});
        auto limited=preferences::GenerationPreferences::create(limitedWorkspace,loop,
            [&](ii::runtime::Task task){jobs.push_back(std::move(task));return true;},nearLimit);
        run();
        check(limitedWorkspace->available.get() &&
              !limitedWorkspace->fields.get()[1]->edit(std::string(1000,'p')) && jobs.empty(),
              "an edit that would produce an unreadable settings file was accepted");
        check(readModelPreferences(nearLimit,disk,error) && disk==large,"size-limit rejection changed the file");
        limited->close();
    } catch (const std::exception &error) {std::cerr << error.what() << '\n'; return 1;}
}
