# Society container state and application Aliases

Dreamscapes requires iiSocietyContainer 0.16.0 and iiSocietyHelper 0.8.0. Its
GenerationController owns a Helper, which owns the FileSystem bridge and its
read-only `iiSocietyContainer::Container` object. The application exposes this
object as `generation.container` and `Main.societyContainer`. Home recent files,
Published and Generation History use its Alias snapshots, including search.
Dreamscapes does not construct an independent dashboard filesystem scanner.

An Alias identifies a container and logical file key. It supplies existence,
local availability, the source address, version and preview metadata. Existence
checks for published results and stored image previews go through Helper. Native
canvas previews are rendered and cached by Helper, with no application preview
file writer. Replica metadata remains usable when the payload is not downloaded.

Generation produces transient engine output and asks Helper to publish the PNG in
Generation History. Using an image in the editor or a generation input requests
a native document in Files. A frame or input preview is received from Helper.
Application worker files and in-memory image encoders are transient generation
resources; they do not publish documents or image originals into public sections.

The editor holds an in-memory iiSharedCanvas Document and a file Alias. It has no
native writable DocumentFile binding. Save, Save As, completed strokes, document
edits and undo/redo send a document snapshot and the expected version to Helper.
Helper owns the native transaction. A conflicting external revision is reported
without overwriting the stored document. Unsynchronized edits remain visible as
modified. File Alias availability is updated from Helper's container snapshot.
Helper availability changes also notify GenerationController's connection and
container-path properties, including while generation is active.
Alias content updates notify the container mirror without emitting an unrelated
connection-change signal or resetting the current result gallery selection.
Raster exports, original-media downloads and native exports also use Helper.

The previous PNG/Files contract remains: generated `.png` originals belong to
Generation History, while automatically created `.iisc` documents belong to
Files. Explicit external native open/save/export operations are supported even
without a selected Society container. App storage selection, model
resolution, download request state, input cleanup and legacy migration use the
same bridge. There is no application-owned SharedStorage or StorageMap writer.

Verification includes Container Alias snapshots without disk probes, Helper
publication/deletion and native version conflicts, Dreamscapes PNG publication
and editor conversion, native document save/reopen/edit/undo, image export and
the Home/history/editor GUI routes. Native macOS installation and startup are
verified separately from source tests.
