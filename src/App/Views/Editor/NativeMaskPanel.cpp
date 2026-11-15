#include "NativeMaskPanel.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace dreamscapes::editor {
namespace {
using ii::core::ErrorCode;
using namespace iiSharedCanvas;
std::string combineName(MaskCombineMode value) {
    switch (value) {
    case MaskCombineMode::Add:return "Add / union";
    case MaskCombineMode::Subtract:return "Subtract";
    case MaskCombineMode::Intersect:return "Intersect";
    case MaskCombineMode::Replace:return "Replace";
    }
    return "Unavailable";
}
}
MaskPanelModel::MaskPanelModel(std::shared_ptr<native::DocumentSession> document):document_(std::move(document)) {
    if (!document_) throw std::invalid_argument("Mask panel requires a document session");
}
std::shared_ptr<MaskPanelModel> MaskPanelModel::create(std::shared_ptr<native::DocumentSession> document) {
    auto value=std::shared_ptr<MaskPanelModel>(new MaskPanelModel(std::move(document)));value->start();return value;
}
void MaskPanelModel::start() {
    const std::weak_ptr<MaskPanelModel> weak=shared_from_this();
    auto update=[weak](const auto &,std::uint64_t){if (auto self=weak.lock();self && self->visible_) self->synchronize();};
    selection_=document_->selectedLayer.observe(update);revision_=document_->revision.observe(update);
    generation_=document_->documentGeneration.observe(update);
}
ii::core::Result<void> MaskPanelModel::writable() const {
    if (owner_!=std::this_thread::get_id()) return ii::core::fail(ErrorCode::wrong_executor,"Mask panel requires its owner thread");
    return {};
}
void MaskPanelModel::setVisible(bool value) {
    if (owner_!=std::this_thread::get_id() || visible_==value) return;
    visible_=value;
    if (visible_) synchronize();
    else {
        if (synchronizing_) pending_=true;
        // Retire row objects first: an observer may reopen this panel during
        // any notification, and those old objects must stay inactive.
        (void)rows.set({});
        if (visible_) return;
        (void)canAdd.set(false);
        if (visible_) return;
        (void)canEdit.set(false);
    }
}
void MaskPanelModel::synchronize() {
    using namespace iiSharedCanvas;
    if (owner_!=std::this_thread::get_id() || !visible_) return;
    if (synchronizing_) {pending_=true;return;}
    synchronizing_=true;
    try {
        do {
            pending_=false;if (!visible_) break;
            const auto id=document_->hasCurrentLayerSelection()?document_->selectedLayer.get():std::string{};
            const auto generation=document_->documentGeneration.get();
            const bool layerChanged=layer_!=id || layerGeneration_!=generation;
            const auto snapshot=document_->snapshot();const auto *doc=snapshot.get();
            const auto *layer=doc?findLayer(*doc,id):nullptr;
            const bool eligible=layer && layerRole(*layer)==LayerRole::Artwork;
            const auto name=layer?layerProperties(*layer).name:std::string{};
            auto chosen=(layer_==id && layerGeneration_==generation)?selectedMask.get():std::string{};
            const auto *stack=eligible?&layerProperties(*layer).appearance.maskStack:nullptr;
            if (stack && std::ranges::none_of(*stack,[&](const auto &entry){return entry.id==chosen;}))
                chosen=stack->empty()?std::string{}:stack->front().id;
            if (!stack) chosen.clear();
            const LayerMaskEntry *selected=nullptr;
            if (stack) {
                const auto found=std::ranges::find_if(*stack,[&](const auto &entry){return entry.id==chosen;});
                if (found!=stack->end()) selected=&*found;
            }
            std::unordered_map<std::string,std::shared_ptr<MaskRow>> existing;
            if (layer_==id && layerGeneration_==generation)
                for (const auto &row:rows.get()) existing.emplace(row->id,row);
            layer_=eligible?id:std::string{};layerGeneration_=generation;
            std::vector<std::shared_ptr<MaskRow>> next;
            if (stack) for (const auto &entry:*stack) {
                auto found=existing.find(entry.id);
                auto row=found==existing.end()?std::make_shared<MaskRow>():found->second;
                if (found==existing.end()) {row->id=entry.id;row->owner=shared_from_this();}
                (void)row->name.set(entry.name.empty()?entry.id:entry.name);if (pending_) break;
                (void)row->selected.set(entry.id==chosen);if (pending_) break;
                (void)row->enabled.set(entry.mask.enabled);if (pending_) break;
                next.push_back(std::move(row));
            }
            if (pending_) continue;
            (void)selectedMask.set(chosen);if (pending_) continue;
            (void)rows.set(std::move(next));if (pending_) continue;
            (void)empty.set(rows.get().empty());if (pending_) continue;
            updateListHeight();if (pending_) continue;
            (void)canAdd.set(eligible);if (pending_) continue;
            (void)canEdit.set(bool(selected));if (pending_) continue;
            (void)layerName.set(name);if (pending_) continue;
            const auto *appearance=eligible?&layerProperties(*layer).appearance:nullptr;
            (void)stackEnabled.set(appearance && appearance->maskStackEnabled);if (pending_) continue;
            (void)legacyMasks.set(appearance && (appearance->rasterMask || appearance->vectorMask || appearance->filterMask));
            if (pending_) continue;
            (void)enabled.set(selected && selected->mask.enabled);if (pending_) continue;
            (void)inverted.set(selected && selected->mask.inverted);if (pending_) continue;
            (void)density.set(selected?selected->mask.density:0);if (pending_) continue;
            (void)densityPercent.set(selected?selected->mask.density*100:0);if (pending_) continue;
            (void)feather.set(selected?selected->mask.featherRadius:0);if (pending_) continue;
            (void)combine.set(selected?combineName(selected->combine):std::string{});
            if (pending_) continue;
            if (layerChanged) (void)status.set({});
        } while (pending_);
        synchronizing_=false;
    } catch (...) {synchronizing_=false;throw;}
}
ii::core::Result<void> MaskPanelModel::select(std::string_view id) {
    if (auto valid=writable();!valid) return valid;
    if (!visible_ || !document_->hasCurrentLayerSelection() || layer_!=document_->selectedLayer.get() ||
        layerGeneration_!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::closed,"Select an artwork layer first");
    if (std::ranges::none_of(rows.get(),[&](const auto &row){return row->id==id;}))
        return ii::core::fail(ErrorCode::invalid_argument,"Mask is unavailable");
    (void)selectedMask.set(std::string(id));synchronize();return {};
}
ii::core::Result<void> MaskPanelModel::createMask() {
    if (auto valid=writable();!valid) return valid;
    if (!visible_ || !canAdd.get() || !document_->hasCurrentLayerSelection() || layer_!=document_->selectedLayer.get() ||
        layerGeneration_!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::closed,"Select an artwork layer first");
    const auto layer=layer_;const auto generation=layerGeneration_;
    auto created=document_->createLayerMask(layer);
    if (!created) {(void)status.set(created.error().message);return std::unexpected(created.error());}
    if (visible_ && layer_==layer && layerGeneration_==generation && document_->hasCurrentLayerSelection() &&
        document_->selectedLayer.get()==layer) (void)select(*created);
    (void)status.set({});return {};
}
ii::core::Result<void> MaskPanelModel::duplicateSelected() {
    if (auto valid=writable();!valid) return valid;
    if (!visible_ || !canEdit.get() || !document_->hasCurrentLayerSelection() || layer_!=document_->selectedLayer.get() ||
        layerGeneration_!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::closed,"Select a mask first");
    const auto layer=layer_,mask=selectedMask.get();const auto generation=layerGeneration_;
    auto created=document_->duplicateLayerMask(layer,mask);
    if (!created) {(void)status.set(created.error().message);return std::unexpected(created.error());}
    if (visible_ && layer_==layer && layerGeneration_==generation && document_->hasCurrentLayerSelection() &&
        document_->selectedLayer.get()==layer) (void)select(*created);
    (void)status.set({});return {};
}
ii::core::Result<void> MaskPanelModel::removeSelected() {
    if (auto valid=writable();!valid) return valid;
    if (!visible_ || !canEdit.get() || !document_->hasCurrentLayerSelection() || layer_!=document_->selectedLayer.get() ||
        layerGeneration_!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::closed,"Select a mask first");
    auto result=document_->removeLayerMask(layer_,selectedMask.get());
    if (!result) (void)status.set(result.error().message);else (void)status.set({});return result;
}
ii::core::Result<void> MaskPanelModel::update(const native::MaskPatch &patch) {
    if (auto valid=writable();!valid) return valid;
    if (!visible_ || !canEdit.get() || !document_->hasCurrentLayerSelection() || layer_!=document_->selectedLayer.get() ||
        layerGeneration_!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::closed,"Select a mask first");
    auto result=document_->updateLayerMask(layer_,selectedMask.get(),patch);
    if (!result) (void)status.set(result.error().message);else (void)status.set({});return result;
}
ii::core::Result<void> MaskPanelModel::setEnabled(bool value) {native::MaskPatch patch;patch.enabled=value;return update(patch);}
ii::core::Result<void> MaskPanelModel::setStackEnabled(bool value) {
    if (auto valid=writable();!valid) return valid;
    if (!visible_ || !canAdd.get() || !document_->hasCurrentLayerSelection() || layer_!=document_->selectedLayer.get() ||
        layerGeneration_!=document_->documentGeneration.get())
        return ii::core::fail(ErrorCode::closed,"Select an artwork layer first");
    auto result=document_->setLayerMaskStackEnabled(layer_,value);
    if (!result) (void)status.set(result.error().message);else (void)status.set({});return result;
}
ii::core::Result<void> MaskPanelModel::setInverted(bool value) {native::MaskPatch patch;patch.inverted=value;return update(patch);}
ii::core::Result<void> MaskPanelModel::setDensity(double value) {native::MaskPatch patch;patch.density=value;return update(patch);}
ii::core::Result<void> MaskPanelModel::setFeather(double value) {native::MaskPatch patch;patch.featherRadius=value;return update(patch);}
ii::core::Result<void> MaskPanelModel::setCombine(iiSharedCanvas::MaskCombineMode value) {native::MaskPatch patch;patch.combine=value;return update(patch);}
ii::core::Result<void> MaskPanelModel::selectRow(const MaskRow *row) {
    if (auto valid=writable();!valid) return valid;
    if (!row || std::ranges::none_of(rows.get(),[&](const auto &item){return item.get()==row;}))
        return ii::core::fail(ErrorCode::closed,"Mask row is no longer active");
    return select(row->id);
}
ii::core::Result<void> MaskPanelModel::setRowEnabled(const MaskRow *row,bool value) {
    if (auto valid=writable();!valid) return valid;
    if (!row || std::ranges::none_of(rows.get(),[&](const auto &item){return item.get()==row;}))
        return ii::core::fail(ErrorCode::closed,"Mask row is no longer active");
    if (!visible_ || !canAdd.get() || !document_->hasCurrentLayerSelection() || layer_!=document_->selectedLayer.get() ||
        layerGeneration_!=document_->documentGeneration.get()) return ii::core::fail(ErrorCode::closed,"Select an artwork layer first");
    native::MaskPatch patch;patch.enabled=value;auto result=document_->updateLayerMask(layer_,row->id,patch);
    if (!result) (void)status.set(result.error().message);else (void)status.set({});return result;
}
void MaskPanelModel::updateListHeight() {
    (void)listHeight.set(std::max(1.0,std::min(maximumListHeight_,rowHeight_*static_cast<double>(rows.get().size()))));
}
void MaskPanelModel::sizeChanged(double w,double h,double inset,double rowHeight,double maximumListHeight) {
    if (!std::isfinite(w)||!std::isfinite(h)||!std::isfinite(inset)||!std::isfinite(rowHeight)||!std::isfinite(maximumListHeight)||
        w<0||h<0||w>8192||h>8192||inset<0||rowHeight<=0||maximumListHeight<=0) return;
    const lvui::SliderConfiguration slider;
    const auto content=std::max(slider.range_inset*2,w-inset);const lvui::ComboBoxConfiguration configuration;
    rowHeight_=rowHeight;maximumListHeight_=maximumListHeight;
    (void)width.set(std::max(1.0,w));(void)height.set(std::max(1.0,h));(void)contentWidth.set(content);
    (void)comboWidth.set(std::max(configuration.indicator_size+configuration.right_padding,content*2/3));
    updateListHeight();
}
void MaskRow::select() {if (auto value=owner.lock()) (void)value->selectRow(this);}
void MaskRow::toggle(bool value) {if (auto model=owner.lock()) (void)model->setRowEnabled(this,value);}
void MaskRow::toggleCurrent() {toggle(!enabled.get());}
}
