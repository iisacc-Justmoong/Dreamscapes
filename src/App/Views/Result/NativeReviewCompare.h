#pragma once
#include "NativeReviewSession.h"
#include <iiGUI>
#include <iiSDKCore>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace dreamscapes::review {
struct CompareTransform final {
    double scale=1,centerX=.5,centerY=.5;
    bool fit=true;
    friend bool operator==(const CompareTransform &,const CompareTransform &)=default;
};
struct CompareItem final {
    std::string key,source,title;
    ii::core::Observable<std::shared_ptr<const ii::gui::RasterImage>> image;
    ii::core::Observable<CompareTransform> transform{CompareTransform{}};
    ii::core::Observable<std::string> status{"Loading preview…"};
    std::shared_ptr<ii::gui::NativeViewSource> surface;
};
// Session-only comparison: immutable previews and transforms never modify results.
class CompareModel final : public std::enable_shared_from_this<CompareModel> {
  public:
    using Item=std::shared_ptr<CompareItem>;
    using PaneFactory=std::function<std::shared_ptr<ii::gui::NativeViewSource>(const Item &)>;
    static std::shared_ptr<CompareModel> create();
    ii::core::Observable<std::vector<Item>> items;
    ii::core::Observable<bool> visible{false},galleryVisible{true},synchronized{true};
    ii::core::Observable<std::string> syncLabel{"Sync on"};
    ii::core::Observable<double> width{0},height{0},contentWidth{0},gridHeight{0},cellWidth{1},cellHeight{1},paneHeight{0};
    void setPaneFactory(PaneFactory);
    ii::core::Result<void> open(const std::vector<vm::Value> &entries);
    void close();
    ii::core::Result<void> setViewport(double width,double height,double gap,double toolbarHeight,double minimumWidth);
    bool publishImage(const Item &,std::shared_ptr<const ii::gui::RasterImage>,std::string status);
    ii::core::Result<void> fit();
    ii::core::Result<void> actualSize();
    ii::core::Result<void> zoom(const Item &,double factor);
    ii::core::Result<void> pan(const Item &,double dx,double dy);
    ii::core::Result<void> setSynchronized(bool);
    void activate(const Item &);
    Item activeItem() const;
  private:
    CompareModel()=default;
    void requireOwner() const;
    bool contains(const Item &) const;
    void layout();
    void updateFit();
    void apply(const Item &,CompareTransform);
    const std::thread::id owner_=std::this_thread::get_id();
    PaneFactory factory_;
    std::weak_ptr<CompareItem> active_;
    double gap_=0,toolbarHeight_=0,minimumWidth_=0;
    std::uint64_t revision_=0;
};
}
