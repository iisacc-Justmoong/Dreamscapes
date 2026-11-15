#include "NativeHomeRecentGenerations.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
constexpr auto missing = std::numeric_limits<std::size_t>::max();
template<class Observable, class T> void update(Observable &target, T value, std::exception_ptr &failure) {
    try { (void)target.set(std::move(value)); } catch (...) { if (!failure) failure = std::current_exception(); }
}
}
HomeRecentGenerationsModel::HomeRecentGenerationsModel(HomeRecentGenerationsStrings strings) : strings_(std::move(strings)) {
    if (!strings_.count || !strings_.edit) throw std::invalid_argument("Recent generations requires text formatters");
    (void)heading.set(strings_.heading); (void)viewAll.set(strings_.viewAll);
}
std::shared_ptr<HomeRecentGenerationsModel> HomeRecentGenerationsModel::create(HomeRecentGenerationsStrings strings) {
    auto result = std::shared_ptr<HomeRecentGenerationsModel>(new HomeRecentGenerationsModel(std::move(strings)));
    result->publish(); return result;
}
HomeRecentGenerationsModel::~HomeRecentGenerationsModel() {
    if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {}
}
ii::core::Result<void> HomeRecentGenerationsModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Recent generations requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed, "Recent generations is closed");
    if (updating_) return ii::core::fail(ErrorCode::busy, "Recent generations update is in progress");
    return {};
}
vm::Value HomeRecentGenerationsModel::query(vm::Read operation, std::vector<vm::Value> arguments) const {
    auto result = catalog_.queries(vm::Domain::facts).send({operation, std::move(arguments)});
    if (!result) {
        if (result.error().cause) std::rethrow_exception(result.error().cause);
        throw std::runtime_error("Recent generation projection is unavailable");
    }
    return *result;
}
std::string HomeRecentGenerationsModel::key(const vm::Value &file) const { return vm::text(query(vm::Read::key, {file})); }
std::size_t HomeRecentGenerationsModel::indexForKey(const std::string &value) const {
    if (value.empty()) return missing;
    for (std::size_t i = 0; i < files_.size(); ++i) if (key(files_[i]) == value) return i;
    return missing;
}
std::size_t HomeRecentGenerationsModel::indexForToken(const std::string &value) const {
    for (std::size_t i = 0; i < tokens_.size(); ++i) if (tokens_[i] == value) return i;
    return missing;
}
std::string HomeRecentGenerationsModel::tokenForKey(const std::string &value) const {
    if (owner_ != std::this_thread::get_id() || !active_) return {};
    const auto index = indexForKey(value); return index == missing ? std::string{} : tokens_[index];
}
std::optional<std::size_t> HomeRecentGenerationsModel::sourceIndexForToken(const std::string &value) const {
    if (owner_ != std::this_thread::get_id() || !active_) return {};
    const auto index = indexForToken(value); return index == missing ? std::optional<std::size_t>{} : index;
}
void HomeRecentGenerationsModel::send(const vm::ImageSelectionCommand &command) {
    auto result = selection_.commands().send(command);
    if (!result) {
        if (result.error().cause) std::rethrow_exception(result.error().cause);
        throw std::runtime_error("Recent image selection is unavailable");
    }
}
ii::core::Result<void> HomeRecentGenerationsModel::setFiles(vm::Value value, std::vector<std::string> previews, bool loading) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    if (value.kind() != ii::core::ValueKind::array) return ii::core::fail(ErrorCode::invalid_argument, "Recent generations must be a list");
    auto files = vm::array(value);
    if (files.size() != previews.size() || files.size() > 65536 ||
        std::ranges::any_of(files, [](const auto &file) { return file.kind() != ii::core::ValueKind::object; }))
        return ii::core::fail(ErrorCode::invalid_argument, "Recent generation entries or preview count are invalid");
    const auto keepAlive = shared_from_this();
    try {
        std::map<std::string, std::size_t> occurrences;
        std::map<std::pair<std::string, std::size_t>, std::string> identities;
        std::vector<std::string> tokens; tokens.reserve(files.size()); auto next = nextToken_;
        for (const auto &file : files) {
            auto id = key(file); auto identity = std::make_pair(id, occurrences[id]++);
            const auto old = identity_.find(identity); std::string token;
            if (old != identity_.end()) token = old->second;
            else {
                if (next == std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range, "Recent generation identities exhausted");
                token = "generation-" + std::to_string(++next);
            }
            identities.emplace(std::move(identity), token); tokens.push_back(std::move(token));
        }
        send({vm::ImageSelectionAction::setEntries, value});
        files_ = std::move(files); previews_ = std::move(previews); tokens_ = std::move(tokens);
        identity_ = std::move(identities); nextToken_ = next; loading_ = loading;
        if (indexForKey(current_) == missing) current_ = files_.empty() ? std::string{} : key(files_.front());
        publish(); return {};
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update recent generations", std::current_exception()); }
}
ii::core::Result<void> HomeRecentGenerationsModel::setAvailabilityMessage(std::string value) {
    if (auto valid=writable(); !valid) return valid;
    if (availabilityMessage_==value) return {};
    const auto keepAlive=shared_from_this();
    try {availabilityMessage_=std::move(value);publish();return {};}
    catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot update history availability",std::current_exception());}
}
ii::core::Result<void> HomeRecentGenerationsModel::setFocusedKey(std::string value) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    if (!value.empty() && indexForKey(value) == missing)
        return ii::core::fail(ErrorCode::invalid_argument, "Focused recent generation is unavailable");
    const auto keepAlive = shared_from_this();
    try { current_ = std::move(value); publish(); return {}; }
    catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update recent generation focus", std::current_exception()); }
}
ii::core::Result<void> HomeRecentGenerationsModel::restoreSelection(bool mode, std::vector<std::string> keys) {
    if (auto valid = writable(); !valid) return valid;
    const auto keepAlive = shared_from_this();
    try {
        std::set<std::string> available;
        for (const auto &file : files_) if (vm::isImage(file)) available.insert(vm::text(vm::imageKey(file)));
        vm::Array accepted; std::set<std::string> seen;
        for (const auto &key : keys)
            if (available.contains(key) && seen.insert(key).second) accepted.emplace_back(key);
        send({vm::ImageSelectionAction::reset});
        if (mode) {
            send({vm::ImageSelectionAction::setMode, vm::Value(true)});
            send({vm::ImageSelectionAction::setKeys, vm::Value(std::move(accepted))});
        }
        publish(); return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot restore recent generation selection", std::current_exception()); }
}
ii::core::Result<void> HomeRecentGenerationsModel::focusToken(const std::string &token) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    const auto index = indexForToken(token);
    if (index == missing) return ii::core::fail(ErrorCode::cancelled, "Recent generation is no longer available");
    const auto keepAlive = shared_from_this();
    try {
        if (auto result = setFocusedKey(key(files_[index])); !result) return result;
        auto handler = handlers_.focus; if (handler) handler(current_);
        return {};
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot focus recent generation", std::current_exception()); }
}
ii::core::Result<void> HomeRecentGenerationsModel::activate(const std::string &token) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    if (indexForToken(token) == missing) return ii::core::fail(ErrorCode::cancelled, "Recent generation is no longer available");
    const auto keepAlive = shared_from_this();
    if (auto result = focusToken(token); !result) return result;
    if (!active_) return ii::core::fail(ErrorCode::closed, "Recent generations was retired");
    const auto index = indexForToken(token);
    if (index == missing) return ii::core::fail(ErrorCode::cancelled, "Recent generation is no longer available");
    try {
        if (selectionMode.get()) {
            const auto before = selectedImageKeys.get();
            send({vm::ImageSelectionAction::toggle, files_[index]}); publish();
            auto handler = handlers_.selection;
            if (handler && before != selectedImageKeys.get()) handler(true, selectedImageKeys.get());
        }
        else { auto handler = handlers_.open; auto file = files_[index]; if (handler) handler(token, std::move(file)); }
        return {};
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot activate recent generation", std::current_exception()); }
}
ii::core::Result<void> HomeRecentGenerationsModel::toggleMode() {
    if (auto valid = writable(); !valid) return valid;
    const auto keepAlive = shared_from_this();
    try {
        const bool next = !selectionMode.get(); send({vm::ImageSelectionAction::reset});
        if (next) send({vm::ImageSelectionAction::setMode, vm::Value(true)});
        publish(); auto handler = handlers_.selection;
        if (handler) handler(next, selectedImageKeys.get());
        return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot change recent generation selection mode", std::current_exception()); }
}
void HomeRecentGenerationsModel::editSelection() {
    if (!writable() || !selectionMode.get()) return;
    const auto keepAlive = shared_from_this();
    std::vector<std::string> selected;
    for (std::size_t i = 0; i < files_.size(); ++i) if (selection_.isSelected(files_[i])) selected.push_back(tokens_[i]);
    auto handler = handlers_.edit; if (!selected.empty() && handler) handler(std::move(selected));
}
void HomeRecentGenerationsModel::browse() {
    if (!writable()) return;
    const auto keepAlive = shared_from_this(); auto handler = handlers_.browse; if (handler) handler();
}
void HomeRecentGenerationsModel::sizeChanged(double width, double height) {
    if (!writable() || !std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > 8192 || height > 8192) return;
    const auto keepAlive = shared_from_this(); const int count = width >= 500 ? 2 : 1;
    (void)viewportWidth.set(width); (void)viewportHeight.set(height); (void)columns.set(count); (void)itemWidth.set(std::max(1.0, width / count));
}
void HomeRecentGenerationsModel::setHandlers(Handlers handlers) {
    if (owner_ == std::this_thread::get_id() && active_ && updating_ &&
        !handlers.focus && !handlers.open && !handlers.edit && !handlers.browse && !handlers.selection) { handlers_ = {}; return; }
    if (auto valid = writable(); !valid) throw std::logic_error(valid.error().message);
    handlers_ = std::move(handlers);
}
void HomeRecentGenerationsModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Recent generations requires its owner thread");
    if (!active_) return;
    if (updating_) throw std::logic_error("Recent generations update is in progress");
    active_ = false; handlers_ = {};
}
void HomeRecentGenerationsModel::publish() {
    if (auto valid = writable(); !valid) throw std::logic_error(valid.error().message);
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_}; updating_ = true;
    const auto presentation = selection_.presentation();
    const bool mode = vm::boolean(vm::get(presentation, "selectionMode"));
    const auto count = vm::array(vm::get(presentation, "selectedImages")).size();
    const auto available = vm::array(vm::get(presentation, "images")).size();
    std::vector<std::string> keys;
    for (const auto &key : vm::array(vm::get(presentation, "selectedKeys"))) keys.push_back(vm::text(key));
    std::vector<RecentGenerationRow> visible; visible.reserve(files_.size());
    for (std::size_t i = 0; i < files_.size(); ++i) {
        const auto &file = files_[i];
        visible.push_back({tokens_[i], key(file), vm::text(vm::get(file, "name")), previews_[i],
            vm::text(query(vm::Read::model, {file})) + " · " + vm::text(query(vm::Read::kind, {file})),
            vm::text(query(vm::Read::resultSummary, {file})), selection_.isSelected(file)});
    }
    std::exception_ptr failure;
    update(focusedKey, current_, failure); update(rows, std::move(visible), failure);
    update(showEmpty, files_.empty(), failure);
    update(emptyMessage, loading_ ? strings_.loading : (availabilityMessage_.empty()?strings_.empty:availabilityMessage_), failure);
    update(countSummary, strings_.count(files_.size()), failure); update(selectionMode, mode, failure);
    update(canSelect, available > 1 || mode, failure); update(selectedCount, count, failure); update(canEdit, mode && count > 0, failure);
    update(selectedImageKeys, std::move(keys), failure);
    update(selectLabel, mode ? strings_.cancel : strings_.select, failure); update(editLabel, strings_.edit(count), failure);
    if (failure) std::rethrow_exception(failure);
}
} // namespace dreamscapes::home
