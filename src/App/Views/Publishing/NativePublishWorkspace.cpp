#include "NativePublishWorkspace.h"
#include "NativePublishMedia.h"
#include "App/Views/Editor/NativeEditorFrame.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace dreamscapes::publishing {
using ii::core::ErrorCode;
namespace {
std::string destinationKey(std::string_view platform,std::string_view field) {
    return "publish.destination."+std::string(platform)+"."+std::string(field);
}
const std::string *textSetting(const iiSharedCanvas::PublicationMetadata &metadata,const std::string &key) {
    const auto found=metadata.settings.find(key);
    return found==metadata.settings.end()?nullptr:std::get_if<std::string>(&found->second);
}
const bool *boolSetting(const iiSharedCanvas::PublicationMetadata &metadata,const std::string &key) {
    const auto found=metadata.settings.find(key);
    return found==metadata.settings.end()?nullptr:std::get_if<bool>(&found->second);
}
std::vector<std::string> tags(std::string_view value) {
    std::vector<std::string> result;
    while (!value.empty()) {
        const auto end=value.find(',');auto part=value.substr(0,end);
        const auto first=part.find_first_not_of(" \t\r\n"),last=part.find_last_not_of(" \t\r\n");
        if (first!=std::string_view::npos) result.emplace_back(part.substr(first,last-first+1));
        if (end==std::string_view::npos) break;
        value.remove_prefix(end+1);
    }
    return result;
}
void applyMetadata(iiSocialIntegration::ReviewSource &source,const iiSharedCanvas::PublicationMetadata &metadata,
                   std::string_view platform) {
    auto text=[&](std::string_view name,std::string fallback) {
        if (const auto *value=textSetting(metadata,destinationKey(platform,name))) return *value;
        if (const auto *value=textSetting(metadata,std::string(name))) return *value;
        return fallback;
    };
    auto flag=[&](std::string_view name,bool fallback) {
        if (const auto *value=boolSetting(metadata,destinationKey(platform,name))) return *value;
        if (const auto *value=boolSetting(metadata,std::string(name))) return *value;
        return fallback;
    };
    if (const auto *value=textSetting(metadata,destinationKey(platform,"title"))) source.post.title=*value;
    else source.post.title=metadata.title;
    auto body=metadata.description;
    for (const auto name:{"credits","mentions"}) if (const auto *value=textSetting(metadata,name);value && !value->empty()) {
        if (!body.empty()) body+='\n';body+=*value;
    }
    if (const auto *value=textSetting(metadata,destinationKey(platform,"text"))) body=*value;
    source.post.text=std::move(body);
    if (const auto *value=textSetting(metadata,destinationKey(platform,"tags"))) source.post.tags=tags(*value);
    else if (const auto *value=textSetting(metadata,"tags")) source.post.tags=tags(*value);
    source.post.link=text("link",text("linkedSource",source.post.link));
    source.post.visibility=text("visibility",source.post.visibility);
    source.post.language=text("language",source.post.language);
    source.post.contentWarning=text("contentWarning",source.post.contentWarning);
    source.post.replyControl=text("replyControl",source.post.replyControl);
    if (platform=="x" && source.post.replyControl=="everyone") source.post.replyControl.clear();
    source.post.madeWithAi=flag("madeWithAi",source.post.madeWithAi);
    const auto adult=boolSetting(metadata,"adult"),violence=boolSetting(metadata,"violence");
    const auto sensitive=(adult || violence)?(adult && *adult)||(violence && *violence):source.post.sensitive;
    source.post.sensitive=flag("sensitive",sensitive);
    if (platform=="pinterest") source.post.coverImageUrl=text("coverImageUrl",source.post.coverImageUrl);
    if (platform=="youtube") {
        source.post.youtube.categoryId=text("categoryId",source.post.youtube.categoryId);
        if (const auto *audience=textSetting(metadata,destinationKey(platform,"madeForKids"))) {
            if (*audience=="Yes") source.post.youtube.madeForKids=true;
            else if (*audience=="No") source.post.youtube.madeForKids=false;
            else if (*audience=="Channel default") source.post.youtube.madeForKids.reset();
        }
        if (const auto *synthetic=boolSetting(metadata,destinationKey(platform,"syntheticMedia")))
            source.post.youtube.containsSyntheticMedia=*synthetic;
        else if (const auto *synthetic=boolSetting(metadata,"madeWithAi"))
            source.post.youtube.containsSyntheticMedia=*synthetic;
        source.post.youtube.notifySubscribers=flag("notifySubscribers",source.post.youtube.notifySubscribers);
    }
    if (const auto *publishTime=textSetting(metadata,"publishTime")) {
        const auto *when=textSetting(metadata,"scheduledAt");
        source.scheduledAt=*publishTime=="Schedule" && when?*when:std::string{};
    }
}
struct DestinationOptions final {iiSocialIntegration::ReviewOptions options;std::string error;};
DestinationOptions destinationOptions(const iiSharedCanvas::Document *document,std::string_view id,iiSocialIntegration::Platform platform) {
    DestinationOptions result;
    if (platform==iiSocialIntegration::Platform::Pinterest) result.options.repeatIntroduction=true;
    if (!document || !document->publicationMetadata) return result;
    const auto &settings=document->publicationMetadata->settings;
    const auto sequence=settings.find(destinationKey(id,"sequence"));
    if (sequence!=settings.end()) {
        if (const auto *value=std::get_if<std::string>(&sequence->second)) {
            if (*value=="Thread") result.options.sequence=iiSocialIntegration::Sequence::Thread;
            else if (*value=="Separate posts") result.options.sequence=iiSocialIntegration::Sequence::Separate;
            else if (*value=="Single post") result.options.sequence=iiSocialIntegration::Sequence::Single;
            else if (*value!="Automatic") result.error="Saved publish structure is not recognized";
        } else result.error="Saved publish structure is not recognized";
    }
    const auto media=settings.find(destinationKey(id,"mediaPerPost"));
    if (media!=settings.end()) {
        if (const auto *value=std::get_if<std::string>(&media->second)) {
            if (*value!="Automatic") {
                unsigned count=0;const auto parsed=std::from_chars(value->data(),value->data()+value->size(),count);
                if (value->empty() || parsed.ec!=std::errc{} || parsed.ptr!=value->data()+value->size() ||
                    count==0 || count>iiSocialIntegration::reviewPolicy(platform).mediaPerPost)
                    result.error="Saved publication options are not recognized";
                else result.options.mediaPerPost=count;
            }
        } else result.error="Saved publication options are not recognized";
    }
    for (const auto &[key,target]:{
        std::pair{"includeCaptions",&result.options.includeCaptions},std::pair{"numberParts",&result.options.numberParts},
        std::pair{"repeatIntroduction",&result.options.repeatIntroduction},std::pair{"splitText",&result.options.splitText}}) {
        const auto entry=settings.find(destinationKey(id,key));
        if (entry!=settings.end()) {
            if (const auto *value=std::get_if<bool>(&entry->second)) *target=*value;
            else result.error="Saved publication options are not recognized";
        }
    }
    return result;
}
}
WorkspaceModel::WorkspaceModel(std::shared_ptr<native::DocumentSession> document) : document_(std::move(document)) {
    if (!document_) throw std::invalid_argument("Publish workspace requires a document session");
}
std::shared_ptr<WorkspaceModel> WorkspaceModel::create(std::shared_ptr<native::DocumentSession> document) {
    auto model=std::shared_ptr<WorkspaceModel>(new WorkspaceModel(std::move(document))); model->start(); return model;
}
void WorkspaceModel::start() {
    const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
    documentRevision_=document_->revision.observe([weak](const auto &,std::uint64_t) {
        if (auto model=weak.lock()) model->synchronizeMetadata();
    });
    auto invalidate=[weak](const auto &,std::uint64_t) {
        if (auto model=weak.lock()) {model->source_.reset(); model->rebuild();}
    };
    documentVisual_=document_->visualRevision.observe(invalidate);
    documentGeneration_=document_->documentGeneration.observe(invalidate);
    documentFrame_=document_->frame.observe(invalidate);
    std::vector<std::shared_ptr<Target>> rows;
    for (const auto &capability : iiSocialIntegration::capabilities()) {
        auto row=std::make_shared<Target>(); row->id=capability.id; row->name=capability.name;
        row->official=capability.officialPublishingApi; row->owner=shared_from_this(); rows.push_back(std::move(row));
    }
    (void)targets.set(std::move(rows));
    synchronizeMetadata();
}
ii::core::Result<void> WorkspaceModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Publish workspace requires its owner thread");
    return {};
}
ii::core::Result<void> WorkspaceModel::select(std::string_view id) {
    if (auto valid=writable(); !valid) return valid;
    const auto found=std::ranges::find_if(targets.get(),[&](const auto &target){return target->id==id;});
    const auto platform=iiSocialIntegration::platform(id);
    if (found==targets.get().end() || !platform) return ii::core::fail(ErrorCode::invalid_argument,"Unknown publication target");
    const auto requested=(*found)->id;
    platform_=*platform;platformKey_=requested;
    const auto policy=iiSocialIntegration::reviewPolicy(*platform);
    (void)platformName.set((*found)->name);
    if (platformKey_!=requested) return {};
    (void)maximumMedia.set(policy.mediaPerPost);
    if (platformKey_!=requested) return {};
    (void)policyText.set("Up to "+std::to_string(policy.mediaPerPost)+" media per post");
    if (platformKey_!=requested) return {};
    for (const auto &target : targets.get()) {
        (void)target->selected.set(target->id==requested);
        if (platformKey_!=requested) return {};
    }
    synchronizeMetadata(); return {};
}
ii::core::Result<void> WorkspaceModel::editTitle(std::string value) {
    if (auto valid=writable(); !valid) return valid;
    auto result=document_->editPublication("title",value);
    return result;
}
ii::core::Result<void> WorkspaceModel::setTargetIncluded(std::string_view id,bool value) {
    if (auto valid=writable(); !valid) return valid;
    if (std::ranges::none_of(targets.get(),[&](const auto &target){return target->id==id;}))
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown publication target");
    auto result=document_->editPublicationSetting(destinationKey(id,"selected"),value);
    if (result) synchronizeMetadata();
    return result;
}
ii::core::Result<void> WorkspaceModel::editDescription(std::string value) {
    if (auto valid=writable(); !valid) return valid;
    auto result=document_->editPublication("description",value);
    return result;
}
void WorkspaceModel::setDraftSaveHandler(std::function<ii::core::Result<void>()> value) {
    if (!writable()) return;
    saveDraft_=std::move(value);(void)canSaveDraft.set(draftSaveAvailable_ && bool(saveDraft_));
}
void WorkspaceModel::setDraftSaveAvailable(bool value) {
    if (!writable()) return;
    draftSaveAvailable_=value;(void)canSaveDraft.set(value && bool(saveDraft_));
}
void WorkspaceModel::setDraftMessage(std::string value) {
    if (writable()) (void)draftMessage.set(std::move(value));
}
ii::core::Result<void> WorkspaceModel::saveDraft() {
    if (auto valid=writable(); !valid) return valid;
    auto handler=saveDraft_;
    if (!canSaveDraft.get() || !handler) return ii::core::fail(ErrorCode::unsupported,"Draft save is unavailable");
    auto result=handler();
    if (!result) (void)draftMessage.set(result.error().message);
    return result;
}
ii::core::Result<void> WorkspaceModel::setReviewSource(iiSocialIntegration::ReviewSource source) {
    if (auto valid=writable(); !valid) return valid;
    if (!document_->hasDocument.get()) return ii::core::fail(ErrorCode::closed,"No canvas document is open");
    source_=std::move(source); synchronizeMetadata(); return {};
}
ii::core::Result<void> WorkspaceModel::setThreaded(bool value) {
    if (auto valid=writable(); !valid) return valid;
    if (!platform_ || !canThread.get()) return ii::core::fail(ErrorCode::unsupported,"Reply chains are unavailable");
    auto result=document_->editPublicationSetting(destinationKey(platformKey_,"sequence"),
        std::string(value?"Thread":"Separate posts"));
    if (!result) return result;
    synchronizeMetadata(); return {};
}
void WorkspaceModel::setPublicationService(std::shared_ptr<PublicationService> service) {
    if (!writable()) return;
    publicationAccounts_={};publicationBusy_={};publicationStatus_={};publicationOutcomes_={};
    publicationService_=std::move(service);
    if (publicationService_) {
        const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
        publicationAccounts_=publicationService_->accountsRevision.observe([weak](const auto &,std::uint64_t) {
            if (auto model=weak.lock()) model->synchronizePublication();
        });
        publicationBusy_=publicationService_->busy.observe([weak](const auto &,std::uint64_t) {
            if (auto model=weak.lock()) model->synchronizePublication();
        });
        publicationStatus_=publicationService_->status.observe([weak](const auto &message,std::uint64_t) {
            if (auto model=weak.lock()) (void)model->publicationStatus.set(message);
        },true);
        publicationOutcomes_=publicationService_->outcomes.observe([weak](const auto &,std::uint64_t) {
            if (auto model=weak.lock()) model->synchronizePublication();
        });
    } else (void)publicationStatus.set({});
    synchronizePublication();
}
ii::core::Result<std::vector<PublicationRequest>> WorkspaceModel::publicationRequests() const {
    if (!publicationService_ || publicationService_->busy.get())
        return ii::core::fail(ErrorCode::unsupported,"Publication service is unavailable or busy");
    const auto snapshot=document_->snapshot();
    if (!snapshot || !source_) return ii::core::fail(ErrorCode::unsupported,"Prepare canvas media first");
    std::vector<PublicationRequest> requests;
    for (const auto &target:targets.get()) {
        if (!target->included.get()) continue;
        const auto platform=iiSocialIntegration::platform(target->id);
        if (!platform || !target->official || !publicationService_->hasAccount(target->id))
            return ii::core::fail(ErrorCode::unsupported,"A selected target requires a supported connected account");
        auto configured=destinationOptions(snapshot.get(),target->id,*platform);
        if (!configured.error.empty()) return ii::core::fail(ErrorCode::invalid_argument,configured.error);
        auto source=*source_;
        if (snapshot->publicationMetadata) applyMetadata(source,*snapshot->publicationMetadata,target->id);
        auto plan=iiSocialIntegration::planPublication(*platform,source,configured.options);
        if (!plan.ready() || !plan.scheduledAt.empty())
            return ii::core::fail(ErrorCode::unsupported,"Resolve the selected target's publication review");
        requests.push_back({target->id,std::move(plan)});
    }
    if (requests.empty()) return ii::core::fail(ErrorCode::invalid_argument,"Select a publication target");
    return requests;
}
void WorkspaceModel::synchronizePublication() {
    if (!writable()) return;
    const auto result=publicationService_?publicationService_->outcomes.get():nullptr;
    for (const auto &target:targets.get()) {
        (void)target->accountStatus.set(!target->official?"Publishing unavailable":
            publicationService_ && publicationService_->hasAccount(target->id)?"Account configured":"Connect an account");
        std::string message;
        if (result) {
            const auto found=std::ranges::find_if(*result,[&](const auto &item){return item.id==target->id;});
            if (found!=result->end()) {
                message=found->message;
                if (found->needsReconciliation) message+=" · Check the provider before retrying";
                if (found->completed>1) message+=" · "+std::to_string(found->completed)+" posts";
            }
        }
        (void)target->resultStatus.set(std::move(message));
    }
    (void)canPublish.set(bool(publicationRequests()));
}
ii::core::Result<void> WorkspaceModel::publish() {
    if (auto valid=writable();!valid) return valid;
    auto requests=publicationRequests();
    if (!requests) {(void)publicationStatus.set(requests.error().message);return std::unexpected(requests.error());}
    auto result=publicationService_->submit(std::move(*requests),mediaLease_);
    if (!result) (void)publicationStatus.set(result.error().message);
    return result;
}
std::shared_ptr<ReviewField> WorkspaceModel::reviewField(std::string_view id) const {
    if (owner_!=std::this_thread::get_id()) return {};
    const auto &rows=reviewFields.get();
    const auto found=std::ranges::find_if(rows,[&](const auto &field){return field->id==id;});
    return found==rows.end()?nullptr:*found;
}
void WorkspaceModel::closeReview() {
    if (writable()) (void)reviewOpen.set(false);
}
ii::core::Result<void> WorkspaceModel::validateReviewField(const ReviewField *field) const {
    if (auto valid=writable(); !valid) return valid;
    if (synchronizing_ || !document_->hasDocument.get() || !field || field->platform!=platformKey_ ||
        field->documentGeneration_!=document_->documentGeneration.get() ||
        std::ranges::none_of(reviewFields.get(),[&](const auto &current){return current.get()==field;}))
        return ii::core::fail(ErrorCode::cancelled,"The publication review field is no longer current");
    return {};
}
ii::core::Result<void> ReviewField::edit(std::string value) {
    auto model=owner.lock();if (!model) return ii::core::fail(ErrorCode::closed,"Publication review is closed");
    if (auto valid=model->validateReviewField(this); !valid) return valid;
    if (!isText) return ii::core::fail(ErrorCode::invalid_argument,"This publication field is not text");
    return model->document_->editPublicationSetting(destinationKey(platform,id),std::move(value));
}
ii::core::Result<void> ReviewField::select(int value) {
    auto model=owner.lock();if (!model) return ii::core::fail(ErrorCode::closed,"Publication review is closed");
    if (auto valid=model->validateReviewField(this); !valid) return valid;
    if (!isChoice || value<0 || static_cast<std::size_t>(value)>=choices_.size())
        return ii::core::fail(ErrorCode::invalid_argument,"Unknown publication choice");
    return model->document_->editPublicationSetting(destinationKey(platform,id),choices_[static_cast<std::size_t>(value)]);
}
ii::core::Result<void> ReviewField::toggle(bool value) {
    auto model=owner.lock();if (!model) return ii::core::fail(ErrorCode::closed,"Publication review is closed");
    if (auto valid=model->validateReviewField(this); !valid) return valid;
    if (!isToggle) return ii::core::fail(ErrorCode::invalid_argument,"This publication field is not a switch");
    return model->document_->editPublicationSetting(destinationKey(platform,id),value);
}
ii::core::Result<void> ReviewField::reset() {
    auto model=owner.lock();if (!model) return ii::core::fail(ErrorCode::closed,"Publication review is closed");
    if (auto valid=model->validateReviewField(this); !valid) return valid;
    return model->document_->removePublicationSetting(destinationKey(platform,id));
}
void WorkspaceModel::synchronizeReviewFields(const iiSharedCanvas::Document *document) {
    if (!document || !platform_) {
        fieldsPlatform_.clear();fieldsGeneration_=0;(void)reviewFields.set({});return;
    }
    const auto generation=document_->documentGeneration.get();
    const auto policy=iiSocialIntegration::reviewPolicy(*platform_);
    if (fieldsPlatform_!=platformKey_ || fieldsGeneration_!=generation) {
        std::vector<std::shared_ptr<ReviewField>> rows;
        auto append=[&](std::string id,std::string label,int kind,std::vector<std::string> choices={}) {
            auto field=std::make_shared<ReviewField>();field->id=std::move(id);field->label=std::move(label);
            field->platform=platformKey_;field->owner=shared_from_this();field->documentGeneration_=generation;
            field->isText=kind==0;field->isChoice=kind==1;field->isToggle=kind==2;
            if (field->isChoice) {field->choices_=std::move(choices);(void)field->options.set(field->choices_);}
            rows.push_back(std::move(field));
        };
        append("title","Title",0);append("text","Description / introduction",0);
        append("tags","Tags",0);append("link","Source / destination link",0);
        if (*platform_==iiSocialIntegration::Platform::Mastodon)
            append("visibility","Visibility",1,{"public","unlisted","private","direct"});
        else if (*platform_==iiSocialIntegration::Platform::YouTube)
            append("visibility","Visibility",1,{"public","unlisted","private"});
        else append("visibility","Visibility",1,{"public"});
        std::vector<std::string> sequences{"Automatic","Single post","Separate posts"};
        if (policy.threads) sequences.push_back("Thread");
        append("sequence","Publish structure",1,std::move(sequences));
        std::vector<std::string> counts{"Automatic"};
        for (unsigned count=1;count<=std::min(20U,policy.mediaPerPost);++count) counts.push_back(std::to_string(count));
        append("mediaPerPost","Attachments per post",1,std::move(counts));
        append("includeCaptions","Include canvas captions",2);
        append("numberParts","Number the parts",2);
        append("repeatIntroduction","Repeat introduction",2);
        if (policy.textOnly) append("splitText","Split long text",2);
        if (*platform_==iiSocialIntegration::Platform::X) {
            append("madeWithAi","AI disclosure",2);
            append("replyControl","Who can reply",1,{"everyone","following","mentionedUsers","subscribers","verified"});
        } else if (*platform_==iiSocialIntegration::Platform::Threads)
            append("replyControl","Who can reply",1,{"everyone","accounts_you_follow","mentioned_only"});
        if (*platform_==iiSocialIntegration::Platform::Mastodon) {
            append("sensitive","Sensitive media",2);append("contentWarning","Content warning",0);append("language","Language (BCP 47)",0);
        }
        if (*platform_==iiSocialIntegration::Platform::Pinterest) append("coverImageUrl","Video cover HTTPS URL",0);
        if (*platform_==iiSocialIntegration::Platform::YouTube) {
            append("categoryId","Video category ID",0);
            append("madeForKids","Made for kids",1,{"Channel default","Yes","No"});
            append("syntheticMedia","Altered / synthetic media",2);append("notifySubscribers","Notify subscribers",2);
        }
        fieldsPlatform_=platformKey_;fieldsGeneration_=generation;(void)reviewFields.set(std::move(rows));
        if (pending_) return;
    }
    const auto empty=iiSharedCanvas::PublicationMetadata{};
    const auto &metadata=document->publicationMetadata?*document->publicationMetadata:empty;
    auto shared=[&](std::string_view key,std::string fallback={}) {
        const auto *value=textSetting(metadata,std::string(key));return value?*value:std::move(fallback);
    };
    for (const auto &field:reviewFields.get()) {
        std::string value;bool flag=false;
        const bool inherited=field->id=="title" || field->id=="text" || field->id=="tags" ||
            field->id=="link" || field->id=="visibility" || field->id=="madeWithAi" ||
            field->id=="sensitive" || field->id=="contentWarning" || field->id=="language" || field->id=="syntheticMedia";
        if (field->id=="title") value=metadata.title;
        else if (field->id=="text") {
            value=metadata.description;
            for (const auto key:{"credits","mentions"}) if (const auto *extra=textSetting(metadata,key);extra && !extra->empty()) {
                if (!value.empty()) value+='\n';value+=*extra;
            }
        } else if (field->id=="tags") {
            if (source_) for (const auto &tag:source_->post.tags) {if (!value.empty()) value+=", ";value+=tag;}
            value=shared("tags",std::move(value));
        } else if (field->id=="link") value=shared("link",shared("linkedSource",source_?source_->post.link:std::string{}));
        else if (field->id=="visibility") value=shared("visibility",source_?source_->post.visibility:"public");
        else if (field->id=="sequence" || field->id=="mediaPerPost") value="Automatic";
        else if (field->id=="replyControl") {
            value=source_?source_->post.replyControl:std::string{};
            value=shared("replyControl",value.empty()?"everyone":value);
        } else if (field->id=="categoryId") value=shared("categoryId",source_?source_->post.youtube.categoryId:"22");
        else if (field->id=="madeForKids") {
            if (source_ && source_->post.youtube.madeForKids) value=*source_->post.youtube.madeForKids?"Yes":"No";
            else value="Channel default";
        } else if (field->id=="contentWarning") value=shared("contentWarning",source_?source_->post.contentWarning:std::string{});
        else if (field->id=="language") value=shared("language",source_?source_->post.language:std::string{});
        else if (field->id=="coverImageUrl") value=shared("coverImageUrl",source_?source_->post.coverImageUrl:std::string{});
        else if (field->id=="repeatIntroduction") flag=*platform_==iiSocialIntegration::Platform::Pinterest;
        else if (field->id=="madeWithAi" || field->id=="syntheticMedia") {
            flag=source_ && (field->id=="madeWithAi"?source_->post.madeWithAi:
                source_->post.youtube.containsSyntheticMedia.value_or(source_->post.madeWithAi));
            if (const auto *saved=boolSetting(metadata,"madeWithAi")) flag=*saved;
        } else if (field->id=="sensitive") {
            flag=source_ && source_->post.sensitive;
            const auto adult=boolSetting(metadata,"adult"),violence=boolSetting(metadata,"violence");
            if (adult || violence) flag=(adult && *adult)||(violence && *violence);
            if (const auto *saved=boolSetting(metadata,"sensitive")) flag=*saved;
        } else if (field->id=="notifySubscribers") {
            flag=!source_ || source_->post.youtube.notifySubscribers;
            if (const auto *saved=boolSetting(metadata,"notifySubscribers")) flag=*saved;
        }
        else flag=true;
        const auto found=metadata.settings.find(destinationKey(platformKey_,field->id));
        const bool overridden=found!=metadata.settings.end();bool valid=true;
        if (overridden) {
            if (field->isToggle) {if (const auto *saved=std::get_if<bool>(&found->second)) flag=*saved;else valid=false;}
            else {if (const auto *saved=std::get_if<std::string>(&found->second)) value=*saved;else valid=false;}
        }
        int index=0;
        if (field->isChoice) {
            auto choices=field->choices_;
            const auto match=std::ranges::find(choices,value);
            if (!valid || match==choices.end()) {valid=false;index=int(choices.size());choices.push_back("Unavailable saved value");}
            else index=int(match-choices.begin());
            (void)field->options.set(std::move(choices));if (pending_) return;
        }
        if (!valid && settingError_.empty()) settingError_="Saved publication options are not recognized";
        (void)field->overridden.set(overridden);if (pending_) return;
        (void)field->origin.set(overridden?"Custom":inherited?"IISC":"Auto");if (pending_) return;
        (void)field->status.set(valid?std::string{}:std::string("Saved value unavailable"));if (pending_) return;
        (void)field->text.set(std::move(value));if (pending_) return;
        (void)field->checked.set(flag);if (pending_) return;
        (void)field->index.set(index);if (pending_) return;
        (void)field->editable.set(true);if (pending_) return;
    }
}
void WorkspaceModel::synchronizeMetadata() {
    if (owner_!=std::this_thread::get_id()) return;
    if (synchronizing_) {pending_=true;return;}
    synchronizing_=true;
    try {
        do {
            pending_=false;settingError_.clear();options_=iiSocialIntegration::ReviewOptions{};
            const auto snapshot=document_->snapshot();
            const auto *document=snapshot.get();
            for (const auto &target:targets.get()) {
                bool included=false;std::string status;
                if (document && document->publicationMetadata) {
                    const auto &settings=document->publicationMetadata->settings;
                    const auto found=settings.find(destinationKey(target->id,"selected"));
                    if (found!=settings.end()) {
                        if (const auto *value=std::get_if<bool>(&found->second)) included=*value;
                        else status="Selection value unavailable";
                    }
                }
                (void)target->included.set(included);
                if (pending_) break;
                (void)target->selectionStatus.set(std::move(status));
                if (pending_) break;
            }
            if (pending_) continue;
            const auto policy=platform_?std::optional(iiSocialIntegration::reviewPolicy(*platform_)):std::nullopt;
            if (platform_) {auto configured=destinationOptions(document,platformKey_,*platform_);
                options_=configured.options;settingError_=std::move(configured.error);}
            (void)canThread.set(document && policy && policy->threads);
            (void)threaded.set(settingError_.empty() && policy && policy->threads &&
                (options_.sequence==iiSocialIntegration::Sequence::Thread || options_.sequence==iiSocialIntegration::Sequence::Automatic));
            if (pending_) continue;
            synchronizeReviewFields(document);
            if (pending_) continue;
            rebuild();
        } while (pending_);
        synchronizing_=false;
    } catch (...) {synchronizing_=false;throw;}
}
void WorkspaceModel::setFramePresenter(std::shared_ptr<editor::FramePresenter> presenter) {
    imageObserver_={};statusObserver_={};
    if (framePresenter_) (void)framePresenter_->setVisible(false);
    framePresenter_=std::move(presenter);
    if (!framePresenter_) {
        (void)renderedFrame.set({});(void)hasFrame.set(false);(void)renderStatus.set("Canvas renderer unavailable");return;
    }
    const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
    imageObserver_=framePresenter_->image.observe([weak](const auto &image,std::uint64_t) {
        if (auto model=weak.lock()) {(void)model->renderedFrame.set(image);(void)model->hasFrame.set(bool(image));}
    },true);
    statusObserver_=framePresenter_->status.observe([weak](const auto &status,std::uint64_t) {
        if (auto model=weak.lock()) (void)model->renderStatus.set(status);
    },true);
    (void)framePresenter_->setViewport(previewWidth.get(),previewHeight.get());
    (void)framePresenter_->setVisible(visible_);
}
void WorkspaceModel::setMediaModel(std::shared_ptr<PublicationMediaModel> model) {
    if (!writable() || mediaModel_==model) return;
    const auto epoch=++mediaEpoch_;
    mediaObserver_={};mediaStatusObserver_={};
    auto previous=std::exchange(mediaModel_,{});
    source_.reset();mediaLease_.reset();
    if (previous) previous->setVisible(false);
    if (epoch!=mediaEpoch_) return;
    mediaModel_=std::move(model);
    if (!mediaModel_) {rebuild();return;}
    const std::weak_ptr<WorkspaceModel> weak=shared_from_this();
    auto attached=mediaModel_;
    const std::weak_ptr<PublicationMediaModel> expected=attached;
    auto observer=attached->prepared.observe([weak,expected](const auto &prepared,std::uint64_t) {
        auto self=weak.lock();auto media=expected.lock();if (!self || !media || self->mediaModel_!=media) return;
        self->mediaLease_=prepared;
        if (prepared) {(void)self->setReviewSource(prepared->source);}
        else {self->source_.reset();self->rebuild();}
    },true);
    if (epoch!=mediaEpoch_ || mediaModel_!=attached) return;
    mediaObserver_=std::move(observer);
    mediaStatusObserver_=attached->status.observe([weak,expected](const auto &,std::uint64_t) {
        auto self=weak.lock();auto media=expected.lock();
        if (self && media && self->mediaModel_==media && !self->source_) self->rebuild();
    });
    attached->setVisible(visible_);
}
void WorkspaceModel::setVisible(bool value) {
    visible_=value;
    if (framePresenter_) (void)framePresenter_->setVisible(value);
    if (mediaModel_) mediaModel_->setVisible(value);
}
void WorkspaceModel::previewSizeChanged(double w,double h) {
    if (!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>8192||h>8192) return;
    (void)previewWidth.set(w);(void)previewHeight.set(h);
    if (framePresenter_) (void)framePresenter_->setViewport(w,h);
}
void WorkspaceModel::listSizeChanged(double w,double h) {
    if (!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>8192||h>8192) return;
    (void)listWidth.set(w);(void)listHeight.set(h);
}
void WorkspaceModel::rebuild() {
    if (rebuilding_) {rebuildPending_=true;return;}
    rebuilding_=true;
    try {
        do {
            rebuildPending_=false;plan_={};
            if (!platform_ || !source_) {
                (void)plannedPosts.set(0);
                if (rebuildPending_) continue;
                const auto message=mediaModel_?mediaModel_->status.get():std::string{};
                (void)planText.set(message.empty()?"Media not prepared":message);
            } else if (!settingError_.empty()) {
                (void)plannedPosts.set(0);
                if (!rebuildPending_) (void)planText.set(settingError_);
            } else {
                auto source=*source_;
                if (const auto *document=document_->document();document && document->publicationMetadata)
                    applyMetadata(source,*document->publicationMetadata,platformKey_);
                plan_=iiSocialIntegration::planPublication(*platform_,source,options_);
                (void)plannedPosts.set(plan_.posts.size());
                if (!rebuildPending_) (void)planText.set(std::to_string(plan_.posts.size())+" planned posts");
            }
            if (!rebuildPending_) synchronizePublication();
        } while (rebuildPending_);
        rebuilding_=false;
    } catch (...) {rebuilding_=false;throw;}
}
void WorkspaceModel::sizeChanged(double w,double h,double maximumPanel) {
    if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(maximumPanel) ||
        w<0 || h<0 || w>8192 || h>8192 || maximumPanel<=0) return;
    (void)width.set(w); (void)height.set(h);
    const auto panel=std::min(maximumPanel,w/3.0);
    (void)panelWidth.set(std::max(1.0,panel)); (void)contentWidth.set(std::max(1.0,w-panel));
}
void Target::choose() {
    if (auto model=owner.lock()) if (auto result=model->select(id);result && model->platformKey_==id)
        (void)model->reviewOpen.set(true);
}
void Target::setIncluded(bool value) {if (auto model=owner.lock()) (void)model->setTargetIncluded(id,value);}
}
