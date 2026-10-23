# Video generation workspace

## 비디오 준비 진행률과 모델 자산 캐시

LTX 패키지의 임베디드 텍스트 인코더를 준비할 때 `preparing-model`의 실제 완료량을 받고, CPU 그래프 완료는 `computing(step > 0, total = 0)`으로 받는다. 변경된 진행률만 기존 정지 감시 시간을 갱신하며 동일한 진행률 반복이나 telemetry heartbeat는 정지 판정을 피하기 위한 진전으로 사용하지 않는다. SDK 준비 카운터 단위는 MiB이다. 감시 시간 초과 오류는 마지막 단계·완료량과 진단 파일 경로를 보존하며, 파일 경로가 없으면 최근 worker 출력으로 원인을 확인할 수 있게 한다.

비디오 `--cache-dir`은 `Models/.society-runtime/iiLocalDiffusion/cache/video`의 지속 경로이다. 해시로 식별하고 검증한 임베디드 인코더를 작업 완료·실패·취소 뒤에도 재사용한다. 작업별 출력·프리뷰·스토리보드와 그 임시 디렉터리의 정리 규칙은 유지한다. 캐시가 원본 모델이나 사용자 편집 내용을 대체하지 않는다. `videoPreparationProgressRenewsDeadline`은 준비·실제 그래프 진행·반복 알림 정지 판정과 작업 간 캐시 재사용을 검사한다.

진행 감시 검사는 출력 버퍼 확인과 만료 주입으로 판정한다. 준비·CPU 배치·샘플 단계가 증가하면 감시 타이머가 갱신되고, 같은 완료량의 heartbeat는 실패해야 한다. 정상 진행 경로는 그 판정 이후 취소하여 정리 상태를 확인한다. 비동기 결과 게시나 외장 저장소 속도를 진행 감시의 성공 조건에 포함하지 않으며, 제품의 15분 무진전 제한은 변경하지 않는다.

정상 비디오 큐 검사는 24·60·120 FPS 각각 두 출력과 참조 이미지의 소유권, `.iisc` 저장 및 생성 기록 게시를 확인한다. 이 검사는 저장소 처리 속도에 대한 성능 검사가 아니므로 비동기 게시 완료를 최대 60초 기다린다. 실행 단계의 정지 감시 시간과는 별개이다.

Reference display is independent of generation input preparation. The monitor,
shot references and image-key cards build local display URLs through CanvasPreview;
native IISC composition occurs on the asynchronous image provider. Displaying or
editing a key's position/weight does not prepare a PNG in Society's runtime tree.
Submitting a recipe still captures actual local PNG inputs through the existing
generation preparation path. Width, height, model and all recipe fields retain
their existing semantics.

The desktop sidebar's **Video** entry opens a dedicated, retained workspace for precise video generation parameters. Its layout follows the canvas and timeline from Figma `464:9774` and the generation inspector from `498:11999` in the existing Dreamscapes window. Design examples are not templates or initial draft data. Home's QuickGenerate and mobile Video shortcut retain their existing queue/result path.

## Layout

The workspace separates a left preview/timeline column and a full-height 402px inspector, with independent parameter scrolling and fixed generation actions. View controls and status use the compact native LVRS presentation; all user-defined tracks and generation parameters retain their existing ownership.

All text, controls, menus, field materials, colors, spacing and interaction feedback reuse the installed LVRS package. A new workspace has a blank preview and empty shot/keyframe collections, no selection, and a playhead at F000. Preview images and shot thumbnails only use user-selected image conditions or actual generated output. No design sample image is displayed.

## Editing and generation

`VideoTimelineState.qml` starts with an empty track list. The gutter **+** menu adds a named Layers, Images, Audio or Caption track (internal types Shots and Contents are retained). Tracks stay in that type order; position controls reorder tracks within their type. Shots/image keys reference stable track IDs, and image conditions are never distributed by a fixed numeric lane. Track edits and deletion participate in undo/redo. Audio/Caption rows have no initial placeholders.

