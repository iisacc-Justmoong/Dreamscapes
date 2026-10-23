# Photo library export ownership

`PhotoLibraryExporter::save` accepts one local image source without opening a
Society drive or rebuilding a result gallery. The result view uses the published
`documentSource` for native IISC commands while keeping its asynchronous preview
URI for display. Ordinary result images retain their original local source.
The chosen command URL is captured when the command starts; file dialogs retain
that URL even if the result selection changes.

`false` means the request was not accepted and emits `rejected(message)`;
`lastRejection()` provides the same reason to C++ callers. Unsupported, nonlocal
and busy starts are rejected immediately. Rejection does not finish or fail an
accepted save. `true` accepts work with `saved(assetIdentifier)` or
`failed(message)` as its eventual outcome. Missing, unreadable or malformed local
files are preparation failures after acceptance, keeping their I/O off the GUI.

## Preparation and native dispatch

The value-owned PrepareOperation runs on a worker before native dispatch. It
returns Image `{path, mimeType}`, an error and an optional shared resource lease.
Ordinary images keep their original path and bytes; their readable header and
content MIME are inspected on the worker. Native IISC uses the frozen
`iiSocietyHelper::exportMedia` adapter: detached read-only snapshot, frame zero,
complete document-view extent, PNG alpha and available PNG text. Existing media
limits reject unsupported sizes instead of producing a reduced preview.

A native canvas is prepared in a private provider-owned temporary directory.
The GUI dispatch and native request retain its resource lease until the first
native reply. A separate local lease keeps the file alive until dispatch returns,
even if synchronous completion reenters Qt, destroys the view or starts another
save. First-reply cleanup releases the request's lease even when native code
retains obsolete callback copies. Failures and owner deletion have the same
cleanup contract. The source document and its editable data remain unchanged.

Native SaveOperation receives only the prepared Image payload on the application
GUI owner, independent of a view-owned watcher. The platform adapters do not
inspect the source to derive MIME on the GUI thread. Apple retains its add-only
PhotoKit authorization and file-resource import. Android retains its Activity
permission dispatch and single-threaded MediaStore copy/publication worker; it
now receives the prepared content MIME. Unsupported platforms expose
`supported = false`. OS permission and publication policy is unchanged.

The operation, preparer and input are copied before publishing busy state.
Closing the view during that notification or during preparation cannot destroy
accepted work. Only feedback uses a weak reference to the view owner. Native
replies can arrive from any thread: an atomic per-request gate accepts one outcome
and queues GUI delivery. Duplicate/obsolete replies cannot finish a later request.
Preparation/native exceptions become failures; exceptions after a reply cannot
replace its first outcome. Application process termination is outside the
view-lifetime guarantee.

Completion clears busy state and delivers saved/failed before publishing idle.
Terminal observers can start another save or destroy the owner. Only a
still-current owner receives final idle notification. File and editor frame
exporters use the same terminal-before-idle ordering. ImageFileExporter separates
rejected starts from an accepted job's eventual failure as well.

## Verification

`Dreamscapes.PhotoLibrary` checks original bytes/resolution/text, content MIME,
worker preparation, live GUI input during gated work, acceptance followed by
missing-file failure, native full-size IISC preparation, lease retention/cleanup,
view destruction before dispatch, synchronous reentrant completion, retries,
duplicate/stale/concurrent replies and exceptions. The result-view fixtures use
production `GenerationController::storedImage`, an actual native snapshot/working
file and the real asynchronous image provider. They verify original command
identity, save-dialog selection capture, full-size file output and an injected
Photos operation while preserving source bytes. File/frame exporter tests also
verify delivery before an idle observer deletes the owner and reentrant next jobs.

These fixtures do not add assets to the user's real Photos library or establish
physical Android/iOS permission and import behavior. Native-platform execution
and installed-bundle startup are recorded separately from source and SDK tests.
