#include "App/Views/Result/NativeReviewGenerationBridge.h"
#include "App/ViewModels/GenerationProjection.h"
#include "App/ViewModels/ViewData.h"
#include <QCoreApplication>
#include <QUrl>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
QVariantMap result(const QString &id) {
    return {{"id", id}, {"documentSource", QUrl("file:///results/" + id + ".iisc")},
            {"imageSource", QUrl("file:///results/" + id + ".png")}, {"mediaType", "Image"},
            {"opaque", QVariantMap{{"url", QUrl("file:///reference%20one.png")}, {"value", 42}}}};
}
class PreviewRecorder final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::string address;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view value, std::uint32_t,
                                                      std::uint32_t) const override {
        address = value;
        return {};
    }
};
void exercise() {
    dreamscapes::GenerationProjection projection;
    const auto first = result("one"), second = result("two"), third = result("three");
    projection.setResults({first, second});
    auto model = dreamscapes::review::ReviewGalleryModel::create(std::make_shared<dreamscapes::review::Session>());
    auto preview = std::make_shared<PreviewRecorder>();
    model->setImageResolver(preview);
    QVariantList edited, compared, exported, detailed;
    {
        dreamscapes::review::NativeReviewGenerationBridge bridge(&projection, model,
            {.edit = [&](const auto &entries) { edited = entries; },
             .compare = [&](const auto &entries) { compared = entries; },
             .exportResults = [&](const auto &entries) { exported = entries; },
             .detail = [&](const auto &entries) { detailed = entries; }});
        check(model->session()->totalCount() == 2, "initial results did not reach the native model");
        (void)model->imageForToken(model->items.get().front()->token, 152, 132);
        check(preview->address == "file:///results/one.png", "the original QUrl did not reach the native preview resolver");
        model->session()->selectAt(0);
        model->edit();
        check(edited == QVariantList{first}, "edit did not retain the original payload and URL types");
        auto updated = first;
        updated["opaque"] = QVariantMap{{"url", QUrl("file:///changed.png")}, {"value", 99}};
        projection.setResults({updated, second});
        model->edit();
        check(edited == QVariantList{updated}, "updated result did not replace the original payload");
        projection.setResults({first, second});
        const auto focused = model->session()->focusedKey();
        projection.setResults({first, second, third});
        check(model->session()->totalCount() == 3 && model->session()->selectedCount() == 1 &&
              model->session()->focusedKey() == focused, "new results lost selection or focus");
        model->session()->selectAt(1, false, true);
        model->compare();
        check(compared == QVariantList({first, second}), "compare did not retain original selected results");
        model->exportResults();
        check(exported == QVariantList({first, second}), "export did not retain original selected results");
        model->detail();
        check(detailed == QVariantList{second}, "detail did not retain the focused original result");
        model->session()->setReview(dreamscapes::review::State::Shortlisted);
        model->session()->setFilter("shortlisted");
        projection.setResults({first, second, third, result("four")});
        check(model->session()->visibleCount() == 2 && model->session()->selectedCount() == 2,
              "a filtered arrival lost review or selection state");
    }
    check(!model->canEdit.get() && !model->canCompare.get() && !model->canExport.get(),
          "closed bridge left actions enabled");
    projection.setResults({third});
    check(model->session()->totalCount() == 4, "closed bridge still updated the native session");

    auto source = std::make_unique<dreamscapes::GenerationProjection>();
    source->setResults({first});
    auto detachedModel = dreamscapes::review::ReviewGalleryModel::create(std::make_shared<dreamscapes::review::Session>());
    dreamscapes::review::NativeReviewGenerationBridge detached(source.get(), detachedModel,
        {.edit = [&](const auto &entries) { edited = entries; }});
    detachedModel->session()->selectAt(0);
    check(detachedModel->canEdit.get(), "provided edit handler did not become available");
    source.reset();
    check(!detachedModel->canEdit.get(), "destroyed projection left an enabled action");
    detached.detach();

    dreamscapes::GenerationProjection closedProjection;
    auto closedModel = dreamscapes::review::ReviewGalleryModel::create(std::make_shared<dreamscapes::review::Session>());
    {
        dreamscapes::review::NativeReviewGenerationBridge closing(&closedProjection, closedModel);
        closedModel->close();
        closedProjection.setResults({first});
    }

    dreamscapes::GenerationProjection throwingProjection;
    auto throwingModel = dreamscapes::review::ReviewGalleryModel::create(std::make_shared<dreamscapes::review::Session>());
    throwingProjection.setResults({first});
    dreamscapes::review::NativeReviewGenerationBridge throwingBridge(&throwingProjection, throwingModel,
        {.edit = [&](const auto &entries) { edited = entries; }});
    throwingModel->session()->selectAt(0);
    auto observer = std::make_shared<dreamscapes::review::ChangePort>([](const auto &) { throw std::runtime_error("observer"); });
    check(bool(throwingModel->session()->observe(observer)), "could not subscribe the failing observer");
    auto changed = first;
    changed["opaque"] = QVariantMap{{"url", QUrl("file:///after-error.png")}, {"value", 123}};
    bool observedFailure = false;
    try { throwingProjection.setResults({changed}); } catch (const std::runtime_error &) { observedFailure = true; }
    check(observedFailure, "observer failure was not propagated");
    throwingModel->edit();
    check(edited == QVariantList{changed}, "observer failure left the action mapped to stale data");

    dreamscapes::GenerationProjection staleProjection;
    auto staleModel = dreamscapes::review::ReviewGalleryModel::create(std::make_shared<dreamscapes::review::Session>());
    staleProjection.setResults({first});
    int staleActions = 0;
    dreamscapes::review::NativeReviewGenerationBridge staleBridge(&staleProjection, staleModel,
        {.edit = [&](const auto &) { ++staleActions; }});
    staleModel->session()->selectAt(0);
    auto external = first;
    external["prompt"] = "independently changed";
    staleModel->session()->replaceEntries({dreamscapes::nativeValue(external)});
    staleModel->edit();
    check(staleActions == 0, "a stale identity forwarded an unrelated original payload");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "Native review bridge preserves live results, selection and opaque payloads\n";
}
