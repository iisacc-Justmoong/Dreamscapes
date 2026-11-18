#pragma once

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace dreamscapes_ctest {
class Runner final {
public:
    explicit Runner(std::string_view selection = {}) : selection_(selection) {}
    template<class Function> void run(std::string_view name, Function&& function)
    {
        if (!selection_.empty() && selection_ != name &&
            !(name.size() > selection_.size() && name.starts_with(selection_) && name[selection_.size()] == ':')) return;
        ++cases_;
        try { std::forward<Function>(function)(); }
        catch (const std::exception& error) { ++failures_; std::cerr << name << ": " << error.what() << '\n'; }
        catch (...) { ++failures_; std::cerr << name << ": unexpected exception\n"; }
    }
    [[nodiscard]] int result() const
    {
        if (!cases_) { std::cerr << "No test cases were executed\n"; return 1; }
        std::cout << cases_ << " test cases, " << failures_ << " failures\n";
        return failures_ ? 1 : 0;
    }
private:
    std::string selection_;
    unsigned cases_ = 0, failures_ = 0;
};
[[noreturn]] inline void fail(const char* file, int line, const char* expression)
{ throw std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + expression); }
template<class Actual, class Expected>
void compare(Actual&& actual, Expected&& expected, const char* file, int line, const char* expression)
{ if (!(actual == expected)) fail(file, line, expression); }
} // namespace dreamscapes_ctest

#define CTEST_CHECK(expression) \
    do { if (!(expression)) ::dreamscapes_ctest::fail(__FILE__, __LINE__, #expression); } while (false)
#define CTEST_CHECK_MESSAGE(expression, message) \
    do { if (!(expression)) ::dreamscapes_ctest::fail(__FILE__, __LINE__, (message)); } while (false)
#define CTEST_COMPARE(actual, expected) \
    ::dreamscapes_ctest::compare((actual), (expected), __FILE__, __LINE__, #actual " == " #expected)
#define CTEST_THROWS(exception_type, expression) \
    do { \
        bool thrown = false; \
        try { (void)(expression); } catch (const exception_type&) { thrown = true; } \
        if (!thrown) ::dreamscapes_ctest::fail(__FILE__, __LINE__, "expected " #exception_type " from " #expression); \
    } while (false)
