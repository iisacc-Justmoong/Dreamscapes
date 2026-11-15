# Native Home workspace composition

The native Home workspace follows Dreamscapes Figma `1467:4554` and the
existing `DesktopHome.qml` behavior. The sidebar, recent files, selected file,
queue, recent generations and generation composer are independently supplied
native view sources. A source is required for every region: missing or failed
content must not silently turn into an empty production panel. The application
adapter owns live data, translation, navigation, file commands and generation;
the workspace owns only layout and presentation state.

At a window width below 700 the sidebar is 48 wide, otherwise 181. The
content viewport scrolls. At a viewport width of 1040 or more the file and
session panels are placed in two columns; otherwise they stack vertically.
The 12 or 16 point content inset, 12 point gaps, 360 point minimum file-panel
height and 160 point session panels follow the existing layout. Caller-supplied
composer height participates in the file-panel height calculation. Resizing
must not modify files or submit a generation. Header and footer content and
actions are supplied by the application rather than example data from Figma.

The LVUI workspace composes the existing native views through iiGUI NativeView.
Until LVUI supplies a responsive grid, a small C++ layout backend moves the
two already-mounted child views instead of destroying them at a breakpoint.
It does not draw or style the controls. This retains the native collection's
scroll position and focus while resizing, independently of document state.
The LVUI view passes `Metric.space12` and `Metric.space16` to that layout
backend; the application does not define its own spacing tokens.
The enclosing view requires an actual composer source instead of substituting
an empty one. A headless widget test mounts the real native panels, exercises
commands and resizing, and checks the compact sidebar tooltip's geometry and
clip. It also verifies that a failed replacement preserves the prior workspace
and that retirement releases the model. A separate headless test mounts the
real LVUI composer with the other native panels, checks prompt focus and
workspace height across a resize, and sends a command to an isolated draft
handler. It does not open the application or run generation.

The view is an intermediate native composition, not the application's Home.
The remaining QuickGenerate canvas and picker, other routes, the top-level
window and complete controller adapters remain required. The workspace must
not replace the running Home until all
its native regions and commands have functional parity, and the native
tooltip and resizing behavior are checked in an actual host. Automated
headless tests are distinct from a visually verified installed application.

When the composer reports a new natural height during a Home resize, the
workspace retains the latest height and recalculates after the active layout
publication. A resize must not discard that height or trigger generation.
The composer-to-workspace binding observes the panel's measured height and
retains no generation or file ownership. Retiring the binding stops future
layout updates; it does not close either model or cancel work.

The recent-file and recent-generation views may use separate preview resolvers:
their session adapters generate different lazy preview identifiers. The
selected-file preview continues to use the common resolver. If a specialized
resolver is not supplied, that view falls back to the common one.

The Home content adapter reads the active generation controller's Society
mirror and applies the same history projection as the existing Home. It keeps
recent-file selection, filtering, result focus and multi-selection as view
state across mirror updates. Removing a selected file reconciles the selection
to the next available entry and updates the selected-file panel. It passes the
original Society value to the existing open/edit commands, and View all uses
the existing storage navigation. Changing or destroying the active controller
must detach the old mirror; retiring the adapter must detach its commands.
Mirror notifications do not request storage refreshes or run generation.
The adapter is tested with an isolated `GenerationController` and Society
mirror, including reentrant notifications, controller replacement and
retirement. A headless LVUI workspace test combines that adapter with the
existing generation draft, checks thumbnails and file selection, and follows
the existing View all and submit signals. It tests pointer selection after
resizing and scrolling. It does not run an inference job or launch the app.
The nested collection input fix is in iiGUI's shared
`CollectionWidgetNavigation` header; builds must use a version containing it.

The old `HomeContentRow`, `HomePromptStarters` and `GenerationHistory` QML
definitions had no remaining application references and are no longer bundled.
The active recent-file, selected-file and recent-generation paths are distinct
views and remain present while the application transition is in progress.

The product Home session composes the real generation draft, Society content,
queue, reference picker and native workspace into a single retained source.
It forwards submission and advanced settings to the existing workspace
controller, routes the active generation controller's complete submitted job
batch to the existing review navigation, and disconnects the previous
controller on replacement. It exposes file and new-canvas commands, and uses
caller-provided navigation, translations, artwork and host file dialog. View resizing and
content synchronization must never submit generation. The product session is
not the application entrypoint until the other routes and window controls have
equivalent native views.

`Dreamscapes.NativeHomeProductSession` exercises that composition with a
temporary Society store, a fake model fixture and a paused generation queue.
It verifies multi-job submission, review navigation and controller replacement
without executing model inference or launching the application. Its iiGUI
widget tree also checks that the same session presents the selected Society
file and accepts the attachment and queue commands.
