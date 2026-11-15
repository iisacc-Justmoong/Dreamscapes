#include "NativeHomeRecentFilesSession.h"
#include "CanvasPreviewAddress.h"
#include "App/ViewModels/ViewData.h"
#include <QJSValue>
#include <QThread>
#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <utility>

namespace dreamscapes::home {
namespace {
std::atomic_uint64_t nextSession{0};
constexpr std::array<const char *, 5> filterKeys{"All types", "Canvas", "Image", "Video", "Document"};
QVariant normalized(QVariant value) {
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}
std::string filterKey(const QString &value, const HomeRecentFilesStrings &strings) {
    if (value.isEmpty()) return filterKeys[0];
    for (std::size_t index = 0; index < filterKeys.size(); ++index)
        if (value == QString::fromUtf8(filterKeys[index]) || value == QString::fromStdString(strings.filters[index])) return filterKeys[index];
    return {};
}
}
NativeHomeRecentFilesSession::NativeHomeRecentFilesSession(HomeRecentFilesStrings strings, Callbacks callbacks,
                                                           PreviewSource preview, QObject *parent)
    : QObject(parent), model_(HomeRecentFilesModel::create(strings)), strings_(std::move(strings)),
      callbacks_(std::move(callbacks)), preview_(preview ? std::move(preview) : PreviewSource(canvasPreviewSource)),
      sessionId_(++nextSession) {
    const QPointer<NativeHomeRecentFilesSession> guard(this);
    model_->setHandlers({.select = [guard](std::string key) {
            if (guard && !*guard->updating_ && guard->callbacks_.select) {
                auto callback = guard->callbacks_.select; callback(QString::fromStdString(key));
            }
        }, .open = [guard](std::string token, vm::Value) {
            if (!guard || !guard->callbacks_.open) return;
            const auto index = guard->model_->sourceIndexForToken(token);
            if (!index || *index >= static_cast<std::size_t>(guard->files_.size())) return;
            auto callback = guard->callbacks_.open; auto file = guard->files_.at(static_cast<qsizetype>(*index)); callback(std::move(file));
        }, .browse = [guard] {
            if (guard && guard->callbacks_.browse) { auto callback = guard->callbacks_.browse; callback(); }
        }, .filter = [guard](std::string key) {
            if (guard && !*guard->updating_ && guard->callbacks_.filter) {
                auto callback = guard->callbacks_.filter; callback(QString::fromStdString(key));
            }
        }});
}
NativeHomeRecentFilesSession::~NativeHomeRecentFilesSession() {
    callbacks_ = {};
    if (model_ && model_->active()) try { model_->setHandlers({}); } catch (...) {}
}
ii::core::Result<void> NativeHomeRecentFilesSession::synchronize(QVariantList files, bool loading, QString selectedKey, QString filter) {
    using ii::core::ErrorCode;
    if (thread() != QThread::currentThread()) return ii::core::fail(ErrorCode::wrong_executor, "Recent files session requires its owner thread");
    const auto updating = updating_;
    if (*updating) return ii::core::fail(ErrorCode::busy, "Recent files session is updating");
    struct Reset final { std::shared_ptr<bool> value; ~Reset() { *value = false; } } reset{updating}; *updating = true;
    const QPointer<NativeHomeRecentFilesSession> guard(this);
    auto model = model_;
    try {
        for (auto &file : files) file = normalized(std::move(file));
        const bool changed = files != files_;
        std::optional<vm::Value> snapshot;
        if (changed || !hasSnapshot_ || loading != loading_) {
            snapshot = nativeValue(files);
            if (snapshot->kind() != ii::core::ValueKind::array || files.size() > 65536 ||
                std::ranges::any_of(vm::array(*snapshot), [](const auto &file) { return file.kind() != ii::core::ValueKind::object; }))
                return ii::core::fail(ErrorCode::invalid_argument, "Recent files must contain file objects");
        }
        if (changed) {
            if (revision_ == std::numeric_limits<std::uint64_t>::max()) return ii::core::fail(ErrorCode::out_of_range, "Recent preview revision exhausted");
            ++revision_;
            files_ = std::move(files); previewTokens_.clear(); previewIndices_.clear(); previewCache_.clear();
            previewTokens_.reserve(static_cast<std::size_t>(files_.size())); previewCache_.resize(static_cast<std::size_t>(files_.size()));
            for (qsizetype index = 0; index < files_.size(); ++index) {
                auto token = "recent-preview://" + std::to_string(sessionId_) + "/" + std::to_string(revision_) + "/" + std::to_string(index);
                previewIndices_.emplace(token, static_cast<std::size_t>(index)); previewTokens_.push_back(std::move(token));
            }
        }
        if (snapshot) {
            if (auto result = model->setFiles(std::move(*snapshot), previewTokens_, loading); !result) return result;
            if (!guard) return ii::core::fail(ErrorCode::closed, "Recent files session was retired");
            hasSnapshot_ = true; loading_ = loading;
        }
        if (!guard) return ii::core::fail(ErrorCode::closed, "Recent files session was retired");
        const auto selected = selectedKey.toStdString();
        if (!selected.empty() && selected != model->selectedKey.get() && !model->tokenForKey(selected).empty()) {
            if (auto result = model->setSelectedKey(selected); !result) return result;
        }
        if (!guard) return ii::core::fail(ErrorCode::closed, "Recent files session was retired");
        const auto key = filterKey(filter, strings_);
        if (!key.empty() && key != model->filter.get()) {
            if (auto result = model->setFilter(key); !result) return result;
        }
        if (!guard) return ii::core::fail(ErrorCode::closed, "Recent files session was retired");
        const auto actual = QString::fromStdString(model->selectedKey.get());
        auto callback = callbacks_.select;
        *updating = false;
        if (actual != selectedKey && callback) callback(actual);
        return {};
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot update recent files session", std::current_exception()); }
}
std::optional<std::string> NativeHomeRecentFilesSession::previewAddress(std::string_view token) {
    if (thread() != QThread::currentThread()) return {};
    const auto found = previewIndices_.find(std::string(token));
    if (found == previewIndices_.end() || found->second >= previewCache_.size()) return {};
    const auto index = found->second;
    if (previewCache_[index]) return *previewCache_[index];
    const auto revision = revision_;
    const QPointer<NativeHomeRecentFilesSession> guard(this);
    try {
        auto source = preview_; const auto file = files_.at(static_cast<qsizetype>(index)).toMap();
        auto address = source(file).toEncoded(QUrl::FullyEncoded).toStdString();
        if (!guard || revision_ != revision || index >= previewCache_.size()) return {};
        previewCache_[index] = address; return address;
    } catch (...) { return {}; }
}
NativeHomeRecentFilesImages::NativeHomeRecentFilesImages(NativeHomeRecentFilesSession *session,
    std::shared_ptr<const lvui::native::ImageResolver<ii::gui::RasterImage>> base)
    : session_(session), base_(std::move(base)) {}
std::shared_ptr<const ii::gui::RasterImage>
NativeHomeRecentFilesImages::image(std::string_view source, std::uint32_t width, std::uint32_t height) const {
    auto session = session_;
    auto base = base_;
    if (!session || !base) return {};
    auto address = session->previewAddress(source);
    return address && !address->empty() ? base->image(*address, width, height) : nullptr;
}
} // namespace dreamscapes::home
