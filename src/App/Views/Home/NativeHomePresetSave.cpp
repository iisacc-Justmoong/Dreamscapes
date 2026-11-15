#include "NativeHomePresetSave.h"
#include <iiSDKCore/Core/Text/Utf8.h>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace dreamscapes::home {
namespace {
bool blank(std::string_view text) {
    if (!ii::core::valid_utf8(text) || text.find('\0') != std::string_view::npos) return true;
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index++]);
        unsigned value = first; unsigned extra = 0;
        if (first >= 0xf0) { value = first & 7; extra = 3; }
        else if (first >= 0xe0) { value = first & 15; extra = 2; }
        else if (first >= 0xc0) { value = first & 31; extra = 1; }
        while (extra--) value = (value << 6) | (static_cast<unsigned char>(text[index++]) & 63);
        const bool space = (value >= 9 && value <= 13) || value == 0x20 || value == 0x85 || value == 0xa0 ||
            value == 0x1680 || (value >= 0x2000 && value <= 0x200a) || value == 0x2028 || value == 0x2029 ||
            value == 0x202f || value == 0x205f || value == 0x3000 || value == 0xfeff;
        if (!space) return false;
    }
    return true;
}
}
HomePresetSaveModel::HomePresetSaveModel(std::shared_ptr<HomeComposerModel> composer, std::string title,
        std::string hint, std::string save, std::string cancel)
    : heading(std::move(title)), placeholder(std::move(hint)), saveLabel(std::move(save)), cancelLabel(std::move(cancel)),
      composer_(std::move(composer)) {
    if (!composer_ || !composer_->active() || heading.empty() || placeholder.empty() || saveLabel.empty() || cancelLabel.empty())
        throw std::invalid_argument("Preset editor requires an active model and accessible labels");
}
std::shared_ptr<HomePresetSaveModel> HomePresetSaveModel::create(std::shared_ptr<HomeComposerModel> composer,
        std::string heading, std::string placeholder, std::string save, std::string cancel) {
    auto result = std::shared_ptr<HomePresetSaveModel>(new HomePresetSaveModel(std::move(composer),
        std::move(heading), std::move(placeholder), std::move(save), std::move(cancel)));
    result->start(); return result;
}
HomePresetSaveModel::~HomePresetSaveModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
void HomePresetSaveModel::start() {
    const std::weak_ptr<HomePresetSaveModel> weak = shared_from_this();
    commandsObservation_ = composer_->commandsAvailable.observe([weak](bool, std::uint64_t) {
        if (auto model = weak.lock(); model && model->active_) model->synchronize();
    });
    synchronize();
}
void HomePresetSaveModel::synchronize() {
    const bool available = composer_ && composer_->commandsAvailable.get();
    (void)enabled.set(available);
    if (active_) (void)canSave.set(available && !blank(draft.get()));
}
ii::core::Result<void> HomePresetSaveModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Preset editor requires its owner thread");
    if (!active_ || !composer_ || !composer_->active()) return ii::core::fail(ErrorCode::closed, "Preset editor is closed");
    if (!composer_->commandsAvailable.get()) return ii::core::fail(ErrorCode::unsupported, "Preset editor has no command handler");
    return {};
}
ii::core::Result<void> HomePresetSaveModel::edit(std::string name) {
    if (auto result = writable(); !result) return result;
    if (!ii::core::valid_utf8(name) || name.find('\0') != std::string::npos)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Preset name must be NUL-free UTF-8");
    const auto keepAlive = shared_from_this();
    try {
        (void)draft.set(std::move(name));
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed, "Preset editor retired during edit");
        synchronize(); return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot edit preset name", std::current_exception()); }
}
ii::core::Result<void> HomePresetSaveModel::save() {
    if (auto result = writable(); !result) return result;
    if (blank(draft.get())) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Preset name is blank");
    const auto keepAlive = shared_from_this();
    auto result = composer_->dispatch(HomeComposerCommandKind::savePreset, draft.get());
    if (!result || !active_) return result;
    try {
        (void)draft.set({});
        if (active_) synchronize();
        return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot reset preset name", std::current_exception()); }
}
void HomePresetSaveModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Preset editor requires its owner thread");
    if (!active_) return;
    active_ = false; commandsObservation_.reset(); composer_.reset(); (void)enabled.set(false); (void)canSave.set(false);
}
} // namespace dreamscapes::home
