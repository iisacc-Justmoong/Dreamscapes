#pragma once
#include "NativeReviewCompare.h"
#include <memory>
namespace dreamscapes::review {
class ReviewGalleryModel;
std::shared_ptr<ii::gui::NativeViewSource> comparePaneSource(std::weak_ptr<CompareModel>,
    std::weak_ptr<ReviewGalleryModel>,const CompareModel::Item &);
}
