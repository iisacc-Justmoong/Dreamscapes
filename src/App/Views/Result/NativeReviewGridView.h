#pragma once
#include "NativeReviewGrid.h"
#include <LVUI>
#include <iiGUI>

namespace dreamscapes::review {
[[nodiscard]] ii::core::Result<void> buildReviewGrid(ii::gui::WidgetBuilder &,ii::gui::Widget parent,
                                                     std::shared_ptr<ReviewGridModel>);
}
class NativeReviewGridBackend final {
  public:
    explicit NativeReviewGridBackend(std::shared_ptr<dreamscapes::review::ReviewGridModel>);
    std::shared_ptr<dreamscapes::review::ReviewGridModel> model;
    std::shared_ptr<dreamscapes::review::ReviewGalleryModel> gallery;
    ii::core::Observable<std::vector<dreamscapes::review::ReviewGalleryModel::Item>> &items;
    ii::core::Observable<double> &viewportWidth,&viewportHeight,&cellSize;
    ii::core::Observable<std::int64_t> &columns;
    const double minimumCellWidth;
    void sizeChanged(double width,double height);
    [[nodiscard]] std::string tileTitle(const std::shared_ptr<dreamscapes::review::ReviewGalleryItem> &item) const;
    [[nodiscard]] std::string previewMessage(std::string token) const;
    void openDetail(std::string token) {if (model->active() && gallery->active()) gallery->openDetail(std::move(token));}
};
