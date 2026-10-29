#pragma once
#include "ViewValue.h"
#include <iiSDKCore/Runtime/Query.h>

namespace dreamscapes::vm {
enum class TimelineAction {
    write,
    reset,
    restore,
    selectTrack,
    addTrack,
    renameTrack,
    moveTrack,
    removeTrack,
    assignShotTrack,
    checkpoint,
    undo,
    redo,
    seek,
    selectShot,
    selectShotRange,
    selectAllShots,
    setRangeText,
    jumpCut,
    changeShot,
    toggleExclude,
    split,
    addShot,
    duplicateShot,
    rippleDelete,
    resizeComposition,
    slipKeys,
    trim,
    nudgeShot,
    addKey,
    editKey,
    deleteKey,
    copyKey,
    pasteKey
};
struct TimelineCommand {
    TimelineAction action;
    std::vector<Value> arguments;
    bool invalidNumericInput = false;
};
struct TimelineReply {
    Value returned;
    bool reset = false, compositionRestored = false;
};

class VideoTimeline final {
  public:
    VideoTimeline();
    const ii::runtime::QueryPort<TimelineCommand, TimelineReply> &commands() const {
        return commands_;
    }
    Value presentation() const;
    Value snapshot() const;
    Value track(std::string_view) const;
    Value tracksOfType(std::string_view) const;
    int trackPosition(std::string_view) const;
    std::string destinationTrack(std::string_view, std::string_view) const;
    int startOf(std::string_view) const;
    int snappedFrame(double) const;
    Value recipeShots(std::string_view) const;
    Value trackShots(std::string_view) const;
    Value trackKeys(std::string_view, bool ordered) const;
    int shotIndex(std::string_view) const;
    std::string shotReference(std::string_view) const;
    std::size_t undoDepth() const {
        return undo_.size();
    }
    std::size_t redoDepth() const {
        return redo_.size();
    }

  private:
    TimelineReply execute(const TimelineCommand &);
    bool reject(std::string);
    void checkpoint();
    void seek(double);
    bool selectTrack(std::string_view);
    std::string insertTrack(std::string, std::string, int);
    int totalFrames() const;
    Value selectedShot() const;
    Value selectedKey() const;
    void restore(const Value &);
    Object data_;
    std::vector<Value> undo_, redo_;
    Value clipboard_;
    ii::runtime::QueryPort<TimelineCommand, TimelineReply> commands_;
};
} // namespace dreamscapes::vm
