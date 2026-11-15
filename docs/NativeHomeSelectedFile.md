# Home selected-file view migration

The LVUI selected-file view follows Dreamscapes Home Figma node `1467:5053`.
It is paired with the recent-file selection, uses the existing LVUI label,
button, image and layout tokens, and does not paint a separate panel background.
The image is supplied by the application's asynchronous image service; the
Figma example is not product data.

The Home layout supplies the panel's outside spacing. The selected-file view
fills that content rectangle and must not apply the same inset a second time.

The Qt-free selected-file model receives the current file and preview address.
It projects the existing file identity, availability, path, dimensions,
structure, color and modification state without modifying the source document
or triggering a storage scan. An absent value is shown as `Unavailable`; an
unavailable Society file offers `Download and open`. Opening is an injected
command, and no command is available without a file and an active handler.
Replacing the file immediately clears details from the previous file.
Asynchronous metadata replies are admitted only for the exact file snapshot
that initiated them, so a late result cannot contaminate a later selection.

The existing Home file-details reader and canvas-preview address logic remain
the authoritative product adapters during the transition. They deliver native
model inputs and the original file object, including opaque Society fields, to
the existing `WorkspaceSession` open/download path. Their lifetime is separate
from the LVUI view; no QML engine or QQuick item is needed by the native model
or its view. The original QML Home still exists on platforms whose full native
window has not yet replaced it.

On macOS the transition presenter connects this LVUI content to the existing
Home selection and open signal. It preserves the complete Society file value,
including remote availability and opaque fields. While another surface covers
the panel, the native surface is unavailable, or the panel is clipped, the
existing Home content remains available. Language changes reconstruct the
surface with the current selection; shutdown waits for the preview worker to
retire. The full Home and application window have not yet migrated.

Tests must cover an empty selection, local and remote files, metadata arrival
and stale replies, handler retirement, navigation with the original file,
and LVUI widget updates with an injected image resolver. Real file previews
and the existing canvas preview path are validated separately; synthetic UI
fixtures must not be described as real Society or model generation evidence.
