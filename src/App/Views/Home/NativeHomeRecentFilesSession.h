#pragma once

#include "NativeHomeRecentFiles.h"
#include <LVUI/NativeImageResolver.h>
#include <iiGUI/Image.h>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariant>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dreamscapes::home {
// Temporary transport for the existing storage/controller types. The LVUI
// view and its model do not depend on a QML engine or QQuick widget.
class NativeHomeRecentFilesSession final : public QObject {
  public:
    struct Callbacks final {
        std::function<void(QString)> select;
        std::function<void(QVariant)> open;
        std::function<void()> browse;
        std::function<void(QString)> filter;
    };
    using PreviewSource = std::function<QUrl(const QVariantMap &)>;
    NativeHomeRecentFilesSession(HomeRecentFilesStrings, Callbacks, PreviewSource = {}, QObject *parent = nullptr);
    ~NativeHomeRecentFilesSession() override;
    [[nodiscard]] std::shared_ptr<HomeRecentFilesModel> model() const { return model_; }
    [[nodiscard]] ii::core::Result<void> synchronize(QVariantList files, bool loading, QString selectedKey, QString filter);
    [[nodiscard]] std::optional<std::string> previewAddress(std::string_view token);
  private:
    std::shared_ptr<HomeRecentFilesModel> model_;
    HomeRecentFilesStrings strings_;
    Callbacks callbacks_;
    PreviewSource preview_;
    QVariantList files_;
    std::vector<std::string> previewTokens_;
    std::vector<std::optional<std::string>> previewCache_;
    std::unordered_map<std::string, std::size_t> previewIndices_;
    std::uint64_t sessionId_ = 0, revision_ = 0;
    bool hasSnapshot_ = false, loading_ = false;
    std::shared_ptr<bool> updating_ = std::make_shared<bool>(false);
};

class NativeHomeRecentFilesImages final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    NativeHomeRecentFilesImages(NativeHomeRecentFilesSession *,
        std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>>);
    [[nodiscard]] std::shared_ptr<const ii::gui::RasterImage>
    image(std::string_view source, std::uint32_t width, std::uint32_t height) const override;
  private:
    QPointer<NativeHomeRecentFilesSession> session_;
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> base_;
};
} // namespace dreamscapes::home
