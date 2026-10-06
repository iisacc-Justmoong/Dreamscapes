# Video generation workspace

The desktop sidebar's **Video** entry opens a dedicated, retained workspace. It implements the canvas and timeline from Figma `464:9774` and the generation inspector from `515:89044` in the existing Dreamscapes window. Home's QuickGenerate and mobile Video shortcut retain their existing queue/result path.

## Layout

The workspace uses a 16 px inset, a preview column, a fixed 300 px inspector, and an independent 406 px timeline below both columns. The inspector scrolls vertically without moving the preview or timeline. The preview fits one scale to the selected output dimensions, preserving aspect ratio. Large workspaces preserve the design's 116 px gap between preview and inspector; smaller workspaces use 24 px. Narrow windows scroll a minimum 720 px workspace rather than stack or overlap the inspector. A short window scrolls a minimum 660 px workspace. The timeline's 1568 px minimum editing surface scrolls horizontally; its 184 px track headings and 8 px gutter remain aligned with the ruler.

All text, controls, menus, field materials, colors, spacing and interaction feedback reuse the installed LVRS package. The original coastal architecture asset is local and registered as a Qt resource. It is labeled as a reference preview and is replaced by selected keyframes or actual generated output. It is never reported as a generated result.

## Editing and generation

`VideoTimelineState.qml` owns shot boundaries, selection, excluded/locked state, per-shot prompts, image keyframes, frame numbers, image condition weights, undo and redo. The initial four shot ranges preserve the design's F000–023, F024–047, F048–083 and F084–119 boundaries; the final shot is excluded. Kept shots are concatenated in timeline order. Splitting, trimming, rolling a boundary, sliding a shot, duplicating and ripple deletion change that draft. Changes are bounded to at least two output frames per shot. Keyboard controls include V/B, frame arrows, I/O, Space and undo/redo. The Generation/Shot inspector tabs share the selected shot. Image conditions can be chosen at the first, last or current frame, dragged, nudged, weighted, copied, pasted and deleted.

The inspector exposes width/height, duration/FPS, count, steps, CFG, seed, interpolation, temporal decoding, execution and H.264 quality/preset. Camera motion parameters and a Camera/Zoom track are omitted. Width and height must be divisible by 32; invalid fields block submission. Duration/FPS resize the draft's frame extent. Explicit **Save recipe** stores parameters and the timeline snapshot in the application settings; **Saved recipe** restores it. Navigation retains the unsaved draft after first opening.

`GenerationController::enqueueVideoRecipe` validates a complete immutable submission, copies every image condition to Society's GenerationInputs, and creates the existing serial queue. The controller writes an SDK storyboard into its job workspace with kept shots, shot prompts, per-shot frame counts, local image conditions and weights. It forwards every exposed runtime parameter to the installed iiLocalDiffusion video launcher. Queue ownership stays in the Video workspace, so generation does not navigate to the Home result screen. All requested output variations stay selectable and playable in the preview; cancellation uses the existing queue cancellation and **Save video** exports the verified MP4 bytes.

Audio and Caption rows preserve the design's track layout and show empty states. Audio mixing, caption rendering and text overlays are not implemented by this generation workspace. The weight view displays image condition anchors; arbitrary curve interpolation is not sent to the LTX SDK. The coastal reference is a design sample, not an automatically submitted image condition.

## Verification

The fitted preview frame stays centered horizontally and vertically inside the preview surface, independently of the inspector and timeline. The workspace regression checks 1:1, 16:9, 9:16, 4:1 and 1:4 output ratios across four window sizes, including the narrow scrolling workspace, and verifies both centering and containment. `DREAMSCAPES_CENTER_CAPTURE_DIR` captures these ratio variants in an existing directory under `build/`.

Invalid numeric edits block generation. Both the default preset reset and saved recipe restoration clear the field error and restore the displayed value; the workspace regression test covers both paths.

`Dreamscapes.Video` runs the retained Home video queue/player/export path and `videoWorkspaceRoutesEditsAndGenerates`. The workspace test opens the real sidebar entry, asserts the 300/406 px geometry, independent scrolling and aspect fit, exercises split/undo and image frame/weight editing, checks draft retention and resized windows, and verifies kept-shot/image-condition values in the accepted queue. It then runs the deterministic process fixture to produce and decode MP4, checks workspace result ownership, plays the output and records optional screenshots via `DREAMSCAPES_CAPTURE_DIR`. Fixture output proves request and UI plumbing, not learned LTX image quality.

`videoRecipeValidatesRuntimeContract` checks dimensions, numeric types and limits, enum validation, duplicate/out-of-range image frame anchors and shot-count/frame-count constraints. Existing video queue tests continue to verify immutable first-frame copying, bad output rejection and cancellation. A real installed LTX model invocation remains a distinct runtime smoke check.
