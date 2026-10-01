# New canvas chooser

Home's **New Canvas** action and the platform **New** shortcut open `NewCanvasDialog.qml`.
The chooser implements Dreamscapes Home's Figma new-canvas boards with the installed LVRS
`LabelButton`, `MenuItem`, `InputField`, `LabelMenuButton`, and `ContextMenu`. These action
controls retain LVRS's default, hover, press, release rebound, and keyboard focus behavior.
Preset information uses the detached appearance of `ListItem.Navigation`, composed from
LVRS labels without button behavior. The modal host uses Qt Quick Controls' focus and Escape handling.

## Design and catalogue

- Figma file: `bn8O4AHKr1X9DWnhR1TgEy`, Home page `0:1`.
- [Video reference](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=302-9026).
- [Custom-size reference](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=310-1970).
- Runtime catalogue: `src/App/Views/Home/Data/canvas-presets.json`, copied from the verified
  Figma revision-4 catalogue. It contains 300 presets in 14 categories. The Social media
  category has 69 platform formats across 16 platforms, including X, Threads, Facebook,
  Bluesky, Mastodon, LINE and KakaoTalk. Counts intentionally differ by category.
- The JSON retains source URLs, check date and `documented-recommendation`/`starter`
  classification. These are starting canvas dimensions, not claims that every platform
  only accepts that exact size. Update this file and catalogue regression checks together.
- Search covers all categories and matches names, platform, category, section, native
  dimensions and pixel dimensions. `A4` returns 11 existing presets. Empty search results
  show a recovery action and disable creation.

## Layout and interaction

The modal has 12px padding on every edge, matching the Content frame of the
[LVRS ApplicationWindow](https://www.figma.com/design/0GkItQYSNIR0lZ3iJhfJzc/Layerd-Visual-Render-System?node-id=997-3).
`Main.qml` supplies its `appContent` as the available placement area. Centering, maximum
size and resize behavior respect the title-bar reserve, mobile safe area and keyboard
insets already applied to that area; compact popups no longer cover the window's top controls.

Each preset's 44px information row matches the baseline ListItem layout: 12px horizontal
insets, Body and Caption labels and a 4px text gap. It has no hover, press, release animation
or tab stop. The enclosing card owns pointer selection, accessibility and Space/Enter
activation. Selection and keyboard focus are indicated at the card boundary.
Keyboard focus scrolls the card into view after the viewport finishes layout and follows
viewport size changes. If the viewport is shorter than a card, its information row remains visible.
Figma's 17 review boards and the [supplied example](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=300-29539)
use ordinary detached frames for these information rows, with no inherited prototype reactions.

The toolbar retains search, Custom size and Close. Desktop uses the 200-pixel category
menu, a three-column preset gallery when space permits, and a 268-pixel details panel.
The gallery and details scroll independently within the window. The footer keeps selection
information, Cancel and Create canvas visible. Figma's tall review frames are not runtime
window heights. Below 1000 pixels the category dropdown replaces the sidebar; gallery
and details stack in one scrollable body, with a bounded toolbar and footer.
Gallery and detail delegates load only while the chooser is visible, so Home startup
does not instantiate a category's preview cards.

Selecting a preset supplies its width, height, unit and PPI. Editing dimensions creates a
custom selection. Unit changes convert the existing dimensions, rather than reinterpreting
the same number. Physical units support mm and inches; `round(mm / 25.4 * PPI)` produces
pixels, using nearest-even rounding at an exact half pixel to match the authored catalogue. A4 at 300 PPI is 2480 × 3508 px. Pixel dimensions must be whole numbers. Inputs
must be finite, positive, at most 32768 px on either axis and 268435456 pixels in total;
PPI must be positive and at most 2400. All 300 designed presets fit those bounds.

Tab traverses native LVRS controls and preset cards; Space/Enter activate menu and card selections.
The platform Find shortcut focuses the chooser's search, Ctrl+Return creates a valid
canvas, and Escape dismisses the chooser. Cancel preserves the Home prompt and current
editor document. Selecting a generated image still opens its original image and metadata
directly in the editor.

## Canvas document

`EditorCanvas` is a consumer adapter over `iiSharedCanvas::CanvasItem`; it creates a finite
SDK `Document` at the resolved pixel extent. White and Black backgrounds are editable
vector layers, avoiding a large initial bitmap for print sizes. Transparent documents
start without a background layer. The editor retains preset identity, physical dimensions,
PPI and background and fits the document to its available viewport. Creation succeeds
before the chooser closes. Invalid input leaves the current document intact.

This change creates an in-memory editor document. Saving projects and wiring every editor
tool to document edits are separate existing editor work; the chooser does not claim that
a new project has been persisted to Society.

## Verification

`Dreamscapes.NewCanvas` checks all 300 identities and dimensions, SNS sections, global
search, physical conversions, invalid values, and SDK document backgrounds.
`Dreamscapes.NewCanvasGui` checks desktop, compact and mobile window sizes, creation,
empty search, keyboard selection, cancellation, draft preservation, result-image routing
and the existing mobile editor toolbar. Build and native installation evidence is saved
under `build/new-canvas-verification/`.
`newCanvasPaddingAndStaticDetails` checks 12px padding, placement inside the app content,
resizing, the detached label geometry, unchanged hover/press/release appearance, and card
keyboard selection at desktop, compact, mobile and minimum widths. Evidence for this
revision is saved under `build/new-canvas-static-verification/`.
The `packagedApplicationStarts` GUI test can also verify an installed bundle by setting
`DREAMSCAPES_TEST_APP_PATH` to its `Contents/MacOS/Dreamscapes` executable. It checks the
production registrations, root loading, one window and the Society helper handshake.
