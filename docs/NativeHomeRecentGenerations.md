# Home recent generations migration contract

The Home recent-generations area follows Figma node `1467:5050`. Its two-column
layout becomes one column below 500 pixels. Each 48-pixel cell contains a
60-by-40 preview and the actual result name, model, media type and available
generation metadata. The parent Home layout supplies the 12-pixel outside
inset. Use LVUI components and tokens; sample content is not product data.

The header retains View all and the existing Select, Cancel and Edit controls.
In ordinary mode clicking a result opens it through the existing file route;
in selection mode it toggles eligible images. Edit passes the original selected
image values to the existing multi-image editor route. Video and other ineligible
items must not enter image selection. Selected items are view state, not changes
to generation records or source files.

Maintain focus and the scroll position when results arrive. Keyboard navigation
uses the current number of columns; Enter activates the focused entry. The
collection must preserve stable identities under reorder and mutation, and keep
only visible cells and previews active. Empty and loading states come from the
real storage snapshot. Local and remote metadata and opening behavior must
remain unchanged. A native presentation failure must reveal the legacy panel
until its containing Home window has itself been migrated.

The Qt-free recent-generations view model projects real file entries, reuses
the image-selection policy and retains stable tokens across reordering. The
LVUI and iiGUI grid uses the shared native collection navigation and image
resolver. Automated tests cover responsive column counts, loading, selection,
video exclusion, pointer and keyboard activation, removal, reentrant result
changes and bounded widget and preview creation for 1,000 entries.

On macOS the application attaches the native grid to the current Home panel
through a temporary presenter. The adapter preserves the original result
objects for single-file and multi-image editing, and synchronizes selection
mode and image keys with the existing Home selection model. The original QML
panel remains the transport and layout host; it is uncovered if the native
surface fails, a modal overlaps it, or the surface is retired. Other platforms
continue using the existing view until their native hosts are connected.

Presenter and session tests use the actual Home selection model plus isolated
QML fixtures to check original values, modal overlap, panel-name collisions,
language changes, fallback and asynchronous shutdown. They do not establish
manually verified platform rendering or installed-application behavior. The
containing window and the remaining QML views still require migration.
