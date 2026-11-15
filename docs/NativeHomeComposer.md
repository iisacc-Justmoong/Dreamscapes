# Native Home generation composer contract

The native Home composer is based on the existing Dreamscapes Home Figma
composer `1467:4971` and `DesktopGenerationComposer.qml`. It must retain the
prompt and negative prompt, Image/Video/Audio/Board navigation, reference
attachments and paint canvas, model selection, aspect ratio, resolution,
output count, seed, preset load and save, advanced parameters, video duration
and frame rate, generation submission, validation and errors. The actual
options, model IDs, file references, values and localized labels come from
application services, not from Figma examples.

The native presentation model is a snapshot and command boundary. It does not
edit files, select a model on its own, start generation during a view update,
or replace the authoritative generation draft. The product controller supplies
the current snapshot and handles typed commands; callbacks may synchronously
publish an updated snapshot. A command referencing a removed menu option or
attachment must be rejected instead of acting on a different item after a
reorder. Submitting while generation submission is busy is rejected; empty
prompt behavior remains the existing controller's responsibility. Closing the
view stops commands without closing or mutating the generation job.

`HomeComposerModel` implements this boundary without Qt. The transitional
`NativeHomeComposerSession` observes the existing `GenerationComposerModel`
and parameter store without a QML engine. It routes edits, model selection,
presets and submission through those controllers and receives attachment,
paint and advanced-panel commands from its host. It supplies no invented
models or choice lists. A controller or session that retires during a command
or snapshot update cannot leave an enabled command behind.
Model and preset choices are refreshed on their respective controller
notifications rather than being fetched again for each prompt keystroke.

The model and session tests cover stable choice and attachment IDs, draft
updates, video controls, invalid seed, a temporary preset round trip, busy
submission, the existing submission signal and retirement. They do not run
inference or alter the user's files. An isolated Society fixture with two
small dummy files verifies that the existing model inventory, model selection,
and image/video reclassification reach the native snapshot. The tests do not
yet exercise the complete native composer or its connection to the running app.

This is a migration step. The current composer and its paint canvas remain
QML/Qt views until the complete LVUI composer, native attachment and canvas
interfaces, dialogs, shortcuts and generation/navigation path are connected
and tested. A snapshot model and session alone do not provide the UI.

The first composer subview is the plain-text prompt field. It reads the shared
draft, forwards only user edits, exposes an accessible clear action for a
nonempty prompt and reports its measured height to the containing layout. It
must preserve UTF-8 and line breaks, and external draft refreshes must not
submit a job. The prompt's Ctrl/Command-Return submits via the existing draft
controller; ordinary Return edits a new line and the negative-prompt field does
not submit. Headless widget and controller-integration tests exercise these
behaviors and verify retirement disables the field. The LVUI text editor
currently has a documented 64 KiB input limit; that limit and real-platform
input methods must be resolved or otherwise preserved before this field
replaces the application's current prompt.

Model, media, aspect ratio, resolution, count, duration, frame rate and preset
menus share one LVUI choice view. The host supplies localized labels and menu
options; the view projects the authoritative selection and dispatches a stable
option key. A popup action whose option was removed or disabled before the
click must be rejected. Menu interaction must not load a model or begin
generation except through the existing controller's explicit selection path.
The Qt-free model and LVUI/iiGUI menu are implemented. Headless tests cover
keyed menu reordering, disabled and removed options, responsive controller
updates and the existing image/video model selection using an isolated Society
fixture. The complete composer layout and platform popup behavior remain to be
connected and verified. LVUI's current keyed menu has a 4,096-item limit and
does not virtualize; inventories larger than that need an accessible search or
paging path before the native composer can replace the existing one.

The Seed setting is an editable draft, separate from the applied generation
seed. Opening the editor copies the current seed, using an empty field for
Random. Editing or canceling it must not modify the generation request.
Apply and Random use the existing controller commands, close the editor only
after success, and leave the draft intact if validation fails. A later
external seed update must be shown on the setting and on the next opening;
it must not overwrite a draft while the user is editing it.
The LVUI seed control and Qt-free model implement that contract. Headless
tests cover the native popover, rejected input and Random, as well as the
existing generation controller's validation and seed state through the
transitional session. The popover's sizing and platform input still need
verification when the whole composer is connected to a native window.

Saving a preset uses a separate editable name. The Save action is disabled
for a blank name. Cancel closes the popover without changing the draft or the
stored presets; a successful save uses the existing parameter store, clears
the draft and closes. A rejected save preserves the draft and keeps the editor
open so that the user can correct the problem.
The LVUI popup and native view model implement that behavior. Widget tests
cover retry and cancel, and a controller integration test saves to an
isolated temporary preset store while checking an overlong-name rejection.
No user preset is modified by those tests. Platform popup sizing remains to
be checked with the containing native composer.

