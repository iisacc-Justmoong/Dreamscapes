#include "App/Native/NativeApplicationRuntime.h"
#include "App/Native/NativeApplicationWindow.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

int main() {
    using namespace std::chrono_literals;
    using namespace dreamscapes::native;
    try {
        ii::runtime::Application application({}, {.workers={.workers=2,.capacity=16}});
        int stopped=0,ready=0,closed=0;
        bool sameLoop=false;
        auto runtime=NativeApplicationRuntime::create(application,[&] {++stopped;});
        if (!runtime->attach()) throw std::runtime_error("Cannot attach Core runtime");
        std::shared_ptr<NativeApplicationWindow> window;
        window=NativeApplicationWindow::create({.title="Dreamscapes lifecycle test",.size={320,240}},
            [&](ii::runtime::EventLoop &loop) -> ii::core::Result<NativeApplicationWindow::Content> {
                sameLoop=&loop==&application.event_loop();
                return NativeApplicationWindow::Content{.build=[](ii::gui::WidgetBuilder &builder) -> ii::core::Result<void> {
                    auto root=builder.vstack({},"root");
                    if (!root) return std::unexpected(root.error());
                    return {};
                }};
            }, {.ready=[&] {
                    ++ready;
                    if (!application.event_loop().after(50ms,[&] {(void)window->close();}))
                        throw std::runtime_error("Cannot schedule native window close");
                }, .closed=[&] {++closed;}});
        if (auto attached=window->attach(application); !attached) throw std::runtime_error(attached.error().message);
        auto result=application.exec();
        if (!result) throw std::runtime_error(result.error().message);
        if (!sameLoop || ready!=1 || closed!=1 || stopped!=1 ||
            application.state()!=ii::runtime::ApplicationState::stopped)
            throw std::runtime_error("Core native window lifecycle did not complete");
    } catch (const std::exception &error) {std::cerr<<error.what()<<'\n';return 1;}
}
