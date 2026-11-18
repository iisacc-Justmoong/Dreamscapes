#pragma once
#include <iiSocialIntegration>
#include <iiSDKCore>
#include <functional>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace dreamscapes::publishing {
struct PublicationRequest final {
    std::string id;
    iiSocialIntegration::PublicationPlan plan;
};
struct PublicationOutcome final {
    std::string id,postId,message;
    std::size_t completed=0;
    bool published=false,needsReconciliation=false;
};

// Account grants stay in this native session and never enter view properties.
class PublicationService final : public std::enable_shared_from_this<PublicationService> {
  public:
    using Submit=std::function<bool(ii::runtime::Task)>;
    static std::shared_ptr<PublicationService> create(ii::runtime::EventLoop &,Submit,
        std::shared_ptr<iiSocialIntegration::Transport> transport={});
    ~PublicationService();
    [[nodiscard]] ii::core::Result<void> configure(std::string,iiSocialIntegration::Account);
    [[nodiscard]] bool hasAccount(std::string_view) const;
    [[nodiscard]] ii::core::Result<void> submit(std::vector<PublicationRequest>,std::shared_ptr<const void> mediaLease);
    void close();
    ii::core::Observable<bool> busy{false};
    ii::core::Observable<std::string> status;
    ii::core::Observable<std::uint64_t> accountsRevision{0};
    ii::core::Observable<std::shared_ptr<const std::vector<PublicationOutcome>>> outcomes;
  private:
    struct Bridge;
    PublicationService(ii::runtime::EventLoop::Sender,Submit,std::shared_ptr<iiSocialIntegration::Transport>);
    void start();
    void deliver();
    [[nodiscard]] ii::core::Result<void> writable() const;
    const std::thread::id owner_=std::this_thread::get_id();
    ii::runtime::EventLoop::Sender sender_;
    ii::runtime::TaskToken wake_;
    Submit submit_;
    std::shared_ptr<iiSocialIntegration::Transport> transport_;
    std::map<std::string,iiSocialIntegration::Account,std::less<>> accounts_;
    std::shared_ptr<Bridge> bridge_;
    std::shared_ptr<std::stop_source> stop_;
    bool active_=true;
};

// Called once before workers start. Secret environment values are consumed and
// removed from this process before child generators can inherit them.
[[nodiscard]] std::map<std::string,iiSocialIntegration::Account,std::less<>> publicationAccountsFromEnvironment();
}
