#include "App/Native/NativeApplicationWindow.h"
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
        ii::core::ErrorCode code = ii::core::ErrorCode::failed;
        std::thread other([&] { auto result = window->chooseFile({}, [&](auto) { called = true; }); if (!result) code = result.error().code; });
        other.join();
        if (code != ii::core::ErrorCode::wrong_executor || called)
            throw std::runtime_error("A foreign thread accessed the native file picker");
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
