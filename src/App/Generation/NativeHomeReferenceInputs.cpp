#include "NativeHomeReferenceInputs.h"
#include "ImageParameterCodec.h"
#include <Generation/ImageParameters.hpp>
#include <QCoreApplication>
#include <QPointer>
#include <utility>

namespace dreamscapes {
void NativeHomeReferenceInputs::setError(QString value) {
    if (error_ == value) return;
    error_ = std::move(value); emit inputErrorChanged();
}
bool NativeHomeReferenceInputs::addAttachment(const QUrl &source) {
    const auto previous = store_.attachments().size();
    const bool result = store_.add(source);
    const bool changed = previous != store_.attachments().size();
    const QPointer<NativeHomeReferenceInputs> guard(this);
    if (changed) emit attachmentsChanged();
    if (guard) setError(store_.error());
    return result;
}
bool NativeHomeReferenceInputs::removeAttachment(const QString &key) {
    const bool result = store_.removeKey(key);
    const QPointer<NativeHomeReferenceInputs> guard(this);
    if (result) emit attachmentsChanged();
    if (guard) setError(store_.error());
    return result;
}
QVariantMap NativeHomeReferenceInputs::generationParameters(const QString &prompt, const QString &model,
                                                              const QString &ratio, int count) {
    const auto size = homeReferenceOutputSize(ratio);
    if (size.isEmpty()) { setError(QCoreApplication::translate("HomeCanvas", "Unsupported canvas ratio.")); return {}; }
    auto result = imageParametersToMap(iiLocalDiffusion::ImageParameters::defaults());
    result["prompt"] = prompt; result["model"] = model; result["width"] = size.width(); result["height"] = size.height();
    result["outputCount"] = count; result["referenceImages"] = store_.previewSources();
    setError({}); return result;
}
} // namespace dreamscapes
