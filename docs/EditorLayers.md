# Editor layer panels

The existing resizable editor dock now presents four views from Dreamscapes Figma: [Layer 564:92996](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=564-92996), [Adjust 564:93315](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=564-93315), [Histogram 564:93634](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=564-93634), and [Information 564:93953](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=564-93953). The mobile Layer sheet uses the same presenter. Other editor tools retain their current presenters.

The reference frame is 432 × 559, with a 398 × 525 content area. The four native LVRS segmented buttons, typography, colors, radii, inputs, slider, menus and icon buttons use the installed LVRS framework. The hierarchy composes `LV.HierarchyList` and `LV.HierarchyItem`, the native parts of `LV.Hierarchy`, with 32 px rows and a trailing visibility control. The 32 exported SVG assets preserve their original view boxes and root geometry. Narrow docks reflow actions and scroll taller content.

The dock and mobile sheet load this presenter when Layer is opened. Keeping the hierarchy outside the initial scene's inline component structure avoids exhausting the QML creation stack during application startup and route changes.

## Document operations

Panel headings and property labels use the passive [PanelRow](PanelRows.md) surface. Hover, press and keyboard focus effects belong only to the native value controls and action buttons; the layer hierarchy retains its selectable rows.

The trailing visibility icon uses the original 16 px horizontal eye exported from [Hierarchy 564:93009](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=564-93009), saved as `Visibility.svg`, without rotation. Figma's slot is named Chevron, but its current component is `generalshow`; it must not be replaced with a directional chevron. The eye's horizontal orientation is independent of the left-side group disclosure and the layer's visible/hidden state.

The Layer footer uses the native LVRS Default tone for New layer, Group, Merge down, Duplicate, Apply mask and Lock, and Borderless only for Delete, matching Figma nodes `564:93011` through `564:93017`. Each action preserves its tone's background when disabled: unavailable operations retain the Default surface, while Delete stays transparent. Lock retains the Default tone when the layer is locked. Capability checks and lock/unlock behavior still control whether actions can execute.

The hierarchy reads the actual document and selected layer, identifies static/dynamic bitmap/vector content, displays saved artboards as collapsible groups, and controls selection and visibility. Blend and opacity update the document. Duplicate copies referenced assets and dynamic keyframes independently. New Paint layer and Place image use existing document operations. Group creates a transparent, full-canvas native artboard for an ungrouped layer on a finite canvas; nested groups are not supported. Merge down renders two adjacent visible, unlocked, unanimated static layers in the same artboard into one bitmap. Both operations use the standard edit transaction and support undo and `.iisc` persistence. Lock uses the existing pixel/position lock controls. Apply mask applies the current selection to pixel alpha; it does not create an independent editable mask layer.

## Adjust, histogram and information

Adjust offers twelve labeled icon actions. Contrast, exposure, levels, curves, shadows/highlights, hue/saturation, vibrance and white balance open the existing relevant controls; Auto tone and Black & white execute the corresponding pixel operation. Reset restores the active adjustment session. Only unlocked static raster layers are eligible. Gradient map and Color lookup remain visibly labeled but disabled with a reason because the current editing engine has no gradient-map or imported-LUT implementation.

Histogram renders the current finite-canvas composite or selected layer at the current frame, excludes fully transparent pixels, computes 256 bins per RGB channel and real mean/standard deviation, and offers channel selection, refresh, and shadow/highlight clipping overlays. Overlays do not modify document pixels. Infinite canvases report an unavailable histogram.

Information reads source dimensions, content type, actual embedded asset ID, untagged RGB and 8-bit raster precision (or vector geometry), rather than copying Figma's example profile and precision. Sample color reads a real composite pixel on the next canvas click; Copy color and Copy details use the system clipboard. Reveal source opens the saved document's directory because media is embedded in that document. Unsaved documents cannot reveal a source file.

## Validation

The hierarchy's active row must track the selected document layer after duplicate, direct document selection, visibility updates and user row activation. The Layer GUI regression checks both document IDs and native row selection; all seven footer tones and rendered background colors are checked against Figma's default and borderless states before locking, while locked and after unlocking. It also checks each existing footer asset's local file, native icon slot and rendered size.

`Dreamscapes.EditorLayers` exercises all four real editor views, duplicate/visibility controls, actual adjustment/reset, channel selection, color sampling/copy, and resizing. `Dreamscapes.EditorTools` checks exact RGB bins, read-only histogram/overlays/sample, independent duplicated pixels, merge appearance, group persistence and undo. Related editor layout, generic tool panel and canvas suites cover integration. Optional `DREAMSCAPES_CAPTURE_DIR` writes four native panel captures. Packaged startup can target the installed application with `DREAMSCAPES_TEST_APP_PATH`.
