# Tooltips

Dreamscapes uses the installed `LVRS 1.0` `LV.Tooltip` component for hover help.
The visual reference is [LVRS Tooltip, Figma 997:13](https://www.figma.com/design/0GkItQYSNIR0lZ3iJhfJzc/Layerd-Visual-Render-System?node-id=997-13),
read on 2026-10-19. The stock component supplies a rounded body and directional
tail, Glass 25 material, theme color, opaque text, and placement within the
window. Applications do not override its material, font, geometry, or scale.

The LVRS origin-visibility check also distinguishes title-bar controls from
the content safe area. A visible chrome button remains a valid origin while
the tooltip body stays within the content viewport. The regression fixture
keeps offscreen controls within the virtual display and verifies native title
bar placement separately on Cocoa/Metal.

LVRS also keeps dismissal latched while its closing overlay restores hover to
the underlying control. Escape must stay dismissed while the pointer remains
at the same button, rather than reopening after the next show delay.

Stock LVRS also preserves the target button's own press handling. The tooltip
body can host interactive content, while its decorative tail passes clicks to
the originating control. Regression coverage includes clicks before and after
the tooltip appears, in both Qt and LVRS windows and in the shared app toolbar.

## Integration

Create a local `LV.Tooltip` with an explicit `target` and `text`. The default
automatic interaction provides delayed hover, keyboard focus, cancellation
before the delay, Escape dismissal, and nonmodal input. Derive its text from the
target's accessible name or its current data. Do not use `Controls.ToolTip.text`
or `Controls.ToolTip.visible`: those attached properties create Qt's shared
style tooltip even when attached to an LVRS button; they do not use `LV.Tooltip`.

The shared application toolbar covers Home, generation results, and Editor.
Search results, compact inspector tabs and reset, timeline tools, paint tools,
photo tiles, and layer actions use the same component. A layer action has a
single tooltip for its label or unavailability reason. Its enabled tile hosts
help when its button is disabled, preserving the reason on hover without
displaying two overlapping bubbles. Existing mask, transform, video validation,
and compact sidebar tooltips keep their contextual visibility rules.

## Verification

Build with `cmake --build build --target Dreamscapes DreamscapesGuiTests -j4`.
Run `ctest --test-dir build -R '^Dreamscapes.Tooltips$' --output-on-failure`.
The GUI tests verify actual pointer entry, pending-hover cancellation, LVRS
material and outline, target anchoring, Escape dismissal, and account-menu
activation. A source contract rejects Qt shared tooltip call sites throughout
the application QML. Editor panel and timeline suites exercise the remaining
hosts; the LVRS SDK owns the tooltip's general geometry and interaction tests.

For native visual verification, run `DreamscapesGuiTests
applicationTooltipsUseLvrs` with `QT_QPA_PLATFORM=cocoa`, `QSG_RHI_BACKEND=metal`,
and `DREAMSCAPES_CAPTURE_DIR` pointing below `build/`. The fixture uses an
isolated Society container and does not interact with production documents.
Installed-bundle startup and a user's already-running process are separate
verification levels.
