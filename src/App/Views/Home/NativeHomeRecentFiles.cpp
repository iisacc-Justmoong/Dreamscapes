#include "NativeHomeRecentFiles.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();
template<class Observable, class T> void update(Observable &target, T value, std::exception_ptr &failure) {
    try { (void)target.set(std::move(value)); }
    catch (...) { if (!failure) failure = std::current_exception(); }
}
}
HomeRecentFilesModel::HomeRecentFilesModel(HomeRecentFilesStrings strings) : strings_(std::move(strings)) {
    if (!strings_.count || !strings_.selection) throw std::invalid_argument("Recent files requires text formatters");
    (void)heading.set(strings_.heading); (void)viewAll.set(strings_.viewAll);
    (void)fileColumn.set(strings_.fileColumn); (void)specificationsColumn.set(strings_.specificationsColumn);
    (void)updatedColumn.set(strings_.updatedColumn); (void)stateColumn.set(strings_.stateColumn);
    (void)allTypesLabel.set(strings_.filters[0]); (void)canvasLabel.set(strings_.filters[1]);
    (void)imageLabel.set(strings_.filters[2]); (void)videoLabel.set(strings_.filters[3]); (void)documentLabel.set(strings_.filters[4]);
}
std::shared_ptr<HomeRecentFilesModel> HomeRecentFilesModel::create(HomeRecentFilesStrings strings) {
    auto result = std::shared_ptr<HomeRecentFilesModel>(new HomeRecentFilesModel(std::move(strings)));
    result->publish();
    return result;
}
HomeRecentFilesModel::~HomeRecentFilesModel() {
    if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {}
}
ii::core::Result<void> HomeRecentFilesModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Recent files requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed, "Recent files is closed");
    if (updating_) return ii::core::fail(ErrorCode::busy, "Recent files update is in progress");
    return {};
}
vm::Value HomeRecentFilesModel::query(vm::Read operation, std::vector<vm::Value> arguments) const {
    auto result = catalog_.queries(vm::Domain::facts).send({operation, std::move(arguments)});
    if (!result) {
        if (result.error().cause) std::rethrow_exception(result.error().cause);
        throw std::runtime_error("Recent file projection is unavailable");
    }
    return *result;
}
std::string HomeRecentFilesModel::key(const vm::Value &file) const { return vm::text(query(vm::Read::key, {file})); }
std::size_t HomeRecentFilesModel::indexForKey(const std::string &wanted) const {
    if (wanted.empty()) return missing;
    for (std::size_t index = 0; index < files_.size(); ++index) if (key(files_[index]) == wanted) return index;
    return missing;
}
std::size_t HomeRecentFilesModel::indexForToken(const std::string &token) const {
    for (std::size_t index = 0; index < tokens_.size(); ++index) if (tokens_[index] == token) return index;
    return missing;
}
std::string HomeRecentFilesModel::tokenForKey(const std::string &value) const {
    if (owner_ != std::this_thread::get_id() || !active_) return {};
    const auto index = indexForKey(value);
    return index == missing ? std::string{} : tokens_[index];
}
std::optional<std::size_t> HomeRecentFilesModel::sourceIndexForToken(const std::string &token) const {
    if (owner_ != std::this_thread::get_id() || !active_) return {};
    const auto index = indexForToken(token);
    return index == missing ? std::optional<std::size_t>{} : index;
}
void HomeRecentFilesModel::sizeChanged(double width, double height) {
    if (!writable() || !std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > 8192 || height > 8192) return;
    const auto keepAlive = shared_from_this();
    (void)viewportWidth.set(width); (void)viewportHeight.set(height);
    (void)showFilter.set(width > 420); (void)showUpdated.set(width >= 440); (void)showSpecifications.set(width >= 550);
}
ii::core::Result<void> HomeRecentFilesModel::setFiles(vm::Value value, std::vector<std::string> previews, bool loading) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    if (value.kind() != ii::core::ValueKind::array) return ii::core::fail(ErrorCode::invalid_argument, "Recent files must be a list");
    auto files = vm::array(value);
    if (files.size() != previews.size() || files.size() > 65536)
        return ii::core::fail(ErrorCode::invalid_argument, "Recent file preview count is invalid");
    if (std::ranges::any_of(files, [](const auto &file) { return file.kind() != ii::core::ValueKind::object; }))
        return ii::core::fail(ErrorCode::invalid_argument, "Recent files must contain objects");
    const auto keepAlive = shared_from_this();
    try {
        std::map<std::string, std::size_t> occurrence;
        std::map<std::pair<std::string, std::size_t>, std::string> identities;
        std::vector<std::string> tokens; tokens.reserve(files.size());
        auto next = nextToken_;
        for (const auto &file : files) {
            auto id = key(file); const auto identity = std::make_pair(id, occurrence[id]++);
            const auto existing = identity_.find(identity);
            std::string token;
            if (existing != identity_.end()) token = existing->second;
            else {
                if (next == std::numeric_limits<std::uint64_t>::max())
                    return ii::core::fail(ErrorCode::out_of_range, "Recent file identities exhausted");
                token = "recent-" + std::to_string(++next);
            }
            identities.emplace(identity, token); tokens.push_back(std::move(token));
        }
        files_ = std::move(files); previews_ = std::move(previews); tokens_ = std::move(tokens);
        identity_ = std::move(identities); nextToken_ = next; loading_ = loading;
        auto current = current_;
        if (indexForKey(current) == missing) current = files_.empty() ? std::string{} : key(files_.front());
        const bool changed = current != current_;
        current_ = current;
        publish();
        auto handler = handlers_.select;
        if (changed && handler) handler(std::move(current));
        return {};
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update recent files", std::current_exception()); }
}
ii::core::Result<void> HomeRecentFilesModel::setAvailabilityMessage(std::string value) {
    if (auto valid=writable(); !valid) return valid;
    if (availabilityMessage_==value) return {};
    const auto keepAlive=shared_from_this();
    try {availabilityMessage_=std::move(value);publish();return {};}
    catch (...) {return ii::core::fail(ii::core::ErrorCode::failed,"Cannot update file availability",std::current_exception());}
}
ii::core::Result<void> HomeRecentFilesModel::setFilter(std::string value) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    if (value != "All types" && value != "Canvas" && value != "Image" && value != "Video" && value != "Document")
        return ii::core::fail(ErrorCode::invalid_argument, "Unknown recent-file filter");
    const auto keepAlive = shared_from_this();
    try { currentFilter_ = std::move(value); publish(); return {}; }
    catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot filter recent files", std::current_exception()); }
}
ii::core::Result<void> HomeRecentFilesModel::filterAt(int index) {
    static constexpr std::array<const char *, 5> keys{"All types", "Canvas", "Image", "Video", "Document"};
    if (index < 0 || index >= static_cast<int>(keys.size()))
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Unknown recent-file filter index");
    const auto keepAlive = shared_from_this();
    const std::string selected = keys[static_cast<std::size_t>(index)];
    if (auto result = setFilter(selected); !result) return result;
    try { auto handler = handlers_.filter; if (handler) handler(selected); return {}; }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot deliver recent-file filter", std::current_exception()); }
}
ii::core::Result<void> HomeRecentFilesModel::setSelectedKey(std::string value) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    if (!value.empty() && indexForKey(value) == missing)
        return ii::core::fail(ErrorCode::invalid_argument, "Selected recent file is unavailable");
    const auto keepAlive = shared_from_this();
    try { current_ = std::move(value); publish(); return {}; }
    catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot select recent file", std::current_exception()); }
}
ii::core::Result<void> HomeRecentFilesModel::selectToken(const std::string &token) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    const auto index = indexForToken(token);
    if (index == missing || std::ranges::none_of(rows.get(), [&](const auto &row) { return row.token == token; }))
        return ii::core::fail(ErrorCode::cancelled, "Recent file row is no longer visible");
    auto selected = key(files_[index]);
    if (selected.empty()) return ii::core::fail(ErrorCode::invalid_argument, "Recent file has no identity");
    const auto keepAlive = shared_from_this();
    if (auto result = setSelectedKey(selected); !result) return result;
    try { auto handler = handlers_.select; if (handler) handler(std::move(selected)); return {}; }
    catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot deliver recent-file selection", std::current_exception()); }
}
ii::core::Result<void> HomeRecentFilesModel::openToken(const std::string &token) {
    using ii::core::ErrorCode;
    if (auto valid = writable(); !valid) return valid;
    const auto index = indexForToken(token);
    if (index == missing || std::ranges::none_of(rows.get(), [&](const auto &row) { return row.token == token; }))
        return ii::core::fail(ErrorCode::cancelled, "Recent file row is no longer visible");
    const auto keepAlive = shared_from_this();
    try { auto handler = handlers_.open; auto file = files_[index]; if (handler) handler(token, std::move(file)); return {}; }
    catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot open recent file", std::current_exception()); }
}
void HomeRecentFilesModel::setHandlers(Handlers handlers) {
    // A session can retire synchronously from an observer while a snapshot is
    // being published. Clearing commands is safe and must take effect before
    // publication finishes; attaching new commands still requires no update.
    if (owner_ == std::this_thread::get_id() && active_ && updating_ &&
        !handlers.select && !handlers.open && !handlers.browse && !handlers.filter) {
        handlers_ = {};
        return;
    }
    if (auto valid = writable(); !valid) throw std::logic_error(valid.error().message);
    handlers_ = std::move(handlers);
}
void HomeRecentFilesModel::browse() {
    if (!writable()) return;
    const auto keepAlive = shared_from_this(); auto handler = handlers_.browse; if (handler) handler();
}
void HomeRecentFilesModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Recent files requires its owner thread");
    if (!active_) return;
    if (updating_) throw std::logic_error("Recent files update is in progress");
    active_ = false; handlers_ = {};
}
void HomeRecentFilesModel::publish() {
    if (auto valid = writable(); !valid) throw std::logic_error(valid.error().message);
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_}; updating_ = true;
    std::vector<RecentFileRow> visible; visible.reserve(files_.size());
    const auto currentFilter = currentFilter_;
    for (std::size_t index = 0; index < files_.size(); ++index) {
        const auto &file = files_[index];
        const auto kind = vm::text(query(vm::Read::kind, {file}));
        if (currentFilter != "All types" && currentFilter != kind) continue;
        auto description = vm::text(vm::get(file, "metadata1"));
        if (description.empty()) description = vm::text(vm::get(file, "description"));
        if (description.empty()) description = kind;
        const auto fileKey = key(file);
        visible.push_back({tokens_[index], fileKey, vm::text(vm::get(file, "name")), std::move(description),
            previews_[index], vm::text(query(vm::Read::dimensions, {file})), vm::text(query(vm::Read::date, {file})),
            vm::text(query(vm::Read::state, {file})), !fileKey.empty() && fileKey == current_});
    }
    const auto selected = indexForKey(current_);
    auto count = strings_.count(files_.size());
    auto summary = selected == missing ? strings_.noSelection : strings_.selection(vm::text(vm::get(files_[selected], "name")));
    static constexpr std::array<std::string_view, 5> keys{"All types", "Canvas", "Image", "Video", "Document"};
    const auto filterIndex = std::ranges::find(keys, currentFilter_) - keys.begin();
    const bool empty = visible.empty();
    std::exception_ptr failure;
    update(selectedKey, current_, failure); update(filter, currentFilter_, failure);
    update(rows, std::move(visible), failure);
    update(showEmpty, empty, failure);
    update(filterLabel, strings_.filters[static_cast<std::size_t>(filterIndex)], failure);
    update(emptyMessage, loading_ ? strings_.loading : (availabilityMessage_.empty()?strings_.empty:availabilityMessage_), failure);
    update(countSummary, std::move(count), failure); update(selectionSummary, std::move(summary), failure);
    if (failure) std::rethrow_exception(failure);
}
} // namespace dreamscapes::home
