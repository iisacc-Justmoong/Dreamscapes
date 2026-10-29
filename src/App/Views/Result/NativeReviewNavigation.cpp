#include "NativeReviewNavigation.h"
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace dreamscapes::review {
namespace {
class SyncGuard final {
  public:
    explicit SyncGuard(bool &value) : value_(value) { value_ = true; }
    ~SyncGuard() { value_ = false; }

  private:
    bool &value_;
};
} // namespace

ReviewNavigation::ReviewNavigation(std::shared_ptr<Session> session,
                                   std::shared_ptr<lvui::native::CollectionNavigation> navigation, bool ownsNavigation)
    : session_(std::move(session)), navigation_(std::move(navigation)), ownsNavigation_(ownsNavigation) {
    if (!session_ || !navigation_)
        throw std::invalid_argument("Review navigation requires a session and a navigation object");
}

std::shared_ptr<ReviewNavigation> ReviewNavigation::create(std::shared_ptr<Session> session) {
    auto result = std::shared_ptr<ReviewNavigation>(new ReviewNavigation(
        std::move(session), std::make_shared<lvui::native::CollectionNavigation>(), true));
    result->start();
    return result;
}

std::shared_ptr<ReviewNavigation> ReviewNavigation::bind(
    std::shared_ptr<Session> session, std::shared_ptr<lvui::native::CollectionNavigation> navigation) {
    auto result = std::shared_ptr<ReviewNavigation>(new ReviewNavigation(std::move(session), std::move(navigation), false));
    result->start();
    return result;
}

ReviewNavigation::~ReviewNavigation() {
    if (active_ && owner_ == std::this_thread::get_id())
        close();
}

void ReviewNavigation::requireOwner() const {
    if (owner_ != std::this_thread::get_id())
        throw std::logic_error("Review navigation requires its owner thread");
    if (!active_)
        throw std::logic_error("Review navigation is closed");
}

void ReviewNavigation::start() {
    requireOwner();
    const std::weak_ptr<ReviewNavigation> weak = shared_from_this();
    sessionObserver_ = std::make_shared<ChangePort>([weak](const Change &change) {
        if (auto owner = weak.lock(); owner && owner->active_ && !owner->syncing_)
            owner->syncFromSession(change.entriesChanged);
    });
    if (const auto result = session_->observe(sessionObserver_); !result)
        throw std::runtime_error(result.error().message);
    navigation_->configure({.mode = lvui::native::CollectionSelectionMode::multiple,
                            .select_on_move = false, .anchor_on_unselected_focus = true});
    syncFromSession(true);
    navigationObserver_ = navigation_->state.observe([weak](const auto &state, std::uint64_t) {
        if (auto owner = weak.lock(); owner && owner->active_ && !owner->syncing_)
            owner->syncFromNavigation(state);
    });
}

lvui::native::CollectionNavigation &ReviewNavigation::navigation() {
    requireOwner();
    return *navigation_;
}

std::string ReviewNavigation::tokenForKey(std::string_view key) const {
    requireOwner();
    const auto found = tokensByKey_.find(std::string(key));
    return found == tokensByKey_.end() ? std::string{} : found->second;
}

std::string ReviewNavigation::keyForToken(std::string_view token) const {
    requireOwner();
    const auto found = keysByToken_.find(std::string(token));
    return found == keysByToken_.end() ? std::string{} : found->second;
}

std::vector<std::string> ReviewNavigation::visibleTokens() const {
    requireOwner();
    std::vector<std::string> result;
    result.reserve(session_->visibleCount());
    for (std::size_t i = 0; i < session_->visibleCount(); ++i) {
        auto token = tokenForKey(session_->keyAt(static_cast<int>(i)));
        if (token.empty())
            throw std::logic_error("Visible review result has no navigation identity");
        result.push_back(std::move(token));
    }
    return result;
}

void ReviewNavigation::syncFromSession(bool refreshIdentities) {
    requireOwner();
    if (syncing_)
        return;
    SyncGuard guard(syncing_);
    if (refreshIdentities) {
        std::unordered_map<std::string, std::string> nextTokens, nextKeys;
        const auto &keys = session_->allKeys();
        nextTokens.reserve(keys.size());
        nextKeys.reserve(keys.size());
        auto nextToken = nextToken_;
        for (const auto &key : keys) {
            const auto old = tokensByKey_.find(key);
            std::string token;
            if (old != tokensByKey_.end())
                token = old->second;
            else {
                if (nextToken == std::numeric_limits<std::uint64_t>::max())
                    throw std::overflow_error("Review navigation identity space is exhausted");
                token = "review-" + std::to_string(++nextToken);
            }
            nextKeys.emplace(token, key);
            nextTokens.emplace(key, std::move(token));
        }
        tokensByKey_ = std::move(nextTokens);
        keysByToken_ = std::move(nextKeys);
        nextToken_ = nextToken;
    }
    const auto snapshot = session_->navigationState();
    const auto tokenFor = [&](const std::string &key) {
        if (key.empty())
            return std::string{};
        const auto found = tokensByKey_.find(key);
        if (found == tokensByKey_.end())
            throw std::logic_error("Review navigation state references a missing result");
        return found->second;
    };
    lvui::native::CollectionSelectionState state;
    state.current_key = tokenFor(snapshot.currentKey);
    state.anchor_key = tokenFor(snapshot.anchorKey);
    state.selected_keys.reserve(snapshot.selectedKeys.size());
    for (const auto &key : snapshot.selectedKeys)
        state.selected_keys.push_back(tokenFor(key));
    (void)navigation_->set_state(std::move(state));
}

void ReviewNavigation::syncFromNavigation(const lvui::native::CollectionSelectionState &state) {
    requireOwner();
    if (syncing_)
        return;
    NavigationState snapshot;
    const auto resolve = [&](const std::string &token, std::string &key) {
        if (token.empty())
            return true;
        const auto found = keysByToken_.find(token);
        if (found == keysByToken_.end())
            return false;
        key = found->second;
        return true;
    };
    bool valid = resolve(state.current_key, snapshot.currentKey) && resolve(state.anchor_key, snapshot.anchorKey);
    for (const auto &token : state.selected_keys) {
        std::string key;
        if (!resolve(token, key)) {
            valid = false;
            break;
        }
        snapshot.selectedKeys.push_back(std::move(key));
    }
    if (!valid) {
        syncFromSession();
        return;
    }
    {
        SyncGuard guard(syncing_);
        const auto result = session_->restoreNavigation(std::move(snapshot));
        if (result)
            return;
    }
    syncFromSession();
}

void ReviewNavigation::close() {
    if (!active_)
        return;
    requireOwner();
    active_ = false;
    navigationObserver_.reset();
    sessionObserver_.reset();
    if (ownsNavigation_)
        navigation_->close();
    tokensByKey_.clear();
    keysByToken_.clear();
    navigation_.reset();
    session_.reset();
}
} // namespace dreamscapes::review
