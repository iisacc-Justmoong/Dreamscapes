# Layer histogram presentation

The Layer panel owns an `EditorLayerHistogram` presentation object. It is active
only while that panel is visible and its Histogram tab is selected. Opening or
closing this view does not edit, save or revise a document. The old synchronous
`EditorCanvas::layerHistogram` entry point is removed. The document owner has no
dependency on its analysis consumer; synchronous native pixel tests can call the
detached calculator without involving a live canvas. QML uses the view-owned
asynchronous object exclusively.

## Work and data ownership

Canvas content/frame/artboard/selected-scope changes invalidate the presentation.
A 60 ms coalescing delay captures the latest request. One worker runs at a time,
with at most one latest pending request. Hiding, rebinding or destroying the
owner retires its intent without joining the renderer. A late result cannot
replace the current view. Each panel has its own owner; closing one panel does
not cancel another panel's request.

The request contains the SDK's shared immutable document snapshot, native frame
and artboard IDs, authored visibility and selected scope. No live canvas,
QObject, file binding or storage service crosses the worker boundary. Native
iiSharedCanvas compositing and all pixel counting run on the worker. Visibility
overrides copy native raster values there, without an additional GUI payload
copy. Initial SDK snapshot capture still has its existing owner-thread cost.

Completed native summaries use a bounded 32-entry cache. Cache keys describe the
document revision, frame, artboard and scope. Cached views are checked before
snapshot capture. Actual content changes clear summaries. RGB/Red/Green/Blue
switching projects the same published result and starts no rendering. Repeated
property reads return the published map and perform no work. Explicit Refresh
retries an optional failure, while an error in the analysis never replaces a
document's editing error.
Repeated frame changes while the same pending state is displayed retire obsolete
work without rebuilding or republishing that identical state to chart bindings.
The gated negative control first observed 19 redundant notifications for 19
frame changes; the regression requires zero after the pending state is published.
Frame identity is needed only when relevant visible content may vary with time:
dynamic/video sources, motion or partial frame ranges. A static full-duration
composition retains its published histogram during navigation. Selected-layer
scope considers that layer, so an unrelated animation starts no extra work.
Hidden panels ignore frame notifications without rebuilding placeholder maps.

Live brush revisions defer capture and analysis until stroke completion or
cancellation. Frame navigation can update a visible histogram, but neither the
histogram nor its completion sends file, project or document-edit notifications.

## Numeric compatibility

The plot retains 256 exact encoded-byte RGB bins and counts each positive-alpha
pixel once. Channel means and population standard deviations are in [0,255].
The RGB summary retains the previous integer gray value `(11R+16G+5B)/32`.
Transparent pixels are excluded. Selected-layer scope retains the native
transform, opacity, mask and artboard region, with the existing background rule.
There is no downsampling or omitted parameter.

## Layer command boundary

Reapplying a group's current visibility is an idempotent metadata check. It
does not copy/serialize a document, create an edit/history entry, notify file
observers or cancel a live gesture. A real visibility change uses the typed
native DocumentEditor result and existing optimistic/durable storage path; an
active brush still rejects a real change. The negative working-file regression
observed revision 1 becoming 2 after reapplying `true`. Its corrected form checks
201 unchanged assignments, byte/version/notification preservation, a live stroke
and one real persisted change.

The SDK's floating-point PhotoOperations histogram uses normalized RGBA and
linear-light Rec709 luminance. Expanding a full packed-ARGB frame to that format
would increase memory and change this established display contract. The view
adapter therefore reduces the native packed renderer result directly in one
pass, using standard C++ data. It does not add a second rendering engine or a
new document format.

## Verification and limits

The negative native fixture used 32 layers and a 2048 by 2048 canvas. The old
QML-exposed synchronous call took 4,534,296 microseconds and did not deliver an
already queued event before returning. This is a method-level reproduction,
excluding canvas creation, first QML initialization and storage.

Regressions cover exact byte bins and alpha, immutable input/event delivery while
a renderer is held, latest-frame bounds, channel/cache reuse, hidden/live-stroke
deferral, owner/source lifetime, stale rebinding, direct terminal deletion,
optional failure retry and artboard/selected scope. The mounted four-state Layer
panel separately checks active ownership, asynchronous completion and read-only
channel changes. Renderer cancellation is cooperative; an already executing SDK
render may finish before its result is discarded. These checks do not establish
application-wide FPS or eliminate initial SDK snapshot capture costs.
