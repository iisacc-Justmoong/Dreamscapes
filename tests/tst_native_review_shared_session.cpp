#include "App/Views/Result/GenerationReviewModel.h"
#include "App/Views/Result/NativeReviewGalleryView.h"
#include <iiGUI/WidgetState.h>
#include <QCoreApplication>
#include <QUrl>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template <class T> T take(ii::core::Result<T> result) {
    if (!result) {
        if (result.error().cause) std::rethrow_exception(result.error().cause);
        throw std::runtime_error(result.error().message);
    }
    return std::move(*result);
}
void good(ii::core::Result<void> result) {
    if (!result) {
        if (result.error().cause) std::rethrow_exception(result.error().cause);
        throw std::runtime_error(result.error().message);
    }
}
void exercise() {
    using namespace dreamscapes;
    GenerationReviewModel product;
    auto session = product.nativeSession();
    auto state = std::make_shared<ii::gui::detail::WidgetState>([](ii::runtime::Task task) { task(); });
    auto root = take(state->content_host({}, "root", {960, 600}));
    std::shared_ptr<review::ReviewGalleryModel> gallery;
    QVariantList edited;
    good(root.set_content([&](ii::gui::WidgetBuilder &builder) {
        return review::buildReviewGallery(builder, {}, [&](const auto &model) {
            gallery = model;
            model->setEditHandler([&](auto) { edited = product.actionEntries(); });
        }, session);
    }));
    check(gallery->session() == session, "The LVUI gallery created a separate review session");
    const QVariantMap first{{"id", "first"}, {"documentSource", QUrl("file:///one.iisc")},
                            {"imageSource", QUrl("file:///one.png")}, {"mediaType", "Image"},
                            {"opaque", QVariantMap{{"reference", QUrl("file:///original%20reference.png")}}}};
    const QVariantMap second{{"id", "second"}, {"documentSource", QUrl("file:///two.iisc")},
                             {"imageSource", QUrl("file:///two.png")}, {"mediaType", "Image"}};
    product.setEntries({first, second});
    check(gallery->items.get().size() == 2, "The actual app model did not populate the LVUI gallery");
    product.selectAt(0);
    check(gallery->session()->selectedCount() == 1 && gallery->items.get().front()->selected.get(),
          "Product selection did not reach the native view");
    good(take(root.content_item("shortlist")).activate());
    check(product.shortlistedCount() == 1, "Native review action did not update the product model");
    good(take(root.content_item("edit")).activate());
    check(edited == QVariantList{first} && edited.front().toMap().value("imageSource").metaType() == QMetaType::fromType<QUrl>() &&
              edited.front().toMap().value("opaque").toMap().value("reference").metaType() == QMetaType::fromType<QUrl>(),
          "The editor action lost the original payload or URL types");
    good(take(root.content_item("shortlisted")).activate());
    check(product.filter() == "shortlisted" && product.rowCount() == 1, "The native filter did not update the product model");
    good(root.clear_content());
    gallery.reset();
    product.setFilter("all");
    product.selectAt(1);
    check(product.selectedEntries() == QVariantList{second}, "Closing the LVUI view damaged the product session");
    state->close();
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    return 0;
}
