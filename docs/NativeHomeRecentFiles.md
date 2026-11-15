# Home recent files migration contract

The Home recent-files view is being moved to LVUI and iiGUI using Dreamscapes
Figma node `1467:5049`. The Figma names and pictures are examples; the product
must use the real recent-file projection and preview resolver. The containing
Home layout supplies the outside 12px inset. The view uses LVUI controls and
tokens, not product-specific colors or component overrides.

The header contains the file count, type filter and View all command. The
scrollable list presents the thumbnail, name, summary, specifications,
modified date and state. At the same breakpoints as the current Home view it
omits lower-priority columns and the filter when the content is narrow. An
empty or loading list reports the corresponding status. File counts and
metadata must come from the product snapshot rather than invented data.

Selection and keyboard focus are view state. A stable file identity must survive
reordering, filtering and changes to other entries. Filtering does not change
the selected file. Removing a selected file chooses the first remaining file,
matching the current view. Selecting a row updates the shared selected-file
panel; Enter and double-click open the selected file through the original
application route, and View all navigates to the existing file browser. The
exact original file value, including remote-storage fields, must be retained for
open. List scrolling and selection must not edit a file or start a generation.

The native view model and renderer do not depend on Qt, QML or LVRS. The
existing Home and storage controllers remain separate transport adapters until
the application window has migrated. Use a keyed, virtualized LVUI collection
and asynchronous image resolver, retaining only visible row widgets. Tests
must cover empty and loading views, filtering, row identities, live selection,
reordering, source removal, keyboard and pointer commands, remote file
navigation, responsive layout, image loading and native renderer boundaries.

The LVUI list, native navigation and Qt-free projection are implemented. The
macOS application attaches the native list to the current Home panel through a
temporary presenter. The original QML panel remains the layout and transport
host, and becomes visible again if the native surface fails, is obscured by a
modal, or is closing. The containing window and the rest of Home still require
migration; this adapter does not remove their Qt, QML or LVRS dependencies.

The session keeps the original file values for open and creates stable preview
addresses without reading every file. The image resolver resolves addresses for
visible rows on demand, and the existing native image loader performs decoding
asynchronously. Replaced snapshots invalidate their preview addresses, including
when the source changes while an address is being resolved. Filtering is a view
operation and never rewrites the original files.

Automated coverage exercises the model, actual iiGUI widget tree, input and
virtualized collection, session, presenter and the Qt-free renderer boundary.
The widget test uses 1,000 items and checks that only a bounded number of row
widgets and preview requests are active. Presenter tests use a QML fixture to
check the temporary connection; they do not establish a manually verified
installed-application or platform-rendering result.

The intermediate native Home composition keeps this same list widget mounted
while switching between its wide and narrow layout. A headless composition test
checks that its scroll position and keyboard focus survive the breakpoint.
