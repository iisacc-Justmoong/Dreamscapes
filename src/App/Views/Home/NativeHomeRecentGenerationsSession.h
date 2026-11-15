#pragma once

#include "NativeHomeRecentGenerations.h"
#include <LVUI/NativeImageResolver.h>
#include <iiGUI/Image.h>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dreamscapes::home {
// Temporary adapter for the existing Home storage and selection state. The
// model and the LVUI widget themselves are independent of Qt and QML.
class NativeHomeRecentGenerationsSession final : public QObject {
  public:
    struct Callbacks final {
        std::function<void(QString)> focus;
        std::function<void(QVariant)> open;
        std::function<void(QVariantList)> edit;
        std::function<void()> browse;
        std::function<void(bool, QStringList)> selection;
    };
    using PreviewSource = std::function<QUrl(const QVariantMap &)>;
    NativeHomeRecentGenerationsSession(HomeRecentGenerationsStrings, Callbacks, PreviewSource = {}, QObject *parent = nullptr);
    ~NativeHomeRecentGenerationsSession() override;
    [[nodiscard]] std::shared_ptr<HomeRecentGenerationsModel> model() const { return model_; }
    [[nodiscard]] ii::core::Result<void> synchronize(QVariantList, bool loading, QString focusedKey, bool selectionMode, QStringList selectedKeys);
    [[nodiscard]] std::optional<std::string> previewAddress(std::string_view token);
  private:
    std::shared_ptr<HomeRecentGenerationsModel> model_;
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
class NativeHomeRecentGenerationsImages final : public lvui::native::ImageResolver<ii::gui::RasterImage> {
  public:
    NativeHomeRecentGenerationsImages(NativeHomeRecentGenerationsSession *,
        std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>>);
    [[nodiscard]] std::shared_ptr<const ii::gui::RasterImage>
    image(std::string_view source, std::uint32_t width, std::uint32_t height) const override;
  private:
    QPointer<NativeHomeRecentGenerationsSession> session_;
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> base_;
};
} // namespace dreamscapes::home
