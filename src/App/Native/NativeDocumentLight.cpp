#include "NativeDocumentSession.h"
#include "App/Views/Editor/NativeLightAdjustment.h"
#include <limits>
#include <utility>

namespace dreamscapes::native {
ii::core::Result<void> DocumentSession::commitLightAdjustment(editor::PreparedLightAdjustment prepared) {
    using ii::core::ErrorCode;
    if (auto valid=writable();!valid) return valid;
    if (!document_ || !prepared.source || !prepared.candidate || document_.get()!=prepared.source.get() ||
        documentGeneration.get()!=prepared.generation || revision.get()!=prepared.revision ||
        !hasCurrentLayerSelection() || selectedLayer.get()!=prepared.layerId || frame.get()!=prepared.frame)
        return ii::core::fail(ErrorCode::cancelled,"The canvas, layer or frame changed while preparing the adjustment");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() || visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &value;explicit Guard(bool &flag):value(flag){value=true;}~Guard(){value=false;}} guard(editingStructure_);
    try {
        document_=std::move(prepared.candidate);
        if (prepared.generation!=documentGeneration.get()) return ii::core::fail(ErrorCode::cancelled,"The active canvas changed");
        return changed(prepared.generation);
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot commit light adjustment",std::current_exception());}
}
}
