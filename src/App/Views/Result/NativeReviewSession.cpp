#include "NativeReviewSession.h"
#include <iiJson/Json.h>
#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace dreamscapes::review {
using vm::Array;
using vm::get;
using vm::has;
using vm::Object;
using vm::text;
using vm::Value;
namespace {
std::string scalar(const Value &value);
std::string sourceOf(const Value &entry) {
    for (auto key : {"documentSource", "mediaSource", "image", "imageSource", "path"})
        if (auto source = scalar(get(entry, key)); !source.empty())
            return source;
    return {};
}
bool inputBoolean(const Value &value) {
    return std::visit(
        [](const auto &v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::string>) {
                std::string lower = v;
                std::ranges::transform(lower, lower.begin(),
                                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                return !lower.empty() && lower != "false" && lower != "0";
            } else if constexpr (std::is_arithmetic_v<T>)
                return v != 0;
            else
                return false;
        },
        value.storage());
}
bool imageEntry(const Value &entry) {
    const auto type = scalar(get(entry, "mediaType"));
    return Session::imageEligible(type, !has(entry, "isImage") || inputBoolean(get(entry, "isImage")),
                                  !scalar(get(entry, "documentSource")).empty());
}
std::string scalar(const Value &value) {
    return std::visit(
        [](const auto &v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::string>)
                return v;
            else if constexpr (std::same_as<T, bool>)
                return v ? "true" : "false";
            else if constexpr (std::same_as<T, std::int64_t> || std::same_as<T, std::uint64_t>)
                return std::to_string(v);
            else if constexpr (std::same_as<T, double>) {
                std::ostringstream out;
                out.imbue(std::locale::classic());
                out << std::setprecision(15) << v;
                return out.str();
            } else
                return {};
        },
        value.storage());
}
std::string available(const Value &entry, std::string_view key) {
    const auto value = scalar(get(entry, key));
    return value.empty() ? "Unavailable" : value;
}
std::string modelName(const Value &entry) {
    if (auto name = scalar(get(entry, "modelName")); !name.empty())
        return name;
    const auto &model = get(entry, "model");
    for (auto key : {"name", "path", "id"})
        if (auto value = scalar(get(model, key)); !value.empty())
            return value;
    return available(entry, "model");
}
std::optional<std::int64_t> parseInteger(std::string_view source) {
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.front())))
        source.remove_prefix(1);
    while (!source.empty() && std::isspace(static_cast<unsigned char>(source.back())))
        source.remove_suffix(1);
    if (!source.empty() && source.front() == '+') {
        source.remove_prefix(1);
        if (!source.empty() && (source.front() == '+' || source.front() == '-'))
            return {};
    }
    if (source.empty())
        return {};
    std::int64_t result = 0;
    const auto parsed = std::from_chars(source.data(), source.data() + source.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == source.data() + source.size() ? std::optional(result)
                                                                                   : std::nullopt;
}
std::optional<int> displayInteger(const Value &value) {
    return std::visit(
        [](const auto &v) -> std::optional<int> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::same_as<T, std::string>) {
                const auto parsed = parseInteger(v);
                if (parsed && *parsed >= std::numeric_limits<int>::min() && *parsed <= std::numeric_limits<int>::max())
                    return static_cast<int>(*parsed);
            } else if constexpr (std::is_arithmetic_v<T>) {
                const auto n = static_cast<long double>(v);
                if (std::isfinite(n) && n >= std::numeric_limits<int>::min() && n <= std::numeric_limits<int>::max())
                    return static_cast<int>(std::round(n));
            }
            return {};
        },
        value.storage());
}
std::string resolution(const Value &entry) {
    const auto width = displayInteger(get(entry, "width")), height = displayInteger(get(entry, "height"));
    if (!width || !height || *width <= 0 || *height <= 0)
        return "Unavailable";
    return std::to_string(*width) + " × " + std::to_string(*height);
}
std::optional<int> digits(std::string_view value, std::size_t at, std::size_t count) {
    if (at + count > value.size())
        return {};
    int result = 0;
    for (std::size_t i = at; i < at + count; ++i) {
        if (value[i] < '0' || value[i] > '9')
            return {};
        result = result * 10 + value[i] - '0';
    }
    return result;
}
// Generated jobs use UTC ISO-8601 timestamps. Offset and local timestamps are
// also accepted; malformed or impossible dates never produce invented values.
std::optional<std::int64_t> timestamp(std::string_view value) {
    using namespace std::chrono;
    if (value.size() < 19 || value[4] != '-' || value[7] != '-' || (value[10] != 'T' && value[10] != 't') ||
        value[13] != ':' || value[16] != ':')
        return {};
    const auto y = digits(value, 0, 4), m = digits(value, 5, 2), d = digits(value, 8, 2), h = digits(value, 11, 2),
               minute = digits(value, 14, 2), second = digits(value, 17, 2);
    if (!y || !m || !d || !h || !minute || !second || *h > 23 || *minute > 59 || *second > 59)
        return {};
    const year_month_day date{year{*y}, month{static_cast<unsigned>(*m)}, day{static_cast<unsigned>(*d)}};
    if (!date.ok())
        return {};
    std::size_t pos = 19;
    int ms = 0;
    if (pos < value.size() && value[pos] == '.') {
        ++pos;
        int count = 0;
        while (pos < value.size() && value[pos] >= '0' && value[pos] <= '9') {
            if (count < 3)
                ms = ms * 10 + (value[pos] - '0');
            ++count;
            ++pos;
        }
        if (!count)
            return {};
        while (count++ < 3)
            ms *= 10;
    }
    auto total = duration_cast<milliseconds>(sys_days{date}.time_since_epoch()).count() +
                 static_cast<std::int64_t>(*h) * 3600000 + static_cast<std::int64_t>(*minute) * 60000 +
                 static_cast<std::int64_t>(*second) * 1000 + ms;
    if (pos == value.size()) {
        std::tm local{};
        local.tm_year = *y - 1900;
        local.tm_mon = *m - 1;
        local.tm_mday = *d;
        local.tm_hour = *h;
        local.tm_min = *minute;
        local.tm_sec = *second;
        local.tm_isdst = -1;
        const auto seconds = std::mktime(&local);
        if (seconds == static_cast<std::time_t>(-1))
            return {};
        return static_cast<std::int64_t>(seconds) * 1000 + ms;
    }
    if ((value[pos] == 'Z' || value[pos] == 'z') && pos + 1 == value.size())
        return total;
    if ((value[pos] == '+' || value[pos] == '-') && pos + 6 == value.size() && value[pos + 3] == ':') {
        const auto oh = digits(value, pos + 1, 2), om = digits(value, pos + 4, 2);
        if (!oh || !om || *oh > 23 || *om > 59)
            return {};
        const auto offset = (static_cast<std::int64_t>(*oh) * 60 + *om) * 60000;
        return value[pos] == '+' ? total - offset : total + offset;
    }
    return {};
}
std::string elapsed(const Value &entry) {
    const auto first = timestamp(text(get(entry, "startedAt"))), last = timestamp(text(get(entry, "finishedAt")));
    if (!first || !last || *first > *last)
        return "Unavailable";
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(2) << (static_cast<double>(*last - *first) / 1000.) << " s";
    return out.str();
}
std::string prettyJson(const Value &value) {
    const auto encoded = ii::json::stringify(value);
    if (!encoded)
        return "Unavailable";
    std::string result;
    int depth = 0;
    bool inside = false, escaped = false;
    auto newline = [&] {
        result.push_back('\n');
        result.append(static_cast<std::size_t>(depth) * 4, ' ');
    };
    for (std::size_t i = 0; i < encoded->size(); ++i) {
        const char c = (*encoded)[i];
        if (inside) {
            result.push_back(c);
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inside = false;
        } else if (c == '"') {
            inside = true;
            result.push_back(c);
        } else if (c == '{' || c == '[') {
            result.push_back(c);
            ++depth;
            if (i + 1 < encoded->size() && ((*encoded)[i + 1] == '}' || (*encoded)[i + 1] == ']')) {
                --depth;
                newline();
                result.push_back((*encoded)[++i]);
            } else {
                newline();
            }
        } else if (c == '}' || c == ']') {
            --depth;
            if (i > 0 && (*encoded)[i - 1] != '{' && (*encoded)[i - 1] != '[')
                newline();
            result.push_back(c);
        } else if (c == ',') {
            result.push_back(c);
            newline();
        } else if (c == ':') {
            result.append(": ");
        } else
            result.push_back(c);
    }
    result.push_back('\n');
    return result;
}
std::string settings(const Value &entry) {
    const auto &parameters =
        get(entry, text(get(entry, "mediaType")) == "Video" ? "videoParameters" : "advancedParameters");
    return vm::object(parameters).empty() ? "Unavailable" : prettyJson(parameters);
}
struct Seed {
    bool negative = false;
    std::uint64_t magnitude = 0;
};
std::optional<Seed> seed(const Value &entry) {
    const auto &value = get(entry, "seed");
    if (!has(entry, "seed") || value.kind() == ii::core::ValueKind::null)
        return {};
    auto fromSigned = [](std::int64_t n) {
        return n < 0 ? Seed{true, std::uint64_t(-(n + 1)) + 1} : Seed{false, std::uint64_t(n)};
    };
    if (auto n = std::get_if<std::int64_t>(&value.storage()))
        return fromSigned(*n);
    if (auto n = std::get_if<std::uint64_t>(&value.storage()))
        return fromSigned(std::bit_cast<std::int64_t>(*n));
    if (auto n = std::get_if<double>(&value.storage())) {
        if (*n < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
            *n >= -static_cast<double>(std::numeric_limits<std::int64_t>::min()))
            return Seed{};
        return fromSigned(static_cast<std::int64_t>(std::round(*n)));
    }
    if (auto s = std::get_if<std::string>(&value.storage())) {
        const auto n = parseInteger(*s);
        return n ? fromSigned(*n) : Seed{};
    }
    return Seed{};
}
bool seedLess(const Seed &a, const Seed &b) {
    if (a.negative != b.negative)
        return a.negative;
    return a.negative ? a.magnitude > b.magnitude : a.magnitude < b.magnitude;
}
int hex(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
bool unreserved(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}
} // namespace

