#include "NativeDocumentSession.h"
#include "App/Views/Editor/NativeAudioTrack.h"
#include <limits>
#include <utility>

namespace dreamscapes::native {
ii::core::Result<void> DocumentSession::commitAudioEdit(editor::PreparedAudioEdit prepared) {
    using ii::core::ErrorCode;
    if (auto valid=writable();!valid) return valid;
    if (!document_ || !prepared.source || document_.get()!=prepared.source.get() ||
        documentGeneration.get()!=prepared.generation || revision.get()!=prepared.revision)
        return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the audio edit");
    if (!prepared.changed) return {};
    if (!prepared.candidate) return ii::core::fail(ErrorCode::invalid_argument,"Audio edit has no document");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &value;explicit Guard(bool &flag):value(flag){value=true;}~Guard(){value=false;}} guard(editingStructure_);
    document_=std::move(prepared.candidate);
    (void)frameCount.set(document_->timeline.frameCount);
    if (prepared.generation!=documentGeneration.get()) return ii::core::fail(ErrorCode::cancelled,"The active canvas changed");
    return changed(prepared.generation,false);
}
}
