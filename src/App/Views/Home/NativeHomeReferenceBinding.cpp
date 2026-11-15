#include "NativeHomeReferenceBinding.h"
#include "App/Generation/GenerationComposerModel.h"
#include "App/Generation/HomeReferenceStore.h"
#include "App/Generation/NativeHomeReferenceInputs.h"
#include <QThread>
#include <stdexcept>
#include <utility>

namespace dreamscapes::home {
NativeHomeReferenceBinding::NativeHomeReferenceBinding(NativeHomeReferenceInputs *inputs, GenerationComposerModel *draft,
        NativeHomeComposerSession *session, Detail detail, QObject *parent)
    : QObject(parent), inputs_(inputs), draft_(draft), session_(session), detail_(std::move(detail)) {
    if (!inputs || !draft || !session || !detail_ || inputs->thread() != QThread::currentThread() ||
        draft->thread() != QThread::currentThread() || session->thread() != QThread::currentThread() ||
        thread() != QThread::currentThread() || !session->isFor(draft) || draft->canvas() != inputs)
        throw std::invalid_argument("Home references require a matching draft, provider and composer session on their owner thread");
    connect(inputs, &NativeHomeReferenceInputs::attachmentsChanged, this, [this] { requestSynchronize(); });
    connect(inputs, &NativeHomeReferenceInputs::inputErrorChanged, this, [this] { requestSynchronize(); });
    connect(inputs, &QObject::destroyed, this, [this] { inputs_ = nullptr; requestSynchronize(); });
    connect(draft, &GenerationComposerModel::bindingsChanged, this, [this] { requestSynchronize(); });
    connect(draft, &QObject::destroyed, this, [this] { draft_ = nullptr; requestSynchronize(); });
    connect(session, &QObject::destroyed, this, [this] { session_ = nullptr; });
    auto initial = synchronize();
    if (!initial) throw std::invalid_argument(initial.error().message);
}
NativeHomeReferenceBinding::~NativeHomeReferenceBinding() {
    if (inputs_) disconnect(inputs_, nullptr, this, nullptr);
    if (draft_) disconnect(draft_, nullptr, this, nullptr);
    if (session_) disconnect(session_, nullptr, this, nullptr);
    if (session_ && session_->thread() == QThread::currentThread()) try { (void)session_->setInputState({}, {}); } catch (...) {}
}
void NativeHomeReferenceBinding::requestSynchronize() {
    const QPointer<NativeHomeReferenceBinding> guard(this);
    auto result = synchronize();
    if (guard) lastError_ = result ? QString{} : QString::fromStdString(result.error().message);
}
ii::core::Result<void> NativeHomeReferenceBinding::synchronize() {
    using ii::core::ErrorCode;
    if (thread() != QThread::currentThread()) return ii::core::fail(ErrorCode::wrong_executor, "Home references require their owner thread");
    if (!session_) return ii::core::fail(ErrorCode::closed, "Home composer session is unavailable");
    const auto update = update_;
    if (update->updating) { update->pending = true; return {}; }
    struct Reset final { std::shared_ptr<UpdateState> value; ~Reset() { value->updating = false; } } reset{update};
    update->updating = true;
    const QPointer<NativeHomeReferenceBinding> guard(this);
    try {
        for (unsigned round = 0; round < 64; ++round) {
            update->pending = false;
            std::vector<HomeComposerAttachment> entries;
            std::string inputError;
            if (inputs_ && draft_ && session_->isFor(draft_) && draft_->canvas() == inputs_) {
                inputError = inputs_->inputError().toStdString();
                const auto source = inputs_->attachments(); entries.reserve(static_cast<std::size_t>(source.size()));
                auto formatter = detail_;
                for (const auto &value : source) {
                    const auto item = value.toMap();
                    entries.push_back({HomeReferenceStore::key(item).toStdString(), item.value("filename").toString().toStdString(),
                        item.value("previewSource").toUrl().toEncoded(QUrl::FullyEncoded).toStdString(), formatter(item.value("sizeBytes").toLongLong())});
                    if (!guard) return ii::core::fail(ErrorCode::closed, "Home reference binding was retired");
                }
            }
            auto session = session_;
            if (!session) return ii::core::fail(ErrorCode::closed, "Home composer session is unavailable");
            auto result = session->setInputState(std::move(entries), std::move(inputError));
            if (!guard) return ii::core::fail(ErrorCode::closed, "Home reference binding was retired");
            if (!result) return result;
            if (!update->pending) return {};
        }
        return ii::core::fail(ErrorCode::busy, "Home references did not settle");
    } catch (...) { return ii::core::fail(ErrorCode::failed, "Cannot project Home references", std::current_exception()); }
}
} // namespace dreamscapes::home
