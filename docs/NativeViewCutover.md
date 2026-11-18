# LVUI view cutover

Dreamscapes is being switched to an iiGUI application window and LVUI views.
The native Home composer, Image and Video parameter screens, and the result
review route bind to session-local C++ view models. Their controls and review
state update dynamically; details and validation are documented in
[Native generation view models](NativeDynamicGeneration.md). The editor,
storage, publishing, preferences and New Canvas routes also have native models; see
[Native editor and workspace view models](NativeDynamicWorkspaces.md) for the
connected file, canvas and in-memory behavior and the absent providers. Many other migrated
views remain minimal static LVUI presentations retaining identifiable text.
The cutover is not functionally equivalent to the former application:
some editing tools, interactive social authorization and other product integrations
have not been reconnected. Local Society file browsing, native `.iisc`
open/save, canvas rendering, timeline audio/visual playback, Image/Video generation and
session-authorized publication are connected, with the limits
documented there. No action on an
unavailable backend should be represented as successful.

`resources/migration/view-manifest.json` records each removed QML view, its
content hash, and its LVUI replacement. No QML files remain in the source tree.
The replacement views use LVUI components and tokens and compile for iiGUI. The
native entry point must not instantiate a QML engine or link a Qt Quick view.
Legacy backend source is retained separately; it is not evidence that the new
native application can execute the prior product workflows. Setting
`DREAMSCAPES_NATIVE_VIEW_APP=OFF` without the foundation-only option is rejected:
the old Qt Quick view target cannot be built from the retired view sources.
The macOS build re-signs its generated bundle ad hoc after linking, so a
signature left by an older build cannot describe the new native executable.

The app's `--help` and `--list-views` commands expose the view inventory to
agents without opening a window; `--view image` or `--view video` selects the
initial native view. Any replacement path returned by `--list-views` can also
be supplied to `--view`. `src/App/Native/dreamscapes_views.py` provides
the same source inventory to Python; neither interface performs generation or
accesses a user's files. The native Home shows unavailable services as such.

The build and windowless tests check the source tree and native bundle for QML, the replacement
inventory and the compiled widget tree. An actual launch, visual parity,
device behavior and previously available product functions are separate
claims and are not established by these tests.
