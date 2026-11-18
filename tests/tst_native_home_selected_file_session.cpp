#include "App/Views/Home/NativeHomeSelectedFileSession.h"
#include <iiSharedCanvas>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void exercise() {
    QTemporaryDir directory(QStringLiteral(DREAMSCAPES_TEST_DIRECTORY "/native-selected-file-XXXXXX"));
    check(directory.isValid(), "temporary fixture unavailable");
    iiSharedCanvas::Document document;
    document.extent = {40, 20};
    document.assets.emplace_back(iiSharedCanvas::RasterAsset{"red", makeRasterLayer(40, 20, 0x80ff0000U)});
    document.layers.emplace_back(iiSharedCanvas::StaticBitmapLayer{{"paint", "Paint"}, iiSharedCanvas::StaticSource{"red"}});
    const auto encoded = iiSharedCanvas::encodeIisc(document);
    check(encoded.ok(), "cannot encode iisc fixture");
    const auto path = directory.filePath(QString::fromUtf8("선택 파일.iisc"));
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(reinterpret_cast<const char *>(encoded.bytes.data()),
          qsizetype(encoded.bytes.size())) == qsizetype(encoded.bytes.size()), "cannot write iisc fixture");
    file.close();
    const QVariantMap selected{{"id", "one"}, {"name", "선택 파일.iisc"}, {"source", QUrl::fromLocalFile(path)},
        {"relativePath", "Work/선택 파일.iisc"}, {"version", "v1"}, {"available", true},
        {"opaque", QVariantMap{{"url", QUrl("file:///opaque%20original.iisc")}}}};
    QVariantMap opened;
    auto session = std::make_unique<dreamscapes::home::NativeHomeSelectedFileSession>(
        dreamscapes::home::HomeSelectedFileStrings{}, [&](QVariantMap file) { opened = std::move(file); });
    auto model = session->model();
    check(bool(session->setFile(selected)), "local selection was rejected");
    check(model->previewSource.get().starts_with("image://iisc-preview/") && model->canOpen.get(),
          "the existing iisc preview address did not reach the native model");
    QElapsedTimer elapsed; elapsed.start();
    while (model->dimensions.get() != "40 × 20 px" && elapsed.elapsed() < 10000) {
        QCoreApplication::processEvents(); QThread::msleep(2);
    }
    check(model->dimensions.get() == "40 × 20 px" && model->structure.get() == "1 pages · 1 layers" &&
          model->color.get() == "ARGB 8-bit", "actual iisc metadata did not reach the native model");
    model->open();
    check(opened == selected && opened.value("source").metaType() == QMetaType::fromType<QUrl>(),
          "opening lost the exact original file object or URL type");
    check(file.open(QIODevice::ReadOnly) && file.readAll() == QByteArray(reinterpret_cast<const char *>(encoded.bytes.data()),
          qsizetype(encoded.bytes.size())), "reading the preview or metadata modified the original file");
    file.close();
    const QVariantMap remote{{"id", "two"}, {"name", "remote.iisc"}, {"available", false},
        {"source", QUrl::fromLocalFile(path)}, {"previewSource", QUrl("file:///remote-thumbnail.png")}};
    check(bool(session->setFile(remote)), "remote selection was rejected");
    check(model->openLabel.get() == "Download and open" && model->previewSource.get() == "file:///remote-thumbnail.png" &&
          model->dimensions.get() == "Unavailable", "remote file retained old details or used the local iisc path");
    model->open(); check(opened == remote, "remote open lost its original Society fields");
    session.reset();
    check(!model->canOpen.get(), "retired session left its open command enabled");
    auto retiring = std::make_unique<dreamscapes::home::NativeHomeSelectedFileSession>(
        dreamscapes::home::HomeSelectedFileStrings{}, [](QVariantMap) {});
    auto retained = retiring->model();
    auto observer = retained->name.observe([&](const auto &, std::uint64_t) { retiring.reset(); });
    auto *raw = retiring.get();
    const auto retired = raw->setFile(selected);
    check(!retired && retired.error().code == ii::core::ErrorCode::closed && !retained->canOpen.get(),
          "session retirement during publication left a live action or invalidated the update guard");
}
}
int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    try { exercise(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
