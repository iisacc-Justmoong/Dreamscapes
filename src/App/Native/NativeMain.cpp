#include "NativeApplicationShellView.h"
#include "NativeApplicationWindow.h"
#include "NativeApplicationRuntime.h"
#include "NativeApplicationLifecycle.h"
#include "NativeApplicationCommand.h"
#include "NativeSocietyLicense.h"
#include "NativeAssets.h"
#include "NativeGenerationRuntime.h"
#include "NativeSocietyGeneration.h"
#include "NativeSocietyVideoGeneration.h"
#ifdef DREAMSCAPES_NATIVE_RESIDENT_CLIENT
#include "NativeResidentCommand.h"
#endif
#ifdef DREAMSCAPES_NATIVE_REVIEW_IMAGES
#include "NativeFileImages.h"
#endif
#include <iiSocietyHelper>
#include <iiSDKCore>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
struct ApplicationResources final {
    std::shared_ptr<dreamscapes::native::NativeApplicationRuntime> runtime;
    std::shared_ptr<dreamscapes::native::NativeApplicationShell> shell;
    dreamscapes::native::NativeApplicationLifecycle lifecycle;
    void shutdown() {
        auto result=lifecycle.stop();
        if (result) return;
        if (result.error().cause) {
            try {std::rethrow_exception(result.error().cause);}
            catch (...) {std::throw_with_nested(std::runtime_error(result.error().message));}
        }
        throw std::runtime_error(result.error().message);
    }
    ~ApplicationResources() {try {shutdown();} catch (...) {}}
};
ii::core::Result<void> registerShellShutdown(ApplicationResources &resources,
    const std::shared_ptr<dreamscapes::native::NativeApplicationShell> &shell) {
    const std::weak_ptr<dreamscapes::native::NativeApplicationShell> weak=shell;
    const auto add=[&](std::string name,auto operation) {
        return resources.lifecycle.add(std::move(name),[weak,operation] {
            if (auto owner=weak.lock()) operation(*owner);
        });
    };
    if (auto result=add("license",[](auto &owner) {if (auto license=owner.launchLicense()) license->close();}); !result) return result;
    if (auto result=add("publication",[](auto &owner) {owner.disconnectPublication();}); !result) return result;
    if (auto result=add("generation",[](auto &owner) {owner.disconnectGenerationQueue();}); !result) return result;
    if (auto result=add("preferences",[](auto &owner) {owner.disconnectGenerationPreferences();}); !result) return result;
    if (auto result=add("society",[](auto &owner) {owner.disconnectSocietyStorage();}); !result) return result;
    if (auto result=add("canvas files",[](auto &owner) {owner.disconnectCanvasFiles();}); !result) return result;
    if (auto result=add("timeline audio",[](auto &owner) {owner.disconnectTimelineAudio();}); !result) return result;
    return add("frame renderer",[](auto &owner) {owner.disconnectFrameRenderer();});
}
std::filesystem::path environmentPath(const char *name) {
    const auto value=std::getenv(name);
    if (!value || !*value) return {};
    auto decoded=ii::core::path_from_utf8(value);
    return decoded?decoded->lexically_normal():std::filesystem::path("invalid-environment-path");
}
}
int main(int argc,char **argv) {
    using namespace dreamscapes::native;
    std::vector<std::string_view> arguments;
    for (int i=1;i<argc;++i) arguments.emplace_back(argv[i]);
#ifdef DREAMSCAPES_NATIVE_RESIDENT_CLIENT
    if (arguments.size()==1 && arguments[0]=="--iisaccson-stdin") {
        std::string prompt(256*1024+1,'\0');
        std::cin.read(prompt.data(),std::streamsize(prompt.size())); prompt.resize(std::size_t(std::cin.gcount()));
        const auto result=residentCommand(prompt);
        (result.code ? std::cerr : std::cout) << result.text;
        return result.code;
    }
#endif
    std::string initialView;
    if (auto command=applicationCommand(arguments)) {
        if (!command->initialView) {
            (command->code ? std::cerr : std::cout) << command->text;
            return command->code;
        }
        initialView=std::move(*command->initialView);
    }
    std::vector<std::string> coreArguments;
    for (int i=0;i<argc;++i) coreArguments.emplace_back(argv[i]);
    ii::runtime::Application application(std::move(coreArguments),{.workers={.workers=4,.capacity=64}});
    auto resources=std::make_shared<ApplicationResources>();
    const std::weak_ptr<ApplicationResources> weakResources=resources;
    resources->runtime=NativeApplicationRuntime::create(application,[weakResources] {
        if (auto owner=weakResources.lock()) owner->shutdown();
    });
    if (auto attached=resources->runtime->attach(); !attached) {
        std::cerr << "Dreamscapes: " << describeApplicationError(attached.error()) << '\n'; return 1;
    }
    auto publicationAccounts=dreamscapes::publishing::publicationAccountsFromEnvironment();
    ii::gui::HostOptions options;
    options.title="Dreamscapes";
    // Preserve the former desktop window geometry while the views are moved.
    options.size={1920,1200};
    options.minimum_size={320,480};
    // Cocoa can place native traffic lights over a full-size content surface.
    // Other hosts retain their system frame and use the same LVUI content bar.
    const bool nativeTitleControls=ii::gui::supports_custom_window_chrome() &&
        ii::gui::host_capabilities().backend==ii::gui::HostBackend::cocoa;
    if (nativeTitleControls) options.chrome=ii::gui::WindowChrome::custom;
    auto windowLink=std::make_shared<std::weak_ptr<NativeApplicationWindow>>();
    auto window=NativeApplicationWindow::create(std::move(options),[initialView=std::move(initialView),windowLink,resources,nativeTitleControls,publicationAccounts=std::move(publicationAccounts)](ii::runtime::EventLoop &loop) mutable
            -> ii::core::Result<NativeApplicationWindow::Content> {
        if (&loop!=&resources->runtime->eventLoop())
            return ii::core::fail(ii::core::ErrorCode::failed,"Native window did not use the Core application event loop");
        auto shell=NativeApplicationShell::create(queueReadySvg);
        if (nativeTitleControls) shell->titleBar()->setWindowFeatures(true,[windowLink]() -> ii::core::Result<void> {
            if (auto window=windowLink->lock()) return window->beginWindowDrag();
            return ii::core::fail(ii::core::ErrorCode::closed,"Native window is closed");
        });
        resources->shell=shell;
        if (auto registered=registerShellShutdown(*resources,shell); !registered)
            return std::unexpected(registered.error());
#ifdef DREAMSCAPES_NATIVE_REVIEW_IMAGES
        shell->setReviewImageResolver(std::make_shared<NativeFileImages>(loop,resources->runtime->workers()));
#endif
        if (auto connected=shell->connectFrameRenderer(loop,std::make_shared<dreamscapes::editor::RuntimeFrameExecutor>(resources->runtime->workers())); !connected)
            return std::unexpected(connected.error());
        const std::weak_ptr<ApplicationResources> weakResources=resources;
        if (auto connected=shell->connectTimelineAudio(loop,[weakResources](ii::runtime::Task task) {
                if (auto owner=weakResources.lock()) return owner->runtime->post(RuntimeLane::audio,std::move(task));
                return false;
            }); !connected) return std::unexpected(connected.error());
        if (auto connected=shell->connectPublication(loop,[weakResources](ii::runtime::Task task) {
                if (auto owner=weakResources.lock()) return owner->runtime->post(RuntimeLane::publication,std::move(task));
                return false;
            },std::move(publicationAccounts)); !connected) return std::unexpected(connected.error());
        auto license=LaunchLicense::createForApplication(loop,[&] {
            return societyLicenseProbe(loop,[weakResources](ii::runtime::Task task) {
                if (auto owner=weakResources.lock()) return owner->runtime->post(RuntimeLane::general,std::move(task));
                return false;
            },applicationVersion);
        });
        if (license) {
            shell->setLaunchLicense(license);
            (void)license->start();
        }
        const auto preferencesPath=dreamscapes::preferences::generationPreferencesPath();
        if (!preferencesPath.empty()) {
            if (auto connected=shell->connectGenerationPreferences(loop,[weakResources](ii::runtime::Task task) {
                    if (auto owner=weakResources.lock()) return owner->runtime->post(RuntimeLane::preferences,std::move(task));
                    return false;
                },preferencesPath); !connected) return std::unexpected(connected.error());
        }
        if (auto connected=shell->connectCanvasFiles(loop,[weakResources](ii::runtime::Task task) {
                if (auto owner=weakResources.lock()) return owner->runtime->post(RuntimeLane::general,std::move(task));
                return false;
            }); !connected) return std::unexpected(connected.error());
        const auto societySelection=iiSocietyHelper::Monitoring::defaultSelectionFile();
        std::filesystem::path explicitSocietyRoot;
        if (const auto value=std::getenv("SOCIETY_CONTAINER_PATH"); value && *value) {
            std::u8string text;
            for (const unsigned char c:std::string_view(value)) text.push_back(static_cast<char8_t>(c));
            explicitSocietyRoot=std::filesystem::path(text);
        }
        if (!societySelection.empty()) {
            if (auto connected=shell->connectSocietyStorage(loop,[weakResources](ii::runtime::Task task) {
                    if (auto owner=weakResources.lock()) return owner->runtime->post(RuntimeLane::general,std::move(task));
                    return false;
                },societySelection,explicitSocietyRoot); !connected) return std::unexpected(connected.error());
            GenerationWorkerPaths paths;
            paths.explicitExecutable=environmentPath("IILD_GENERATOR_EXECUTABLE");
            paths.python=environmentPath("IILD_PYTHON_EXECUTABLE");
#ifdef DREAMSCAPES_DIFFUSION_EXECUTABLE
            const auto configuredWorker=ii::core::path_from_utf8(DREAMSCAPES_DIFFUSION_EXECUTABLE);
            paths.configuredExecutable=configuredWorker?configuredWorker->lexically_normal():
                std::filesystem::path("invalid-configured-worker-path");
#endif
#ifdef DREAMSCAPES_DIFFUSION_PACKAGE_WORKER
            paths.requireConfiguredExecutable=false;
#endif
            auto home=environmentPath("HOME");
#if defined(_WIN32)
            paths.windows=true;
            if (home.empty()) home=environmentPath("USERPROFILE");
#endif
            if (!home.empty()) paths.sdkRoot=home/".local"/"SDK"/"iiLocalDiffusion";
            const auto discovery=discoverGenerationRuntime(loop,[weakResources](ii::runtime::Task task) {
                    if (auto owner=weakResources.lock()) return owner->runtime->post(RuntimeLane::general,std::move(task));
                    return false;
                },std::move(paths),{},[weakResources,loopPointer=&loop,societySelection,explicitSocietyRoot](auto result) mutable {
                    auto owner=weakResources.lock();
                    if (!owner || !owner->shell) return;
                    auto &shell=owner->shell;
                    if (!result) {(void)shell->generation(false)->serviceStatus.set(result.error().message);return;}
                    SocietyGenerationOptions generationOptions{societySelection,explicitSocietyRoot,std::move(result->stagingRoot)};
                    auto transport=dreamscapes::generation::processWorkerTransport(std::move(result->command));
                    auto imageRunner=dreamscapes::generation::makeWorkerGenerationRunner(societyGenerationServices(generationOptions,transport));
                    auto videoRunner=makeSocietyVideoRunner(std::move(generationOptions),std::move(transport));
                    dreamscapes::generation::QueueRunner runner=[imageRunner=std::move(imageRunner),videoRunner=std::move(videoRunner)](
                        const auto &request,const auto &canceled,const auto &progress,const auto &output) {
                        if (request.media=="Image") return imageRunner(request,canceled,progress,output);
                        if (request.media=="Video") return videoRunner(request,canceled,progress,output);
                        return dreamscapes::generation::QueueCompletion{dreamscapes::generation::QueueState::Failed,"Unsupported generation media"};
                    };
                    auto connected=shell->connectGenerationQueue(*loopPointer,[weakResources](ii::runtime::Task task) {
                        if (auto active=weakResources.lock()) return active->runtime->post(RuntimeLane::generation,std::move(task));
                        return false;
                    },std::move(runner),true,true);
                    if (!connected) (void)shell->generation(false)->serviceStatus.set(connected.error().message);
                });
            if (!discovery) (void)shell->generation(false)->serviceStatus.set(discovery.error().message);
        }
        shell->setCanvasFilePicker([windowLink](ii::gui::FileDialogOptions options,ii::gui::FileDialogCompletion completion) {
            if (auto window=windowLink->lock()) return window->chooseFile(std::move(options),std::move(completion));
            return ii::core::Result<void>{ii::core::fail(ii::core::ErrorCode::closed,"Native window is closed")};
        });
        if (!initialView.empty())
            if (auto changed=shell->navigate(initialView); !changed) return std::unexpected(changed.error());
        return NativeApplicationWindow::Content{
            .build=[shell](ii::gui::WidgetBuilder &builder){return buildNativeApplicationShell(builder,{},shell);},
            .resources=resources,
            .resize=[shell](ii::gui::Size size){return shell->setViewport(size);}};
    });
    *windowLink=window;
    if (auto attached=window->attach(application); !attached) {
        std::cerr << "Dreamscapes: " << describeApplicationError(attached.error()) << '\n'; return 1;
    }
    auto result=application.exec();
    if (!result) {std::cerr << "Dreamscapes: " << describeApplicationError(result.error()) << '\n'; return 1;}
    return *result;
}
