#include "NativeHomeContentSession.h"
#include "App/Generation/GenerationController.h"
#include "App/ViewModels/WorkspaceSession.h"
#include <Container.h>
#include <QThread>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
NativeHomeContentSession::NativeHomeContentSession(WorkspaceSession *workspace, HomeRecentFilesStrings fileStrings,
        HomeSelectedFileStrings selectedStrings, HomeRecentGenerationsStrings generationStrings,
        std::shared_ptr<const ImageResolver> base, QObject *parent)
    : QObject(parent), workspace_(workspace) {
    if (!workspace || workspace->thread() != QThread::currentThread() || thread() != QThread::currentThread())
        throw std::invalid_argument("Home content requires a workspace on its owner thread");
    const QPointer<NativeHomeContentSession> guard(this);
    files_ = std::make_unique<NativeHomeRecentFilesSession>(std::move(fileStrings), NativeHomeRecentFilesSession::Callbacks{
        .select = [guard](QString key) { if (guard) { guard->selectedKey_ = std::move(key); guard->requestRefresh(); } },
        .open = [guard](QVariant file) { if (guard && guard->workspace_) guard->workspace_->openHomeFile(file.toMap()); },
        .browse = [guard] { if (guard && guard->workspace_) guard->workspace_->showStorage("files"); },
        .filter = [guard](QString value) { if (guard) guard->filter_ = std::move(value); }});
    selected_ = std::make_unique<NativeHomeSelectedFileSession>(std::move(selectedStrings), [guard](QVariantMap file) {
        if (guard && guard->workspace_) guard->workspace_->openHomeFile(file);
    });
    generations_ = std::make_unique<NativeHomeRecentGenerationsSession>(std::move(generationStrings),
        NativeHomeRecentGenerationsSession::Callbacks{
            .focus = [guard](QString key) { if (guard) guard->focusedKey_ = std::move(key); },
            .open = [guard](QVariant file) { if (guard && guard->workspace_) guard->workspace_->openHomeFile(file.toMap()); },
            .edit = [guard](QVariantList files) { if (guard && guard->workspace_) guard->workspace_->openImages(files); },
            .browse = [guard] { if (guard && guard->workspace_) guard->workspace_->showStorage("generation-history"); },
            .selection = [guard](bool mode, QStringList keys) {
                if (guard) { guard->selectionMode_ = mode; guard->selectedKeys_ = std::move(keys); }
            }});
    fileImages_ = std::make_shared<NativeHomeRecentFilesImages>(files_.get(), base);
    generationImages_ = std::make_shared<NativeHomeRecentGenerationsImages>(generations_.get(), std::move(base));
    connect(workspace, &WorkspaceSession::generationChanged, this, [this] { rebind(); });
    connect(workspace, &QObject::destroyed, this, [this] { workspace_ = nullptr; rebind(); });
    rebind();
}
NativeHomeContentSession::~NativeHomeContentSession() = default;
std::shared_ptr<HomeRecentFilesModel> NativeHomeContentSession::recentFiles() const { return files_->model(); }
std::shared_ptr<HomeSelectedFileModel> NativeHomeContentSession::selectedFile() const { return selected_->model(); }
std::shared_ptr<HomeRecentGenerationsModel> NativeHomeContentSession::recentGenerations() const { return generations_->model(); }
void NativeHomeContentSession::rebind() {
    QObject::disconnect(mirrorChanged_); QObject::disconnect(storageChanged_); QObject::disconnect(generationDestroyed_);
    generation_ = workspace_ ? qobject_cast<GenerationController *>(workspace_->generation()) : nullptr;
    hasHistory_ = false;
    if (generation_) {
        mirrorChanged_ = connect(generation_->container(), &iiSocietyContainer::Container::stateChanged, this, [this] { requestRefresh(); });
        storageChanged_ = connect(generation_, &GenerationController::storageChanged, this, [this] { requestRefresh(); });
        generationDestroyed_ = connect(generation_, &QObject::destroyed, this, [this] { generation_ = nullptr; rebind(); });
    }
    requestRefresh();
}
void NativeHomeContentSession::requestRefresh() {
    const QPointer<NativeHomeContentSession> guard(this);
    auto result = refresh();
    if (guard) lastError_ = result ? QString{} : QString::fromStdString(result.error().message);
}
ii::core::Result<void> NativeHomeContentSession::refresh() {
    using ii::core::ErrorCode;
    if (thread() != QThread::currentThread()) return ii::core::fail(ErrorCode::wrong_executor, "Home content requires its owner thread");
    const auto update = update_;
    if (update->updating) { update->pending = true; return {}; }
    struct Reset final { std::shared_ptr<UpdateState> value; ~Reset() { value->updating = false; } } reset{update};
    update->updating = true;
    const QPointer<NativeHomeContentSession> guard(this);
    try {
        for (unsigned round = 0; round < 64; ++round) {
            update->pending = false;
            auto generation = generation_;
            auto *container = generation ? generation->container() : nullptr;
            const auto current = container ? container->recentFiles() : QVariantList{};
            const auto historyInput = container ? container->generationHistory() : QVariantList{};
            const bool loading = generation && (generation->storageInitializing() || container->loading());
            if (!hasHistory_ || historyInput != historyInput_) {
                auto history = generation ? generation->historyEntries(historyInput) : QVariantList{};
                if (!guard) return ii::core::fail(ErrorCode::closed, "Home content was retired");
                historyInput_ = historyInput; history_ = std::move(history); hasHistory_ = true;
            }
            currentFiles_ = current;
            auto fileResult = files_->synchronize(currentFiles_, loading, selectedKey_, filter_);
            if (!guard) return ii::core::fail(ErrorCode::closed, "Home content was retired");
            if (!fileResult) return fileResult;
            selectedKey_ = QString::fromStdString(files_->model()->selectedKey.get());
            const auto token = files_->model()->tokenForKey(selectedKey_.toStdString());
            const auto index = files_->model()->sourceIndexForToken(token);
            const auto selected = index && *index < static_cast<std::size_t>(currentFiles_.size())
                ? currentFiles_.at(static_cast<qsizetype>(*index)).toMap() : QVariantMap{};
            if (!hasSelected_ || selected != currentSelected_) {
                auto selectedResult = selected_->setFile(selected);
                if (!guard) return ii::core::fail(ErrorCode::closed, "Home content was retired");
                if (!selectedResult) return selectedResult;
                currentSelected_ = selected; hasSelected_ = true;
            }
            auto result = generations_->synchronize(history_, loading, focusedKey_, selectionMode_, selectedKeys_);
            if (!guard) return ii::core::fail(ErrorCode::closed, "Home content was retired");
            if (!result) return result;
            auto model = generations_->model();
            focusedKey_ = QString::fromStdString(model->focusedKey.get()); selectionMode_ = model->selectionMode.get();
            selectedKeys_.clear(); for (const auto &key : model->selectedImageKeys.get()) selectedKeys_.push_back(QString::fromStdString(key));
            if (!update->pending) return {};
        }
        return ii::core::fail(ErrorCode::busy, "Home content did not settle");
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot synchronize Home content", std::current_exception()); }
}
} // namespace dreamscapes::home
