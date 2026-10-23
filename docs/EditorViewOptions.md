# Editor view options

Rulers use document pixels (`px`), independent of the canvas PPI and grid/guide
units. The view bar labels the control `Rulers · px`, and the ruler corner shows
`px`. Numeric ticks keep their document-pixel values while zoom and pan change
their screen positions. `EditorViewOptions.rulerUnit` exposes the fixed unit to
both views; no document dimensions or physical-unit conversion is changed.

The editor implements the Figma [view bar](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1157-72320), [Snap](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1186-72316), [Grid](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1186-72320) and [Guides](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=1186-72328) settings.

The 40 px desktop bar uses split controls: the labelled face changes visibility/enabled state, and the 22 px chevron opens details. Settings retain the selected mode when reopened. The right Constrain and Preset controls stay at the right edge. The left controls scroll horizontally instead of wrapping. Popovers are 384 px wide, clamp to smaller windows, and scroll vertically while retaining their header and actions. Content is loaded only while a popover is open. All button roles stay LVRS Default; selection uses that role's pressed surface through `EditorButtonState`.

Numeric edits, toggles and preset changes keep the existing control instances
and keyboard focus. Only geometry type, units and guide orientation rebuild
the layout when their visible fields change. Composite/Outlines and the
Outlines button share one display state, so either control restores Composite.

ComboBox controls use their own fixed `LV.ComboBox.Primary/Borderless` enumeration. Its numeric values differ from `LV.AbstractButton` roles; the button-state policy accepts both namespaces without allowing interaction-dependent tone expressions. The canvas surface starts below the 40 px view bar, while sidebar, tool rail and inspector bounds retain their existing layout.

## Settings and ownership

`resources/editor/ViewOptions.json` is the single default/validation catalogue. `EditorViewOptions` is the QML presentation adapter; `EditorViewOptionDefinitions.js` supplies the visible fields for each state. `AlignmentGeometry` uses C++23 standard types only and owns grid construction, guide construction, clipping and snap detection. `EditorCanvasAlignment` resolves the actual document, artboard, layer, selection and viewport coordinates.

Settings and created guides belong to each open canvas's view session. They survive panel closure and switching between already-open canvases. They do not add raster/vector layers, modify document pixels, create Undo entries or change the IISC file format. New document adoption clears placed guides. Session guides/preferences are not serialized into IISC. Existing text baseline metadata is used only when the current text authoring session supplies it; imported paths do not acquire invented text baselines.

## Snap

Smart, Pixel, Vector and Layout presets configure the same editable target switches and increment values. Targets are canvas edges/centres, layer edges/centres, guides, grid, pixel edges/centres, vector path anchors, actual authored text baselines, equal spacing and equal size. Visible-only and Ignore locked filter layer candidates. Hidden grid/guide targets are excluded when Visible only is enabled. Guide snapping also respects the Guides `Snap to guides` switch.

Tolerance and release are screen pixels, converted through the current zoom. A retained target releases at the configured release distance. Priority can favour guides or the grid; axis constraints restrict horizontal or vertical correction. Movement compares the moving shape's corners/centre/edge midpoints. Equal spacing compares the nearest 64 eligible layers without copying their vector data. Equal-size matching is applied during resize. Layer geometry is cached for a gesture and invalidated by accepted document/settings changes.

Line snapping uses exact nearby geometry for every moving anchor, independent
of adaptive display and line-density limits. Its search radius uses the full
configured tolerance. Horizontal/vertical constraints also work with angled
guides and equal-size matching. Deleted guides and newly hidden layer targets
are discarded immediately. Visible-only respects zero opacity and the pixel
grid's zoom reveal threshold.

Element creation, moving and handle resizing use native document coordinates. Rotation and scale use the configured increments; Shift retains its existing constrained-transform behaviour. Constrain holds proportions during corner resize. An accepted drag remains one document transaction/Undo step. Changing view settings, generating guides, moving guides, or cancelling an unfinished transform does not create document edits.

Explicit Width/Height entries keep the requested numeric dimensions. Pointer
snap increments and the view bar's corner constraint do not alter those inputs;
the inspector's ratio control supplies the paired dimension when requested.
Scale increments are measured from the unchanged 100% size, including steps
that do not divide 100. A zero Release value disables target retention.

## Grid

| Type | Geometry controls |
|---|---|
| Rectangular | X/Y spacing, subdivisions, major interval, rotation |
| Isometric | Step, axis angle, subdivisions, rotation |
| Polar | Radial step, spokes, start/sweep angle, ring count |
| Perspective | One/two-point projection, horizon, density, vanishing points, depth interval |
| Pixel | Pixel step, zoom reveal threshold, pixel edge/centre snapping |
| Baseline | Increment, offset, major interval |
| Layout | Columns/rows, X/Y gutters, X/Y margins |

