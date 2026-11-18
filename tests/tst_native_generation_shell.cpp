#include "App/Native/NativeApplicationShell.h"
#include "NativeAssets.h"
#include <iiSDKCore>
#include <iostream>
#include <stdexcept>
#include <vector>

void check(bool value,const char *message) {if (!value) throw std::runtime_error(message);}
void good(ii::core::Result<void> value) {if (!value) throw std::runtime_error(value.error().message);}
int main() {
    try {
        using namespace dreamscapes;
        ii::runtime::EventLoop loop;std::vector<std::function<void()>> tasks;
        auto app=native::NativeApplicationShell::create(native::queueReadySvg);
        const vm::Value firstSource(vm::Object{{"stamp",vm::Value("first")}}),secondSource(vm::Object{{"stamp",vm::Value("second")}});
        good(app->setGenerationModelCatalog({{"image-a","Image A",true,false,firstSource},{"video-a","Video A",false,true}}));
        check(!app->generation(false)->canGenerate.get(),"generation was enabled without a provider");
        good(app->connectGenerationQueue(loop,[&](auto task){tasks.push_back(std::move(task));return true;},
            [](const auto &request,const auto &canceled,const auto &progress,const auto &output) {
                progress({"denoising",1,3});
                if (canceled.load()) return generation::QueueCompletion{generation::QueueState::Canceled,{}};
                if (vm::text(vm::get(request.parameters,"prompt"))=="Fail")
                    return generation::QueueCompletion{generation::QueueState::Failed,"Engine failure evidence"};
                if (vm::text(vm::get(request.parameters,"prompt"))=="Canceled with files")
                    return generation::QueueCompletion{generation::QueueState::Canceled,"Recoverable files: /fixture/output"};
                output(vm::Value(vm::Object{{"path",vm::Value("/fixture/result.png")},{"isImage",vm::Value(true)}}));
                return generation::QueueCompletion{generation::QueueState::Completed,{}};
            },true,false));
        check(app->homeQueue()->detail.get()=="Generation progress and failures appear here.",
              "the connected Home queue still says generation is disconnected");
        good(app->navigate("image"));
        check(!app->generation(false)->canGenerate.get(),"an empty prompt enabled generation");
        good(app->generation(false)->edit("prompt","A quiet landscape"));
        check(!app->generation(false)->edit("width","33") && !app->generation(false)->canGenerate.get(),
              "an invalid generation parameter enabled submission");
        good(app->generation(false)->edit("width","768"));
        check(app->generation(false)->canGenerate.get() && !app->generation(true)->canGenerate.get(),
              "provider capabilities were not reflected in generation controls");
        good(app->generation(false)->submit());
        check(app->generationQueue()->jobs.get().size()==1 && tasks.size()==1 &&
              app->reviewQueue()->running.get()==1 && !app->atHome.get(),"advanced submission did not start the review session");
        const auto request=app->generationQueue()->jobs.get().front()->request;
        check(request.model=="image-a" && request.modelSource==firstSource && vm::text(vm::get(request.parameters,"prompt"))=="A quiet landscape" &&
              vm::number(vm::get(request.parameters,"width"))==768,"advanced parameters were not captured for the job");
        good(app->setGenerationModelCatalog({{"image-a","Image A",true,false,secondSource},{"video-a","Video A",false,true}}));
        check(app->generationQueue()->jobs.get().front()->request.modelSource==firstSource,
              "an in-flight job silently adopted a later model identity");
        auto task=std::move(tasks.front());tasks.clear();task();(void)loop.poll();
        check(app->reviewQueue()->completed.get()==1 && app->reviewSession()->totalCount()==1,
              "completed generation was not projected into the review session");
        good(app->navigate("image"));good(app->generation(false)->edit("prompt","Fail"));
        good(app->generation(false)->submit());
        task=std::move(tasks.front());tasks.clear();task();(void)loop.poll();
        check(app->reviewQueue()->failed.get()==1 && app->reviewQueue()->items.get().back()->canInspectFailure.get(),
              "a failed job did not expose its evidence");
        app->reviewQueue()->items.get().back()->inspectFailure();
        check(app->generationFailure()->open.get() &&
              app->generationFailure()->reportText().find("Engine failure evidence")!=std::string::npos &&
              app->generationFailure()->reportText().find("denoising")!=std::string::npos,
              "failure details did not retain the provider error and actual stage");
        check(app->generationFailure()->reportText().find("RECOMMENDED CHECKS")!=std::string::npos &&
              app->generationFailure()->reportText().find("Deadline expired: not reported")!=std::string::npos &&
              app->generationFailure()->reportText().find("Worker exit: not reported")!=std::string::npos &&
              app->generationFailure()->reportText().find("no local language-model analysis was performed")!=std::string::npos,
              "native diagnostics omitted checks or invented an unreported worker status");
        std::string copied;
        app->generationFailure()->setClipboardWriter([&](std::string_view value) {copied=value;return ii::core::Result<void>{};});
        app->generationFailure()->copyReport();
        check(copied==app->generationFailure()->reportText(),"failure report copy omitted evidence");
        app->generationFailure()->dismiss();
        good(app->navigate("image"));good(app->generation(false)->edit("prompt","Canceled with files"));
        good(app->generation(false)->submit());
        task=std::move(tasks.front());tasks.clear();task();(void)loop.poll();
        const auto canceledRow=app->reviewQueue()->items.get().back();
        check(app->reviewQueue()->canceled.get()==1 && canceledRow->canInspectFailure.get() &&
              canceledRow->detailsLabel.get()=="Details","canceled work hid its recovery details or was marked failed");
        canceledRow->inspectFailure();
        check(app->generationFailure()->open.get() && app->generationFailure()->title.get()=="Generation details" &&
              app->generationFailure()->reportText().find("/fixture/output")!=std::string::npos &&
              app->generationFailure()->reportText().find("CANCELED")!=std::string::npos,
              "canceled work did not expose an accurately labeled recovery report");
        app->generationFailure()->dismiss();
        good(app->navigate("home"));
        good(app->composer()->dispatch(home::HomeComposerCommandKind::editPrompt,"  "));
        check(!app->composer()->dispatch(home::HomeComposerCommandKind::submit) &&
              !app->composer()->snapshot.get().errorText.empty(),"invalid Home submission hid its reason");
        good(app->composer()->dispatch(home::HomeComposerCommandKind::editPrompt,"Quick prompt"));
        check(app->composer()->snapshot.get().errorText.empty(),"correcting the Home prompt did not clear the submission error");
        good(app->composer()->dispatch(home::HomeComposerCommandKind::chooseRatio,"9:16"));
        good(app->composer()->dispatch(home::HomeComposerCommandKind::chooseResolution,"4096"));
        good(app->composer()->dispatch(home::HomeComposerCommandKind::submit));
        check(app->generationQueue()->jobs.get().size()==4 && tasks.size()==1 &&
              vm::text(vm::get(app->generationQueue()->jobs.get().back()->request.parameters,"prompt"))=="Quick prompt",
              "Home submission did not use the same queue");
        const auto &quickParameters = app->generationQueue()->jobs.get().back()->request.parameters;
        check(vm::number(vm::get(quickParameters,"width"))==2304 && vm::number(vm::get(quickParameters,"height"))==4096,
              "the selected 4K dimensions did not reach the generation queue");
        app->disconnectGenerationQueue();
        task=std::move(tasks.front());tasks.clear();task();(void)loop.poll();
        check(!app->generation(false)->canGenerate.get() && !app->composer()->canSubmit.get(),
              "disconnecting the provider left generation enabled");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