`VideoTimelineState.qml` owns shot boundaries, selection, excluded/locked state, per-shot prompts, image keyframes, frame numbers, image condition weights, undo and redo. The empty timeline's ruler reflects duration × FPS without creating shots. Generating from an empty shot collection uses the composition prompt, duration, FPS and explicitly added image conditions in one internal generation segment; submission never inserts that segment into the draft. Only **+ Layer** creates a visible shot, and its first shot spans the current generation duration. User-added kept shots are concatenated in timeline order. Splitting, trimming, rolling a boundary, sliding a shot, duplicating and ripple deletion change that draft. Changes are bounded to at least two output frames per shot. Deleting the final shot returns to an empty timeline. Keyboard controls include V/B, frame arrows, I/O, Space and undo/redo. The Generation/Shot inspector tabs share the selected shot. Image conditions can be chosen at the first, last or current frame, dragged, nudged, weighted, copied, pasted and deleted; their initial strength is the runtime default of 1.0.

The inspector exposes width/height, duration/FPS, aspect ratio, count, camera motion, steps, CFG, seed, interpolation, temporal decoding, execution and H.264 quality/preset. Defaults match `normalizeVideoRecipe`: 1024 × 576, 5 seconds, 24 FPS, one output, 30 steps, CFG 3 and a random seed (`-1`), with the existing decoding/execution/encoding defaults. Prompt and negative prompt start empty unless the user configured defaults in Preferences. No example negative prompt or fixed seed is injected. Camera motion defaults to None and uses the SDK's text-conditioning path; no Camera/Zoom track is added. Width and height must be divisible by 32; invalid fields block submission. Duration/FPS resize the draft's frame extent even when it contains no shots. **Reset** restores generation defaults and clears tracks, shots, keyframes, selections, clipboard, undo/redo, playback and timeline zoom. The workspace has parameter presets, but no template or saved-recipe controls and does not read or write the former `VideoGeneration/savedRecipe` setting. Existing saved settings are left untouched. Navigation retains user edits after first opening.

`GenerationController::enqueueVideoRecipe` validates a complete immutable submission, copies every image condition to Society's GenerationInputs, and creates the existing serial queue. The controller writes an SDK storyboard into its job workspace with kept shots, shot prompts, per-shot frame counts, local image conditions and weights. It forwards every exposed runtime parameter to the installed iiLocalDiffusion video launcher. Queue ownership stays in the Video workspace, so generation does not navigate to the Home result screen. All requested output variations stay selectable and playable in the preview; cancellation uses the existing queue cancellation and **Save video** exports the verified MP4 bytes.

Save video captures the selected result when opening the system dialog. Cancelling
does no export; choosing another result before accepting does not change the
captured source. The worker-owned export keeps UI input available and exposes
busy/pending/completed/error feedback. The accepted copy can finish after its view
closes without retaining that view or opening/synchronizing a Society drive.
`videoWorkspaceRoutesEditsAndGenerates` checks cancellation, source capture and
the actual exported bytes after selection changes with the dialog open.

Audio and Caption rows exist only after the user adds tracks and show empty states. Audio mixing, caption rendering and text overlays are not implemented by this generation workspace. The weight view displays image condition anchors; arbitrary curve interpolation is not sent to the LTX SDK.

## Verification

