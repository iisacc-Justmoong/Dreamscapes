#include "NativeReviewInspectorViewModel.h"
#include <iiGUI/NativeTextLayout.h>
#include <iiGUI/TextInputModel.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace dreamscapes::review {
namespace {
std::shared_ptr<lvui::CodeMemoryDocument> makeDocument() {
    auto value = lvui::CodeMemoryDocument::create({}, {.maximum_bytes = 64 * 1024 * 1024,
        .maximum_edit_bytes = 64 * 1024 * 1024});
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
bool canWrap(std::string_view text) {
    if (text.size() > ii::gui::TextInputModel::maximum_bytes ||
        text.size() > ii::gui::NativeTextLayout::maximum_bytes) return false;
    std::size_t lines = 1;
    for (std::size_t i=0; i<text.size(); ++i) {
        if (text[i]!='\r' && text[i]!='\n') continue;
        if (++lines > ii::gui::NativeTextLayout::maximum_lines) return false;
        if (text[i]=='\r' && i+1<text.size() && text[i+1]=='\n') ++i;
    }
    return true;
}
bool validLayout(const InspectorViewLayout &value) {
    const auto valid=[](double n) { return std::isfinite(n) && n>=1 && n<=4096; };
    return valid(value.previewHeight) && valid(value.minimumFieldHeight) && valid(value.maximumFieldHeight) &&
        valid(value.longFieldHeight) && value.minimumFieldHeight<=value.maximumFieldHeight &&
        value.longFieldHeight>=value.minimumFieldHeight && value.longFieldHeight<=value.maximumFieldHeight;
}
std::string display(const InspectorField &field) {
    auto result=field.value;
    if (field.mixed) for (const auto &value : field.values) { result+='\n'; result+=value; }
    return result;
}
}
InspectorDisplayField::InspectorDisplayField(std::string value, InspectorViewLayout layout)
    : key(std::move(value)), document(makeDocument()), layout_(layout) {
    if (key.empty() || !validLayout(layout_)) throw std::invalid_argument("Invalid inspector field configuration");
    (void)height.set(layout_.minimumFieldHeight);
}
ii::core::Result<void> InspectorDisplayField::writable() const {
    if (owner_ != std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Inspector field requires its owner thread");
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Inspector field is closed");
    return {};
}
ii::core::Result<void> InspectorDisplayField::update(std::string name, std::string value) {
    if (auto result=writable(); !result) return result;
    if (text.get()!=value) {
        const auto snapshot=document->snapshot();
        if (!snapshot) return ii::core::fail(ii::core::ErrorCode::failed,snapshot.error().message);
        if ((*snapshot)->revision()>=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            return ii::core::fail(ii::core::ErrorCode::out_of_range,"Inspector document revision exhausted");
        const auto changed=document->replace({.revision=(*snapshot)->revision(),.range={0,(*snapshot)->length()},.text=value});
        if (!changed) return ii::core::fail(ii::core::ErrorCode::invalid_argument,changed.error().message);
        const bool wrapped=canWrap(value);
        if (!wrapped) (void)usesWrappedText.set(false);
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Inspector field was retired during update");
        (void)text.set(std::move(value));
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Inspector field was retired during update");
        (void)revision.set(static_cast<std::int64_t>(changed->revision));
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Inspector field was retired during update");
        (void)height.set(wrapped ? layout_.minimumFieldHeight : layout_.longFieldHeight);
        if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Inspector field was retired during update");
        if (wrapped) (void)usesWrappedText.set(true);
    }
    (void)label.set(std::move(name));
    return {};
}
ii::core::Result<void> InspectorDisplayField::setMeasuredHeight(double value) {
    if (auto result=writable(); !result) return result;
    if (!std::isfinite(value) || value<0) return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid inspector field height");
    if (usesWrappedText.get()) (void)height.set(std::clamp(std::ceil(value),layout_.minimumFieldHeight,layout_.maximumFieldHeight));
    return {};
}
void InspectorDisplayField::wrappingFallbackChanged(bool fallback) {
    if (!writable() || !fallback) return;
    (void)usesWrappedText.set(false); (void)height.set(layout_.longFieldHeight);
}
InspectorViewModel::InspectorViewModel(std::shared_ptr<ReviewInspectorModel> inspector, InspectorViewStrings text,
                                       InspectorViewLayout layout)
    : strings(std::move(text)), inspector_(std::move(inspector)), layout_(layout) {
    if (!inspector_ || !inspector_->active() || !validLayout(layout_) || !strings.selected || !strings.field || !strings.job || !strings.original)
        throw std::invalid_argument("Inspector view requires an active inspector, labels and valid layout");
}
std::shared_ptr<InspectorViewModel> InspectorViewModel::create(std::shared_ptr<ReviewInspectorModel> inspector,
        InspectorViewStrings strings, InspectorViewLayout layout) {
    auto result=std::shared_ptr<InspectorViewModel>(new InspectorViewModel(std::move(inspector),std::move(strings),layout));
    result->start(); return result;
}
InspectorViewModel::~InspectorViewModel() { if (active_ && owner_==std::this_thread::get_id()) try { close(); } catch (...) {} }
void InspectorViewModel::start() {
    const std::weak_ptr<InspectorViewModel> weak=shared_from_this();
    observations_.push_back(inspector_->fields.observe([weak](const auto &value,std::uint64_t) { if (auto self=weak.lock(); self && self->active_) self->updateFields(value); }));
    observations_.push_back(inspector_->selectionCount.observe([weak](int value,std::uint64_t) { if (auto self=weak.lock(); self && self->active_) self->updateSelection(value); }));
    observations_.push_back(inspector_->previewSource.observe([weak](const auto &value,std::uint64_t) { if (auto self=weak.lock(); self && self->active_) self->updatePreview(value); }));
    observations_.push_back(inspector_->jobId.observe([weak](const auto &value,std::uint64_t) { if (auto self=weak.lock(); self && self->active_) self->updateJob(value); }));
    observations_.push_back(inspector_->originalSource.observe([weak](const auto &value,std::uint64_t) { if (auto self=weak.lock(); self && self->active_) self->updateOriginal(value); }));
    observations_.push_back(inspector_->canExpand.observe([weak](bool value,std::uint64_t) { if (auto self=weak.lock(); self && self->active_) (void)self->canExpand.set(value); }));
    observations_.push_back(inspector_->canSave.observe([weak](bool value,std::uint64_t) { if (auto self=weak.lock(); self && self->active_) (void)self->canSave.set(value); }));
    updateFields(inspector_->fields.get()); updateSelection(inspector_->selectionCount.get());
    updatePreview(inspector_->previewSource.get()); updateJob(inspector_->jobId.get());
    updateOriginal(inspector_->originalSource.get()); (void)canExpand.set(inspector_->canExpand.get()); (void)canSave.set(inspector_->canSave.get());
    (void)previewHeight.set(layout_.previewHeight);
}
void InspectorViewModel::updateFields(const std::vector<InspectorField> &source) {
    std::unordered_map<std::string,InspectorDisplayField::Ptr> old;
    for (const auto &field : fields.get()) old.emplace(field->key,field);
    std::vector<InspectorDisplayField::Ptr> next; next.reserve(source.size());
    for (const auto &field : source) {
        auto found=old.find(field.label);
        auto item=found==old.end()?std::make_shared<InspectorDisplayField>(field.label,layout_):found->second;
        if (found!=old.end()) old.erase(found);
        auto updated=item->update(strings.field(field.label),display(field));
        if (!active_) return;
        if (!updated) throw std::runtime_error(updated.error().message);
        next.push_back(std::move(item));
    }
    for (auto &[key,item] : old) item->close();
    (void)fields.set(std::move(next));
}
void InspectorViewModel::updateSelection(int value) {
    (void)selectionSummary.set(value>1 ? strings.selected(static_cast<std::size_t>(value)) : strings.focused);
}
void InspectorViewModel::updatePreview(const std::string &value) {
    (void)previewSource.set(value); (void)noPreview.set(value.empty());
}
void InspectorViewModel::updateJob(const std::string &value) {
    (void)jobText.set(value.empty()?std::string{}:strings.job(value)); (void)jobVisible.set(!value.empty());
}
void InspectorViewModel::updateOriginal(const std::string &value) {
    (void)originalText.set(strings.original(value.empty() || value=="Unavailable" ? strings.unavailable : value));
}
ii::core::Result<void> InspectorViewModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ii::core::ErrorCode::wrong_executor,"Inspector view requires its owner thread");
    if (!active_ || !inspector_ || !inspector_->active()) return ii::core::fail(ii::core::ErrorCode::closed,"Inspector view is closed");
    return {};
}
ii::core::Result<void> InspectorViewModel::setViewport(double width,double height) {
    if (auto result=writable(); !result) return result;
    if (!std::isfinite(width) || !std::isfinite(height) || width<0 || height<0 || width>8192 || height>8192)
        return ii::core::fail(ii::core::ErrorCode::invalid_argument,"Invalid inspector viewport");
    const auto keepAlive=shared_from_this();
    (void)viewportWidth.set(std::max(1.0,width));
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Inspector view retired during layout");
    (void)viewportHeight.set(std::max(1.0,height));
    if (!active_) return ii::core::fail(ii::core::ErrorCode::closed,"Inspector view retired during layout");
    (void)previewHeight.set(std::max(1.0,std::min(height,layout_.previewHeight))); return {};
}
void InspectorViewModel::expand() { if (writable()) inspector_->expand(); }
void InspectorViewModel::save() { if (writable()) inspector_->save(); }
void InspectorViewModel::close() {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Inspector view requires its owner thread");
    if (!active_) return;
    active_=false; observations_.clear();
    for (const auto &item : fields.get()) item->close();
    inspector_.reset(); (void)canExpand.set(false); (void)canSave.set(false);
}
} // namespace dreamscapes::review
