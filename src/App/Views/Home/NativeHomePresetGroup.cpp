#include "NativeHomePresetGroup.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
HomePresetGroupModel::HomePresetGroupModel(std::string title, std::shared_ptr<ii::gui::NativeViewSource> load,
        std::shared_ptr<ii::gui::NativeViewSource> saveSource)
    : heading(std::move(title)), choice(std::move(load)), save(std::move(saveSource)) {
    if (heading.empty() || !choice || !save) throw std::invalid_argument("Preset group requires its heading and both controls");
}
std::shared_ptr<HomePresetGroupModel> HomePresetGroupModel::create(std::string heading,
        std::shared_ptr<ii::gui::NativeViewSource> choice, std::shared_ptr<ii::gui::NativeViewSource> save) {
    return std::shared_ptr<HomePresetGroupModel>(new HomePresetGroupModel(std::move(heading), std::move(choice), std::move(save)));
}
HomePresetGroupModel::~HomePresetGroupModel() { if (active_ && owner_ == std::this_thread::get_id()) try { close(); } catch (...) {} }
ii::core::Result<void> HomePresetGroupModel::setViewport(double width, double height, double gap, double desiredSave) {
    using ii::core::ErrorCode;
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor, "Preset group requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed, "Preset group is closed");
    for (const auto value : {width,height,gap,desiredSave}) if (!std::isfinite(value) || value < 0 || value > 8192)
        return ii::core::fail(ErrorCode::invalid_argument, "Invalid preset group geometry");
    const auto keepAlive = shared_from_this();
    try {
        const auto actualWidth = std::max(1.0, width);
        const auto remaining = std::max(0.0, actualWidth-gap);
        const auto actualSave = std::min(desiredSave, std::max(0.0, remaining-1));
        (void)viewportWidth.set(actualWidth); if (!active_) return ii::core::fail(ErrorCode::closed, "Preset group retired during layout");
        (void)saveWidth.set(actualSave); if (!active_) return ii::core::fail(ErrorCode::closed, "Preset group retired during layout");
        (void)choiceWidth.set(std::max(1.0, remaining-actualSave)); return {};
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update preset group geometry", std::current_exception()); }
}
void HomePresetGroupModel::close() {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("Preset group requires its owner thread");
    active_ = false;
}
} // namespace dreamscapes::home
