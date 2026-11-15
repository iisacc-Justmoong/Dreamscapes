#include "NativeHomeSidebar.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
HomeSidebarModel::HomeSidebarModel(std::vector<HomeSidebarDefinition> definitions) : definitions_(std::move(definitions)) {
    std::set<std::string> keys;
    for (const auto &entry : definitions_)
        if (entry.key.empty() || !keys.insert(entry.key).second || (!entry.divider && entry.title.empty()))
            throw std::invalid_argument("Sidebar entries require unique keys and action titles");
}
std::shared_ptr<HomeSidebarModel> HomeSidebarModel::create(std::vector<HomeSidebarDefinition> definitions) {
    auto result = std::shared_ptr<HomeSidebarModel>(new HomeSidebarModel(std::move(definitions)));
    result->publish(); return result;
}
HomeSidebarModel::~HomeSidebarModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
ii::core::Result<void> HomeSidebarModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Home sidebar requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed, "Home sidebar is closed");
    if (updating_) return ii::core::fail(ErrorCode::busy, "Home sidebar update is in progress");
    return {};
}
ii::core::Result<void> HomeSidebarModel::setSelected(std::string key) {
    if (auto valid = writable(); !valid) return valid;
    if (!key.empty() && std::ranges::none_of(definitions_, [&](const auto &entry) { return !entry.divider && entry.key == key; }))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Unknown Home sidebar action");
    const auto keepAlive = shared_from_this();
    try { current_ = std::move(key); publish(); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update Home sidebar selection", std::current_exception()); }
}
ii::core::Result<void> HomeSidebarModel::setCompact(bool value) {
    if (auto valid = writable(); !valid) return valid;
    const auto keepAlive = shared_from_this();
    try { compact_ = value; publish(); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot update Home sidebar layout", std::current_exception()); }
}
ii::core::Result<void> HomeSidebarModel::activate(const std::string &key) {
    if (auto valid = writable(); !valid) return valid;
    if (std::ranges::none_of(definitions_, [&](const auto &entry) { return !entry.divider && entry.key == key; }))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Unknown Home sidebar action");
    const auto keepAlive = shared_from_this();
    try { auto handler = handler_; if (handler) handler(key); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot deliver Home sidebar action", std::current_exception()); }
}
void HomeSidebarModel::setHandler(Handler value) {
    if (owner_ == std::this_thread::get_id() && active_ && updating_ && !value) { handler_ = {}; return; }
    if (auto valid = writable(); !valid) throw std::logic_error(valid.error().message);
    handler_ = std::move(value);
}
void HomeSidebarModel::sizeChanged(double width, double height) {
    if (!writable() || !std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > 8192 || height > 8192) return;
    const auto keepAlive = shared_from_this(); (void)viewportWidth.set(width); (void)viewportHeight.set(height);
}
void HomeSidebarModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Home sidebar requires its owner thread");
    if (!active_) return;
    if (updating_) throw std::logic_error("Home sidebar update is in progress");
    active_ = false; handler_ = {};
}
void HomeSidebarModel::publish() {
    if (auto valid = writable(); !valid) throw std::logic_error(valid.error().message);
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_}; updating_ = true;
    std::vector<HomeSidebarRow> result; result.reserve(definitions_.size());
    for (const auto &entry : definitions_) {
        const bool selected = !entry.divider && entry.key == current_;
        result.push_back({entry.key, compact_ ? std::string{} : entry.title, entry.title, entry.icon, entry.divider,
            selected, selected ? HomeSidebarState::selected : HomeSidebarState::normal});
    }
    std::exception_ptr failure;
    try { (void)rows.set(std::move(result)); } catch (...) { failure = std::current_exception(); }
    try { (void)selected.set(current_); } catch (...) { if (!failure) failure = std::current_exception(); }
    try { (void)compact.set(compact_); } catch (...) { if (!failure) failure = std::current_exception(); }
    if (failure) std::rethrow_exception(failure);
}
} // namespace dreamscapes::home
