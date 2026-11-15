#include "App/Views/Home/NativeHomeRecentGenerationsSession.h"
#include <QCoreApplication>
#include <QUrl>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) { if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); } }
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::string last;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source, std::uint32_t, std::uint32_t) const override { last = source; return pixels; }
    std::shared_ptr<const ii::gui::RasterImage> pixels;
};
void exercise() {
    const QVariantMap a{{"id", "a"}, {"name", "A"}, {"source", QUrl("https://example.invalid/a.png")}, {"opaque", QVariantMap{{"n", 42}}}};
    const QVariantMap b{{"id", "b"}, {"name", "B"}, {"source", QUrl::fromLocalFile("/b.png")}, {"opaque", QVariantMap{{"n", 77}}}};
    const QVariantMap v{{"id", "v"}, {"name", "V"}, {"source", QUrl::fromLocalFile("/v.mp4")}, {"mediaType", "Video"}};
    QVariant opened; QVariantList edited; QString focused; QStringList selected; bool mode = false; int browsed = 0, previewCalls = 0, selectionCalls = 0;
    auto base = std::make_shared<Images>(); base->pixels = *ii::gui::rgba_image(1, 1, {1, 2, 3, 255});
    auto session = std::make_unique<home::NativeHomeRecentGenerationsSession>(home::HomeRecentGenerationsStrings{},
        home::NativeHomeRecentGenerationsSession::Callbacks{
            .focus = [&](QString key) { focused = std::move(key); }, .open = [&](QVariant file) { opened = std::move(file); },
            .edit = [&](QVariantList files) { edited = std::move(files); }, .browse = [&] { ++browsed; },
            .selection = [&](bool value, QStringList keys) { mode = value; selected = std::move(keys); ++selectionCalls; }},
        [&](const QVariantMap &file) { ++previewCalls; return QUrl::fromLocalFile("/preview/" + file.value("id").toString()); });
    auto model = session->model();
    good(session->synchronize({a, b, v}, false, "b", false, {}));
    check(model->focusedKey.get() == "b" && focused.isEmpty() && previewCalls == 0, "hydration performed commands or resolved invisible previews");
    auto rows = model->rows.get(); auto images = std::make_shared<home::NativeHomeRecentGenerationsImages>(session.get(), base);
    check(images->image(rows[0].preview, 60, 40) == base->pixels && base->last == "file:///preview/a" && previewCalls == 1,
        "visible preview did not resolve lazily");
    (void)images->image(rows[0].preview, 60, 40); check(previewCalls == 1, "preview address was not cached");
    good(model->activate(rows[0].token));
    check(opened.toMap() == a && opened.toMap().value("source").metaType() == QMetaType::fromType<QUrl>() && focused == "a",
        "open lost original result fields or focus");
    good(model->toggleMode()); good(model->activate(rows[0].token)); good(model->activate(rows[1].token));
    check(mode && selected == QStringList({"https://example.invalid/a.png", "file:///b.png"}), "selection did not reach the host");
    model->editSelection(); check(edited.size() == 2 && edited[0].toMap() == a && edited[1].toMap() == b, "edit did not preserve original result objects");
    const auto calls = selectionCalls;
    good(session->synchronize({a, b, v}, false, "b", true, {"file:///b.png"}));
    check(model->focusedKey.get() == "b" && model->selectedCount.get() == 1 && selectionCalls == calls,
        "host state was not restored without echo");
    model->browse(); check(browsed == 1, "View all did not reach the host");
    const auto stale = rows[0].preview;
    check(!session->synchronize({a, 7}, false, "a", false, {}), "invalid file snapshot was accepted");
    check(session->previewAddress(stale) == "file:///preview/a", "invalid snapshot retired a valid preview");
    good(session->synchronize({b}, false, "b", true, {"file:///b.png"}));
    check(!session->previewAddress(stale) && previewCalls == 1, "old preview remained valid or refresh loaded images eagerly");
    session.reset(); check(!images->image(stale, 60, 40), "closed session resolved a preview");
    model->browse(); check(browsed == 1, "closed session retained host actions");
}
void reentrantPreview() {
    const QVariantMap a{{"id", "a"}, {"source", "file:///a.png"}}, b{{"id", "b"}, {"source", "file:///b.png"}};
    home::NativeHomeRecentGenerationsSession *sessionPtr = nullptr; bool changed = false;
    home::NativeHomeRecentGenerationsSession session(home::HomeRecentGenerationsStrings{}, {}, [&](const QVariantMap &file) {
        if (!changed) { changed = true; good(sessionPtr->synchronize({b}, false, "b", false, {})); }
        return QUrl::fromLocalFile("/preview/" + file.value("id").toString());
    });
    sessionPtr = &session; good(session.synchronize({a}, false, "a", false, {}));
    const auto old = session.model()->rows.get()[0].preview;
    check(!session.previewAddress(old), "reentrant refresh returned a stale preview");
    check(session.previewAddress(session.model()->rows.get()[0].preview) == "file:///preview/b", "new snapshot used an old preview");
}
}
int main(int argc, char **argv) { QCoreApplication app(argc, argv); try { exercise(); reentrantPreview(); } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
