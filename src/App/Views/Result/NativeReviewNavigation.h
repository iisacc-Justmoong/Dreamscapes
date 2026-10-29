#pragma once

#include "NativeReviewSession.h"
#include <LVUI/NativeCollectionNavigation.h>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace dreamscapes::review {

// Connects a product-owned review session to LVUI's virtual collection. The
// stable widget keys are opaque and never replace original result identities.
class ReviewNavigation final : public std::enable_shared_from_this<ReviewNavigation> {
  public:
    static std::shared_ptr<ReviewNavigation> create(std::shared_ptr<Session> session);
    static std::shared_ptr<ReviewNavigation> bind(std::shared_ptr<Session> session,
                                                std::shared_ptr<lvui::native::CollectionNavigation> navigation);
    ~ReviewNavigation();
    ReviewNavigation(const ReviewNavigation &) = delete;
    ReviewNavigation &operator=(const ReviewNavigation &) = delete;

    lvui::native::CollectionNavigation &navigation();
    std::string tokenForKey(std::string_view key) const;
    std::string keyForToken(std::string_view token) const;
    std::vector<std::string> visibleTokens() const;
    void close();

  private:
    ReviewNavigation(std::shared_ptr<Session> session,
                     std::shared_ptr<lvui::native::CollectionNavigation> navigation, bool ownsNavigation);
    void requireOwner() const;
    void start();
    void syncFromSession(bool refreshIdentities = false);
    void syncFromNavigation(const lvui::native::CollectionSelectionState &state);

    const std::thread::id owner_ = std::this_thread::get_id();
    std::shared_ptr<Session> session_;
    std::shared_ptr<lvui::native::CollectionNavigation> navigation_;
    std::shared_ptr<ChangePort> sessionObserver_;
    ii::core::ObservableSubscription navigationObserver_;
    std::unordered_map<std::string, std::string> tokensByKey_, keysByToken_;
    std::uint64_t nextToken_ = 0;
    bool active_ = true, syncing_ = false, ownsNavigation_ = false;
};

} // namespace dreamscapes::review
