<a id="desktop-home"></a>

# Desktop Home

The desktop Home implements [Dreamscapes Figma window 261:3148](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=261-3148), read on 2026-10-19 at 1374 × 1026. Its purpose is fast content creation: one prompt workspace, an operational queue summary, recent files, and recent generations. The default desktop launch size remains 1920 × 1200; mobile remains 390 × 844.

`DesktopHome.qml` owns layout and navigation. `QuickGenerate.qml` owns the shared draft and submission boundary; its retained `DesktopGenerationComposer.qml` presents the desktop controls. `Main.qml` connects these controls to the existing generation, storage, and editor services. LVRS is used at its installed defaults: text styles, control heights, icon sizes, and File card geometry are not rescaled or restyled by Home.

<a id="layout-and-data"></a>

## Layout and data

- The shared application title bar is 32 logical pixels high, with native macOS traffic lights and search, notification, and account controls. Home, Image, Video, and Editor use the same title bar and content origin so changing views does not shift the sidebar. The explicit window movement handle occupies this strip; text and canvas drags remain content interactions.
- The sidebar retains its 181-pixel width, 8-pixel insets, 24-pixel rows, colored icons, and selected Default-button state. Below 700 logical pixels it becomes the existing 48-pixel icon rail. Keyboard navigation, accessible names, tooltips, and scrolling remain available.
- The Home viewport has 24-pixel horizontal and 20-pixel top insets, with 20 pixels between sections. Horizontal insets become 12 pixels in compact windows.
- The generation panel has a 300-pixel minimum height on wide layouts (328 pixels with the installed native editor), 16-pixel padding, and a 288-pixel settings rail. Below 820 pixels of available panel width, settings move below the prompt. The viewport scrolls vertically; mode, reference, and footer rows scroll horizontally when necessary. Controls retain their native sizes.
- The prompt uses the stock LVRS TextEditor: an editor at four native medium-control heights (144 pixels in the installed macOS theme) with native internal scrolling, selection, clipboard, composition, and undo behavior. Its header supplies Clear. The Image/Video/Audio/Board buttons use the Default type, including their selected state.
- References use actual image or `.iisc` attachments, with previews, names, sizes, and removal. They are owned by the existing HomeCanvas input service. No Figma sample attachment or unsupported PDF input is shipped as real content. Paint canvas opens the normal new-canvas flow; it does not expose an inactive inline paint surface.
- Model, aspect ratio, resolution, batch size, and seed feed accepted generation requests. Image resolutions use a selected short edge (512, 576, 768, 1024, or 1536), quantized to eight pixels. Video retains the supported ratio-specific dimensions and its duration/FPS draft. Seed defaults to random; explicit seeds must leave room for the batch without exceeding 32 bits.
- Negative prompt is editable in a native popover. Untouched values follow model-preference defaults; edited values are preserved. Custom image resolution, negative prompt, and loaded preset values use the validated advanced request path. Image references are copied into immutable job-owned `.iisc` inputs before a job is accepted. Legacy callers retain model-derived sampling defaults.
- Load preset and Save preset use the existing AdvancedImageParameters preset store and validation. Presets contain generation settings, not volatile attachment snapshots. Advanced parameters transfers the current draft into the existing Image or Video workspace. Home and Result retain their respective presentations while sharing the prompt, ratio, count, and reference state.
- `HomeQueueBar.qml` counts actual running/connecting/downloading and queued jobs. View queue expands and scrolls to the existing operational queue, including its cancel/details controls. Completed and failed jobs are not counted as waiting.
- Recent files and Recent Generations are the two visible galleries. Both use stock LVRS Medium File cards (210 × 240), with up to 20 real entries from the Society mirror. Five cards fit the design reference width; narrower windows scroll the row horizontally. Vertical wheel input continues scrolling Home, and keyboard Home/End and activation are retained.
- `.iisc` previews come from CanvasPreview/iiSharedCanvas. Other previews, names, and dates come from actual storage records. Loading, empty states, and errors remain explicit. Figma artwork and example filenames are not bundled as application data.

The previous inspiration styles, prompt starters, and published gallery are absent from Home. Published records remain available through existing storage/search services. Additional VAE/model-storage controls remain in the expandable Generation settings area and through Assets.

<a id="interactions"></a>

## Interactions

Generate submits through the existing QuickGenerate acceptance boundary. The result view opens immediately after a batch is accepted, before loading or inference completes; every batch ID remains tracked. Model defaults and the ordinary request route are retained when no custom advanced values are set. Audio and Board report their current unsupported status while preserving the draft.

Result retains its own composer viewport height while the shared draft is presented in Home or Editor. The taller desktop panel cannot resize a hidden result gallery and clamp its scroll position; returning from editing preserves the inspected page.

All files and Generation history open Dreamscapes' internal browser, with the full mirrored collection rather than Home's recent-item limit. Files, including unavailable aliases, use the existing direct container download/open route. File cards open real documents; generation selection can open multiple results as separate canvases. Explicit Society/account navigation remains available but is not required for local browsing.

Search filters the storage snapshot before recent-item limits are applied, so older matching files remain discoverable. Creation/removal refreshes through the SDK's asynchronous file watcher. Notification/account menus, shortcuts, new-canvas flow, and Editor return navigation retain their existing services.

Application icons and search results use stock `LV.Tooltip` hover help, shared
with Editor. See [Tooltips](Tooltips.md) for the LVRS design reference and the
regression coverage that prevents Qt default tooltip call sites.

<a id="assets"></a>

## Assets

Existing Figma-exported sidebar and application icons are reused alongside installed LVRS icons. The queue's six-pixel status dot is the original Figma SVG, saved locally as `Assets/queue-ready.svg` and packaged as a resource. Runtime does not use temporary Figma asset URLs. No sample gallery or reference images are packaged.

<a id="verification"></a>

## Verification

`Dreamscapes.DesktopHome` checks the section order, native control/card geometry, responsive layout, retained drafts, preset round trips, attachment parameters, queue counts, sidebar navigation, full-snapshot search, file-watch updates, empty states, and rendered previews. `referenceGenerateOpensResultImmediately` additionally submits custom resolution, negative prompt, seed, and a batch with an image reference through the real shell/controller boundary, verifies persisted parameters, removes the source, and receives the original pixels in a native callback fixture.

`Dreamscapes.QuickGenerate`, `Dreamscapes.LiveGeneration`, `Dreamscapes.HomeCanvasPreviews`, `Dreamscapes.Preferences`, `Dreamscapes.Video`, `Dreamscapes.EditorSidebar`, and `Dreamscapes.PromptFields` cover the shared routes and control behavior. These deterministic fixtures do not run diffusion weights or consume production generation jobs.

For visual evidence, `DREAMSCAPES_DESKTOP_HOME_CAPTURE` saves the tested window at the reference size. `DREAMSCAPES_DESKTOP_HOME_CONTENT_CAPTURE` captures its content. Fixtures use isolated Society directories under `build/`, preserving user files. Build, test, packaged startup, installation, and a running user's adoption of the new bundle are separate evidence levels.