Reference cards reflect the generation canvas's current attachment snapshot,
using each attachment's stable key rather than a row index for removal. Their
image preview, file name, size description and accessible remove label are
provided by the host. Updating or reordering the snapshot must not remove an
attachment or cause a stale remove action to target a different file. The
horizontal list and native image resolver belong to the view; the generation
canvas and native file picker remain the host's responsibility.
The LVUI card list and C++ model are implemented and tested with keyed
reordering, stale actions, horizontal scrolling and a supplied image resolver.
The remove action is currently always visible, while the existing QML card
reveals it on hover or focus; that visual difference remains to be reconciled.
The session integration test uses injected attachment data and callbacks; it
does not claim that the native list is connected to the actual Home canvas or
that image decoding and platform rendering were verified.

Attach, Paint canvas, Advanced parameters and Generate use the existing
composer commands. Generate's label reflects the current media type and
output count, and is disabled while submission or generation is pending.
Neither layout nor a controller state refresh may start a generation. The
attachment picker, painting surface and advanced settings are provided by the
host and are not replaced by placeholder actions.
The shared action view uses LVUI button variants and a Qt-free action model.
Its widget tests exercise all four commands and the pending state. A
controller integration test verifies the existing submission signal and
host callbacks without starting an inference job or opening a file picker.

The negative-prompt button opens the same native plain-text editing boundary
with its own draft and Done action. Closing the popover preserves the edited
negative prompt and does not submit a generation. The popover must remain
within the window on narrow layouts.
The native popup is composed from the shared prompt editor and tested at a
narrow viewport; a controller integration test confirms that it only edits
the negative prompt and never submits a generation.

The complete composer uses two responsive control grids. At the existing
desktop breakpoint the first grid shows its six controls on one row and the
settings grid shows seven columns. Smaller widths use the existing one/two
and two/three-column arrangements. Video adds duration and frame-rate
settings; preset and advanced controls span two narrow columns. Moving between
layouts must retain the mounted editors, popovers and their local state. The
layout receives breakpoints and preferred widths from the composition boundary.
The LVUI view supplies spacing, padding, control height, caption line height
and attachment height from the shared LVUI metrics; it does not change
generation or file state.
The responsive grid and LVUI parent now mount supplied controls without
rebuilding them. Headless tests exercise wide and narrow layouts, prompt focus,
and visibility of video-only settings. Some tests still use fixture controls;
the running Home connection is not complete. A source factory assembles the
actual LVUI controls and checks that they belong to one generation draft and
are assigned to their matching roles. The panel renders the controller error
as wrapped LVUI text and measures its height; the current iiGUI label limit of
64 KiB remains a limitation for an unusually long error message.

A construction boundary receives translated labels, formatting callbacks,
layout requirements and the application's image resolver. It creates the
native control models and a reusable panel source for the Home workspace.
It does not create a generation controller, start work, open dialogs, or own
the session's lifetime. Closing the panel must not cancel a job or alter the
generation draft. A headless integration test constructs this panel with the
existing generation draft and a temporary preset store, then checks input,
submission signals, error display and busy-state handling without inference.

The Preset setting contains the existing load menu and Save action in one row.
Its heading and actions follow the existing Figma Home composer; the Save
button occupies the design's 52-point width and the load control fills the
remaining row.

The existing generation draft obtains reference-image and painted-canvas
inputs through a replaceable backend interface rather than depending on a
Qt Quick view type. The adapter must preserve the original input parameters
and errors, reject objects without the interface, and stop using an input
provider when its owner is destroyed. A non-view provider can therefore be
connected to a native composer without creating a QQuickItem. The legacy
canvas continues to implement the same interface while it is in use.

A native Home reference provider uses the same attachment import and preview
rules as the legacy canvas. It preserves source images, rejects unsupported
or over-limit inputs, and removes an attachment by its stable source identity.
It produces the existing generation parameters without creating a graphical
canvas. The Paint command still follows the application's new-canvas path;
this reference provider does not claim to implement canvas painting.
The native attachment binding projects the provider's canonical source,
filename, preview and caller-localized size description into the composer.
Adding or removing a reference updates the live card model, and replacing or
destroying the provider clears its cards instead of retaining stale commands.
Input errors follow the existing precedence: a draft validation error takes
priority, otherwise the attachment provider's current error is shown.
The native picker requests the same multiple-image and IISC file types as the
existing Home dialog. Cancellation and late callbacks do not change inputs;
an unavailable file is reported while other selected files remain usable.
The operating-system dialog is supplied by the iiGUI host. Headless tests use
an injected chooser and do not open a platform dialog.
