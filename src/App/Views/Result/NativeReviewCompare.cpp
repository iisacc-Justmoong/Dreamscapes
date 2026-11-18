#include "NativeReviewCompare.h"
#include "App/ViewModels/Native/ViewCatalog.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace dreamscapes::review {
using ii::core::ErrorCode;
std::shared_ptr<CompareModel> CompareModel::create() {return std::shared_ptr<CompareModel>(new CompareModel);}
void CompareModel::requireOwner() const {
    if (owner_!=std::this_thread::get_id()) throw std::logic_error("Comparison requires its owner thread");
}
void CompareModel::setPaneFactory(PaneFactory factory) {requireOwner();factory_=std::move(factory);}
bool CompareModel::contains(const Item &item) const {
    const auto list=items.get();return item && std::ranges::find(list,item)!=list.end();
}
ii::core::Result<void> CompareModel::open(const std::vector<vm::Value> &entries) {
    requireOwner();
    if (!Session::validImages(entries,2,4)) return ii::core::fail(ErrorCode::invalid_argument,"Choose two to four images to compare");
    std::vector<Item> next;std::unordered_set<std::string> keys;
    for (const auto &entry:entries) {
        auto item=std::make_shared<CompareItem>();item->key=Session::keyForEntry(entry);
        item->source=vm::text(vm::imageSource(entry));
        if (item->key.empty() || item->source.empty() || !keys.insert(item->key).second)
            return ii::core::fail(ErrorCode::invalid_argument,"Each compared image needs a distinct source");
        item->title=vm::text(vm::get(entry,"name"));
        if (item->title.empty()) item->title=std::filesystem::path(item->source).filename().string();
        if (item->title.empty()) item->title=item->source;
        if (factory_) item->surface=factory_(item);
        next.push_back(std::move(item));
    }
    const auto revision=++revision_;
    active_=next.front();
    (void)galleryVisible.set(false);if (revision_!=revision) return {};
    (void)items.set(std::move(next));if (revision_!=revision) return {};
    layout();if (revision_!=revision) return {};
    (void)visible.set(true);
    return {};
}
void CompareModel::close() {
    requireOwner();const auto revision=++revision_;
    (void)visible.set(false);if (revision_!=revision) return;
    (void)items.set({});if (revision_!=revision) return;
    active_.reset();(void)galleryVisible.set(true);
}
ii::core::Result<void> CompareModel::setViewport(double w,double h,double gap,double toolbar,double minimum) {
    requireOwner();
    if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(gap) || !std::isfinite(toolbar) ||
        !std::isfinite(minimum) || w<0 || h<0 || w>8192 || h>8192 || gap<0 || toolbar<0 || minimum<=0)
        return ii::core::fail(ErrorCode::invalid_argument,"Invalid comparison viewport");
    gap_=gap;toolbarHeight_=toolbar;minimumWidth_=minimum;(void)width.set(w);(void)height.set(h);layout();return {};
}
void CompareModel::layout() {
    const auto count=items.get().size();const auto available=std::max(0.0,width.get());
    const auto columns=count>1 && available>=minimumWidth_*2+gap_?2u:1u;
    const auto rows=std::max<std::size_t>(1,(count+columns-1)/columns);
    const auto grid=std::max(0.0,height.get()-toolbarHeight_-gap_);
    // LVUI's virtual collection requires positive item dimensions even while
    // its parent is hidden or has not received its first layout yet.
    const auto cellW=std::max(1.0,(available-gap_*(columns-1))/columns);
    const auto cellH=std::max(1.0,(grid-gap_*(rows-1))/rows);
    (void)contentWidth.set(available);(void)gridHeight.set(grid);(void)cellWidth.set(cellW);
    (void)cellHeight.set(cellH);(void)paneHeight.set(std::max(0.0,cellH-toolbarHeight_-gap_));updateFit();
}
bool CompareModel::publishImage(const Item &item,std::shared_ptr<const ii::gui::RasterImage> image,std::string message) {
    requireOwner();if (!contains(item)) return false;
    (void)item->image.set(std::move(image));
    if (!contains(item)) return false;
    (void)item->status.set(item->image.get()?std::string{}:(message.empty()?"Preview unavailable":std::move(message)));
    if (contains(item)) updateFit();return contains(item);
}
void CompareModel::updateFit() {
    const auto list=items.get();if (list.empty()) return;
    double common=std::numeric_limits<double>::infinity();
    for (const auto &item:list) if (auto image=item->image.get();image && image->width() && image->height())
        common=std::min(common,std::min(cellWidth.get()/image->width(),paneHeight.get()/image->height()));
    for (const auto &item:list) {
        if (!contains(item)) return;
        auto transform=item->transform.get();if (!transform.fit) continue;
        const auto image=item->image.get();
        double scale=synchronized.get()?common:(image && image->width() && image->height()?
            std::min(cellWidth.get()/image->width(),paneHeight.get()/image->height()):1);
        if (!std::isfinite(scale) || scale<=0) scale=1;
        transform.scale=std::clamp(scale,.02,64.0);transform.centerX=transform.centerY=.5;
        (void)item->transform.set(transform);
    }
}
void CompareModel::apply(const Item &item,CompareTransform transform) {
    if (synchronized.get()) {for (const auto &target:items.get()) if (contains(target)) (void)target->transform.set(transform);}
    else if (contains(item)) (void)item->transform.set(transform);
}
void CompareModel::activate(const Item &item) {requireOwner();if (contains(item)) active_=item;}
CompareModel::Item CompareModel::activeItem() const {requireOwner();auto value=active_.lock();return contains(value)?value:Item{};}
ii::core::Result<void> CompareModel::fit() {
    requireOwner();auto selected=activeItem();
    for (const auto &item:items.get()) if (synchronized.get() || item==selected) {
        auto t=item->transform.get();t.fit=true;(void)item->transform.set(t);
    }
    updateFit();return {};
}
ii::core::Result<void> CompareModel::actualSize() {
    requireOwner();auto selected=activeItem();
    for (const auto &item:items.get()) if (synchronized.get() || item==selected) {
        auto t=item->transform.get();t.scale=1;t.centerX=t.centerY=.5;t.fit=false;(void)item->transform.set(t);
    }
    return {};
}
ii::core::Result<void> CompareModel::zoom(const Item &item,double factor) {
    requireOwner();if (!contains(item) || !std::isfinite(factor) || factor<=0) return ii::core::fail(ErrorCode::invalid_argument,"Invalid comparison zoom");
    activate(item);auto transform=item->transform.get();transform.scale=std::clamp(transform.scale*factor,.02,64.0);transform.fit=false;apply(item,transform);return {};
}
ii::core::Result<void> CompareModel::pan(const Item &item,double dx,double dy) {
    requireOwner();if (!contains(item) || !std::isfinite(dx) || !std::isfinite(dy)) return ii::core::fail(ErrorCode::invalid_argument,"Invalid comparison pan");
    auto image=item->image.get();if (!image || !image->width() || !image->height()) return {};
    activate(item);auto t=item->transform.get();t.centerX=std::clamp(t.centerX-dx/(image->width()*t.scale),0.0,1.0);
    t.centerY=std::clamp(t.centerY-dy/(image->height()*t.scale),0.0,1.0);t.fit=false;apply(item,t);return {};
}
ii::core::Result<void> CompareModel::setSynchronized(bool value) {
    requireOwner();(void)synchronized.set(value);(void)syncLabel.set(value?"Sync on":"Sync off");
    if (value) if (auto item=activeItem()) {apply(item,item->transform.get());updateFit();}return {};
}
}
