#include "App/Native/NativeApplicationWindow.h"
#include <iiSDKCore>
#include <iostream>
#include <stdexcept>
#include <thread>

int main() {
    using namespace dreamscapes::native;
    try {
        auto window = NativeApplicationWindow::create({}, {});
        bool called = false;
        auto closed = window->chooseFile({}, [&](auto) { called = true; });
        if (closed || closed.error().code != ii::core::ErrorCode::closed || called)
            throw std::runtime_error("An unopened native window accepted a file picker");
        auto drag=window->beginWindowDrag();
        if (drag || drag.error().code!=ii::core::ErrorCode::closed)
            throw std::runtime_error("An unopened native window accepted a drag");
        ii::core::ErrorCode code = ii::core::ErrorCode::failed;
        std::thread other([&] { auto result = window->chooseFile({}, [&](auto) { called = true; }); if (!result) code = result.error().code; });
        other.join();
        if (code != ii::core::ErrorCode::wrong_executor || called)
            throw std::runtime_error("A foreign thread accessed the native file picker");
        ii::runtime::Application app;
        auto invalid=window->attach(app);
        if (invalid || invalid.error().code!=ii::core::ErrorCode::invalid_argument)
            throw std::runtime_error("A Core window without content was accepted");
        auto native=NativeApplicationWindow::create({},[](ii::runtime::EventLoop &) -> ii::core::Result<NativeApplicationWindow::Content> {
            return NativeApplicationWindow::Content{.build=[](ii::gui::WidgetBuilder &) -> ii::core::Result<void> {return {};}};
        });
        if (!native->attach(app)) throw std::runtime_error("Core application did not accept the native window");
        auto duplicate=native->attach(app);
        if (duplicate || duplicate.error().code!=ii::core::ErrorCode::busy)
            throw std::runtime_error("A native window was registered twice");
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
