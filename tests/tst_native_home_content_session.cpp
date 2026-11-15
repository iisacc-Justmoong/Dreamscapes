#include "App/Generation/GenerationController.h"
#include "App/ViewModels/WorkspaceSession.h"
#include "App/Views/Home/NativeHomeContentSession.h"
#include <Container.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dreamscapes;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void good(ii::core::Result<void> result) {
    if (!result) { if (result.error().cause) std::rethrow_exception(result.error().cause); throw std::runtime_error(result.error().message); }
}
class Images final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    mutable std::string last;
    std::shared_ptr<const ii::gui::RasterImage> pixels;
    std::shared_ptr<const ii::gui::RasterImage> image(std::string_view source, std::uint32_t, std::uint32_t) const override {
        last = source; return pixels;
    }
};
QVariantMap row(QString id, QString section, QString name, QString preview) {
    return {{"id", id}, {"containerId", "fixture"}, {"key", section + "/" + id}, {"section", section},
            {"name", name}, {"exists", true}, {"available", false}, {"previewSource", QUrl(preview)}};
}
void exercise() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/home-content-XXXXXX"));
    check(directory.isValid(), "isolated fixture unavailable");
    qputenv("SOCIETY_STORAGE_SETTINGS_PATH", directory.filePath("settings.ini").toUtf8());
    qunsetenv("SOCIETY_CONTAINER_PATH");
    GenerationRuntime runtime;
    runtime.nativeInference = true;
    runtime.modelPreferencesFile = directory.filePath("models.conf");
    runtime.failureReportsDirectory = directory.path();
    runtime.inspectImageModel = [](const auto &) { return iiLocalDiffusion::ImageModelInspection{}; };
    auto generation = std::make_unique<GenerationController>(runtime);
    generation->setForeground(false);
    WorkspaceSession workspace; workspace.setGeneration(generation.get());
    auto base = std::make_shared<Images>(); base->pixels = *ii::gui::rgba_image(1, 1, {1, 2, 3, 255});
    auto session = std::make_unique<home::NativeHomeContentSession>(&workspace, home::HomeRecentFilesStrings{},
        home::HomeSelectedFileStrings{}, home::HomeRecentGenerationsStrings{}, base);
    auto files = session->recentFiles(); auto selected = session->selectedFile(); auto results = session->recentGenerations();
    auto fileImages = session->recentFileImages(), resultImages = session->recentGenerationImages();
    int refreshRequests = 0; QObject::connect(generation->container(), &iiSocietyContainer::Container::refreshRequested,
                                             [&] { ++refreshRequests; });
    auto a = row("a", "files", "A.iisc", "https://example.invalid/a.png");
    auto b = row("b", "files", "B.mp4", "https://example.invalid/b.png");
    b["available"] = true; b["source"] = QUrl("https://example.invalid/b.mp4"); b["mediaType"] = "Video";
    auto r = row("r", "generation-history", "result.png", "https://example.invalid/result.png");
    r["isImage"] = true; r["source"] = QUrl("https://example.invalid/result.png");
    generation->container()->synchronize("fixture", directory.path(), {a, b, r}, true);
    check(session->lastError().isEmpty() && files->rows.get().size() == 2 && results->rows.get().size() == 1,
          "Society mirror did not reach the native Home content");
    check(selected->name.get() == "A.iisc" && files->selectedKey.get() == "a", "initial file was not selected");
    check(fileImages->image(files->rows.get()[0].preview, 36, 36) == base->pixels && base->last == "https://example.invalid/a.png",
          "recent file preview did not use the original Society preview");
    check(resultImages->image(results->rows.get()[0].preview, 60, 40) == base->pixels && base->last == "https://example.invalid/result.png",
          "result preview did not use the projected Society result");
    good(files->selectToken(files->tokenForKey("b")));
    check(selected->name.get() == "B.mp4", "selecting a recent file did not update its panel");
    QVariantMap opened;
    QObject::connect(&workspace, &WorkspaceSession::manualResultRequested, [&](QVariantMap value) { opened = std::move(value); });
    selected->open(); check(opened.value("id") == "b" && opened.value("source").toUrl() == b.value("source").toUrl(),
                            "selected-file open did not pass the original Society object");
    good(results->toggleMode()); good(results->activate(results->rows.get()[0].token));
    check(results->selectedCount.get() == 1, "result selection did not start");
    const auto selectedKeys = results->selectedImageKeys.get();
    b["description"] = "Updated";
    generation->container()->synchronize("fixture", directory.path(), {a, b, r}, true);
    check(files->selectedKey.get() == "b" && selected->name.get() == "B.mp4" &&
          results->selectionMode.get() && results->selectedImageKeys.get() == selectedKeys,
          "mirror update lost Home selection");
    auto c = row("c", "files", "C.iisc", "https://example.invalid/c.png");
    bool replaced = false;
    auto reentrant = files->rows.observe([&](const auto &, std::uint64_t) {
        if (!replaced) { replaced = true; generation->container()->synchronize("fixture", directory.path(), {a, b, c, r}, true); }
    });
    a["description"] = "A changed";
    generation->container()->synchronize("fixture", directory.path(), {a, b, r}, true);
    check(replaced && files->rows.get().size() == 3 && files->selectedKey.get() == "b" &&
          selected->name.get() == "B.mp4" && session->lastError().isEmpty(),
          "reentrant mirror update was lost or reset selection");
    reentrant.reset();
    generation->container()->synchronize("fixture", directory.path(), {a, r}, true);
    check(files->selectedKey.get() == "a" && selected->name.get() == "A.iisc", "removed selection was not reconciled");
    files->browse(); check(workspace.storageBrowserSection() == "files", "recent files View all did not navigate");
    results->browse(); check(workspace.storageBrowserSection() == "generation-history", "recent generations View all did not navigate");
    check(refreshRequests == 0, "a view action requested a storage refresh");
    workspace.setGeneration(nullptr);
    check(files->rows.get().empty() && results->rows.get().empty() && !selected->hasFile.get(), "detached controller left stale Home data");
    generation->container()->synchronize("fixture", directory.path(), {a, b, r}, true);
    check(files->rows.get().empty(), "retired controller still changed Home");
    workspace.setGeneration(generation.get());
    check(files->rows.get().size() == 2 && results->rows.get().size() == 1, "controller reattachment did not restore its mirror");
    auto replacement = std::make_unique<GenerationController>(runtime); replacement->setForeground(false);
    workspace.setGeneration(replacement.get());
    check(files->rows.get().empty() && results->rows.get().empty(), "controller replacement reused stale mirror data");
    replacement->container()->synchronize("fixture", directory.path(), {b, r}, true);
    check(files->rows.get().size() == 1 && selected->name.get() == "B.mp4", "replacement controller was not observed");
    generation->container()->synchronize("fixture", directory.path(), {a, b, c, r}, true);
    check(files->rows.get().size() == 1, "previous controller still affected the replacement session");
    const auto stale = files->rows.get()[0].preview;
    session.reset();
    check(!fileImages->image(stale, 36, 36), "retired session resolved a preview");
    workspace.showStorage("files"); results->browse();
    check(workspace.storageBrowserSection() == "files", "retired session retained navigation commands");
    auto retiring = std::make_unique<home::NativeHomeContentSession>(&workspace, home::HomeRecentFilesStrings{},
        home::HomeSelectedFileStrings{}, home::HomeRecentGenerationsStrings{}, base);
    auto retained = retiring->selectedFile();
    auto retireDuringUpdate = retained->name.observe([&](const auto &, std::uint64_t) { retiring.reset(); });
    b["name"] = "Changed.mp4";
    replacement->container()->synchronize("fixture", directory.path(), {b, r}, true);
    check(!retiring && !retained->canOpen.get(), "session retirement during publication left an active command");
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try { exercise(); } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
