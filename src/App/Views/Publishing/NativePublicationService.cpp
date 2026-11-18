#include "NativePublicationService.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace dreamscapes::publishing {
using ii::core::ErrorCode;
using namespace iiSocialIntegration;
namespace {
struct WorkItem final {std::string id;PublicationPlan plan;Account account;};
std::string environment(const std::string &key,bool secret=false) {
    const auto *value=std::getenv(key.c_str());std::string copy=value?value:std::string{};
    if (secret && value) {
#if defined(_WIN32)
        (void)_putenv_s(key.c_str(),"");
#else
        (void)unsetenv(key.c_str());
#endif
    }
    return copy;
}
bool enabled(std::string_view value) {return value=="1" || value=="true" || value=="yes";}
std::string prefix(std::string value) {
    for (auto &c:value) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return "DREAMSCAPES_PUBLISH_"+value+"_";
}
PublicationOutcome failure(std::string id,std::string message) {
    PublicationOutcome result;result.id=std::move(id);result.message=std::move(message);return result;
}
std::vector<PublicationOutcome> execute(const std::vector<WorkItem> &items,Transport &transport,std::stop_token stop) {
    std::vector<PublicationOutcome> out;out.reserve(items.size());
    // Validate the whole batch before any provider is contacted.
    std::optional<std::pair<std::size_t,std::string>> invalid;
    for (std::size_t i=0;i<items.size() && !invalid;++i) {
        const auto &item=items[i];
        if (!item.plan.ready() || !item.plan.scheduledAt.empty()) {invalid={{i,"Resolve review issues before publishing"}};break;}
        if (item.plan.platform==Platform::X && !item.account.allowSelfThreadReplies &&
            std::ranges::any_of(item.plan.posts,[](const auto &post){return post.replyTo.has_value();})) {
            invalid={{i,"X reply entitlement has not been verified"}};break;
        }
        for (const auto &post:item.plan.posts) {
            auto checked=validatePost(item.plan.platform,post.post,item.account);
            if (checked.error!=Error::None) {invalid={{i,checked.message}};break;}
        }
    }
    if (invalid) {
        for (std::size_t i=0;i<items.size();++i)
            out.push_back(failure(items[i].id,i==invalid->first?invalid->second:"Not submitted; another target needs attention"));
        return out;
    }
    Context context{transport};context.stop=stop;
    for (const auto &item:items) {
        if (stop.stop_requested()) {out.push_back(failure(item.id,"Publishing was cancelled"));continue;}
        try {
            auto result=publishPlan(item.plan,item.account,context);
            PublicationOutcome value;value.id=item.id;value.published=result.published();value.postId=result.result.postId;
            value.completed=result.completed.size();value.needsReconciliation=result.requiresReconciliation();
            value.message=value.published?"Published":result.result.message;
            if (value.message.empty()) value.message="Publication failed";
            out.push_back(std::move(value));
        } catch (...) {auto value=failure(item.id,"Publishing result is unknown");value.needsReconciliation=true;out.push_back(std::move(value));}
    }
    return out;
}
}
std::map<std::string,Account,std::less<>> publicationAccountsFromEnvironment() {
    std::map<std::string,Account,std::less<>> result;
    for (const auto &capability:capabilities()) {
        const auto name=prefix(capability.id);Account account;
        account.accessToken=environment(name+"ACCESS_TOKEN",true);
        if (!capability.officialPublishingApi || account.accessToken.empty()) continue;
        account.targetId=environment(name+"TARGET_ID");account.instanceUrl=environment(name+"INSTANCE_URL");
        account.apiVersion=environment(name+"API_VERSION");
        account.allowBillableRequests=enabled(environment(name+"ALLOW_BILLABLE"));
        account.allowSelfThreadReplies=enabled(environment(name+"ALLOW_THREAD_REPLIES"));
        auto scopes=environment(name+"SCOPES");std::ranges::replace(scopes,',',' ');
        std::istringstream input(scopes);for (std::string scope;input>>scope;) account.scopes.push_back(std::move(scope));
        result.emplace(capability.id,std::move(account));
    }
    return result;
}
struct PublicationService::Bridge final {
    std::mutex mutex;
    std::shared_ptr<const std::vector<PublicationOutcome>> completion;
    ii::runtime::EventLoop::Sender sender;
    ii::runtime::TaskToken wake;
    bool closed=false;
};
PublicationService::PublicationService(ii::runtime::EventLoop::Sender sender,Submit submit,std::shared_ptr<Transport> transport)
    :sender_(std::move(sender)),submit_(std::move(submit)),transport_(std::move(transport)) {
    if (!submit_) throw std::invalid_argument("Publication requires a worker");
    if (!transport_) transport_=std::make_shared<CurlTransport>();
}
std::shared_ptr<PublicationService> PublicationService::create(ii::runtime::EventLoop &loop,Submit submit,std::shared_ptr<Transport> transport) {
    auto service=std::shared_ptr<PublicationService>(new PublicationService(loop.sender(),std::move(submit),std::move(transport)));
    service->start();return service;
}
void PublicationService::start() {
    const std::weak_ptr<PublicationService> weak=shared_from_this();
    const auto registered=sender_.on_signal([weak]{if (auto value=weak.lock()) value->deliver();});
    if (!registered) throw std::runtime_error("Cannot register publication completion");
    wake_=registered.token;bridge_=std::make_shared<Bridge>();bridge_->sender=sender_;bridge_->wake=wake_;
}
PublicationService::~PublicationService() {if (active_ && owner_==std::this_thread::get_id()) try {close();} catch (...) {}}
ii::core::Result<void> PublicationService::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Publication requires its owner thread");
    if (!active_) return ii::core::fail(ErrorCode::closed,"Publication service is closed");return {};
}
ii::core::Result<void> PublicationService::configure(std::string id,Account account) {
    if (auto valid=writable();!valid) return valid;
    const auto kind=platform(id);
    if (!kind || !makePublisher(*kind)->capability().officialPublishingApi || account.accessToken.empty())
        return ii::core::fail(ErrorCode::invalid_argument,"A supported target and user grant are required");
    accounts_.insert_or_assign(std::move(id),std::move(account));
    (void)accountsRevision.set(accountsRevision.get()+1);return {};
}
bool PublicationService::hasAccount(std::string_view id) const {
    return owner_==std::this_thread::get_id() && active_ && accounts_.contains(id);
}
ii::core::Result<void> PublicationService::submit(std::vector<PublicationRequest> requests,std::shared_ptr<const void> lease) {
    if (auto valid=writable();!valid) return valid;
    if (busy.get()) return ii::core::fail(ErrorCode::unsupported,"Publishing is already running");
    if (requests.empty()) return ii::core::fail(ErrorCode::invalid_argument,"Select a publication target");
    std::vector<WorkItem> items;items.reserve(requests.size());
    for (auto &request:requests) {
        const auto found=accounts_.find(request.id);const auto kind=platform(request.id);
        if (found==accounts_.end() || !kind || request.plan.platform!=*kind)
            return ii::core::fail(ErrorCode::invalid_argument,"A selected target has no account or matching plan");
        items.push_back({std::move(request.id),std::move(request.plan),found->second});
    }
    auto bridge=bridge_;auto transport=transport_;stop_=std::make_shared<std::stop_source>();auto stop=stop_;
    (void)outcomes.set({});(void)status.set("Publishing…");(void)busy.set(true);
    bool accepted=false;
    try {accepted=submit_([bridge,transport,stop,items=std::move(items),lease=std::move(lease)] {
        (void)lease;
        std::vector<PublicationOutcome> values;
        try {values=execute(items,*transport,stop->get_token());}
        catch (...) {for (const auto &item:items) {auto value=failure(item.id,"Publishing result is unknown");
            value.needsReconciliation=true;values.push_back(std::move(value));}}
        auto result=std::make_shared<const std::vector<PublicationOutcome>>(std::move(values));
        {std::lock_guard lock(bridge->mutex);if (bridge->closed) return;bridge->completion=std::move(result);}
        (void)bridge->sender.signal(bridge->wake,ii::runtime::Clock::now());
    });} catch (...) {accepted=false;}
    if (!accepted) {(void)busy.set(false);(void)status.set("Publication worker unavailable");return ii::core::fail(ErrorCode::failed,status.get());}
    return {};
}
void PublicationService::deliver() {
    if (!writable()) return;
    std::shared_ptr<const std::vector<PublicationOutcome>> completed;
    {std::lock_guard lock(bridge_->mutex);completed=std::exchange(bridge_->completion,{});}
    if (!completed) return;
    const auto succeeded=std::ranges::count_if(*completed,[](const auto &item){return item.published;});
    (void)outcomes.set(completed);(void)status.set(std::to_string(succeeded)+" published · "+
        std::to_string(completed->size()-succeeded)+" need attention");(void)busy.set(false);stop_.reset();
}
void PublicationService::close() {
    if (!writable()) return;active_=false;if (stop_) stop_->request_stop();
    {std::lock_guard lock(bridge_->mutex);bridge_->closed=true;bridge_->completion.reset();}
    (void)sender_.cancel(wake_);accounts_.clear();submit_={};
}
}
