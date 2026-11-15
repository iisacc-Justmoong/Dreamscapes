#include "NativeHomeRecentGenerationsSession.h"
#include "CanvasPreviewAddress.h"
#include "App/ViewModels/ViewData.h"
#include <QJSValue>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace dreamscapes::home {
namespace {
std::atomic_uint64_t nextSession{0};
QVariant normalized(QVariant value) {
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}
}
NativeHomeRecentGenerationsSession::NativeHomeRecentGenerationsSession(HomeRecentGenerationsStrings strings, Callbacks callbacks,
                                                                       PreviewSource preview, QObject *parent)
    : QObject(parent), model_(HomeRecentGenerationsModel::create(std::move(strings))), callbacks_(std::move(callbacks)),
      preview_(preview ? std::move(preview) : PreviewSource(canvasPreviewSource)), sessionId_(++nextSession) {
    const QPointer<NativeHomeRecentGenerationsSession> guard(this);
    model_->setHandlers({.focus = [guard](std::string key) {
            if (guard && !*guard->updating_ && guard->callbacks_.focus) {
                auto callback = guard->callbacks_.focus; callback(QString::fromStdString(key));
            }
        }, .open = [guard](std::string token, vm::Value) {
            if (!guard || !guard->callbacks_.open) return;
            const auto index = guard->model_->sourceIndexForToken(token);
            if (!index || *index >= static_cast<std::size_t>(guard->files_.size())) return;
            auto callback = guard->callbacks_.open; auto file = guard->files_.at(static_cast<qsizetype>(*index)); callback(std::move(file));
        }, .edit = [guard](std::vector<std::string> tokens) {
            if (!guard || !guard->callbacks_.edit) return;
            QVariantList files;
            for (const auto &token : tokens) {
                const auto index = guard->model_->sourceIndexForToken(token);
                if (!index || *index >= static_cast<std::size_t>(guard->files_.size())) return;
                files.push_back(guard->files_.at(static_cast<qsizetype>(*index)));
            }
            auto callback = guard->callbacks_.edit; callback(std::move(files));
        }, .browse = [guard] {
            if (guard && guard->callbacks_.browse) { auto callback = guard->callbacks_.browse; callback(); }
        }, .selection = [guard](bool mode, std::vector<std::string> keys) {
            if (!guard || *guard->updating_ || !guard->callbacks_.selection) return;
            QStringList result; result.reserve(static_cast<qsizetype>(keys.size()));
            for (const auto &key : keys) result.push_back(QString::fromStdString(key));
            auto callback = guard->callbacks_.selection; callback(mode, std::move(result));
        }});
}
NativeHomeRecentGenerationsSession::~NativeHomeRecentGenerationsSession() {
    callbacks_ = {};
    if (model_ && model_->active()) try { model_->setHandlers({}); } catch (...) {}
}
ii::core::Result<void> NativeHomeRecentGenerationsSession::synchronize(QVariantList files, bool loading, QString focusedKey,
                                                                      bool selectionMode, QStringList selectedKeys) {
    using ii::core::ErrorCode;
    if (thread() != QThread::currentThread()) return ii::core::fail(ErrorCode::wrong_executor, "Recent generations session requires its owner thread");
    const auto updating = updating_;
    if (*updating) return ii::core::fail(ErrorCode::busy, "Recent generations session is updating");
    struct Reset final { std::shared_ptr<bool> value; ~Reset() { *value = false; } } reset{updating}; *updating = true;
    const QPointer<NativeHomeRecentGenerationsSession> guard(this);
    auto model = model_;
    try {
        for (auto &file : files) file = normalized(std::move(file));
        const bool changed = files != files_;
        std::optional<vm::Value> snapshot;
        if (changed || !hasSnapshot_ || loading != loading_) {
            snapshot = nativeValue(files);
            if (snapshot->kind() != ii::core::ValueKind::array || files.size() > 65536 ||
                std::ranges::any_of(vm::array(*snapshot), [](const auto &file) { return file.kind() != ii::core::ValueKind::object; }))
                return ii::core::fail(ErrorCode::invalid_argument, "Recent generations must contain result objects");
        }
        if (changed) {
            if (revision_ == std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range, "Recent preview revision exhausted");
            ++revision_; files_ = std::move(files); previewTokens_.clear(); previewIndices_.clear(); previewCache_.clear();
            previewTokens_.reserve(static_cast<std::size_t>(files_.size())); previewCache_.resize(static_cast<std::size_t>(files_.size()));
            for (qsizetype index = 0; index < files_.size(); ++index) {
                auto token = "recent-generation-preview://" + std::to_string(sessionId_) + "/" + std::to_string(revision_) + "/" + std::to_string(index);
                previewIndices_.emplace(token, static_cast<std::size_t>(index)); previewTokens_.push_back(std::move(token));
            }
        }
        if (snapshot) {
            if (auto result = model->setFiles(std::move(*snapshot), previewTokens_, loading); !result) return result;
            if (!guard) return ii::core::fail(ErrorCode::closed, "Recent generations session was retired");
            hasSnapshot_ = true; loading_ = loading;
        }
        const auto focus = focusedKey.toStdString();
        if (!focus.empty() && focus != model->focusedKey.get() && !model->tokenForKey(focus).empty()) {
            if (auto result = model->setFocusedKey(focus); !result) return result;
        }
        if (!guard) return ii::core::fail(ErrorCode::closed, "Recent generations session was retired");
        std::vector<std::string> keys; keys.reserve(static_cast<std::size_t>(selectedKeys.size()));
        for (const auto &key : selectedKeys) keys.push_back(key.toStdString());
        if (selectionMode != model->selectionMode.get() || keys != model->selectedImageKeys.get()) {
            if (auto result = model->restoreSelection(selectionMode, std::move(keys)); !result) return result;
        }
        if (!guard) return ii::core::fail(ErrorCode::closed, "Recent generations session was retired");
        return {};
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update recent generations session", std::current_exception()); }
}
std::optional<std::string> NativeHomeRecentGenerationsSession::previewAddress(std::string_view token) {
    if (thread() != QThread::currentThread()) return {};
    const auto found = previewIndices_.find(std::string(token));
    if (found == previewIndices_.end() || found->second >= previewCache_.size()) return {};
    const auto index = found->second;
    if (previewCache_[index]) return *previewCache_[index];
    const auto revision = revision_; const QPointer<NativeHomeRecentGenerationsSession> guard(this);
    try {
        auto source = preview_; const auto file = files_.at(static_cast<qsizetype>(index)).toMap();
        auto address = source(file).toEncoded(QUrl::FullyEncoded).toStdString();
        if (!guard || revision_ != revision || index >= previewCache_.size()) return {};
        previewCache_[index] = address; return address;
    } catch (...) { return {}; }
}
NativeHomeRecentGenerationsImages::NativeHomeRecentGenerationsImages(NativeHomeRecentGenerationsSession *session,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> base)
    : session_(session), base_(std::move(base)) {}
std::shared_ptr<const ii::gui::RasterImage>
NativeHomeRecentGenerationsImages::image(std::string_view source, std::uint32_t width, std::uint32_t height) const {
    auto session = session_; auto base = base_;
    if (!session || !base) return {};
    auto address = session->previewAddress(source);
    return address && !address->empty() ? base->image(*address, width, height) : nullptr;
}
} // namespace dreamscapes::home
