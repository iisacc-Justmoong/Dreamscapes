#include "NativeHomeSelectedFile.h"
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
template<class Observable, class T> void update(Observable &target, T value, std::exception_ptr &failure) {
    try { (void)target.set(std::move(value)); }
    catch (...) { if (!failure) failure = std::current_exception(); }
}
std::string property(const vm::Value &value, std::string_view key) {
    if (vm::has(value, key) && vm::get(value, key).kind() != ii::core::ValueKind::null)
        return vm::text(vm::get(value, key));
    for (const auto container : {"generationParameters", "parameters", "metadata"})
        if (vm::has(value, container)) return vm::text(vm::get(vm::get(value, container), key));
    return {};
}
}
std::shared_ptr<HomeSelectedFileModel> HomeSelectedFileModel::create(HomeSelectedFileStrings strings) {
    auto result = std::shared_ptr<HomeSelectedFileModel>(new HomeSelectedFileModel(std::move(strings)));
    result->publish();
    return result;
}
HomeSelectedFileModel::HomeSelectedFileModel(HomeSelectedFileStrings strings) : strings_(std::move(strings)) {
    (void)heading.set(strings_.heading);
    (void)canvasLabel.set(strings_.canvas);
    (void)structureLabel.set(strings_.structure);
    (void)colorLabel.set(strings_.color);
}
HomeSelectedFileModel::~HomeSelectedFileModel() {
    if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {}
}
void HomeSelectedFileModel::requireActiveOwner() const {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Selected file requires its owner thread");
    if (!active_) throw std::logic_error("Selected file is closed");
}
void HomeSelectedFileModel::requireOwner() const {
    requireActiveOwner();
    if (updating_) throw std::logic_error("Selected file update is already in progress");
}
vm::Value HomeSelectedFileModel::query(vm::Read operation, std::vector<vm::Value> arguments) const {
    const auto result = catalog_.queries(vm::Domain::facts).send({operation, std::move(arguments)});
    if (!result) {
        if (result.error().cause) std::rethrow_exception(result.error().cause);
        throw std::runtime_error("Selected file projection is unavailable");
    }
    return *result;
}
ii::core::Result<std::uint64_t> HomeSelectedFileModel::setFile(vm::Value value, std::string preview) {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Selected file requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed, "Selected file is closed");
    if (updating_) return ii::core::fail(ErrorCode::busy, "Selected file update is already in progress");
    if (value.kind() != ii::core::ValueKind::object) return ii::core::fail(ErrorCode::invalid_argument, "Selected file must be an object");
    const auto keepAlive = shared_from_this();
    if (value != file_) {
        if (generation_ == std::numeric_limits<std::uint64_t>::max())
            return ii::core::fail(ErrorCode::out_of_range, "Selected file generation exhausted");
        ++generation_; file_ = std::move(value); metadata_ = vm::Value(vm::Object{});
    }
    preview_ = std::move(preview);
    try { publish(); }
    catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update selected file", std::current_exception()); }
    return generation_;
}
ii::core::Result<void> HomeSelectedFileModel::setMetadata(std::uint64_t generation, vm::Value value) {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Selected file requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed, "Selected file is closed");
    if (updating_) return ii::core::fail(ErrorCode::busy, "Selected file update is already in progress");
    if (generation != generation_ || !hasFile.get())
        return ii::core::fail(ErrorCode::cancelled, "Selected file metadata is stale");
    if (value.kind() != ii::core::ValueKind::object) return ii::core::fail(ErrorCode::invalid_argument, "Selected file metadata must be an object");
    const auto keepAlive = shared_from_this();
    metadata_ = std::move(value);
    try { publish(); }
    catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update selected file metadata", std::current_exception()); }
    return {};
}
void HomeSelectedFileModel::publish() {
    requireOwner();
    const auto keepAlive = shared_from_this();
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_}; updating_ = true;
    const auto key = vm::text(query(vm::Read::key, {file_}));
    const bool present = !key.empty();
    const auto facts = vm::merged(file_, metadata_);
    const auto displayName = present ? vm::text(vm::get(file_, "name")) : strings_.empty;
    auto displayPath = vm::text(vm::get(file_, "relativePath"));
    if (displayPath.empty()) displayPath = vm::text(vm::get(file_, "metadata2"));
    if (displayPath.empty()) displayPath = vm::text(vm::get(file_, "folderPath"));
    if (displayPath.empty()) displayPath = strings_.noFile;
    auto colorValue = property(facts, "colorSpace");
    if (colorValue.empty()) colorValue = strings_.unavailable;
    const auto state = vm::text(query(vm::Read::state, {file_}));
    const auto date = vm::text(query(vm::Read::date, {file_}));
    const auto extra = vm::text(vm::get(file_, "metadata1"));
    const auto line = present ? state + " · " + date + (extra.empty() ? "" : " · " + extra) : "";
    const bool downloaded = !vm::has(file_, "available") || vm::boolean(vm::get(file_, "available"), true);
    std::exception_ptr failure;
    update(name, displayName, failure); update(path, std::move(displayPath), failure);
    update(previewSource, present ? preview_ : std::string{}, failure);
    update(dimensions, vm::text(query(vm::Read::dimensions, {facts})), failure);
    update(structure, present ? vm::text(query(vm::Read::structure, {facts})) : strings_.unavailable, failure);
    update(color, std::move(colorValue), failure); update(status, line, failure);
    update(openLabel, downloaded ? strings_.open : strings_.download, failure);
    update(hasFile, present, failure); update(canOpen, present && bool(open_), failure);
    if (failure) std::rethrow_exception(failure);
}
void HomeSelectedFileModel::setOpenHandler(std::function<void()> handler) {
    if (owner_ == std::this_thread::get_id() && active_ && updating_ && !handler) {
        // A view observer may destroy the session while a snapshot is being
        // published. Retire its action immediately without reentering the
        // snapshot projection; the remaining publication observes open_.
        open_ = {}; (void)canOpen.set(false); return;
    }
    requireOwner(); const auto keepAlive = shared_from_this(); open_ = std::move(handler); publish();
}
void HomeSelectedFileModel::open() {
    requireOwner(); const auto keepAlive = shared_from_this();
    auto handler = open_; if (hasFile.get() && handler) handler();
}
void HomeSelectedFileModel::sizeChanged(double width, double height) {
    // Layout is independent of the selected file snapshot. The native host
    // can report size synchronously while observers render that snapshot.
    requireActiveOwner();
    if (!std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0 || width > 8192 || height > 8192) return;
    (void)viewportWidth.set(width);
    (void)viewportHeight.set(height);
    (void)wide.set(width >= 400);
}
vm::Value HomeSelectedFileModel::file() const { requireOwner(); return file_; }
void HomeSelectedFileModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Selected file requires its owner thread");
    if (!active_) return;
    if (updating_) throw std::logic_error("Selected file update is already in progress");
    active_ = false; open_ = {}; (void)canOpen.set(false);
}
} // namespace dreamscapes::home

namespace {
std::shared_ptr<dreamscapes::home::HomeSelectedFileModel> requireModel(
    std::shared_ptr<dreamscapes::home::HomeSelectedFileModel> model) {
    if (!model) throw std::invalid_argument("Selected file requires a model");
    return model;
}
}
NativeHomeSelectedFileBackend::NativeHomeSelectedFileBackend()
    : NativeHomeSelectedFileBackend(dreamscapes::home::HomeSelectedFileModel::create()) {}
NativeHomeSelectedFileBackend::NativeHomeSelectedFileBackend(std::shared_ptr<dreamscapes::home::HomeSelectedFileModel> source)
    : model(requireModel(std::move(source))), heading(model->heading), openLabel(model->openLabel), name(model->name),
      path(model->path), dimensions(model->dimensions), structure(model->structure), color(model->color), status(model->status),
      previewSource(model->previewSource), canvasLabel(model->canvasLabel), structureLabel(model->structureLabel),
      colorLabel(model->colorLabel), hasFile(model->hasFile), canOpen(model->canOpen), wide(model->wide),
      viewportWidth(model->viewportWidth), viewportHeight(model->viewportHeight) {}
