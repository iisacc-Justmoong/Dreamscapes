# Desktop editor layout

Canvas object ownership, active artboard navigation, and saved document changes
are independent. Selecting a page keeps the existing canvas mounted and its tool
drafts intact. Selection, zoom, pan, display options, panel visibility and seeks
only update the corresponding view state. File rows display live frame/artboard
context through `fileViewInformation` without treating it as file metadata.
See [Performance](Performance.md#ui-lifecycle-and-invalidation) for the notification
and regression contracts.

References: [Dreamscapes Editor, Figma 353:22614](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=353-22614&m=dev)
with [Vertical Toolbar, Figma 1105:91844](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1105-91844&m=dev)
and [Elements panel, Figma 353:22022](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=353-22022&m=dev).

## Shared application toolbar

### Publish action

The desktop editor's top bar includes the `Publish` action from
[Figma 1157:72246](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1157-72246&m=dev).
This scoped update adds its 66-by-22-pixel LVRS label button at the right edge,
with an 8-pixel inset and gap. Its fixed `Primary` tone is LVRS's Accent role;
hover, press, release and keyboard focus use stock LVRS interaction states.
The existing view-option controls and shared application toolbar retain their
layout and behavior.

Mouse release, Space and Return emit `CanvasEditor.publishRequested()` once.
`Main.openPublish()` now opens the internal [publishing workspace](PublishingWorkspace.md)
for the current document. Back restores the retained editor and its view state.
Merely activating the button does not save or modify the current document.
External upload remains separate from this authoring route; its existing backend
is described in [SocialPublishing](SocialPublishing.md).

`Dreamscapes.EditorPublish` checks geometry at desktop widths from 640 to 2319,
the fixed action tone across pointer/keyboard interaction, one event per
activation, the mobile visibility boundary and document-state preservation.
Set `DREAMSCAPES_CAPTURE_DIR` to save `editor-publish-bar.png` from the actual
QML component.

### Search and account controls

The desktop editor displays the same search, notifications and account controls
as Home, using the existing `DesktopHomeToolbar` instance in `Main.qml`. The
controls stay in the shared 32-pixel title bar, above the editor view-options bar, with
the same native traffic-light clearance and 12-pixel right inset. Narrow windows
reduce the search field width without hiding either icon. Opening an editor
over a generation result also keeps this toolbar visible.

In the editor, Cmd/Ctrl+F focuses global search. Matching files, publications and
generation history appear in a nonmodal LVRS menu under the field. Typing,
clearing the query and dismissing the list keep the current canvas, selection,
undo history and panel state. Up/Down select a result; Enter or a result click
opens it through the existing file/download route. A search does not navigate
away from the editor until the user chooses a file. Home retains its existing
inline section filtering.

Account and notification menus reuse the existing account actions and live
generation jobs. While either modal menu is open, canvas input is suspended.
Escape dismisses the application menu or search, without also collapsing the
editor inspector. All toolbar buttons retain the LVRS Default type.

The shared toolbar, inspector, timeline, and photo/layer tiles use stock LVRS
hover help. [Tooltips](Tooltips.md) documents the component integration and the
guard against Qt default tooltip call sites.

`Dreamscapes.EditorApplicationToolbar` verifies the shared placement at widths
from 320 to 1374 pixels, menu input isolation, keyboard search, result routing,
and document/draft preservation. Set `DREAMSCAPES_CAPTURE_DIR` to capture the
native editor and search interface without using production storage.

## Tool rail

툴 탭은 LVRS의 필수 `iconName` 계약에 따라 도구 키를 아이콘 식별자로 전달한다. `iconSource`와 실제 이미지가 같은 기존 SVG를 참조하므로 데스크톱·모바일의 원본 도형과 배치는 유지된다.

The desktop editor places a 52-pixel vertical toolbar between the canvas work
area and the right tool panel. Its 18 LVRS tabs are 36 by 36 pixels, with a
4-pixel gap and 8-pixel outer inset. The transparent, borderless rail has an
18-pixel corner radius and hugs its 732-pixel content height. When the window
is shorter, the rail clamps to that height and scrolls vertically. Selected
tabs use the LVRS selected fill and a 1-pixel border. The 2319-by-1316 reference
leaves 1782 pixels for the workspace, then 52 for the toolbar and 304 for the
panel, after the 181-pixel sidebar. Mobile keeps its 84-pixel horizontal bottom
toolbar with captions and its existing selected-tab styling.
Their original SVGs occupy 22-pixel slots at (7, 8). Desktop tabs display
icons and preserve tool names for accessibility and keyboard navigation.
Up/Down selects the previous/next desktop tool; Home/End reaches the first/last
tool. Selection, programmatic restoration and resizing keep the current tab
inside the scrolling viewport. Mobile retains Left/Right navigation.
All 18 icons retain the original Figma path geometry, natural bounds,
viewBoxes, dimensions, stroke widths, caps and joins. Their source files and
22-pixel slots are reused. The toolbar now gives every tool its own fixed
opaque stroke color; Elements keeps its existing periwinkle. Only the
six-digit stroke colors of the other 17 SVGs differ from their Figma exports,
so the original byte counts and all non-color asset bytes remain unchanged.

## Tool icon colors

Desktop and mobile use the same 18 colored SVGs. Every tool keeps its own
color in both selected and unselected states, including hover and press.
Selection remains visible through the existing LVRS tab background and focus
styling. Icon color identifies the tool and does not indicate whether that
tool is selected. Existing disabled behavior and tab interactions remain
unchanged. The artwork uses flat named colors without gradients, opacity
variants or numeric color levels.

`src/App/Views/Editor/Assets/manifest.json` records each tool's `strokeColor`
and descriptive `colorName` alongside its original Figma node, bounds,
offsets and byte count. The eight LVRS `accentPalette` colors already present
in Theme remain available; the toolbar uses a separate fixed artwork palette
so all 18 tools have distinct colors without altering shared theme tokens.

Asset is removed from the application toolbar and panel catalog. Camera/Photo
is followed directly by File in both layouts and in keyboard navigation.
Panel headings are numbered consecutively from 01 to 18 in that order.

| Tool | Color name | Stroke color |
| --- | --- | --- |
| Elements | Periwinkle | `#8E8EFF` |
| Text | Lavender | `#CBA6F7` |
| Camera/Photo | Sky blue | `#64B5F6` |
| File | Seafoam | `#80CBC4` |
| Background | Green | `#62BA46` |
| Audio track | Pink | `#F74F9E` |
| Canvas | Yellow | `#FFC600` |
| Generative | Purple | `#A550A7` |
| Layers | Blue | `#007AFF` |
| Select | Lime | `#CDDC39` |
| Color | Orange | `#F7821B` |
| Effects | Magenta | `#E879F9` |
| Retouch | Peach | `#FFB4A2` |
| Fill | Turquoise | `#2DD4BF` |
| Brush | Red | `#FF5257` |
| Adjust | Gold | `#F2C55C` |
| Masking | Powder blue | `#A5C7E8` |
| Eraser | Rose | `#F58CA8` |

## Canvas work area

The left sidebar uses the same layout as Home, Image and Video: 181 pixels wide,
or 48 below 700 logical pixels, with 8 pixels of padding on every side. Its
24-pixel menu rows, dividers, icon slots and label positions do not change when
entering or leaving Editor. The shared sidebar has no Editor-specific geometry
or Files icon override. The opaque editor backdrop is a child of the canvas
surface and exactly fills that viewport. The viewport clips document rendering,
checkerboards and overlays, including when zoomed or panned. The sidebar, view
bar, tool rail, inspector, page strip and timeline expose the shared
ApplicationWindow material. The inspector has a 12-pixel content
inset and a zero-width outer border. The mobile tool strip and
primary tool sheet also have transparent surfaces. Buttons, input fields,
selection/hover/pressed states, dividers, document previews and transient menus
retain their existing paints.
The main window keeps the same content top inset between these views, including
fullscreen. Editor remains lazy-loaded and navigation preserves Home drafts.
Hiding the editor also retains the desktop view-bar height, so navigation does
not resize the hidden canvas or refit its retained zoom and pan.
When the shared sidebar, 52-pixel toolbar and 280-pixel minimum panel leave
less than 320 pixels for the canvas, the canvas uses the remaining width;
entering Editor does not collapse navigation ahead of the shared breakpoint.

`Dreamscapes.EditorSidebar` compares the actual window/frame bounds, sidebar,
all ten menu rows, both dividers, icons and labels across Home -> Editor -> Image
-> Video -> Home at 1374, 900, 899, 800, 700, 699 and 640 pixels. It also checks
the compact boundary in both directions, a 480-pixel window height, transparent
sidebar padding and the fullscreen content inset. At 1374 pixels it compares
empty view-bar, rail, inspector and timeline margins with an actual capture of
the ApplicationWindow background, while checking that the opaque backdrop
matches only the clipped canvas viewport. Native Cocoa verification
uses ordinary windows so the test does not change the user's macOS Spaces.

The right
tool panel starts at 304 pixels with a 12-pixel content inset, producing
280 pixels of content. Titles stay upper-left and parameter groups align
right. Existing drag resizing, responsive wrapping, tool drafts and native
tool actions follow [EditorPanelResizing.md](EditorPanelResizing.md).

The native iiSharedCanvas surface fills the remaining work area to the left of
the toolbar and above the bottom timeline, below the desktop view-options bar.
The timeline uses the same horizontal extent as the canvas. Closing the panel
moves the rail to the right window edge; reopening or resizing the panel moves
the rail with its left edge. The panel's maximum width reserves the 52-pixel
rail as well as the existing 320-pixel canvas minimum where space permits.
Fitting a document centers both axes in that surface, unlike the
reference's bottom-aligned sample rectangle. Window resizing, panel resizing
and panel collapse/reopen refit the current document using the SDK's existing
viewport transform. Document dimensions and image content are dynamic.
Multiple-canvas navigation reserves its own space when needed; neither the
toolbar, timeline nor tool panel overlaps the canvas surface.

## Bottom timeline

The editor implements Figma `1065:90081` with **Layers**, Keyframes and Curves.
`EditorTimeline.qml` uses `EditorTimelineModel`, a projection of the current
`EditorCanvas` document. The Video workspace continues to use its own generation
recipe; an editor timeline edit updates native IISC data and its existing undo
history, synchronization and save path.

Visual rows follow the active canvas layer stack, topmost first. Audio rows
follow the native audio tracks. Opening an empty document creates no rows;
opening a document with layers immediately displays those actual layers. No
example shots, content rows, waveforms, captions or keys are inserted. Native
IISC currently has no caption-track schema, so there is no synthetic Caption
row. **Layer +** explicitly adds a static/dynamic bitmap/vector layer, imports
an image layer, or creates an empty native audio track.

Each layer occupies its own In/Out range. Ranges may overlap and do not form a
concatenated shot sequence. IISC uses inclusive `lastFrame`; the UI displays
exclusive Out and `Duration = Out - In`. A layer without an explicit range spans
the document's frame extent. Static layers hold their native source over this
range. Dynamic bitmap/vector layers resolve each frame through the native
source keys, including held content between sparse keys. Video layers use their
native playback settings. The thumbnail follows the playhead; source assets,
opacity/motion keys and actual audio samples drive the other previews.

The Figma header, eight editing icons, 184-pixel desktop gutter, 40-pixel visual
rows, 48-pixel audio rows, compact precision fields and status footer use LVRS.
Selected controls retain their fixed Default tone and show the pressed state.
The gutter reduces to 144 or 88 pixels below 1100 or 600 pixels. The transport,
tools, precision fields and footer each retain one row. Each row clips controls
that extend beyond its available width, without wrapping or shrinking them.
The dock reserves at most 48% of the
editing height with a 160-pixel minimum; vertical scrolling reaches all rows
and controls. Only the lanes scroll horizontally when zoomed. Fit resets zoom
and both scroll positions. Mobile retains the same dock above its toolbar.

Seeking updates `EditorCanvas.frame`, and native frame changes update the
playhead. Playback uses elapsed time and native timeline audio; seeking,
hiding, disabling or switching canvases stops playback. The ruler uses the
actual frame count and rational FPS. Layer selection, range edits, movement,
splitting, duplication, visibility, track names/order, frame-source replacement
and audio edits use the document's transaction history. Locks reuse canvas
layer locks; selection, tools, filters, zoom and edit linking are view state.

See [EditorLayerTimeline.md](EditorLayerTimeline.md) for native timing, editing
and verification details. `Dreamscapes.EditorLayerTimeline` covers actual native
static/dynamic data, every-frame previews, persisted working files, undo/redo,
track management and four responsive widths. `Dreamscapes.EditorTimeline`
covers the mounted editor at six desktop/mobile sizes, its real Layer menu,
canvas selection, precision controls, keyboard playback and fixed button tones.

Desktop document commands no longer consume a second toolbar row. Open,
Save, Save As, Undo and Redo retain their standard shortcuts. A native LVRS
canvas context menu exposes Open, Save, Save As, Undo, Redo, Layers, Fit and
Paint Color through the same existing document dialogs and operations.
Right-click opens it; left-button editing and middle-button panning remain
with iiSharedCanvas. Multi-canvas Previous/Next buttons remain available.
Mobile retains its 84-pixel bottom toolbar, captions and document buttons.

`desktopEditorLayoutCentersCanvas` checks the reference geometry, all 18
icon slots, tool selection, transparent rail corners, selected-tab borders,
centered native transforms across portrait,
landscape and square documents, three window sizes, three panel widths and
panel collapse/reopen. `desktopEditorToolbarScrollsAndNavigates` verifies four
desktop sizes, wheel scrolling, Up/Down and Home/End navigation, visibility of
the last tool, the 320-pixel canvas reservation, and the rail's position across
panel collapse/reopen. The layout test also uses the real context-menu Fit command after
panning and the mobile layout switch. Existing native editor tests use
tool names instead of fixed toolbar indices, standard shortcuts and real
context-menu commands to verify painting,
erasing, history, color picking, layers and document persistence. Historical
422-pixel tool fixtures retain the original 398-pixel control area with the
new 12-pixel inset; the current window
layout checks the 304-pixel default.

```sh
cmake --build build --target Dreamscapes DreamscapesGuiTests Dreamscapes_qmllint --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(EditorLayout|EditorTimeline|EditorPanelResize|EditorToolPanels|EditorCanvasGui|MultiCanvasGui|EditorElements)$' --output-on-failure
```

Build logs, asset hashes, native layout captures and bundle verification are
stored under `build/vertical-editor-toolbar-20261016/` for the vertical layout.
Installed macOS verification is recorded separately from software-rendered
GUI tests.
