#include "NativeApplicationShellView.h"
#include "NativeApplicationWindow.h"
#include "NativeApplicationCommand.h"
#include "NativeSocietyLicense.h"
#include "NativeAssets.h"
#ifdef DREAMSCAPES_NATIVE_REVIEW_IMAGES
#include "NativeFileImages.h"
#endif
#include <iiSocietyHelper/Monitoring/Monitor.h>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
struct ApplicationResources final {
    ii::runtime::Runtime workers{ii::runtime::RuntimeOptions{.workers=2,.capacity=16}};
    ii::runtime::Runtime preferenceWorker{ii::runtime::RuntimeOptions{.workers=1,.capacity=2}};
    std::shared_ptr<dreamscapes::native::NativeApplicationShell> shell;
    ~ApplicationResources() {
        if (shell) {if (auto license=shell->launchLicense()) license->close(); shell->disconnectGenerationPreferences(); shell->disconnectSocietyStorage(); shell->disconnectCanvasFiles(); shell->disconnectFrameRenderer();}
        preferenceWorker.request_stop(ii::runtime::ShutdownMode::drain);
        workers.request_stop(ii::runtime::ShutdownMode::cancel_pending);
    }
};
}
int main(int argc,char **argv) {
    using namespace dreamscapes::native;
    std::vector<std::string_view> arguments;
    for (int i=1;i<argc;++i) arguments.emplace_back(argv[i]);
    std::string initialView;
    if (auto command=applicationCommand(arguments)) {
        if (!command->initialView) {
            (command->code ? std::cerr : std::cout) << command->text;
            return command->code;
        }
        initialView=std::move(*command->initialView);
    }
    ii::gui::HostOptions options;
    options.title="Dreamscapes";
    // Preserve the former desktop window geometry while the views are moved.
    options.size={1920,1200};
    options.minimum_size={320,480};
    auto windowLink=std::make_shared<std::weak_ptr<NativeApplicationWindow>>();
    auto window=NativeApplicationWindow::create(std::move(options),[initialView=std::move(initialView),windowLink](ii::runtime::EventLoop &loop)
            -> ii::core::Result<NativeApplicationWindow::Content> {
        auto resources=std::make_shared<ApplicationResources>();
        auto shell=NativeApplicationShell::create(queueReadySvg);
        resources->shell=shell;
#ifdef DREAMSCAPES_NATIVE_REVIEW_IMAGES
        shell->setReviewImageResolver(std::make_shared<NativeFileImages>(loop,resources->workers));
#endif
        if (auto connected=shell->connectFrameRenderer(loop,std::make_shared<dreamscapes::editor::RuntimeFrameExecutor>(resources->workers)); !connected)
            return std::unexpected(connected.error());
        const std::weak_ptr<ApplicationResources> weakResources=resources;
        auto license=LaunchLicense::create(loop,societyLicenseProbe(loop,[weakResources](ii::runtime::Task task) {
            if (auto owner=weakResources.lock()) return bool(owner->workers.post(std::move(task)));
            return false;
        },applicationVersion));
        shell->setLaunchLicense(license);
        (void)license->start();
        const auto preferencesPath=dreamscapes::preferences::generationPreferencesPath();
        if (!preferencesPath.empty()) {
            if (auto connected=shell->connectGenerationPreferences(loop,[weakResources](ii::runtime::Task task) {
                    if (auto owner=weakResources.lock()) return bool(owner->preferenceWorker.post(std::move(task)));
                    return false;
                },preferencesPath); !connected) return std::unexpected(connected.error());
        }
        if (auto connected=shell->connectCanvasFiles(loop,[weakResources](ii::runtime::Task task) {
                if (auto owner=weakResources.lock()) return bool(owner->workers.post(std::move(task)));
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
                    if (auto owner=weakResources.lock()) return bool(owner->workers.post(std::move(task)));
                    return false;
                },societySelection,std::move(explicitSocietyRoot)); !connected) return std::unexpected(connected.error());
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
    auto result=window->run();
    if (!result) {std::cerr << "Dreamscapes: " << result.error().message << '\n'; return 1;}
    return 0;
}
