#include "VideoTimeline.h"
#include <limits>
#include <set>

namespace dreamscapes::vm {
namespace {
const Array trackTypes{Value("Shots"), Value("Contents"), Value("Audio"), Value("Caption")};
const Value emptyObject(Object{});
Value field(const Value &entry, std::string key, Value value) { auto next = object(entry); next[std::move(key)] = std::move(value); return Value(next); }
int integer(const Value &v) { return static_cast<int>(number(v)); }
int rounded(double n) { return static_cast<int>(std::clamp(std::floor(n + .5), double(std::numeric_limits<int>::min()), double(std::numeric_limits<int>::max()))); }
bool finiteInteger(double n) { return std::isfinite(n) && std::floor(n) == n && n >= std::numeric_limits<int>::min() && n <= std::numeric_limits<int>::max(); }
std::string id(const Value &v) { return text(get(v, "id")); }
bool selected(const Value &selection, const Value &entry) { return contains(selection, id(entry)); }
Value find(const Value &entries, std::string_view identifier) { for (const auto &v : array(entries)) if (id(v) == identifier) return v; return emptyObject; }
}
VideoTimeline::VideoTimeline() : commands_([this](const auto &request) { return execute(request); }) {
    data_ = {{"fps", Value(24)}, {"minimumFrames", Value(2)}, {"playhead", Value(0)}, {"mode", Value("Shots")}, {"tool", Value("Select")}, {"filter", Value("All")},
        {"snap", Value(true)}, {"linked", Value(true)}, {"multiSelect", Value(false)}, {"markIn", Value(0)}, {"markOut", Value(119)}, {"compositionFrames", Value(120)},
        {"tracks", Value(Array{})}, {"shots", Value(Array{})}, {"keys", Value(Array{})}, {"selectedIds", Value(Array{})}, {"selectedTrackId", Value("")}, {"selectedKeyId", Value("")},
        {"nextId", Value(1)}, {"nextTrackId", Value(1)}, {"error", Value("")}};
}
int VideoTimeline::totalFrames() const {
    const auto &shots = array(data_.at("shots"));
    if (shots.empty()) return integer(data_.at("compositionFrames"));
    std::int64_t total = 0; for (const auto &shot : shots) total += integer(get(shot, "frames"));
    return static_cast<int>(std::clamp<std::int64_t>(total, 0, std::numeric_limits<int>::max()));
}
Value VideoTimeline::selectedShot() const { for (const auto &shot : array(data_.at("shots"))) if (selected(data_.at("selectedIds"), shot)) return shot; return emptyObject; }
Value VideoTimeline::selectedKey() const { return find(data_.at("keys"), text(data_.at("selectedKeyId"))); }
Value VideoTimeline::presentation() const {
    auto result = data_; int kept = 0;
    for (const auto &shot : array(data_.at("shots"))) if (!boolean(get(shot, "excluded"))) kept += integer(get(shot, "frames"));
    const auto shot = selectedShot();
    result["trackTypes"] = Value(trackTypes); result["selectedTrack"] = track(text(data_.at("selectedTrackId")));
    result["totalFrames"] = Value(totalFrames()); result["keptFrames"] = Value(kept); result["outputFrames"] = array(data_.at("shots")).empty() ? data_.at("compositionFrames") : Value(kept);
    result["selectedShot"] = shot; result["selectedKey"] = selectedKey(); result["selectedStart"] = Value(startOf(id(shot)));
    result["selectedEnd"] = Value(startOf(id(shot)) + integer(get(shot, "frames")) - 1);
    result["canUndo"] = Value(!undo_.empty()); result["canRedo"] = Value(!redo_.empty());
    return Value(std::move(result));
}
Value VideoTimeline::snapshot() const {
    Object result;
    for (auto key : {"tracks", "selectedTrackId", "nextTrackId", "shots", "keys", "selectedIds", "selectedKeyId", "playhead", "nextId", "compositionFrames", "fps", "markIn", "markOut"}) result[key] = data_.at(key);
    return Value(result);
}
void VideoTimeline::restore(const Value &value) {
    for (auto key : {"tracks", "selectedTrackId", "nextTrackId", "shots", "keys", "selectedIds", "selectedKeyId", "nextId", "compositionFrames", "fps", "markIn", "markOut"}) if (has(value, key)) data_[key] = get(value, key);
    data_["error"] = Value(""); seek(number(get(value, "playhead")));
}
bool VideoTimeline::reject(std::string error) { data_["error"] = Value(std::move(error)); return false; }
void VideoTimeline::checkpoint() { undo_.push_back(snapshot()); if (undo_.size() > 100) undo_.erase(undo_.begin()); redo_.clear(); }
void VideoTimeline::seek(double frame) { if (std::isfinite(frame)) data_["playhead"] = Value(std::clamp(rounded(frame), 0, std::max(0, totalFrames() - 1))); }
Value VideoTimeline::track(std::string_view identifier) const { return find(data_.at("tracks"), identifier); }
Value VideoTimeline::tracksOfType(std::string_view type) const { Array result; for (const auto &v : array(data_.at("tracks"))) if (text(get(v, "type")) == type) result.push_back(v); return Value(result); }
int VideoTimeline::trackPosition(std::string_view identifier) const {
    int i = 0; const auto rows = tracksOfType(text(get(track(identifier), "type")));
    for (const auto &v : array(rows)) { if (id(v) == identifier) return i; ++i; } return -1;
}
bool VideoTimeline::selectTrack(std::string_view identifier) {
    if (id(track(identifier)).empty()) return false;
    data_["selectedTrackId"] = Value(identifier);
    Array selection; for (const auto &shot : array(data_.at("shots"))) if (selected(data_.at("selectedIds"), shot) && text(get(shot, "trackId")) == identifier) selection.emplace_back(id(shot));
    data_["selectedIds"] = Value(selection);
    if (text(get(selectedKey(), "trackId")) != identifier) data_["selectedKeyId"] = Value("");
    return true;
}
std::string VideoTimeline::destinationTrack(std::string_view type, std::string_view identifier) const {
    if (!identifier.empty()) return text(get(track(identifier), "type")) == type ? std::string(identifier) : "";
    const auto selected = text(data_.at("selectedTrackId")); if (text(get(track(selected), "type")) == type) return selected;
    const auto entries = tracksOfType(type); return array(entries).empty() ? "" : id(array(entries).front());
}
std::string VideoTimeline::insertTrack(std::string type, std::string name, int position) {
    auto rows = array(data_.at("tracks")); const auto group = tracksOfType(type);
    const auto rank = [](std::string_view t) { for (std::size_t i = 0; i < trackTypes.size(); ++i) if (text(trackTypes[i]) == t) return i; return trackTypes.size(); };
    const auto before = position >= 0 && static_cast<std::size_t>(position) < array(group).size() ? id(array(group)[position]) : "";
    const auto at = std::find_if(rows.begin(), rows.end(), [&](const auto &v) { return !before.empty() ? id(v) == before : rank(text(get(v, "type"))) > rank(type); });
    const auto next = integer(data_.at("nextTrackId")); const auto identifier = "track-" + std::to_string(next); data_["nextTrackId"] = Value(next + 1);
    rows.insert(at, Value(Object{{"id", Value(identifier)}, {"type", Value(type)}, {"name", Value(name)}})); data_["tracks"] = Value(rows); selectTrack(identifier); return identifier;
}
int VideoTimeline::startOf(std::string_view identifier) const { int frame = 0; for (const auto &shot : array(data_.at("shots"))) { if (id(shot) == identifier) return frame; frame += integer(get(shot, "frames")); } return 0; }
int VideoTimeline::snappedFrame(double frame) const {
    if (!boolean(data_.at("snap"))) return rounded(frame);
    std::vector<int> guides; for (const auto &shot : array(data_.at("shots"))) guides.push_back(startOf(id(shot)));
    for (const auto &key : array(data_.at("keys"))) guides.push_back(integer(get(key, "frame")));
    for (auto key : {"markIn", "markOut", "playhead"}) guides.push_back(integer(data_.at(key)));
    auto best = frame; for (auto guide : guides) if (std::abs(guide - frame) < std::abs(best - frame) || best == frame) best = guide;
    // Stable order on equal distances, matching the native timeline gesture contract.
    if (!guides.empty()) { best = guides.front(); for (auto guide : guides) if (std::abs(guide - frame) < std::abs(best - frame)) best = guide; }
    return rounded(std::abs(best - frame) <= 2 ? best : frame);
}
Value VideoTimeline::recipeShots(std::string_view prompt) const {
    Array segments = array(data_.at("shots")), result;
    if (segments.empty()) segments.emplace_back(Object{{"frames", data_.at("compositionFrames")}, {"prompt", Value(prompt)}});
    for (const auto &shot : segments) {
        if (boolean(get(shot, "excluded"))) continue;
        const auto start = startOf(id(shot)), frames = integer(get(shot, "frames")); Array conditions;
        for (const auto &key : array(data_.at("keys"))) {
            const auto frame = integer(get(key, "frame")); if (frame >= start && frame < start + frames) conditions.emplace_back(Object{{"image", get(key, "source")}, {"frame", Value(frame - start)}, {"strength", get(key, "value")}});
        }
        const auto shotPrompt = text(get(shot, "prompt"));
        result.emplace_back(Object{{"frames", Value(frames)}, {"prompt", Value(shotPrompt.empty() ? std::string(prompt) : shotPrompt)}, {"conditions", Value(conditions)}});
    } return Value(result);
}
TimelineReply VideoTimeline::execute(const TimelineCommand &command) {
    static const Value empty; const auto arg = [&](std::size_t i) -> const Value & { return i < command.arguments.size() ? command.arguments[i] : empty; };
    const auto identifier = text(arg(0)); const auto shots = [&] { return array(data_.at("shots")); }; const auto keys = [&] { return array(data_.at("keys")); };
    const auto nextId = [&](std::string prefix) { const auto n = integer(data_["nextId"]); data_["nextId"] = Value(n + 1); return prefix + std::to_string(n); };
    bool ok = true;
    switch (command.action) {
    case TimelineAction::write: {
        const auto found = data_.find(identifier);
        if (found == data_.end()) return {Value(false)};
        // Scalar view controls have no undo/document side effects.
        if (identifier == "playhead") seek(number(arg(1)));
        else if (identifier == "mode" || identifier == "tool" || identifier == "filter" || identifier == "snap" || identifier == "linked" || identifier == "multiSelect" || identifier == "selectedKeyId" || identifier == "selectedTrackId" || identifier == "selectedIds" || identifier == "markIn" || identifier == "markOut" || identifier == "error" || identifier == "fps" || identifier == "minimumFrames" || identifier == "compositionFrames" || identifier == "shots" || identifier == "tracks" || identifier == "keys") found->second = arg(1);
        else return {Value(false)};
        break;
    }
    case TimelineAction::reset: {
        if (!std::isfinite(number(arg(0))) || !std::isfinite(number(arg(1))) || number(arg(1)) <= 0) return {Value(reject("Choose a valid frame count and frame rate."))};
        data_["fps"] = arg(1); data_["compositionFrames"] = Value(std::max(integer(data_["minimumFrames"]), rounded(number(arg(0)))));
        for (auto name : {"tracks", "shots", "keys", "selectedIds"}) data_[name] = Value(Array{});
        for (auto name : {"selectedTrackId", "selectedKeyId", "error"}) data_[name] = Value("");
        for (auto name : {"nextId", "nextTrackId"}) data_[name] = Value(1);
        data_["playhead"] = Value(0); data_["markIn"] = Value(0); data_["markOut"] = Value(totalFrames() - 1);
        data_["mode"] = Value("Shots"); data_["tool"] = Value("Select"); data_["filter"] = Value("All"); data_["snap"] = Value(true); data_["linked"] = Value(true); data_["multiSelect"] = Value(false);
        undo_.clear(); redo_.clear(); clipboard_ = {}; return {Value(true), true};
    }
    case TimelineAction::restore: restore(arg(0)); return {Value(true), false, has(arg(0), "compositionFrames") && has(arg(0), "fps")};
    case TimelineAction::selectTrack: ok = selectTrack(identifier); break;
    case TimelineAction::addTrack: {
        data_["error"] = Value(""); if (!contains(Value(trackTypes), identifier)) return {Value(reject("Choose a track type."))};
        const auto group = tracksOfType(identifier); const auto count = static_cast<int>(array(group).size());
        const auto position = arg(2).kind() == ii::core::ValueKind::null || number(arg(2)) == -1 ? count : number(arg(2));
        if (!finiteInteger(position) || position < 0 || position > count) { reject("Choose a valid track position."); return {Value("")}; }
        const auto name = trim(text(arg(1), identifier)); if (name.empty()) { reject("Enter a track name."); return {Value("")}; }
        checkpoint(); return {Value(insertTrack(identifier, name, static_cast<int>(position)))};
    }
    case TimelineAction::renameTrack: {
        data_["error"] = Value(""); const auto name = trim(text(arg(1))); const auto old = track(identifier);
        if (id(old).empty() || name.empty()) return {Value(reject("Enter a track name."))};
        if (text(get(old, "name")) == name) break;
        checkpoint(); auto rows = array(data_["tracks"]); for (auto &row : rows) if (id(row) == identifier) row = field(row, "name", Value(name)); data_["tracks"] = Value(rows); break;
    }
    case TimelineAction::moveTrack: {
        data_["error"] = Value(""); const auto entry = track(identifier); auto group = array(tracksOfType(text(get(entry, "type")))); const auto position = number(arg(1));
        if (id(entry).empty() || !finiteInteger(position) || position < 0 || position >= group.size()) return {Value(reject("Choose a position within this track type."))};
        if (trackPosition(identifier) == position) break;
        checkpoint(); std::erase_if(group, [&](const auto &v) { return id(v) == identifier; }); group.insert(group.begin() + static_cast<int>(position), entry);
        auto rows = array(data_["tracks"]); std::size_t index = 0; for (auto &row : rows) if (text(get(row, "type")) == text(get(entry, "type"))) row = group[index++]; data_["tracks"] = Value(rows); selectTrack(identifier); break;
    }
    case TimelineAction::removeTrack: {
        data_["error"] = Value(""); auto rows = array(data_["tracks"]); auto found = std::find_if(rows.begin(), rows.end(), [&](const auto &v) { return id(v) == identifier; }); if (found == rows.end()) return {Value(false)};
        const auto position = found - rows.begin(); auto remainingShots = shots(), remainingKeys = keys();
        std::erase_if(remainingShots, [&](const auto &v) { return text(get(v, "trackId")) == identifier; }); std::erase_if(remainingKeys, [&](const auto &v) { return text(get(v, "trackId")) == identifier; });
        int frames = remainingShots.empty() ? integer(data_["compositionFrames"]) : 0; for (const auto &shot : remainingShots) frames += integer(get(shot, "frames"));
        if (std::ranges::any_of(remainingKeys, [&](const auto &v) { return integer(get(v, "frame")) >= frames; })) return {Value(reject("Move image keyframes inside the remaining sequence before removing this track."))};
        checkpoint(); rows.erase(rows.begin() + position); data_["tracks"] = Value(rows); data_["shots"] = Value(remainingShots); data_["keys"] = Value(remainingKeys);
        Array selection; for (const auto &shot : remainingShots) if (selected(data_["selectedIds"], shot)) selection.emplace_back(id(shot)); data_["selectedIds"] = Value(selection);
        if (id(selectedKey()).empty()) data_["selectedKeyId"] = Value(""); data_["selectedTrackId"] = Value(rows.empty() ? "" : id(rows[std::min<std::size_t>(position, rows.size() - 1)]));
        seek(number(data_["playhead"])); data_["markIn"] = Value(std::min(integer(data_["markIn"]), totalFrames() - 1)); data_["markOut"] = Value(std::min(integer(data_["markOut"]), totalFrames() - 1)); break;
    }
    case TimelineAction::assignShotTrack: {
        data_["error"] = Value(""); const auto destination = text(arg(1)); auto rows = shots();
        if (text(get(track(destination), "type")) != "Shots" || id(find(data_["shots"], identifier)).empty()) return {Value(false)};
        checkpoint(); for (auto &row : rows) if (id(row) == identifier) row = field(row, "trackId", Value(destination)); data_["shots"] = Value(rows); data_["selectedTrackId"] = Value(destination); break;
    }
    case TimelineAction::checkpoint: checkpoint(); break;
    case TimelineAction::undo: case TimelineAction::redo: {
        const bool undo = command.action == TimelineAction::undo; auto &from = undo ? undo_ : redo_, &to = undo ? redo_ : undo_;
        if (from.empty()) return {Value(false)}; to.push_back(snapshot()); const auto value = from.back(); from.pop_back(); restore(value); return {Value(true), false, true};
    }
    case TimelineAction::seek: seek(number(arg(0))); break;
    case TimelineAction::selectShot: {
        const auto shot = find(data_["shots"], identifier); if (id(shot).empty()) return {Value(false)};
        data_["selectedTrackId"] = get(shot, "trackId"); data_["selectedKeyId"] = Value(""); Array selectedIds;
        if (boolean(arg(1)) || boolean(data_["multiSelect"])) { selectedIds = array(data_["selectedIds"]); if (contains(data_["selectedIds"], identifier)) std::erase_if(selectedIds, [&](const auto &v) { return text(v) == identifier; }); else selectedIds.emplace_back(identifier); }
        else selectedIds.emplace_back(identifier); data_["selectedIds"] = Value(selectedIds); break;
    }
    case TimelineAction::jumpCut: {
        std::vector<int> boundaries; for (const auto &shot : shots()) boundaries.push_back(startOf(id(shot))); boundaries.push_back(totalFrames() - 1);
        const bool forward = number(arg(0)) > 0; int frame = forward ? totalFrames() - 1 : 0;
        if (!forward) std::ranges::reverse(boundaries);
        for (auto candidate : boundaries) if (forward ? candidate > number(data_["playhead"]) : candidate < number(data_["playhead"])) { frame = candidate; break; } seek(frame); break;
    }
    case TimelineAction::changeShot: {
        const auto name = text(arg(1)); const auto shot = find(data_["shots"], identifier);
        if (id(shot).empty() || (boolean(get(shot, "locked")) && name != "locked") || get(shot, name) == arg(2)) return {Value(false)};
        checkpoint(); auto rows = shots(); for (auto &row : rows) if (id(row) == identifier) row = field(row, name, arg(2)); data_["shots"] = Value(rows); break;
    }
    case TimelineAction::toggleExclude: {
        if (array(data_["selectedIds"]).empty()) return {Value(false)}; checkpoint(); auto rows = shots(); for (auto &row : rows) if (selected(data_["selectedIds"], row) && !boolean(get(row, "locked"))) row = field(row, "excluded", Value(!boolean(get(row, "excluded")))); data_["shots"] = Value(rows); break;
    }
    case TimelineAction::split: {
        auto rows = shots(); const int head = integer(data_["playhead"]); std::size_t i = 0;
        for (; i < rows.size(); ++i) { const auto start = startOf(id(rows[i])); if (head > start && head < start + integer(get(rows[i], "frames")) - 1) break; }
        if (i == rows.size() || boolean(get(rows[i], "locked"))) return {Value(false)};
        const auto source = rows[i]; const auto left = head - startOf(id(source)), right = integer(get(source, "frames")) - left; if (left < 2 || right < 2) return {Value(false)};
        checkpoint(); const auto newId = nextId("shot-"); auto rightShot = object(source); rightShot["id"] = Value(newId); rightShot["name"] = Value(text(get(source, "name")) + " / B"); rightShot["frames"] = Value(right);
        rows[i] = field(source, "frames", Value(left)); rows.insert(rows.begin() + i + 1, Value(rightShot)); data_["shots"] = Value(rows); data_["selectedIds"] = Value(Array{Value(newId)}); break;
    }
    case TimelineAction::addShot: {
        data_["error"] = Value(""); auto destination = destinationTrack("Shots", identifier);
        if (!identifier.empty() && destination.empty()) return {Value(reject("Choose a Shots track."))};
        checkpoint(); if (destination.empty()) destination = insertTrack("Shots", "Shots", static_cast<int>(array(tracksOfType("Shots")).size()));
        auto rows = shots(); const auto frames = rows.empty() ? integer(data_["compositionFrames"]) : std::max(integer(data_["minimumFrames"]), rounded(number(data_["fps"]))); const auto newId = nextId("shot-");
        rows.emplace_back(Object{{"id", Value(newId)}, {"trackId", Value(destination)}, {"name", Value("New shot")}, {"frames", Value(frames)}, {"excluded", Value(false)}, {"locked", Value(false)}, {"prompt", Value("")}});
        data_["shots"] = Value(rows); data_["selectedIds"] = Value(Array{Value(newId)}); data_["selectedTrackId"] = Value(destination); data_["selectedKeyId"] = Value(""); seek(totalFrames() - frames); break;
    }
    case TimelineAction::duplicateShot: {
        const auto source = selectedShot(); if (id(source).empty()) return {Value(false)};
        checkpoint(); auto rows = shots(); auto at = std::find_if(rows.begin(), rows.end(), [&](const auto &v) { return id(v) == id(source); }); const auto newId = nextId("shot-");
        auto copy = object(source); copy["id"] = Value(newId); copy["name"] = Value(text(get(source, "name")) + " / copy"); copy["locked"] = Value(false); rows.insert(at + 1, Value(copy)); data_["shots"] = Value(rows); data_["selectedIds"] = Value(Array{Value(newId)}); break;
    }
    case TimelineAction::rippleDelete: {
        Array removed; for (const auto &shot : shots()) if (selected(data_["selectedIds"], shot) && !boolean(get(shot, "locked"))) removed.push_back(shot); if (removed.empty()) return {Value(false)};
        checkpoint(); Array nextKeys; for (const auto &key : keys()) {
            const auto frame = integer(get(key, "frame")); int shift = 0; bool inside = false;
            for (const auto &shot : removed) { const auto start = startOf(id(shot)), end = start + integer(get(shot, "frames")); inside |= frame >= start && frame < end; if (end <= frame) shift += integer(get(shot, "frames")); }
            if (!inside) nextKeys.push_back(field(key, "frame", Value(frame - shift)));
        }
        auto rows = shots(); std::erase_if(rows, [&](const auto &shot) { return std::ranges::any_of(removed, [&](const auto &entry) { return id(entry) == id(shot); }); }); data_["shots"] = Value(rows); data_["keys"] = Value(nextKeys);
        data_["selectedIds"] = Value(rows.empty() ? Array{} : Array{get(rows.front(), "id")}); if (id(selectedKey()).empty()) data_["selectedKeyId"] = Value(""); seek(rows.empty() ? 0 : number(data_["playhead"])); data_["markIn"] = Value(rows.empty() ? 0 : std::min(integer(data_["markIn"]), totalFrames() - 1)); data_["markOut"] = Value(totalFrames() - 1); break;
    }
    case TimelineAction::resizeComposition: {
        data_["error"] = Value(""); const double frames = number(arg(0)), fps = arg(1).kind() == ii::core::ValueKind::null ? number(data_["fps"]) : number(arg(1)); const int minimum = integer(data_["minimumFrames"]);
        if (!finiteInteger(frames) || frames < minimum || !std::isfinite(fps) || fps <= 0) return {Value(reject("Choose a valid frame count and frame rate."))};
        if (totalFrames() < minimum) return {Value(reject("Reset the composition before changing its duration."))};
        auto rows = shots(); if (frames < rows.size() * minimum) return {Value(reject("The duration must leave at least " + std::to_string(minimum) + " frames for every shot."))};
        const auto scale = frames / totalFrames(); int remaining = static_cast<int>(frames);
        for (std::size_t i = 0; i < rows.size(); ++i) { const auto count = i == rows.size() - 1 ? remaining : std::min(remaining - static_cast<int>(rows.size() - i - 1) * minimum, std::max(minimum, rounded(number(get(rows[i], "frames")) * scale))); rows[i] = field(rows[i], "frames", Value(count)); remaining -= count; }
        auto nextKeys = keys(); std::set<int> positions; for (auto &key : nextKeys) { const auto frame = std::min(static_cast<int>(frames) - 1, rounded(number(get(key, "frame")) * scale)); if (!positions.insert(frame).second) return {Value(reject("Resizing would place image keyframes at the same frame. Move the keyframes or choose a longer duration."))}; key = field(key, "frame", Value(frame)); }
        if (frames == totalFrames() && frames == number(data_["compositionFrames"]) && fps == number(data_["fps"])) break;
        checkpoint(); data_["compositionFrames"] = Value(static_cast<int>(frames)); data_["fps"] = Value(fps); data_["shots"] = Value(rows); data_["keys"] = Value(nextKeys); seek(number(data_["playhead"])); data_["markIn"] = Value(std::min(integer(data_["markIn"]), static_cast<int>(frames) - 1)); data_["markOut"] = Value(static_cast<int>(frames) - 1); break;
    }
    case TimelineAction::slipKeys: {
        const auto shot = find(data_["shots"], identifier); const auto delta = integer(arg(1)); if (id(shot).empty() || boolean(get(shot, "locked"))) return {Value(false)}; const auto start = startOf(identifier), end = start + integer(get(shot, "frames"));
        auto rows = keys(); std::vector<std::size_t> members; for (std::size_t i = 0; i < rows.size(); ++i) { const auto frame = integer(get(rows[i], "frame")); if (frame >= start && frame < end) { if (frame + delta < start || frame + delta >= end) return {Value(false)}; members.push_back(i); } } if (members.empty()) return {Value(false)};
        checkpoint(); for (auto i : members) rows[i] = field(rows[i], "frame", Value(integer(get(rows[i], "frame")) + delta)); data_["keys"] = Value(rows); break;
    }
    case TimelineAction::trim: {
        auto rows = shots(); const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &v) { return id(v) == identifier; }); if (it == rows.end() || boolean(get(*it, "locked"))) return {Value(false)};
        const int i = static_cast<int>(it - rows.begin()), delta = rounded(number(arg(2))); if (!delta) return {Value(false)}; const bool in = text(arg(1)) == "in", roll = text(arg(3)) == "Roll"; const auto neighbor = in ? i - 1 : i + 1, change = in ? -delta : delta;
        if (integer(get(*it, "frames")) + change < 2 || (roll && (neighbor < 0 || neighbor >= rows.size() || boolean(get(rows[neighbor], "locked")) || integer(get(rows[neighbor], "frames")) - change < 2))) return {Value(false)};
        checkpoint(); const auto boundary = startOf(identifier) + (in ? 0 : integer(get(*it, "frames"))); *it = field(*it, "frames", Value(integer(get(*it, "frames")) + change));
        if (roll) rows[neighbor] = field(rows[neighbor], "frames", Value(integer(get(rows[neighbor], "frames")) - change));
        auto nextKeys = keys(); if (!roll && boolean(data_["linked"])) for (auto &key : nextKeys) if (integer(get(key, "frame")) >= boundary) key = field(key, "frame", Value(integer(get(key, "frame")) + change));
        data_["shots"] = Value(rows); std::erase_if(nextKeys, [&](const auto &key) { return integer(get(key, "frame")) < 0 || integer(get(key, "frame")) >= totalFrames(); }); data_["keys"] = Value(nextKeys); seek(number(data_["playhead"])); break;
    }
    case TimelineAction::nudgeShot: {
        auto rows = shots(); const auto current = selectedShot(); const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto &v) { return id(v) == id(current); }); const auto i = it - rows.begin(); const auto delta = integer(arg(0));
        if (i < 1 || i >= static_cast<std::ptrdiff_t>(rows.size()) - 1 || boolean(get(rows[i], "locked")) || boolean(get(rows[i - 1], "locked")) || boolean(get(rows[i + 1], "locked")) || integer(get(rows[i - 1], "frames")) + delta < 2 || integer(get(rows[i + 1], "frames")) - delta < 2) return {Value(false)};
        checkpoint(); auto nextKeys = keys(); const auto start = startOf(id(current)), end = start + integer(get(current, "frames")); if (boolean(data_["linked"])) for (auto &key : nextKeys) { const auto frame = integer(get(key, "frame")); if (frame >= start && frame < end) key = field(key, "frame", Value(frame + delta)); }
        rows[i - 1] = field(rows[i - 1], "frames", Value(integer(get(rows[i - 1], "frames")) + delta)); rows[i + 1] = field(rows[i + 1], "frames", Value(integer(get(rows[i + 1], "frames")) - delta)); data_["shots"] = Value(rows); data_["keys"] = Value(nextKeys); break;
    }
    case TimelineAction::addKey: {
        data_["error"] = Value(""); if (trim(identifier).empty()) return {Value(false)}; const auto frame = std::clamp(rounded(number(arg(1))), 0, totalFrames() - 1); const auto requested = text(arg(2)); auto destination = destinationTrack("Contents", requested);
        if (!requested.empty() && destination.empty()) return {Value(reject("Choose a Contents track."))};
        for (const auto &key : keys()) if (integer(get(key, "frame")) == frame && text(get(key, "trackId")) != destination) return {Value(reject("An image keyframe already exists at this frame. Choose a different frame or move the existing keyframe."))};
        checkpoint(); if (destination.empty()) destination = insertTrack("Contents", "Contents", static_cast<int>(array(tracksOfType("Contents")).size())); const auto newId = nextId("key-"); auto rows = keys(); std::erase_if(rows, [&](const auto &key) { return integer(get(key, "frame")) == frame; });
        const auto slash = identifier.rfind('/'); rows.emplace_back(Object{{"id", Value(newId)}, {"trackId", Value(destination)}, {"frame", Value(frame)}, {"value", Value(1)}, {"source", Value(identifier)}, {"name", Value(slash == std::string::npos ? identifier : identifier.substr(slash + 1))}, {"curve", Value("Linear")}});
        data_["keys"] = Value(rows); data_["selectedKeyId"] = Value(newId); data_["selectedIds"] = Value(Array{}); data_["selectedTrackId"] = Value(destination); data_["mode"] = Value("Keyframes"); break;
    }
    case TimelineAction::editKey: {
        data_["error"] = Value(""); const auto current = selectedKey(); if (id(current).empty()) return {Value(false)}; auto value = arg(1);
        if (identifier == "trackId" && text(get(track(text(value)), "type")) != "Contents") return {Value(false)};
        if (identifier == "frame") { const auto frame = rounded(number(value)); if (frame < 0 || frame >= totalFrames()) return {Value(false)}; for (const auto &key : keys()) if (id(key) != id(current) && integer(get(key, "frame")) == frame) return {Value(false)}; value = Value(frame); }
        if (identifier == "value" && (number(value) <= 0 || number(value) > 1)) return {Value(false)};
        checkpoint(); auto rows = keys(); for (auto &key : rows) if (id(key) == id(current)) key = field(key, identifier, value); data_["keys"] = Value(rows); if (identifier == "trackId") data_["selectedTrackId"] = value; break;
    }
    case TimelineAction::deleteKey: { if (text(data_["selectedKeyId"]).empty()) return {Value(false)}; checkpoint(); auto rows = keys(); std::erase_if(rows, [&](const auto &key) { return id(key) == text(data_["selectedKeyId"]); }); data_["keys"] = Value(rows); data_["selectedKeyId"] = Value(""); break; }
    case TimelineAction::copyKey: clipboard_ = selectedKey(); break;
    case TimelineAction::pasteKey: {
        if (id(clipboard_).empty()) return {Value(false)}; auto destination = destinationTrack("Contents", ""); if (destination.empty()) destination = text(get(clipboard_, "trackId"));
        const auto result = execute({TimelineAction::addKey, {get(clipboard_, "source"), data_["playhead"], Value(destination)}});
        if (boolean(result.returned)) return execute({TimelineAction::editKey, {Value("value"), get(clipboard_, "value")}}); return result;
    }
    }
    return {Value(ok)};
}
} // namespace dreamscapes::vm