Session::Session() : owner_(std::this_thread::get_id()), lifetime_(std::make_shared<Lifetime>()) {}
Session::~Session() { lifetime_->alive = false; }
void Session::mutableOnOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("Review session requires its owner thread");
    if (lifetime_->notifying)
        throw std::logic_error("Review session cannot be edited during notification");
}
ii::core::Result<void> Session::observe(std::shared_ptr<const ChangePort> port) {
    if (owner_ != std::this_thread::get_id())
        return ii::core::fail(ii::core::ErrorCode::wrong_executor, "Review session requires its owner thread");
    if (lifetime_->notifying)
        return ii::core::fail(ii::core::ErrorCode::busy, "Review session is notifying observers");
    if (!port)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Review observer is missing");
    std::erase_if(observers_, [](const auto &p) { return p.expired(); });
    if (std::ranges::none_of(observers_, [&](const auto &p) { return p.lock() == port; }))
        observers_.push_back(port);
    return {};
}
void Session::publish(Change change) {
    std::erase_if(observers_, [](const auto &p) { return p.expired(); });
    const auto observers = observers_;
    const auto lifetime = lifetime_;
    lifetime->notifying = true;
    std::exception_ptr failure;
    for (const auto &weak : observers) {
        if (!lifetime->alive)
            break;
        if (auto receiver = weak.lock()) {
            auto delivered = receiver->send(change);
            if (!delivered && !failure && delivered.error().code == ii::runtime::QueryErrorCode::handler_failed)
                failure = delivered.error().cause
                              ? delivered.error().cause
                              : std::make_exception_ptr(std::runtime_error("Review observer failed"));
            if (!delivered && !failure && delivered.error().code == ii::runtime::QueryErrorCode::wrong_executor)
                failure = std::make_exception_ptr(std::logic_error("Review observer requires another executor"));
        }
    }
    lifetime->notifying = false;
    if (failure)
        std::rethrow_exception(failure);
}
std::string Session::sourceIdentity(std::string_view source) {
    const auto end = source.find_first_of("?#");
    source = source.substr(0, end);
    // A malformed escape makes Qt's tolerant parser treat every percent
    // sign in this path component as literal. Discarded queries and fragments
    // do not change that component's encoding.
    bool malformedEscape = false;
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] != '%')
            continue;
        if (i + 2 >= source.size() || hex(source[i + 1]) < 0 || hex(source[i + 2]) < 0) {
            malformedEscape = true;
            break;
        }
        i += 2;
    }
    std::string result;
    result.reserve(source.size());
    constexpr char upper[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '%' && malformedEscape) {
            result.append("%25");
            continue;
        }
        if (source[i] == '%' && i + 2 < source.size()) {
            const int a = hex(source[i + 1]), b = hex(source[i + 2]);
            if (a >= 0 && b >= 0) {
                const auto c = static_cast<unsigned char>(a * 16 + b);
                if (unreserved(c) || c == ' ' || c >= 128)
                    result.push_back(static_cast<char>(c));
                else {
                    result.push_back('%');
                    result.push_back(upper[a]);
                    result.push_back(upper[b]);
                }
                i += 2;
                continue;
            }
        }
        result.push_back(source[i]);
    }
    if (!ii::core::valid_utf8(result))
        result = std::string(source);
    // URI schemes and host names are case-insensitive; paths and user info are not.
    const auto colon = result.find(':');
    if (colon != std::string::npos && colon > 0 && std::isalpha(static_cast<unsigned char>(result.front())) &&
        std::ranges::all_of(std::string_view(result).substr(1, colon - 1),
                            [](unsigned char c) { return std::isalnum(c) || c == '+' || c == '-' || c == '.'; })) {
        for (std::size_t i = 0; i < colon; ++i)
            result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
        if (result.substr(colon + 1, 2) == "//") {
            const auto begin = colon + 3;
            const auto end = result.find_first_of("/", begin);
            const auto authorityEnd = end == std::string::npos ? result.size() : end;
            const auto at = result.rfind('@', authorityEnd);
            const auto hostBegin = (at != std::string::npos && at >= begin) ? at + 1 : begin;
            for (auto i = hostBegin; i < authorityEnd; ++i) {
                if (result[i] == '%' && i + 2 < authorityEnd) {
                    i += 2;
                    continue;
                }
                result[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[i])));
            }
        }
    }
    return result;
}
std::string Session::keyForEntry(const Value &entry) {
    const auto &alias = get(entry, "alias");
    const auto container = scalar(has(entry, "containerId") ? get(entry, "containerId") : get(alias, "containerId"));
    const auto key = scalar(has(entry, "key") ? get(entry, "key") : get(alias, "key"));
    if (!container.empty() && !key.empty())
        return container + '|' + key;
    const auto source = sourceOf(entry);
    if (source.empty())
        return {};
    return scalar(get(entry, "id")) + '|' + sourceIdentity(source);
}
bool Session::validImages(const std::vector<Value> &entries, int minimum, int maximum) {
    return minimum <= maximum && static_cast<long long>(entries.size()) >= minimum &&
           static_cast<long long>(entries.size()) <= maximum && std::ranges::all_of(entries, imageEntry);
}
void Session::replaceEntries(const std::vector<Value> &incoming) {
    mutableOnOwner();
    std::unordered_map<std::string, Value> next;
    std::vector<std::string> order;
    for (const auto &item : incoming) {
        auto key = keyForEntry(item);
        if (key.empty() || next.contains(key))
            continue;
        order.push_back(key);
        next.emplace(std::move(key), item);
    }
    if (next == entries_)
        return;
    std::vector<std::string> metadata;
    for (const auto &[key, value] : next) {
        const auto old = entries_.find(key);
        if (old == entries_.end() || old->second != value)
            metadata.push_back(key);
    }
    const auto previousOrdinals = ordinals_;
    entries_ = std::move(next);
    selection_.synchronize(order);
    if (entries_.empty())
        selectionMode_ = false;
    ordinals_.clear();
    int ordinal = 0;
    for (const auto &key : selection_.order()) {
        ordinals_[key] = ++ordinal;
        const auto old = previousOrdinals.find(key);
        if (old == previousOrdinals.end() || old->second != ordinal)
            metadata.push_back(key);
    }
    rebuild(true, metadata, true);
}
Session::RowState Session::rowState(const std::string &key) const {
    return {selection_.state(key), selection_.selected(key), selection_.focusedKey() == key};
}
bool Session::reviewFiltered() const {
    return filter_ == "shortlisted" || filter_ == "rejected" || filter_ == "unreviewed";
}
std::vector<std::string> Session::projection() const {
    std::vector<std::string> result;
    for (const auto &key : selection_.order()) {
        const auto &entry = entries_.at(key);
        if (!jobFilter_.empty() && text(get(entry, "id")) != jobFilter_)
            continue;
        const bool video = text(get(entry, "mediaType")) == "Video";
        const auto state = selection_.state(key);
        if (filter_ == "all" || (filter_ == "images" && !video) || (filter_ == "videos" && video) ||
            (filter_ == "unreviewed" && state == State::Unreviewed) ||
            (filter_ == "shortlisted" && state == State::Shortlisted) ||
            (filter_ == "rejected" && state == State::Rejected))
            result.push_back(key);
    }
    if (sort_ == "newest")
        std::reverse(result.begin(), result.end());
    else if (sort_ == "seed")
        std::stable_sort(result.begin(), result.end(), [&](const auto &a, const auto &b) {
            const auto left = seed(entries_.at(a)), right = seed(entries_.at(b));
            return left && (!right || seedLess(*left, *right));
        });
    return result;
}
void Session::rebuild(bool project, const std::vector<std::string> &metadataChanged, bool entriesChanged) {
    const auto next = project ? projection() : visible_;
    const auto previousCount = visible_.size();
    Change change{.previousCount = previousCount, .entriesChanged = entriesChanged};
    const bool same = next == visible_;
    const bool append =
        !same && next.size() > visible_.size() && std::equal(visible_.begin(), visible_.end(), next.begin());
    if (!same)
        change.projection = append ? ProjectionChange::Append : ProjectionChange::Reset;
    const std::unordered_set<std::string> metadata(metadataChanged.begin(), metadataChanged.end());
    if (same || append)
        for (std::size_t i = 0; i < previousCount; ++i) {
            const auto current = rowState(next[i]);
            const auto old = rowStates_[i];
            unsigned roles = 0;
            if (metadata.contains(next[i]))
                roles |= Metadata;
            if (current.review != old.review)
                roles |= Review;
            if (current.selected != old.selected)
                roles |= Selected;
            if (current.focused != old.focused)
                roles |= Focused;
            if (roles)
                change.rows.push_back({next[i], roles});
        }
    visible_ = next;
    visibleIndices_.clear();
    rowStates_.clear();
    rowStates_.reserve(visible_.size());
    for (std::size_t i = 0; i < visible_.size(); ++i) {
        visibleIndices_[visible_[i]] = static_cast<int>(i);
        rowStates_.push_back(rowState(visible_[i]));
    }
    selected_ = shortlisted_ = rejected_ = 0;
    for (const auto &key : selection_.order()) {
        selected_ += selection_.selected(key);
        shortlisted_ += selection_.state(key) == State::Shortlisted;
        rejected_ += selection_.state(key) == State::Rejected;
    }
    hiddenSelected_ = selected_;
    for (const auto &key : visible_)
        hiddenSelected_ -= selection_.selected(key);
    publish(std::move(change));
}
void Session::publishFocus(const std::string &previous) {
    Change change{.previousCount = visible_.size()};
    for (const auto &key : {previous, selection_.focusedKey()}) {
        const auto it = visibleIndices_.find(key);
        if (it == visibleIndices_.end())
            continue;
        rowStates_[static_cast<std::size_t>(it->second)].focused = (selection_.focusedKey() == key);
        change.rows.push_back({key, Focused});
    }
    publish(std::move(change));
}
void Session::setFilter(std::string_view filter) {
    mutableOnOwner();
    if (filter_ == filter || (filter != "all" && filter != "unreviewed" && filter != "shortlisted" &&
                              filter != "rejected" && filter != "images" && filter != "videos"))
        return;
    filter_ = filter;
    rebuild(true);
}
void Session::setSort(std::string_view sort) {
    mutableOnOwner();
    if (sort_ == sort || (sort != "arrival" && sort != "newest" && sort != "seed"))
        return;
    sort_ = sort;
    rebuild(true);
}
void Session::setJobFilter(std::string job) {
    mutableOnOwner();
    if (jobFilter_ == job)
        return;
    jobFilter_ = std::move(job);
    rebuild(true);
}
void Session::setSelectionMode(bool enabled) {
    mutableOnOwner();
    if (selectionMode_ == enabled)
        return;
    selectionMode_ = enabled;
    publish({.previousCount = visible_.size()});
}
void Session::selectAt(int index, bool range, bool add) {
    mutableOnOwner();
    if (index < 0 || static_cast<std::size_t>(index) >= visible_.size())
        return;
    const auto mode = range ? (add ? SelectionMode::AddRange : SelectionMode::Range)
                            : (add ? SelectionMode::Toggle : SelectionMode::Replace);
    selection_.select(visible_[static_cast<std::size_t>(index)], visible_, mode);
    if (range || add)
        selectionMode_ = true;
    rebuild(false);
}
void Session::focusAt(int index) {
    mutableOnOwner();
    if (index < 0 || static_cast<std::size_t>(index) >= visible_.size())
        return;
    const auto previous = selection_.focusedKey();
    const auto previousAnchor = selection_.anchorKey();
    if (selection_.focus(visible_[static_cast<std::size_t>(index)]))
        publishFocus(previous);
    else if (previousAnchor != selection_.anchorKey())
        publish({.previousCount = visible_.size()});
}
void Session::focusSource(std::string_view source) {
    mutableOnOwner();
    if (source.empty())
        return;
    const auto requested = sourceIdentity(source);
    for (const auto &key : selection_.order())
        for (auto field : {"documentSource", "mediaSource", "image", "imageSource", "source", "path"}) {
            const auto candidate = text(get(entries_.at(key), field));
            if (!candidate.empty() && sourceIdentity(candidate) == requested) {
                const auto previous = selection_.focusedKey();
                const auto previousAnchor = selection_.anchorKey();
                if (selection_.focus(key))
                    publishFocus(previous);
                else if (previousAnchor != selection_.anchorKey())
                    publish({.previousCount = visible_.size()});
                return;
            }
        }
}
void Session::toggle(const Value &entry) {
    mutableOnOwner();
    const auto key = keyForEntry(entry);
    auto visible = visible_;
    if (std::ranges::find(visible, key) == visible.end())
        visible.push_back(key);
    selection_.select(key, visible, SelectionMode::Toggle);
    selectionMode_ = true;
    rebuild(false);
}
void Session::selectAll() {
    mutableOnOwner();
    selection_.selectAll(visible_);
    selectionMode_ = true;
    rebuild(false);
}
void Session::clearSelection() {
    mutableOnOwner();
    selection_.clearSelection();
    selectionMode_ = false;
    rebuild(false);
}
std::vector<std::string> Session::actionKeys() const {
    auto keys = selection_.selectedKeys();
    if (keys.empty() && !selection_.focusedKey().empty())
        keys.push_back(selection_.focusedKey());
    return keys;
}
void Session::toggleReview(State value) {
    mutableOnOwner();
    selection_.toggleState(actionKeys(), value);
    rebuild(reviewFiltered());
}
void Session::setReview(State value) {
    mutableOnOwner();
    selection_.setState(actionKeys(), value);
    rebuild(reviewFiltered());
}
void Session::setEntryReview(std::string_view key, State value) {
    mutableOnOwner();
    selection_.setState({std::string(key)}, value);
    rebuild(reviewFiltered());
}
void Session::markEntries(const std::vector<Value> &entries, State value) {
    mutableOnOwner();
    std::vector<std::string> keys;
    for (const auto &entry : entries)
        keys.push_back(keyForEntry(entry));
    selection_.setState(keys, value);
    rebuild(reviewFiltered());
}
KeyResult Session::key(vm::ReviewKeyInput input, bool textEditing) {
    mutableOnOwner();
    if (textEditing)
        return {};
    input.selected = selected_ > 0;
    input.selectionMode = selectionMode_;
    input.count = static_cast<int>(visible_.size());
    input.focusedIndex = focusedIndex();
    const auto result = commands_.keys().send(input);
    if (!result || result->action == vm::ReviewAction::ignored)
        return {};
    const KeyResult reply{true, result->action, result->index};
    switch (result->action) {
    case vm::ReviewAction::clear:
        clearSelection();
        break;
    case vm::ReviewAction::selectAll:
        selectAll();
        break;
    case vm::ReviewAction::shortlist:
        toggleReview(State::Shortlisted);
        break;
    case vm::ReviewAction::reject:
        toggleReview(State::Rejected);
        break;
    case vm::ReviewAction::detail:
        if (selection_.focusedKey().empty())
            focusAt(0);
        break;
    case vm::ReviewAction::focus:
        if (input.shift)
            selectAt(result->index, true, input.command);
        else
            focusAt(result->index);
        break;
    default:
        break;
    }
    return reply;
}
NavigationState Session::navigationState() const {
    return {selection_.focusedKey(), selection_.anchorKey(), selection_.selectedKeys()};
}
ii::core::Result<bool> Session::restoreNavigation(NavigationState state) {
    mutableOnOwner();
    const auto previousFocus = selection_.focusedKey();
    const auto changed =
        selection_.restoreNavigation(std::move(state.currentKey), std::move(state.anchorKey), state.selectedKeys);
    if (!changed)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument, "Navigation references an unknown result");
    if (changed->selectionChanged)
        rebuild(false);
    else if (changed->changed && previousFocus != selection_.focusedKey())
        publishFocus(previousFocus);
    else if (changed->changed)
        publish({.previousCount = visible_.size()});
    return changed->changed;
}
int Session::indexForKey(std::string_view key) const {
    const auto found = visibleIndices_.find(std::string(key));
    return found == visibleIndices_.end() ? -1 : found->second;
}
int Session::focusedIndex() const { return indexForKey(selection_.focusedKey()); }
std::string Session::keyAt(int index) const {
    return index >= 0 && static_cast<std::size_t>(index) < visible_.size() ? visible_[static_cast<std::size_t>(index)]
                                                                           : std::string{};
}
Value Session::entryFor(std::string_view key) const {
    const auto found = entries_.find(std::string(key));
    return found == entries_.end() ? Value(Object{}) : found->second;
}
Value Session::entryAt(int index) const { return entryFor(keyAt(index)); }
std::optional<Row> Session::rowAt(int index) const {
    const auto key = keyAt(index);
    if (key.empty())
        return {};
    const auto &entry = entries_.at(key);
    const auto media = has(entry, "mediaType") ? scalar(get(entry, "mediaType")) : std::string("Image");
    const auto &thumbnail = has(entry, "thumbnailSource") ? get(entry, "thumbnailSource")
                            : has(entry, "imageSource")   ? get(entry, "imageSource")
                                                          : get(entry, "previewSource");
    const auto ordinal = ordinals_.at(key);
    std::ostringstream title;
    title.imbue(std::locale::classic());
    title << media << " · " << std::setfill('0') << std::setw(2) << ordinal;
    return Row{key,
               media,
               title.str(),
               entry,
               thumbnail,
               selection_.state(key),
               ordinal,
               selection_.selected(key),
               selection_.focusedKey() == key};
}
Value Session::focusedEntry() const { return entryFor(selection_.focusedKey()); }
State Session::reviewState(std::string_view key) const { return selection_.state(std::string(key)); }
bool Session::isSelected(const Value &entry) const { return selection_.selected(keyForEntry(entry)); }
std::vector<Value> Session::entries() const {
    std::vector<Value> result;
    result.reserve(totalCount());
    for (const auto &key : selection_.order())
        result.push_back(entries_.at(key));
    return result;
}
std::vector<Value> Session::selectedEntries() const {
    std::vector<Value> result;
    for (const auto &key : selection_.selectedKeys())
        result.push_back(entries_.at(key));
    return result;
}
std::vector<Value> Session::selectedImages() const {
    auto result = selectedEntries();
    std::erase_if(result, [](const auto &item) { return !imageEntry(item); });
    return result;
}
std::vector<Value> Session::actionEntries() const {
    std::vector<Value> result;
    for (const auto &key : actionKeys())
        result.push_back(entries_.at(key));
    return result;
}
std::vector<Value> Session::contextEntries() const {
    return isSelected(focusedEntry()) ? selectedEntries() : std::vector<Value>{focusedEntry()};
}
std::vector<InspectorRow> Session::inspectorRows() const {
    const auto targets = actionEntries();
    if (targets.empty())
        return {};
    const auto combine = [&](std::string label, auto project) {
        InspectorRow row{.label = std::move(label)};
        for (const auto &target : targets) {
            auto value = project(target);
            if (std::ranges::find(row.values, value) == row.values.end())
                row.values.push_back(std::move(value));
        }
        row.mixed = row.values.size() > 1;
        row.value = row.mixed ? "Mixed · " + std::to_string(row.values.size()) + " values" : row.values.front();
        return row;
    };
    return {combine("Model", modelName),
            combine("Seed", [](const auto &e) { return available(e, "seed"); }),
            combine("Resolution", resolution),
            combine("Generation time", elapsed),
            combine("Steps", [](const auto &e) { return available(e, "steps"); }),
            combine("Device", [](const auto &e) { return available(e, "device"); }),
            combine("Generation settings", settings),
            combine("Prompt", [](const auto &e) { return available(e, "prompt"); }),
            combine("Negative prompt", [](const auto &e) { return available(e, "negativePrompt"); })};
}
} // namespace dreamscapes::review
