#include "NativeDocumentSession.h"
#include "App/Views/Editor/NativeBackgroundColor.h"
#include <limits>
#include <utility>

namespace dreamscapes::native {
ii::core::Result<void> DocumentSession::commitBackgroundFill(editor::PreparedBackgroundFill prepared) {
    using ii::core::ErrorCode;
    if (auto valid=writable();!valid) return valid;
    if (!document_ || !prepared.source || !prepared.candidate || document_.get()!=prepared.source.get() ||
        documentGeneration.get()!=prepared.generation || revision.get()!=prepared.revision)
        return ii::core::fail(ErrorCode::cancelled,"The canvas changed while preparing the background");
    if (editingStructure_) return ii::core::fail(ErrorCode::busy,"Another canvas operation is in progress");
    if (revision.get()==std::numeric_limits<std::uint64_t>::max() || visualRevision.get()==std::numeric_limits<std::uint64_t>::max())
        return ii::core::fail(ErrorCode::out_of_range,"Document revision is exhausted");
    struct Guard {bool &value;explicit Guard(bool &flag):value(flag){value=true;}~Guard(){value=false;}} guard(editingStructure_);
    try {
        auto rows=layers.get();
        if (prepared.inserted) {
            auto row=std::make_shared<DocumentLayerRow>();row->id="canvas.background.layer";row->owner=shared_from_this();
            (void)row->name.set("Background");(void)row->visible.set(true);
            rows.insert(rows.begin(),std::move(row));
        }
        document_=std::move(prepared.candidate);
        if (prepared.inserted) (void)layers.set(std::move(rows));
        if (prepared.generation!=documentGeneration.get()) return ii::core::fail(ErrorCode::cancelled,"The active canvas changed");
        return changed(prepared.generation);
    } catch (...) {return ii::core::fail(ErrorCode::failed,"Cannot commit background fill",std::current_exception());}
}
}
