# Native generation review inspector

The LVUI inspector follows the existing Dreamscapes Image page's review
inspector (`1699:6417`): a selection heading, media preview, preview actions
and scrollable result metadata. It uses the existing review session as the
only owner of focus and selection. A field shows only the values supplied by
the result; mixed values and the source job remain distinct. Preview, expand,
save and video playback are host-supplied services, so changing the view must
not alter generated files or initiate a generation job.

Metadata remains selectable. Text too large for the native plain-text editor
uses a document-backed reader instead of being truncated. The latter currently
does not soft-wrap; this is a remaining iiGUI/LVUI limitation, not equivalent
to the old inspector. An injected media view is required for video playback.
The retained macOS Qt-window transitional presenter mounts the LVUI inspector for image
and video results using the existing review session and routes expand and save
through the existing actions. It restores the existing inspector if the video
backend is unavailable or fails, text cannot be wrapped by the native editor,
surface geometry is unsupported, a modal overlays the panel, or a native host
fails. It observes the live wrapping fallback as well as the initial text
length. This describes the retired Qt window's behavior. The simplified native
application now built by default does not connect this presenter or its
underlying services on any platform; see [Native view cutover](NativeViewCutover.md).

`ReviewInspectorModel` exposes the focused job identifier and selection
count independently of the original result. `InspectorViewModel` keeps
stable field identities when focus or selection changes, formats mixed values
without discarding them, and forwards expand and save to injected handlers.
The caller supplies strings, layout bounds, an image resolver and optionally a
native media view. Changing the viewport is presentation state only. A
session or field retired during a notification must not be reactivated.

`Dreamscapes.NativeReviewInspectorView` checks the generated LVUI widget tree
without opening an operating-system window. It exercises focused and mixed
metadata, the original result passed to actions, injected media controls,
drag selection of ordinary text and a 100,000-character document, as well as
size and lifetime handling. This does not verify actual video playback,
platform clipboard behavior or visual parity in the product.
`Dreamscapes.NativeReviewInspectorPresenter` uses offscreen test panels and a
fake host to verify both panel sizes, preview and save signals, modal and
language changes, fallback and asynchronous retirement. The generated UI and
the product's macOS native-surface connection are separate build evidence;
the installed application was not launched or tested. The video tests use an
injected playback port and synthetic frames, not an actual video decoder.

The native video-preview migration keeps decoding and audio playback behind a
replaceable nonvisual backend. The LVUI view owns only the displayed frame,
play/pause/replay controls and elapsed-time presentation. It must stop when
hidden, clear frames when the result changes, retain the original media URL,
and report decoder failures to the existing failure path. The current macOS
transition adapter uses Qt Multimedia as a nonvisual backend; the LVUI view
and its model do not link to Qt or LVRS. Frame conversion runs away from the
UI thread with one active and at most one newest waiting frame. Source changes
discard queued and completed stale frames. Display rotation and mirroring are
applied before the result is fitted to the viewport. A real source frame
may replace the poster only after it arrives; viewport or selection changes do
not create or modify a video file. Backend and frame callbacks are accepted on
the owning UI thread and stale results from a previous source are discarded.
The image service exposes loading, ready and failed state separately from the
immutable pixels. Until a preview is ready, the native view displays the same
loading or unavailable text as the previous inspector; an absent result is
distinct from an image that failed to load. A resolver without a status port
is supported without guessing a failure.
