#pragma once
#include "../../Views/Result/ReviewSelection.h"
#include "ViewCatalog.h"

namespace dreamscapes::vm {
inline bool ownsDownload(bool active, const Value &state, std::string_view key, std::string_view container) {
    return active && !key.empty() && !container.empty() && text(get(state, "key")) == key &&
           text(get(state, "containerId")) == container;
}
enum class ImageSelectionAction { setEntries, setMode, toggle, selectAt, selectAll, reset, setKeys, setAnchor };
struct ImageSelectionCommand {
    ImageSelectionAction action;
    Value value;
    int index = -1, previous = -1;
    bool additive = false, range = false, toggleClick = false;
};
class ImageSelection final {
  public:
    ImageSelection()
        : commands_([this](const ImageSelectionCommand &command) {
              execute(command);
              return presentation();
          }) {}
    const ii::runtime::QueryPort<ImageSelectionCommand, Value> &commands() const {
        return commands_;
    }
    bool isSelected(const Value &entry) const {
        return mode_ && selection_.selected(text(imageKey(entry)));
    }
    Value presentation() const {
        Array images, selected, keys;
        for (const auto &entry : array(entries_))
            if (isImage(entry)) {
                images.push_back(entry);
                if (selection_.selected(text(imageKey(entry))))
                    selected.push_back(entry);
            }
        for (const auto &key : selection_.selectedKeys())
            keys.emplace_back(key);
        return Value(Object{{"images", Value(images)},
                            {"selectedImages", Value(selected)},
                            {"selectedKeys", Value(keys)},
                            {"anchorKey", Value(anchor_)},
                            {"selectionMode", Value(mode_)}});
    }

  private:
    std::vector<std::string> imageKeys() const {
        std::vector<std::string> keys;
        for (const auto &entry : array(entries_))
            if (isImage(entry))
                keys.push_back(text(imageKey(entry)));
        return keys;
    }
    void execute(const ImageSelectionCommand &command) {
        const auto visible = imageKeys();
        switch (command.action) {
        case ImageSelectionAction::setEntries:
            entries_ = command.value;
            selection_.synchronize(imageKeys());
            break;
        case ImageSelectionAction::setMode:
            mode_ = boolean(command.value);
            break;
        case ImageSelectionAction::setAnchor:
            anchor_ = text(command.value);
            selection_.focus(anchor_);
            break;
        case ImageSelectionAction::setKeys:
            selection_.clearSelection();
            selection_.synchronize(imageKeys());
            for (const auto &key : array(command.value))
                selection_.select(text(key), visible, review::SelectionMode::Toggle);
            break;
        case ImageSelectionAction::toggle:
            if (isImage(command.value))
                selection_.select(text(imageKey(command.value)), visible, review::SelectionMode::Toggle);
            break;
        case ImageSelectionAction::reset:
            mode_ = false;
            anchor_.clear();
            selection_.clearSelection();
            break;
        case ImageSelectionAction::selectAll:
            mode_ = true;
            selection_.selectAll(visible);
            anchor_ = visible.empty() ? "" : visible.front();
            break;
        case ImageSelectionAction::selectAt: {
            const auto &rows = array(entries_);
            if (command.index < 0 || command.index >= rows.size() || !isImage(rows[command.index]))
                return;
            const auto key = text(imageKey(rows[command.index]));
            if ((command.additive || command.range) && !mode_) {
                selection_.clearSelection();
                anchor_ = key;
                if (command.previous >= 0 && command.previous < rows.size() && isImage(rows[command.previous])) {
                    anchor_ = text(imageKey(rows[command.previous]));
                    selection_.select(anchor_, visible, review::SelectionMode::Replace);
                }
                selection_.focus(anchor_);
                mode_ = true;
            }
            if (command.range) {
                if (std::find(visible.begin(), visible.end(), anchor_) == visible.end())
                    anchor_ = key;
                selection_.focus(anchor_);
                selection_.select(key, visible,
                                  command.additive ? review::SelectionMode::AddRange : review::SelectionMode::Range);
            } else {
                selection_.select(key, visible,
                                  command.additive || (mode_ && command.toggleClick) ? review::SelectionMode::Toggle
                                                                                     : review::SelectionMode::Replace);
                anchor_ = key;
            }
            break;
        }
        }
    }
    Value entries_{Array{}};
    review::Selection selection_;
    std::string anchor_;
    bool mode_ = false;
    ii::runtime::QueryPort<ImageSelectionCommand, Value> commands_;
};
} // namespace dreamscapes::vm
