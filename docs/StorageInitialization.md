# Storage initialization and view ownership

An EditorCanvas is a document/view owner. Constructing it, creating an unsaved
canvas, changing its camera/frame or editing an unbound document must not discover
a Society drive, walk its public sections or start a storage mirror/monitor.

## SDK boundary

The iiSocietyHelper default FileSystem constructor still discovers the configured
drive. Existing explicit storage owners retain that contract. View/data-only
owners use `FileSystem::Initialization::Deferred`; this creates only the adapter
and leaves selection, mirror timer and storage-monitor workers inactive.
`open`, `openForSource` and storage commands retain their normal selection and
persistence behavior. `readCanvas` is a detached read and needs no selection.
The existing canvas-presentation registration still runs when the adapter is
created; deferred selection is not a claim that every first-use initializer is
free of cost.

The SDK also supplies immutable `PreparedStorage` values. An I/O owner captures an
already opened selection/mirror with `preparedStorage`; another owner can call
`adoptStorage` without repeating discovery, directory scans or payload reads.
Adoption checks the pinned drive identity, publishes captured metadata, invalidates
older mirror replies and starts the receiver's ordinary monitor. Writable file
bindings, QObject owners, timers, workers and credentials are not transferred.
The asynchronous [working-file open command](WorkingFileOpen.md) uses this value
to adopt its worker's selection without repeating the mirror scan on the GUI
owner. Writable binding and final native validation remain owner operations.

## Dreamscapes adoption

EditorCanvas owns its deferred FileSystem as the named QObject child
`editorDocumentStorage`. Unbound editing and view commands leave that adapter
unselected. An explicit open/save selects its appropriate drive. Existing Alias,
expected-version conflict checks, validated edits and synchronous durable writes
remain in the existing file owner.
The value-owned QObject is parented after all canvas members are initialized and
detached in the destructor body. Child lifecycle events must not evaluate tool
state while those members are being constructed/destroyed. The canvas event
override evaluates derived interaction state only for keyboard, focus,
touch/tablet and shortcut events; other events pass directly to CanvasItem.

Detached image reads and the original-image preparation worker use a deferred
adapter. They no longer discover an unrelated default drive before reading a
source. Bitmap wrapper creation, native preview generation and explicit image
storage select their source/target drive once through their existing commands.
Home attachment and explicit generation runtime/resource-directory helpers use
the same deferred selection, avoiding an unrelated default-drive scan before
opening their requested drive. The long-lived GenerationController/Helper
storage owner keeps discovery because it actually supplies the application mirror.

## Verification and scope

The SDK tests cover deferred versus default discovery, cross-thread value
transfer after source destruction, captured-mirror publication without rescanning
and rejection of replaced drives/default values. Its complete registered suite
and a standalone installed-package consumer verify the new interface, durable
save/reopen and stale-version rejection. Bootstrap tests explicitly distinguish
BLE disablement from invalid-endpoint unavailability; neither starts radio or
requests permissions.

The native canvas regression checks that construction, unsaved creation,
frame/zoom/pan and an unbound edit emit zero storage activation events. Explicit
save then activates the correct drive, and a later edit is durable when reopened.

The constructor fixture contains 2000 ordinary files and records three samples.
The original installed-private SDK baseline and the refreshed-SDK baseline are
kept separately so SDK replacement is not attributed to the consumer change.
This fixture excludes QML editor initialization, total file-open latency and FPS.
The corrected-source replay, with unchanged Helper/SharedCanvasQuick/LVRS
libraries, measured warm samples of 139639/138305 microseconds before and
312/351 microseconds after (two-sample means of 138972 and 331.5 microseconds).
First samples are retained separately. Their variation across runs does not
establish that cold application/editor startup is resolved.
Working-file decode/alias hashing now run in the open worker. Initial
render-snapshot copying, first-use media/device initialization and durable native
commits remain audit frontiers. No foreground input automation or
installed-application replacement is implied by these component/build results.
