#include "App/Native/NativeApplicationLifecycle.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using dreamscapes::native::NativeApplicationLifecycle;
using ii::core::ErrorCode;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) {
    if (!result) throw std::runtime_error(result.error().message);
}
void exercise() {
    NativeApplicationLifecycle lifetime;
    check(!lifetime.isClosing(), "a new application is closing");
    check(!lifetime.add("", [] {}), "an unnamed shutdown handler was accepted");
    check(!lifetime.add("empty", {}), "an empty shutdown handler was accepted");
    ErrorCode foreignAdd=ErrorCode::failed, foreignStop=ErrorCode::failed;
    std::thread other([&] {
        if (auto result=lifetime.add("foreign", [] {}); !result) foreignAdd=result.error().code;
        if (auto result=lifetime.stop(); !result) foreignStop=result.error().code;
    });
    other.join();
    check(foreignAdd==ErrorCode::wrong_executor && foreignStop==ErrorCode::wrong_executor,
          "a foreign thread could change application lifecycle");
    std::vector<std::string> order;
    good(lifetime.add("first", [&] {
        order.push_back("first");
        check(lifetime.isClosing(), "shutdown was not published before cleanup");
        const auto late=lifetime.add("late", [] {});
        check(!late && late.error().code==ErrorCode::closed, "shutdown accepted a new handler");
        const auto nested=lifetime.stop();
        check(!nested && nested.error().code==ErrorCode::busy, "recursive shutdown was accepted");
    }));
    check(!lifetime.add("first", [] {}), "duplicate handler name was accepted");
    good(lifetime.add("failure", [&] { order.push_back("failure"); throw std::runtime_error("fixture cause"); }));
    good(lifetime.add("last", [&] { order.push_back("last"); }));
    const auto stopped=lifetime.stop();
    check(!stopped && stopped.error().message.find("failure")!=std::string::npos && stopped.error().cause,
          "shutdown lost the failing handler and cause");
    check(order==std::vector<std::string>({"first","failure","last"}), "shutdown skipped or reordered a handler");
    const auto again=lifetime.stop();
    check(!again && again.error().message==stopped.error().message && order.size()==3,
          "repeated shutdown changed the result or reran a handler");
    NativeApplicationLifecycle success;
    int calls=0; good(success.add("service", [&] { ++calls; }));
    good(success.stop()); good(success.stop());
    check(calls==1 && success.isClosing(), "successful shutdown was not idempotent");
}
}
int main() {
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
