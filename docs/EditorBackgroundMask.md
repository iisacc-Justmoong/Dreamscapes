# Background mask panel

Dreamscapes implements the editable Figma `777:82857` panel in
`EditorBackgroundPanel.qml`. The same presenter is used by the desktop dock and
mobile tool sheet, using installed LVRS controls, themes and the shared
`iiColorManager` picker. Color/Remove/Mask/Replace navigation retains the other
background operations. The list always reads the current document; it never
inserts the two sample masks from the design.
Mask rows keep their controls when document properties or canvas previews change,
so keyboard focus and menu anchors remain intact. Visibility toggles handle both
click and double-click activation, including quick hide/show comparisons.

## Compact icon layout

The editable Figma frame was revised first, then mapped to the native presenter.
At 432 px including the border and padding, the two-mask reference is 823 px
high (previously 1,260 px). It uses existing LVRS library instances and normal
editable frames; Dreamscapes adds no local Figma component definitions.

Eight 18 px tool icons share one row. Selection modes occupy the left side of
the next row, with semantic selection helpers on the right. Below 258 px of
content width, the helpers move to a second row. Numeric parameters keep their
short names, slider and editable value together in a 30 px row. All original
parameter ranges, defaults, document actions and persistence remain the same.

Mask names stay visible. Visibility and More use 30 px icon buttons, followed
by an icon collection toolbar. Pressure, a new mask per selection and overlay
visibility use an 18 px symbol beside the existing switch. Overlay color and
opacity share that switch's row. The footer uses Apply, Invert and Reset icons.
The repeated active tool/mode, selected mask name and instructional paragraphs
are removed; empty, polygon progress, applied and error states remain concise.

Every icon button retains an accessible name, a keyboard focus target and an
LVRS tooltip. Visibility exposes its checked state. Disabled Subject/Sky
helpers retain their actual unavailability reason in accessible descriptions
and tooltips on their enabled wrapper. Mask action menus keep descriptive
labels because those commands are shown only when requested.
Tooltips observe hover and keyboard focus without adding a tap handler over
the controls. They anchor above the control and close from hover/focus changes,
without an outside-press dismissal grab, so an already open tooltip cannot
consume the next tool click or switch gesture.

Tool, selection, list and overlay icons resolve through the installed LVRS
icon library. `Assets/Background/mask-apply.svg` is the exact Figma export of
the Apply checkmark with the theme foreground override for its accent button.
It is packaged by the existing Background SVG resource glob. No temporary
Figma asset URL is used at runtime.

## Editing contract

| Control | Native behavior |
| --- | --- |
| Target | Select an artwork layer from the active artboard; selection follows the actual editor layer |
| Brush / Eraser | Stream mouse, touch and tablet input through iiPaintEngine `MaskPaintSession`; erasing subtracts coverage from the selected mask |
| Size / Hardness / Flow / Brush opacity / Pen pressure | Pixel diameter, edge softness, per-dab flow, per-stroke coverage ceiling, pressure on size and opacity |
| Lasso / Polygon | Closed painted coverage; polygon points remain visible until Close path; Escape, Clear or tool change cancels unfinished input |
| Rectangle / Ellipse / Gradient | Drag native canvas coordinates; gradient is black-to-white along the drag vector |
| Color range | Click a visible original layer color; iiGraphicAdjust range selection with editable Fuzziness |
| Add / Subtract / Intersect / Replace | Union maximum, product with complement, product, or replacement; the same soft-edge arithmetic is used within an operand and across the stack |
| Select subject / Select sky | Use matching semantic taxonomy/region data in the active artboard; disabled with an explanation when that data is absent |
| Select all / Invert selection / Clear | Full coverage, inverse of the active selection, or clearing transient selection/path input |
| New mask for each completed selection | Append independent operands; switch off to combine successive gestures into the current operand; a first stroke fills an empty operand |
| Masks | Stable identities and dynamic count; select, rename, duplicate, delete, toggle visibility and bypass the whole stack through More |
| Density / Feather / Invert | Nondestructive properties, independently stored per operand; Density zero restores hidden source pixels |
| Overlay color / opacity / visibility | Selected-area preview only; uses the shared color picker and cannot modify exported pixels |
| Apply mask | Enable the native mask stack; this does not flatten or destroy the artwork |
| Reset | Reset current operand properties; header Reset also restores brush/overlay settings; mask pixels and other operands survive |