The Video workspace uses `VideoTimeline.qml` and `VideoTimelineState.qml` for
its generation recipe. It retains empty generation defaults and user-defined
Shots/Contents/Audio/Caption tracks. The editor's finalized Figma design
`1065:90081` is implemented separately by `EditorTimeline.qml` and
`EditorTimelineModel`: its **Layers** rows read and edit the actual native IISC
canvas rather than a generation draft. The two consumers share LVRS controls
and fixed button-state policy, while preserving these different data contracts.
See [EditorLayout.md](EditorLayout.md#bottom-timeline).

Frame-rate controls in this workspace and Home QuickGenerate offer 12, 24, 30, 60 and 120 FPS; the default remains 24 FPS. Both submission APIs accept the same set, including 30 seconds at 120 FPS. An FPS edit changes the empty timeline's frame extent without adding shots or keys. The interpolation factor is a minimum: iiLocalDiffusion automatically increases source-frame spacing for high output rates, limits current LTX source generation to its nominal 25 FPS policy and interpolates to the requested final FPS. The UI forwards the request and does not implement interpolation. SDK planning, local/remote request and real FFmpeg media regressions cover 60/120 FPS, anchor preservation and unchanged output timing; product queue and GUI regressions cover both controls and final FPS metadata.

The fitted preview frame stays centered horizontally and vertically inside the preview surface, independently of the inspector and timeline. The workspace regression checks 1:1, 16:9, 9:16, 4:1 and 1:4 output ratios across four window sizes, including the narrow scrolling workspace, and verifies both centering and containment. `DREAMSCAPES_CENTER_CAPTURE_DIR` captures these ratio variants in an existing directory under `build/`.

Invalid numeric edits block generation. **Reset** clears the field error and restores the displayed value; the workspace regression test exercises the visible reset button.

Disabling **Random seed** requires an explicitly entered nonnegative fixed seed when the draft still has the random sentinel `-1`. The UI marks that seed invalid and blocks submission instead of inserting a fixed value or continuing to generate randomly. Re-enabling random seed clears that requirement. After user shot edits, duration/FPS changes also update the generation extent used when the last shot is deleted, even if the requested frame count already matches the edited shots. Undo/redo keeps duration, FPS and that frame extent synchronized.

`Dreamscapes.Video` runs the retained Home video queue/player/export path, `videoWorkspaceStartsEmptyAndUsesGenerationDefaults`, `videoInspectorStaysWithinViewport` and `videoWorkspaceRoutesEditsAndGenerates`. The empty-state regression verifies blank previews and collections, absence of recipe-saving controls, default parameters, duration/FPS edits without inserting shots, explicit image conditions without shots, deletion of the final shot, undo/redo, complete default reset and a normal generation submission from an empty timeline. The workspace test opens the real sidebar entry, asserts the bounded inspector width and measured timeline height, clipped single-row timeline controls, independent scrolling and aspect fit, explicitly adds its editing fixture shots, exercises split/undo and image frame/weight editing, checks draft retention and resized windows, and verifies kept-shot/image-condition values in the accepted queue. It then runs the deterministic process fixture to produce and decode MP4, checks workspace result ownership, plays the output and records optional screenshots via `DREAMSCAPES_CAPTURE_DIR`. Fixture output proves request and UI plumbing, not learned LTX image quality.

`videoRecipeValidatesRuntimeContract` checks dimensions, numeric types and limits, enum validation, duplicate/out-of-range image frame anchors and shot-count/frame-count constraints. Existing video queue tests continue to verify immutable first-frame copying, bad output rejection and cancellation. A real installed LTX model invocation remains a distinct runtime smoke check.

The inspector containment regression covers 280 × 320, 300 × 740 and 402 × 740 panels. It measures every visible parameter row against the viewport, scrolls to the final encoding control, and verifies that geometry changes leave the parameter snapshot unchanged. No inference job is started by this check.

## Atomic composition edits

Duration and FPS edits now validate a complete shot/keyframe plan before changing
the draft or its undo/redo stacks. Every shot retains its minimum frame count;
proportional rounding reserves enough frames for all remaining shots. A six-shot
composition can therefore shrink to twelve frames with two frames per shot.
Insufficient duration, non-finite counts/rates and a nonpositive rate produce an
explicit timeline error without changing the draft or creating a Redo entry.

Image-condition anchors remain distinct when a composition is resized. If
rounding would put two anchors at the same frame, the edit is rejected while
preserving both conditions. The parameter panel retains its last accepted
values, marks the attempted edit invalid and blocks submission. A valid retry
clears that field error. Resizing does not add/drop conditions or enqueue work.

The previous shrink failure checkpointed a partially changed composition and
then called Undo, exposing that invalid state through Redo. Parameter fields also
accepted the edit regardless of the timeline result. `Dreamscapes.VideoComposition`
exercises the actual workspace/panel/state chain for insufficient duration,
colliding anchors, NaN counts/rates and zero FPS. It verifies unchanged draft
bytes and history on rejection, recovery, exact minimum shot allocation and
valid Undo/Redo. This supplements the native recipe validation and existing full
Video fixture; it does not invoke a user's inference model.


## Compact Figma layout (2026-10-23)

The reference is [Video workspace 464:9774](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=464-9774) and [all parameters 498:11999](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=498-11999). At the reference window size the 402px inspector spans the content height. The preview, its 46px view/playback bars, 28px status strip and 264px timeline occupy the remaining left column. The generation summary, Reset and Generate remain outside the inspector's independent scroll viewport. Narrow windows retain horizontal workspace scrolling; below 360px inspector width the paired controls become one column without dropping parameters.

Width/Height, Duration/Frame rate and Aspect ratio/Output count share rows. Model, preset and execution selectors use their available row width. Compact PanelRow spacing is opt-in and leaves every other panel's default density intact; LVRS metrics, fonts and control rendering remain native. Aspect changes use the SDK's exact 32px-grid sizes, while direct dimension edits remain supported and display Custom when appropriate. All sampling, image, execution and output controls remain reachable through scrolling. Numeric fields retain the raw text while focused and restore their display units on blur; valid prefixes such as 32 must not prevent typing 320, and fractional controls must retain the decimal point during editing. The native keyboard regression covers typing and focus transitions.

Camera motion 1/2/3 select supported SDK movements. None slots are omitted from the submitted camera list. Neutral/static, duplicate and opposing combinations are rejected before queueing. The normalized camera list is retained in every submitted storyboard shot and consumed by iiLocalDiffusion's existing effective-prompt path. This is text-conditioned motion rather than a guaranteed physical camera trajectory.

Select preset and Save to preset persist only generation controls and the selected model. They exclude timeline shots, image keys, file references, jobs and playback state. The catalog uses the generation-preferences path plus `.video-presets.json`, with schema `dreamscapes.video-presets/1`, at most 100 names and an 8 MiB limit. A blank prompt can be saved; it does not become an invented prompt on load. Loading resizes the current composition transactionally, preserves the existing tracks/keys, and does not enqueue inference or create a file/document revision. Injected controllers without a preferences path use an in-memory catalog.

The timeline opens empty with Layers/Keyframes/Curves, range controls, frame snap interval, linked selection, zoom and Fit. Mode buttons retain the native label width so Keyframes does not elide at the reference window size. Its underlying generation recipe retains the existing Shots vocabulary. + Layer explicitly creates a generation shot; the gutter + menu creates user-selected track kinds. Editing tools and track controls appear only after the user creates content and stay on clipped single rows. Ruler, playhead, clips and empty-lane grid share a frame pitch of 114.6666666667/10 pixels at 100%. Tick and grid delegates cover only the visible range and adapt their step when zoomed out, so long/high-FPS drafts do not instantiate one visual per document frame. Zoom, Fit and ruler navigation do not add undo entries or enqueue work. The original Figma SVG icons render at 16×16 and the playhead marker at 22×26, with local resource paths; the reference photograph is dynamic user/output content and is never inserted as sample data.

`Dreamscapes.Video` measures paired-row geometry, full-height inspector separation, sticky-action containment, parameter coverage at 280/300/402px, aspect updates, camera/preset round trips with no inserted shots or jobs, scrolling to the output controls and actual fixture generation/export. `videoPresetsKeepOnlyGenerationControls` checks persistence, Unicode paths, reference exclusion and rejected camera combinations. The recipe-contract regression verifies camera validation and storyboard propagation. Fixture checks do not run the installed 22B model.

The video GUI fixtures allow 30 seconds for queued outputs and asynchronous export completion on external storage. They wait for success or failure, report export errors, and still require captured-source byte equality. The two complete video flows can run in separate processes with the signed application frameworks in `DYLD_FRAMEWORK_PATH` and `DYLD_LIBRARY_PATH`; this verifies the deployment runtime without carrying singleton state between scenarios. These budgets are test orchestration limits, not inference performance guarantees.

Picker fixtures create their source image outside the watched Society drive and import it through the owner API; export targets model the system picker's external save location. This avoids a direct fixture write racing the drive index. The native IISC destination, original-source removal, immutable condition copy, captured result selection and exported MP4 byte comparisons remain asserted.