Units are px, mm, pt or percent; physical units use the actual canvas PPI. Reference is Canvas, Selection or Artboard. Origin offsets are document/reference units; polar origin defaults to the reference centre. Lines/dots, colour, opacity, major lines, origin axes, adaptive display and intersection snapping affect real display or snap geometry. Pixel-grid visibility respects the reveal threshold. Snap geometry remains exact while adaptive display can omit dense minor lines. A missing Selection reference produces no selection-relative grid/guides.

Dots represent actual grid intersections, including perspective rays/depth
rows and polar rings/spokes. Baseline dots use a regular horizontal sampling
of the baseline rows. Adaptive polar display skips configured rings without
changing the outer radius. Origin axes remain lines in either grid style.

## Guides

Single guides support horizontal/vertical/angled placement. Layout guides use rows, columns, gutters and margins. Video safe areas have separate action/title insets; print safe areas use physical bleed, safe-margin and crop-inset values. Composition supports thirds, golden ratio and angled diagonals. All artboards generates guides from actual artboard bounds; Clear existing on create replaces the session guide set.

Every guide mode exposes Canvas/Selection/Artboard reference. The footer trash
button clears all session guides without editing document layers. Guide labels
use stable guide numbers, the configured colour/opacity and stacking position,
and remain visible when a line begins outside the viewport.
Label positions reserve the ruler strips. Colour alpha combines with the
opacity setting; fully transparent guides neither capture pointer input nor
qualify as visible-only snap targets.

Drag a visible, unlocked guide on the canvas to move it perpendicular to its direction; Delete/Backspace removes the selected guide. Locked guides cannot move or be deleted. Solid/dashed lines, colour, opacity, labels, above/below artwork, smart guides and snapping are independent settings. Guide generation is capped at 4,096 lines.

Guide hit testing uses a separate transparent input layer, so visible guides
remain draggable when drawn below artwork. Modal view settings still block
canvas/guide editing until closed.

## Rendering and verification

Live grid, guide, outline and snap overlays use bounded QSG line/point batches on hardware backends. The software backend uses the same geometry through a painted fallback. Overlay input passes through except when an unlocked guide is hit. No viewport-sized image is regenerated on the hardware snap path. Previews use the same geometry at an illustrative zoom. RGB channel inspection uses a bundled shader compiled at build time; Qt ShaderTools is a build tool and is not linked into the application runtime. Pixel preview selects nearest-neighbour rendering. Rulers use viewport coordinates and the same zoom/pan mapping.

Outline mode follows actual vector curves and transformed raster/video extents
at the current frame, excluding hidden layers. Channel inspection renders Red,
Green, Blue and Alpha as grayscale values and restores the RGB composite.
Software rendering converts the SDK's resident viewport tiles; it does not
re-render the complete document. Alpha inspection is clipped to the canvas,
preserving the surrounding workspace. Both rulers show major and minor ticks.

Static icons are the original exported Figma SVGs under `resources/editor/view-options`, at their original 16 px slots. Screenshots remain verification artifacts under `build/view-options-verification`.

```sh
cmake --build build --target Dreamscapes DreamscapesGuiTests DreamscapesViewOptionsTests DreamscapesAlignmentGeometryTests DreamscapesElementTransformTests DreamscapesCanvasFeedbackTests --parallel 4
ctest --test-dir build -R '^Dreamscapes\.(ViewOptions|ViewOptionsHost|AlignmentGeometry|ElementTransform|CanvasFeedback)$' --output-on-failure
```

`Dreamscapes.ViewOptions` tests accepted/rejected values, presets, real guide geometry and locking, native pointer dragging/deletion, rotation/scale increments, spacing, real grid snapping, one-step Undo, all 18 setting states at 384/304 px, preview rendering and mouse/keyboard edits through the view bar. Existing free-transform tests explicitly disable alignment when checking unconstrained geometry. Native Cocoa/Metal captures, canonical-bundle signing, installation and running-process adoption are verified separately.

The detail-input tests operate each visible numeric, colour, toggle, choice and
menu control through native pointer/keyboard events in all 18 modes. Their
per-mode committed-key records are saved under `build/view-options-details`.
The host tests compare actual rendered pixels for all five channels and
exercise Composite, Outlines, Transparency, Pixel preview, Constrain and all
four top-bar presets. Both software and Cocoa/Metal rendering are checked.
