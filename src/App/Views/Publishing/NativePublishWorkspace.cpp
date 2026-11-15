#include "NativePublishWorkspace.h"
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
    source.post.madeWithAi=flag("madeWithAi",source.post.madeWithAi);
    const auto adult=boolSetting(metadata,"adult"),violence=boolSetting(metadata,"violence");
    const auto sensitive=(adult || violence)?(adult && *adult)||(violence && *violence):source.post.sensitive;
    source.post.sensitive=flag("sensitive",sensitive);
    if (const auto *publishTime=textSetting(metadata,"publishTime")) {
        const auto *when=textSetting(metadata,"scheduledAt");
        source.scheduledAt=*publishTime=="Schedule" && when?*when:std::string{};
    }
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
            if (platform_ && *platform_==iiSocialIntegration::Platform::Pinterest) options_.repeatIntroduction=true;
            if (document && document->publicationMetadata && platform_) {
                const auto &settings=document->publicationMetadata->settings;
                const auto found=settings.find(destinationKey(platformKey_,"sequence"));
                if (found!=settings.end()) {
                    if (const auto *value=std::get_if<std::string>(&found->second)) {
                        if (*value=="Thread") options_.sequence=iiSocialIntegration::Sequence::Thread;
                        else if (*value=="Separate posts") options_.sequence=iiSocialIntegration::Sequence::Separate;
                        else if (*value=="Single post") options_.sequence=iiSocialIntegration::Sequence::Single;
                        else if (*value!="Automatic") settingError_="Saved publish structure is not recognized";
                    } else settingError_="Saved publish structure is not recognized";
                }
                const auto media=settings.find(destinationKey(platformKey_,"mediaPerPost"));
                if (media!=settings.end()) {
                    if (const auto *value=std::get_if<std::string>(&media->second)) {
                        if (*value!="Automatic") {
                            unsigned count=0;
                            const auto parsed=std::from_chars(value->data(),value->data()+value->size(),count);
                            if (value->empty() || parsed.ec!=std::errc{} || parsed.ptr!=value->data()+value->size() ||
                                !policy || count==0 || count>policy->mediaPerPost)
                                settingError_="Saved publication options are not recognized";
                            else options_.mediaPerPost=count;
                        }
                    } else settingError_="Saved publication options are not recognized";
                }
                for (const auto &[key,target]:{
                    std::pair{"includeCaptions",&options_.includeCaptions},std::pair{"numberParts",&options_.numberParts},
                    std::pair{"repeatIntroduction",&options_.repeatIntroduction},std::pair{"splitText",&options_.splitText}}) {
                    const auto entry=settings.find(destinationKey(platformKey_,key));
                    if (entry!=settings.end()) {
                        if (const auto *value=std::get_if<bool>(&entry->second)) *target=*value;
                        else settingError_="Saved publication options are not recognized";
                    }
                }
            }
            (void)canThread.set(document && policy && policy->threads);
            (void)threaded.set(settingError_.empty() && policy && policy->threads &&
                (options_.sequence==iiSocialIntegration::Sequence::Thread || options_.sequence==iiSocialIntegration::Sequence::Automatic));
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
void WorkspaceModel::setVisible(bool value) {
    visible_=value;
    if (framePresenter_) (void)framePresenter_->setVisible(value);
}
void WorkspaceModel::previewSizeChanged(double w,double h) {
    if (!std::isfinite(w)||!std::isfinite(h)||w<=0||h<=0||w>8192||h>8192) return;
    (void)previewWidth.set(w);(void)previewHeight.set(h);
    if (framePresenter_) (void)framePresenter_->setViewport(w,h);
}
void WorkspaceModel::rebuild() {
    plan_={};
    if (!platform_ || !source_) {(void)plannedPosts.set(0); (void)planText.set("Media not prepared"); return;}
    if (!settingError_.empty()) {(void)plannedPosts.set(0);(void)planText.set(settingError_);return;}
    auto source=*source_;
    if (const auto *document=document_->document();document && document->publicationMetadata)
        applyMetadata(source,*document->publicationMetadata,platformKey_);
    plan_=iiSocialIntegration::planPublication(*platform_,source,options_);
    (void)plannedPosts.set(plan_.posts.size());
    (void)planText.set(std::to_string(plan_.posts.size())+" planned posts");
}
void WorkspaceModel::sizeChanged(double w,double h,double maximumPanel) {
    if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(maximumPanel) ||
        w<0 || h<0 || w>8192 || h>8192 || maximumPanel<=0) return;
    (void)width.set(w); (void)height.set(h);
    const auto panel=std::min(maximumPanel,w/3.0);
    (void)panelWidth.set(std::max(1.0,panel)); (void)contentWidth.set(std::max(1.0,w-panel));
}
void Target::choose() {if (auto model=owner.lock()) (void)model->select(id);}
void Target::setIncluded(bool value) {if (auto model=owner.lock()) (void)model->setTargetIncluded(id,value);}
}
