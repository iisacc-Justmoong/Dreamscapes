# Desktop Home

The desktop home implements Figma `bn8O4AHKr1X9DWnhR1TgEy`, node
`261:3148` (1374 × 2231 design reference), with the installed LVRS framework. `DesktopHome.qml`
owns presentation; `Main.qml` retains generation, storage and editor state.
`DesktopHomeToolbar.qml` occupies the native title-bar interaction reserve.
The desktop title bar is 48px high with 16px leading placement for the native
macOS traffic lights. AppKit still owns the real controls, accessibility,
fullscreen behavior and window actions. Content begins after an additional
8px separation, while the search and button region is excluded from dragging.
The explicit LVRS window move handle is confined to the top 40 logical pixels.
The installed LVRS macOS backend disables AppKit's whole-window background dragging,
so body input does not move the window through its background.

The current desktop Home begins with QuickGenerate. The inline paint canvas,
paint toolbar and canvas toggle have been removed from the composer. Reference
image attachment and generation input handling remain available.

## Layout and data

- A 181px menu sidebar uses LVRS MenuItem/MenuDivider, with Home, New Canvas,
  Image, Video, Audio, Board, Tools, Files, Assets and Generation History.
  Below 700 logical pixels it becomes a 48px icon rail with accessible names
  and tooltips. All menu rows remain 24px high.
- The scrollable body has a 24px horizontal gutter and 20px top inset, with a
  12px horizontal gutter below 700px. The 181px sidebar has no additional outer
  inset; the compact 48px rail keeps its existing accessible icon layout.
- QuickGenerate places the prompt first, followed by a 64px
  attachment row when populated, and the generation controls. Desktop spacing
  is 16px and the control row is 28px. Mobile/result composer ordering is
  unchanged. Image / 1:1 / 1 defaults and counts through 1000 are retained.
  Generation [prompt fields](PromptFields.md) wrap to the available width and grow
  with their text; the focused caret remains visible in the vertical viewport.
  Empty attachments consume no space. Adding an attachment keeps the prompt at
  the top and does not reveal a canvas. Attachment cards retain previews and
  Remove actions; canvas drag gestures are disabled in this composer.
- Five full-width sections follow with 24px gaps: Continue creating, Recent
  generations, Explore a direction, A starting point not a blank page, and
  From your workspace. File/image cards are 224px high; five cards fill wide
  rows with 16px gaps, and narrow rows flick horizontally. Vertical wheels
  scroll the body even when the pointer is over a row. Keyboard Home/End and
  horizontal touch/wheel navigation remain available.
- Three prompt starters reflow from three columns to one. Selecting a style or
  Use prompt scrolls back to the composer and fills its editable prompt without
  submitting or changing ratio, count, or attachments. Browse
  prompts opens the LVRS prompt library menu; Explore styles focuses its first
  card for keyboard selection.
- Model/VAE selection and the generation queue remain accessible under
  Generation settings; the Assets sidebar action expands and scrolls there.
- `iiSocietyContainer::DashboardFiles` supplies recent files (20), published
  items (4) and generation history (20). Search binds directly to its `query`,
  so filtering happens before limits and includes older matching files.
  The SDK's asynchronous scan and file watches update the UI after changes.
- Images and filenames come from Society. The sample recent files, generations, and
  publications in Figma are data placeholders. Five curated style cards use the
  four original Figma SVG illustrations, with the fluid SVG reused in its two
  design slots.
  Loading, empty results and storage errors are visible.

## Interactions

Home resets scroll. Files scrolls to recent files; Assets scrolls to the Society
model/VAE/queue area. Image and Video focus the existing prompt and preserve its
draft. Canvas and Tools enter the existing CanvasEditor. Audio and Board report
that generation/creation is not available yet. Video retains its existing
unsupported-generation notice at submission.

Accepted QuickGenerate submissions immediately open the shared result screen,
including submissions with reference images. The composer owns a synchronous
`submitting` flag while it emits `generateRequested`; the shell uses this origin
to route image-reference recipes without confusing them with the advanced
workspace's own submissions. The result screen tracks every accepted job in the
batch before model loading or inference begins. The advanced workspace retains
its own canvas and progress presentation.

Image cards open the existing canvas with the selected image. Other file types
and View all files/publications open Society. History uses the shared
`society://generation-history` SDK route. Failed Society launches surface an
error. Account opens an Open Society / Society drive preferences menu; account
identity remains owned by Society. Notifications show real generation-job
states, or an empty state. Find focuses search. No remote account or publication
API is invented by the home view.

## Assets

`Assets/Desktop/*.svg` are unchanged exports from this Figma node. The home
icon is 16 × 16 inside the 18px menu slot; Files is the 13.375 × 15.6659 database
sublayer inside its 18px slot. All other sidebar and toolbar icons are 18px;
search is 12px; publication spark/chevron are 24px. New Canvas uses an unchanged SVG export of the Figma `imagefitContent` instance.
Style previews in `Assets/Directions/*.svg` are unchanged 216 × 224 exports
from node `261:3255`, packaged in QRC and used as previews in the corresponding
LVRS File cards. All local files are non-empty. No temporary Figma asset URL is
used at runtime. Recent/published/history imagery stays data supplied.

## Verification

`DreamscapesGuiTests desktopHomeSidebarReflowsAndRoutes` checks wide and narrow
layouts, native control placement, title-bar and content clearance, horizontal
gutters, section geometry, media switching, editor routing and draft
preservation. `desktopHomeSearchesSocietyAndUpdates` checks
all three section limits, case-insensitive matching beyond the first 20 files,
no-result state, file creation/deletion refresh, history routing, Audio feedback,
and both toolbar menus. `desktopHomeRendersFigmaFrame` checks every visible
exported icon's loaded state and effective dimensions, and captures the 1374 ×
720 frame using either the software renderer or the native macOS renderer.
`desktopHomeContinuousRowsAndPromptStarters` verifies section order, full-width
geometry, prompt-first ordering at desktop/compact widths, the absence of canvas
tools and toggle, attachment addition/removal and forwarding, real empty states,
style preview readiness and prompt-only actions.
`Dreamscapes.DesktopHome` groups the Home regressions; `Dreamscapes.QuickGenerate`
checks sliders, native image drop/painting/color, compact composer fit and
existing result/mobile behavior. `homePromptDragDoesNotMoveWindow` verifies the 40px
move boundary and Home prompt text selection with no window movement at desktop and compact
widths; `viewsLeaveWindowChromeAvailable` retains the title-bar exclusions and view clearance.

`referenceGenerateOpensResultImmediately` clicks Generate with an attached image
for one and three outputs, verifies immediate result-screen routing and all batch
IDs, and checks the 3:4 output dimensions. A native callback fixture receives the
original attachment pixels even after its source file is deleted, then publishes
all expected PNGs. It does not run a diffusion model. The existing advanced
workspace regression verifies that its submissions remain in their own view.

Set `DREAMSCAPES_DESKTOP_HOME_CAPTURE` to a PNG path to capture the tested home.
The fixture image cards are intentionally synthetic Society files; they verify
the data path without consuming generation resources or altering user files.
Build and evidence outputs are under `build/`.

GUI tests explicitly inject the existing process-protocol fixture through the
GenerationController constructor. Production macOS native inference defaults
must not attempt to interpret the fixture's dummy model bytes as real weights.
This injection is confined to the test QML registration; production generation
selection and model validation are unchanged.
Reference-image routing tests temporarily override that registration with a
native callback fixture, restoring the default runtime when the test ends.