Size accepts 1–4096 px. Its quick slider covers 1–100 px; the numeric field retains
the full engine range. Feather accepts 0–128 px, with a 0–10 px quick slider.
Opacity, flow, hardness, density, fuzziness and overlay opacity accept 0–100%.
Completed masks use layer-local bitmap coverage and stay linked to layer motion
and transforms. Existing legacy raster/vector masks remain in the composition.
Native vector stack operands can also be read and edited; painting converts the
selected operand to bitmap coverage without rasterizing its artwork.

A new empty operand is a hide-all mask. The stack starts bypassed until Apply;
white coverage reveals and black coverage hides. After application, subsequent
property and mask edits render immediately. The overlay displays selected areas,
one of Photoshop Quick Mask's supplied display modes, and is not document data.

## Storage and dependencies

- iiSharedCanvas **0.31.0** exposes product-independent ordered mask operands in
  `LayerAppearance::maskStack`, with IISC **1.21** persistence and rendering.
- iiSocietyHelper uses the identical by-value document ABI and owns Files
  publication, durable updates, aliases and version-conflict checks.
- Source pixels, layer opacity and native geometry remain untouched. Only
  committed mask coverage and reusable appearance fields are persisted.
- Document transactions and editor history cover creation, edits, duplicate,
  delete and apply. Invalid properties/references preserve the previous document
  and storage revision. Bypassed masks remain editable after reopening.
- The list has no fixed sample slots; native SDK operand/string/pixel processing
  budgets apply. Failed writes retain the last committed document.
- IISC 1.0–1.20 remains readable. Native 1.21 files require a compatible reader.
  Flattened image export includes the final masks. Layered PSD export rejects
  appearance it cannot preserve explicitly.

## Photoshop reference and evidence boundary

Recovered interface data was inspected directly beneath
`Engineering/Projects/Adobe/Photoshop`:

- `recovered/interface/Required/layouts/Masks/Properties/masks-2662.exv`
- `recovered/interface/Required/drover_layouts/mask_modify_flyout.eve`
- `recovered/interface/Required/drover_layouts/masks/bars/adj_brush_mask_bar.eve`
- `recovered/interface/Required/layouts/Channels/Dialogs/quickMaskOptions-2320.exv`
- `recovered/javascript/shipped/Required/Plug-ins/Generator/assets.generate/lib/dom/mask.js`

These supply the exposed controls and density/feather/overlay behavior, not the
complete proprietary Adobe engine. Subject/Sky does not synthesize detection
results or automatically download a model; it consumes actual supplied semantic
regions. Mouse and tablet routing are exercised by native input events; physical
pen hardware remains a separate device check.

`DreamscapesMaskPanelTests` checks source preservation, mask arithmetic, shape
coverage, pressure/opacity, cancellation, invalid input, transformed coordinates,
actual semantic/color-range results, working-file reopening, history and native
LVRS panel actions at 280/402/432 px. Cocoa screenshots and logs are written to
`build/mask-panel-verification`; full compact panel captures at 280/402/432 px
are written to `build/mask-icons-verification`. The panel regression also checks
icon rendering, concise controls, slider/input alignment, pressure toggling,
keyboard activation on release and visibility icon changes. Build/SDK checks and packaged app startup are
reported separately from any globally installed application.
The verification manifest records the exact SDK versions of its signed bundle.
An independent consumer keeps its observer event loop active during cold app
startup and verifies the bundled libraries, one root window and mutual discovery.
