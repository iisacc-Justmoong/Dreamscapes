#include "NativeReviewGenerationBridge.h"
#include "../../ViewModels/GenerationProjection.h"
#include "../../ViewModels/ViewData.h"
#include <QThread>
#include <exception>
#include <stdexcept>
#include <utility>

namespace dreamscapes::review {
NativeReviewGenerationBridge::NativeReviewGenerationBridge(GenerationProjection *projection,
    std::shared_ptr<ReviewGalleryModel> model, NativeReviewGenerationActions actions, QObject *parent)
    : QObject(parent), projection_(projection), model_(model), actions_(std::move(actions)) {
    if (!projection || !model)
        throw std::invalid_argument("A generation review bridge requires a projection and a gallery model");
    if (projection->thread() != QThread::currentThread() || thread() != QThread::currentThread())
        throw std::logic_error("A generation review bridge must use the model's owner thread");
    (void)model->session(); // Verify that the native model has the same owner.
    attached_ = true;
    try { synchronize(); } catch (...) { attached_ = false; throw; }
    const QPointer<NativeReviewGenerationBridge> weak(this);
    const auto bind = [weak](Action NativeReviewGenerationActions::*member) {
        return [weak, member](std::vector<vm::Value> values) {
            if (weak && weak->attached_)
                weak->deliver(weak->actions_.*member, values);
        };
    };
    try {
        model->setEditHandler(actions_.edit ? bind(&NativeReviewGenerationActions::edit) : ReviewGalleryModel::Action{});
        model->setCompareHandler(actions_.compare ? bind(&NativeReviewGenerationActions::compare) : ReviewGalleryModel::Action{});
        model->setExportHandler(actions_.exportResults ? bind(&NativeReviewGenerationActions::exportResults) : ReviewGalleryModel::Action{});
        model->setDetailHandler(actions_.detail ? bind(&NativeReviewGenerationActions::detail) : ReviewGalleryModel::Action{});
        connect(projection, &GenerationProjection::reviewResultsChanged, this, [this] { synchronize(); });
        connect(projection, &QObject::destroyed, this, [this] { detach(); });
    } catch (...) {
        try { detach(); } catch (...) {}
        throw;
    }
}

NativeReviewGenerationBridge::~NativeReviewGenerationBridge() {
    try { detach(); } catch (...) { /* A destructor must not escape through Qt. */ }
}

void NativeReviewGenerationBridge::detach() {
    if (!attached_)
        return;
    attached_ = false;
    if (projection_)
        disconnect(projection_, nullptr, this, nullptr);
    projection_.clear();
    originals_.clear();
    if (auto model = model_.lock(); model && model->active()) {
        model->setEditHandler({});
        model->setCompareHandler({});
        model->setExportHandler({});
        model->setDetailHandler({});
    }
    model_.reset();
}

void NativeReviewGenerationBridge::synchronize() {
    const auto model = model_.lock();
    if (!attached_ || !projection_ || !model)
        return;
    if (!model->active()) {
        detach();
        return;
    }
    std::vector<vm::Value> entries;
    const auto rows = projection_->reviewResults();
    entries.reserve(static_cast<std::size_t>(rows.size()));
    QHash<QString, QVariant> next;
    for (const auto &row : rows) {
        auto value = nativeValue(row);
        const auto key = QString::fromStdString(Session::keyForEntry(value));
        if (!key.isEmpty() && !next.contains(key))
            next.insert(key, row);
        entries.push_back(std::move(value));
    }
    auto previous = std::move(originals_);
    originals_ = std::move(next);
    try {
        model->session()->replaceEntries(entries);
    } catch (...) {
        // An observer can throw after the session has already committed. Map
        // only payloads that still exactly match its actual retained entries.
        const auto error = std::current_exception();
        QHash<QString, QVariant> retained;
        try {
            for (const auto &entry : model->session()->entries()) {
                const auto key = QString::fromStdString(Session::keyForEntry(entry));
                auto found = originals_.constFind(key);
                if (found != originals_.cend() && nativeValue(*found) == entry)
                    retained.insert(key, *found);
                else if (auto old = previous.constFind(key); old != previous.cend() && nativeValue(*old) == entry)
                    retained.insert(key, *old);
            }
        } catch (...) { retained.clear(); }
        originals_ = std::move(retained);
        std::rethrow_exception(error);
    }
}

void NativeReviewGenerationBridge::deliver(const Action &action, const std::vector<vm::Value> &entries) {
    if (!attached_ || !action)
        return;
    QVariantList originals;
    originals.reserve(static_cast<qsizetype>(entries.size()));
    for (const auto &entry : entries) {
        const auto it = originals_.constFind(QString::fromStdString(Session::keyForEntry(entry)));
        if (it == originals_.cend() || nativeValue(*it) != entry)
            return; // Never silently execute against a partial or stale selection.
        originals.push_back(*it);
    }
    action(originals);
}
} // namespace dreamscapes::review
