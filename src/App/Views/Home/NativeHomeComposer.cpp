#include "NativeHomeComposer.h"
#include <algorithm>
#include <cctype>
#include <exception>
#include <set>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
namespace {
template<class Entry> bool uniqueKeys(const std::vector<Entry> &items) {
    std::set<std::string, std::less<>> seen;
    for (const auto &item : items) if (item.key.empty() || !seen.insert(item.key).second) return false;
    return true;
}
bool validSnapshot(const HomeComposerSnapshot &previous, const HomeComposerSnapshot &state) {
    const auto validChange = [](const auto &before, const auto &after) { return before == after || uniqueKeys(after); };
    const auto &a = previous.choices; const auto &b = state.choices;
    return validChange(a.media, b.media) && validChange(a.models, b.models) && validChange(a.ratios, b.ratios) &&
        validChange(a.resolutions, b.resolutions) && validChange(a.counts, b.counts) &&
        validChange(a.durations, b.durations) && validChange(a.frameRates, b.frameRates) &&
        validChange(a.presets, b.presets) && validChange(previous.attachments, state.attachments);
}
bool choiceAllowed(const std::vector<HomeComposerChoice> &choices, const std::string &value) {
    return std::ranges::any_of(choices, [&](const auto &choice) { return choice.key == value && choice.enabled; });
}
bool blank(const std::string &value) {
    return std::ranges::all_of(value, [](unsigned char ch) { return std::isspace(ch) != 0; });
}
}
std::shared_ptr<HomeComposerModel> HomeComposerModel::create() { return std::shared_ptr<HomeComposerModel>(new HomeComposerModel); }
HomeComposerModel::~HomeComposerModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
ii::core::Result<void> HomeComposerModel::writable() const {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Home composer requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed, "Home composer is closed");
    if (updating_) return ii::core::fail(ErrorCode::busy, "Home composer is publishing a snapshot");
    return {};
}
ii::core::Result<void> HomeComposerModel::setSnapshot(HomeComposerSnapshot next) {
    if (auto valid = writable(); !valid) return valid;
    if (!validSnapshot(snapshot.get(), next)) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home composer option and attachment keys must be unique and nonempty");
    const auto keepAlive = shared_from_this();
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{updating_}; updating_ = true;
    try {
        (void)snapshot.set(std::move(next));
        (void)commandsAvailable.set(bool(handler_));
        const auto &current = snapshot.get();
        (void)canSubmit.set(bool(handler_) && current.generationAvailable && !current.submitting && !current.generationPending);
        // A presentation observer may retire its session while receiving the
        // enabled notification. Detachment must win over that notification.
        if (!handler_) { (void)commandsAvailable.set(false); (void)canSubmit.set(false); }
        return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot publish Home composer state", std::current_exception()); }
}
ii::core::Result<void> HomeComposerModel::setHandler(Handler handler) {
    if (owner_ == std::this_thread::get_id() && active_ && updating_ && !handler) {
        handler_ = {}; return {};
    }
    if (auto valid = writable(); !valid) return valid;
    const auto keepAlive = shared_from_this();
    try {
        handler_ = std::move(handler);
        (void)commandsAvailable.set(bool(handler_));
        const auto &current = snapshot.get();
        (void)canSubmit.set(bool(handler_) && current.generationAvailable && !current.submitting && !current.generationPending);
        return {};
    } catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot attach Home composer commands", std::current_exception()); }
}
ii::core::Result<void> HomeComposerModel::dispatch(HomeComposerCommandKind kind, std::string value) {
    if (auto valid = writable(); !valid) return valid;
    if (dispatching_) return ii::core::fail(ii::core::ErrorCode::busy, "Home composer command is already being delivered");
    if (!handler_) return ii::core::fail(ii::core::ErrorCode::unsupported, "Home composer has no command handler");
    const auto &current = snapshot.get();
    const auto &choices = current.choices;
    if ((kind == HomeComposerCommandKind::submit && !current.generationAvailable) ||
        (kind == HomeComposerCommandKind::chooseAttachments && !current.attachmentsAvailable) ||
        (kind == HomeComposerCommandKind::paint && !current.paintAvailable) ||
        (kind == HomeComposerCommandKind::advanced && !current.advancedAvailable))
        return ii::core::fail(ii::core::ErrorCode::unsupported, "Composer action is unavailable");
    bool accepted = true;
    switch (kind) {
    case HomeComposerCommandKind::chooseMedia: accepted = choiceAllowed(choices.media, value); break;
    case HomeComposerCommandKind::chooseModel: accepted = choiceAllowed(choices.models, value); break;
    case HomeComposerCommandKind::chooseRatio: accepted = choiceAllowed(choices.ratios, value); break;
    case HomeComposerCommandKind::chooseResolution: accepted = choiceAllowed(choices.resolutions, value); break;
    case HomeComposerCommandKind::chooseCount: accepted = choiceAllowed(choices.counts, value); break;
    case HomeComposerCommandKind::chooseDuration: accepted = choiceAllowed(choices.durations, value); break;
    case HomeComposerCommandKind::chooseFrameRate: accepted = choiceAllowed(choices.frameRates, value); break;
    case HomeComposerCommandKind::loadPreset: accepted = choiceAllowed(choices.presets, value); break;
    case HomeComposerCommandKind::removeAttachment:
        accepted = std::ranges::any_of(current.attachments, [&](const auto &item) { return item.key == value; }); break;
    case HomeComposerCommandKind::savePreset: accepted = !blank(value); break;
    case HomeComposerCommandKind::submit:
        if (current.submitting || current.generationPending)
            return ii::core::fail(ii::core::ErrorCode::busy, "Generation submission is already in progress");
        [[fallthrough]];
    case HomeComposerCommandKind::randomSeed:
    case HomeComposerCommandKind::chooseAttachments:
    case HomeComposerCommandKind::paint:
    case HomeComposerCommandKind::advanced: accepted = value.empty(); break;
    case HomeComposerCommandKind::editPrompt:
    case HomeComposerCommandKind::editNegativePrompt:
    case HomeComposerCommandKind::applySeed: break;
    default: accepted = false; break;
    }
    if (!accepted) return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Home composer command refers to an unavailable input");
    const auto keepAlive = shared_from_this();
    struct Reset final { bool &value; ~Reset() { value = false; } } reset{dispatching_}; dispatching_ = true;
    try { auto handler = handler_; return handler({kind, std::move(value)}); }
    catch (...) { return ii::core::fail(ii::core::ErrorCode::failed, "Cannot deliver Home composer command", std::current_exception()); }
}
void HomeComposerModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Home composer requires its owner thread");
    if (updating_) throw std::logic_error("Home composer snapshot is being published");
    active_ = false; handler_ = {}; (void)commandsAvailable.set(false); (void)canSubmit.set(false);
}
} // namespace dreamscapes::home
