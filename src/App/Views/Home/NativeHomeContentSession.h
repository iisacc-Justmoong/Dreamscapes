#pragma once

#include "NativeHomeRecentFilesSession.h"
#include "NativeHomeRecentGenerationsSession.h"
#include "NativeHomeSelectedFileSession.h"
#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <memory>

class GenerationController;
namespace dreamscapes {
class WorkspaceSession;
namespace home {

// Temporary product-backend adapter. The native Home views do not depend on
// QML; this adapter can be replaced when the remaining Qt services migrate.
class NativeHomeContentSession final : public QObject {
  public:
    using ImageResolver = lvui::native::ImageResolver<ii::gui::RasterImage>;
    NativeHomeContentSession(WorkspaceSession *, HomeRecentFilesStrings, HomeSelectedFileStrings,
                             HomeRecentGenerationsStrings, std::shared_ptr<const ImageResolver>, QObject *parent = nullptr);
    ~NativeHomeContentSession() override;
    [[nodiscard]] std::shared_ptr<HomeRecentFilesModel> recentFiles() const;
    [[nodiscard]] std::shared_ptr<HomeSelectedFileModel> selectedFile() const;
    [[nodiscard]] std::shared_ptr<HomeRecentGenerationsModel> recentGenerations() const;
    [[nodiscard]] std::shared_ptr<const ImageResolver> recentFileImages() const { return fileImages_; }
    [[nodiscard]] std::shared_ptr<const ImageResolver> recentGenerationImages() const { return generationImages_; }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] ii::core::Result<void> refresh();

  private:
    struct UpdateState { bool updating = false, pending = false; };
    void rebind();
    void requestRefresh();
    QPointer<WorkspaceSession> workspace_;
    QPointer<GenerationController> generation_;
    QMetaObject::Connection mirrorChanged_, storageChanged_, generationDestroyed_;
    std::unique_ptr<NativeHomeRecentFilesSession> files_;
    std::unique_ptr<NativeHomeSelectedFileSession> selected_;
    std::unique_ptr<NativeHomeRecentGenerationsSession> generations_;
    std::shared_ptr<const ImageResolver> fileImages_, generationImages_;
    std::shared_ptr<UpdateState> update_ = std::make_shared<UpdateState>();
    QVariantList currentFiles_, historyInput_, history_;
    QVariantMap currentSelected_;
    QString selectedKey_, filter_{"All types"}, focusedKey_, lastError_;
    QStringList selectedKeys_;
    bool selectionMode_ = false, hasHistory_ = false, hasSelected_ = false;
};
} // namespace home
} // namespace dreamscapes
