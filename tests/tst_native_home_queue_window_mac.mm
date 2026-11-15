#import <AppKit/AppKit.h>
#include "App/Generation/GenerationController.h"
#include "App/Native/NativeApplicationWindow.h"
#include "App/ViewModels/WorkspaceSession.h"
#include "App/Views/Home/NativeHomeQueueSession.h"
#include "App/Views/Home/NativeHomeQueueView.h"
#include <SharedStorage.h>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void stop(NSApplication *application) {
    [application stop:nil];
    [application postEvent:[NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0] atStart:YES];
}
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
} // namespace

int main(int argc, char **argv) {
    QCoreApplication core(argc, argv);
    if (argc != 2) return 1;
    try {
        QTemporaryDir storage(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-window-XXXXXX.societycontainer"));
        require(storage.isValid() && iiSocietyContainer::SocietyDrive::create(storage.path()).has_value(),
                "isolated Society fixture could not be created");
        qputenv("SOCIETY_STORAGE_SETTINGS_PATH", storage.filePath("settings.ini").toUtf8());
        qunsetenv("SOCIETY_CONTAINER_PATH");
        QFile weight(storage.filePath("Models/example.safetensors"));
        require(weight.open(QIODevice::WriteOnly) && weight.write("test weight") == 11, "fixture model missing");
        weight.close();
        GenerationRuntime runtime;
        runtime.nativeInference = true; runtime.imageExtent = 64; runtime.steps = 1;
        runtime.modelPreferencesFile = storage.filePath("models.conf"); runtime.failureReportsDirectory = storage.path();
        runtime.inspectImageModel = [](const auto &) { return iiLocalDiffusion::ImageModelInspection{}; };
        GenerationController generation(runtime);
        generation.setForeground(false);
        dreamscapes::WorkspaceSession workspace;
        workspace.setGeneration(&generation);
        dreamscapes::home::NativeHomeQueueSession session(&workspace, {}, [&] { workspace.openGenerationReview(); });
        session.setRuntimeNames("Fixture model", "CPU");
        require(generation.connectStorage(storage.path()), "fixture storage was not connected");
        require(!generation.enqueue("fixture", "1:1", 2, 7).isEmpty(), "fixture jobs were not queued");
        std::ifstream file(argv[1]); std::string svg{std::istreambuf_iterator<char>(file), {}};
        require(!svg.empty(), "queue indicator missing");
        @autoreleasepool {
            NSApplication *application = NSApplication.sharedApplication;
            [application setActivationPolicy:NSApplicationActivationPolicyProhibited];
            using Window = dreamscapes::native::NativeApplicationWindow;
            struct Outcome { std::string error; int ready = 0, closed = 0; };
            const auto outcome = std::make_shared<Outcome>();
            std::shared_ptr<Window> window;
            window = Window::create({}, [model = session.model(), svg](ii::runtime::EventLoop &)
                -> ii::core::Result<Window::Content> {
                return Window::Content{.build = [model, svg](ii::gui::WidgetBuilder &builder) {
                    return dreamscapes::home::buildHomeQueue(builder, {}, model, svg);
                }};
            }, {
                .ready = [&] {
                    try {
                        ++outcome->ready;
                        auto mount = window->widgets();
                        require(mount.has_value(), "native window has no LVUI view");
                        auto summary = mount->find("queueSummary");
                        auto name = mount->find("queueModel");
                        auto button = mount->find("openQueue");
                        require(bool(summary) && bool(name) && bool(button), "native queue widgets missing");
                        const auto summaryValue = summary->snapshot();
                        const auto nameValue = name->snapshot();
                        require(bool(summaryValue) && summaryValue->text == "0 running · 2 waiting · 0 failed" &&
                                    bool(nameValue) && nameValue->text == "Fixture model",
                                "product generation state did not reach the native window");
                        require(bool(button->activate()) && workspace.resultVisible(),
                                "native queue action did not reach product navigation");
                        require(bool(window->close()), "native window close failed");
                    } catch (const std::exception &problem) { outcome->error = problem.what(); (void)window->close(); }
                },
                .failed = [&](const ii::core::Error &problem) { outcome->error = problem.message; },
                .closed = [&] { ++outcome->closed; stop(application); }
            });
            dispatch_async(dispatch_get_main_queue(), ^{
                if (!window->showExternal()) { outcome->error = "native window did not start"; stop(application); }
            });
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 30 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                if (!outcome->closed) { outcome->error = "native product window timed out"; stop(application); }
            });
            [application run];
            require(outcome->error.empty() && outcome->ready == 1 && outcome->closed == 1,
                    outcome->error.empty() ? "native product window did not finish" : outcome->error.c_str());
        }
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
