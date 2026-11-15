#include "App/Views/Home/NativeHomeRecentFilesSession.h"
#include <QCoreApplication>
#include <QUrl>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) {
    if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); }
}
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::string last;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source, std::uint32_t, std::uint32_t) const override {
        last = source; return pixels;
    }
    std::shared_ptr<const ii::gui::RasterImage> pixels;
};
void exercise() {
    const QVariantMap first{{"id", "a"}, {"name", "A.iisc"}, {"source", QUrl::fromLocalFile("/fixture/A.iisc")},
        {"opaque", QVariantMap{{"integer", 42}}}};
    const QVariantMap second{{"id", "b"}, {"name", "B.iisc"}, {"available", false},
        {"source", QUrl("https://example.invalid/b")}, {"opaque", QVariantMap{{"integer", 77}}}};
    QVariant opened; QString selected, filter; int browsed = 0, addressCalls = 0;
    auto base = std::make_shared<Images>();
    base->pixels = *ii::gui::rgba_image(1, 1, {1, 2, 3, 255});
    auto session = std::make_unique<home::NativeHomeRecentFilesSession>(home::HomeRecentFilesStrings{},
        home::NativeHomeRecentFilesSession::Callbacks{.select = [&](QString value) { selected = std::move(value); },
            .open = [&](QVariant value) { opened = std::move(value); }, .browse = [&] { ++browsed; },
            .filter = [&](QString value) { filter = std::move(value); }},
        [&](const QVariantMap &value) { ++addressCalls; return QUrl::fromLocalFile("/preview/" + value.value("id").toString()); });
    auto model = session->model();
    good(session->synchronize({first, second}, false, "b", "All types"));
    check(model->selectedKey.get() == "b" && selected.isEmpty() && addressCalls == 0, "hydration changed selection or eagerly inspected previews");
    const auto rows = model->rows.get();
    auto images = std::make_shared<home::NativeHomeRecentFilesImages>(session.get(), base);
    check(images->image(rows[1].preview, 36, 36) == base->pixels && addressCalls == 1 && base->last == "file:///preview/b",
        "visible file did not resolve its actual preview lazily");
    (void)images->image(rows[1].preview, 72, 72);
    check(addressCalls == 1, "repeated visible preview unnecessarily reread its address");
    good(model->openToken(rows[1].token));
    check(opened.toMap() == second && opened.toMap().value("source").metaType() == QMetaType::fromType<QUrl>(),
        "open did not preserve the exact Society file object");
    good(model->selectToken(rows[0].token)); check(selected == "a", "selection did not reach the host");
    good(model->filterAt(3)); check(filter == "Video", "filter command did not reach the host");
    model->browse(); check(browsed == 1, "view-all command did not reach the host");
    good(model->filterAt(0));
    const auto stale = rows[1].preview;
    const auto invalid = session->synchronize({first, 42}, false, "a", "All types");
    check(!invalid, "non-file snapshot was accepted");
    opened.clear(); good(model->openToken(rows[0].token));
    check(opened.toMap() == first && session->previewAddress(rows[1].preview) == "file:///preview/b",
        "rejected snapshot changed the active file or its preview");
    good(session->synchronize({first}, false, "b", "All types"));
    check(model->selectedKey.get() == "a" && selected == "a" && !session->previewAddress(stale),
        "removed selection or stale preview was not reconciled");
    check(addressCalls == 1, "snapshot refresh eagerly resolved unrelated previews");
    session.reset(); check(!images->image(stale, 36, 36), "retired session resolved a stale preview");
    model->browse(); check(browsed == 1, "retired session left its commands attached");
}
void reentrantPreview() {
    const QVariantMap first{{"id", "a"}, {"name", "First.iisc"}};
    const QVariantMap second{{"id", "b"}, {"name", "Second.iisc"}};
    home::NativeHomeRecentFilesSession *sessionPtr = nullptr;
    bool replaced = false;
    home::NativeHomeRecentFilesSession session(home::HomeRecentFilesStrings{}, {}, [&](const QVariantMap &file) {
        if (!replaced) {
            replaced = true;
            good(sessionPtr->synchronize({second}, false, "b", "All types"));
        }
        return QUrl::fromLocalFile("/preview/" + file.value("id").toString());
    });
    sessionPtr = &session;
    good(session.synchronize({first}, false, "a", "All types"));
    const auto stale = session.model()->rows.get()[0].preview;
    check(!session.previewAddress(stale), "preview for a replaced snapshot was accepted");
    const auto current = session.model()->rows.get()[0].preview;
    check(session.previewAddress(current) == "file:///preview/b", "replacement snapshot received a stale preview");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { exercise(); reentrantPreview(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
