# Advanced image parameters and desktop workspace

Design source: [Dreamscapes / Image Generation · Advanced · Expanded](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=59-229).

## View-independent parameter contract

The parameter API can be used without constructing a QML view. The current
`59:229` request is limited to parameter functionality; no new view or redesign
is required. Existing workspace details below describe the checkout's separate
UI integration, not a prerequisite for editing or persisting parameters.

The scalar schema covers Essentials, Composition, References, Sampling,
Fine-tuning, Enhancement and Output. References, ControlNet entries and LoRAs
are ordered collections with explicit add/edit/remove operations. A complete
snapshot can be saved as a preset and restored, including inactive settings.
Draft validity and executable backend capability are intentionally distinct:
unsupported native options remain editable/persistable, but `submissionIssues`
and the queue reject them instead of silently ignoring a requested effect.

IP-Adapter now has native resource preparation and SDK/product forwarding.
Pose additionally has a minimal native DWPose path when built with
`IILD_ENABLE_POSE=ON`: select the inline YOLOX detector and DWPose `.onnx` resources
as an atomic pair. Canceling either picker leaves the draft unchanged. These
`poseDetector`/`poseModel` fields are persisted, snapshotted into the queue and
resolved before execution; old presets default them to empty. Runtime sessions
remain loaded until explicit release or application teardown. Reset clears both.
Depth and other detectors beyond Canny/Tile/Pose, transparent-background generation, face restoration
and safety classification also remain unavailable on this native route.
Their editable fields are not a claim of finished inference functionality.

The latest requested scope is minimum operation only. Further detector families,
trained-model quality validation and detailed behavior/performance tuning are
deferred. Fixtures verify contracts, not the quality of generated artwork.

### Minimum-operation verification (2026-09-29)

The updated parameter and generation targets rebuilt; both suites passed
(14 parameter and 64 generation checks including setup/cleanup, two opt-in
real-model checks skipped). The new queue fixture verifies canonical Pose
resources reach native controls and later draft edits cannot change the queued
request. Logs: `build/advanced-minimum-build.log` and
`build/advanced-minimum-tests.log`. No installed-app replacement is part of this
minimum completion. Detailed behavior and learned-model quality remain deferred.

Canonical macOS app packaging and GUI-test build also succeeded. The selected
GUI run passed all three behavior cases (five passes including setup/cleanup):
basic advanced controls/picker cancellation, draft retention/scrolling, and real
packaged entry-point startup with one window. Log:
`build/advanced-minimum-gui.log`. Offscreen tests report missing fixture-file
selection warnings and an unavailable native menu warning; they do not validate
native menu presentation. The installed `/Applications` app was not replaced.

### View-independent edit verification (2026-09-29)

The ControlNet no-op edit regression was reproduced before the fix: an empty
patch cleared an already-applied control. Typed comparison now preserves Apply
for identical values without weakening strict type validation or preventing
required-source removal in a draft. No view or installed app was changed.

`DreamscapesAdvancedParametersTests` and `DreamscapesGenerationTests` rebuilt
successfully. CTest passed both suites in 39.32 seconds (14 parameter test passes,
63 generation test passes, including QtTest setup/cleanup; two opt-in real-model
tests skipped). These are contract/fixture checks, not proof of real-model image
quality or generation speed. Logs: `build/advanced-backend-contract-red.log`,
`build/advanced-backend-contract-build.log`, and
`build/advanced-backend-contract-tests.log`.

The desktop workspace follows Figma node `237:5543`: selecting sidebar Image
retains the sidebar and toolbar, replaces the home content with a central image
canvas and a 402-unit right parameter column. `AdvancedGenerate.qml` owns an
independent `AdvancedImageParameters` draft. Its vertical viewport scrolls without
moving the canvas or pinned Generate/Reset/Cancel controls. Narrow windows have
a horizontal workspace viewport instead of clipping parameter controls.
`QuickGenerate` keeps its existing draft, defaults and API. No app reinstall or
restart is performed by this implementation.

Scalar rows, references, ControlNet drafts, LoRA weights and full presets are
connected to the document API. Control images/models/masks use local file dialogs;
models and VAEs use the existing Society inventory. Control edits invalidate Apply.
Displayed example files from Figma are not bundled as fake user content.
Submission snapshots are tracked separately from QuickGenerate. Advanced jobs
stay in the workspace, showing actual previews, completed-image selection and
errors. Cancel affects only this workspace's pending batch; Open in canvas uses
the existing CanvasEditor route. Returning Home does not discard either draft.
The workspace is instantiated only after the first Image selection and then
retained; initial Home/mobile loading does not construct the large parameter UI.
Transient incomplete numeric text stays in the view while the typed document
retains its last valid value. Invalid input blocks Generate and preset Save;
Reset/preset replacement restores a coherent valid document and clears errors.
An invalid-input submission message clears once all transient inputs are valid;
backend submission/job errors remain distinct and are not erased by that transition.
LoRA cards and their weight controls form vertically repeated groups.

### Optional ControlNet panels

Figma [Add control / `243:8784`](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=243-8784)
is implemented with the installed LVRS Navigation ListItem: `Add control`, no
leading icon or description, an empty value slot and the existing `generaladd`
trailing icon. The library's default dimensions, padding, typography and radius
remain unchanged; only component properties and parent layout are configured.

Constructing the advanced view no longer creates example Pose/Canny controls.
An empty document shows the Add control row without any Control editor. Clicking
the row explicitly appends one unapplied `None` draft with a stable ID and displays
the existing expanded editor inline in the parameter panel. Its image/model,
process, weight, IP-Adapter, mask, Init and Apply behavior is unchanged. The row
remains available for further additions. Controls restored from a user-saved
preset are displayed from the same document collection; Reset or removing its
last entry clears the editors without removing the Add control row.

`advancedControlsAppearOnlyAfterAdd` verifies the empty view, real pointer clicks
on the LVRS row, its 402 × 44 layout at 1×, expanded editors, independent IDs,
collapse/reopen without losing edits, removal, preset restoration and Reset.
Existing picker regressions now explicitly add their own Pose/Canny fixtures
instead of depending on view-construction side effects. Set
`DREAMSCAPES_ADD_CONTROL_CAPTURE` to a PNG path to capture the actual Add row.

Verification on 2026-09-30: the new regression first failed on the original view
because it constructed two controls (`build/add-control-red.log`). After the
change, all four selected GUI behavior cases passed, including the retained
workspace and canvas-streaming regressions (six passes including setup/cleanup;
`build/add-control-gui-tests.log`). The parameter document suite passed 14/14
including setup/cleanup (`build/add-control-parameter-tests.log`). The canonical
app build and QML lint completed; lint retains an existing Main.qml dynamic
`parameterPanel` property warning outside the changed component.

The idle installed app was normally quit, updated from the canonical bundle and
relaunched. Both bundles passed deep strict signature verification and their
executables matched SHA-256
`b1020b422802feba8418de365822afa749d1076d67b74020072ef2ae91a767f8`.
Native UI observation confirmed no Control editor before addition, the LVRS Add
control row with a plus icon, the expanded Control 1 editor after an actual click,
and removal of that editor on Reset. Initial UI discovery timed out while the app
was starting; subsequent native discovery and interactions succeeded. This is
not a startup-performance improvement claim. The verification-only draft was
reset and the installed app remains on the Image workspace at the Add row. No
real-model inference was submitted for this UI change.

### Generation canvas resolution and streaming

The Image workspace canvas uses the validated document's `width` and `height`
even before an image exists. A single fit scale maps both pixel dimensions into
the available viewport, preserving the ratio in square, portrait and landscape
layouts and during window resize. The resolution caption reports document pixels;
the display size is a zoomed fit, not a change to generated pixel dimensions.
Invalid partial input keeps the last validated canvas, and Reset/preset loading
updates the canvas through the same document binding.

The workspace captures its own accepted submission IDs. The app shell routes
only quick recipes into the QuickGenerate result view; advanced recipes do not
depend on the loader's transient submitting state for routing. Native and worker
previews expose `GenerationController.previewJobId` with `previewImage`, clearing
both at job completion/cancellation. The canvas shows a preview only when its
active batch job owns that frame, then displays the completed Society image in
the same surface. Earlier jobs streaming while this batch waits cannot appear
in the canvas. Multiple outputs remain available through thumbnail selection.
Width/height edits remain draft changes; accepted jobs retain their immutable
submitted dimensions, and displayed images preserve their own aspect ratio.

`advancedCanvasMatchesResolutionAndStreamsItsBatch` checks empty-canvas geometry,
resizes, Reset/preset binding, an unrelated queued job, every decoded preview of
two consecutive jobs, completed PNG dimensions/selection and continued workspace
visibility without opening the quick result screen. The protocol fixture performs
no inference; real checkpoint throughput and quality are separate observations.
Set `DREAMSCAPES_CANVAS_CAPTURE` to a PNG path for its final workspace capture.

Verification on 2026-09-30: the canonical macOS app, GUI and generation test
targets built successfully. Preview ordering/failure/cancellation tests passed
7/7 including setup/cleanup (`build/canvas-preview-backend-tests.log`). The GUI
canvas streaming, retained-workspace and QuickGenerate streaming cases all passed,
including per-frame rendered pixel checks for two outputs. The combined log is
`build/advanced-canvas-gui-tests.log`; its separate packaged-startup case timed out
before root-loaded, and the unchanged startup-only retry also timed out
(`build/advanced-canvas-package-retry.log`). A child sample captured pre-main dyld
library initialization (`build/advanced-canvas-package-child-sample.txt`). These
startup deadline failures are retained, not counted as passing checks or a fixed
startup-performance issue.

The `/Applications/Dreamscapes.app` installation passed deep strict signature
verification and matched the canonical executable SHA-256
`ad076ed42d19958f257706a171971a236538f28658ffc92a593932b679a4f711`.
It was then launched normally, and its native Image workspace visibly showed a
square canvas matching both 1024px inputs and the 1024 × 1024 canvas caption.
The app remains on the Image workspace. No additional real-checkpoint generation
was submitted during verification; streamed-frame evidence uses the isolated
process-protocol fixture, while native inference reuses the existing preview
callback through the same job-tagged controller property.

### Production QML registration

`Dreamscapes.Storage` is a static `NO_PLUGIN` QML module. Its public types are
explicitly registered in `src/main.cpp`; the production list must include
`AdvancedImageParameters`, not only `GenerationController` and file exporters.
The source GUI test registers a derived fixture document, so its success alone
cannot prove that the packaged app exposes the real document type.

An isolated 2026-09-29 startup observation continued beyond the earlier timeout
and reproduced a terminal QML error: `AdvancedImageParameters is not a type`,
making `AdvancedGenerate`, `ImageGenerationWorkspace` and finally `Main`
unavailable. `build/startup-observation-kUrQzm/app.log` retains the red evidence.
The production registration now explicitly includes the parameter document.
`packagedApplicationStarts` also stops discovery on an explicit root-load failure
and reports that diagnostic separately from a missing completion marker. Its
startup/discovery deadlines are unchanged. The rebuilt canonical bundle passed
`packagedApplicationStarts`, followed by both advanced-document and workspace
scroll/draft GUI regressions: five passes including setup/cleanup, no failures,
14.892 seconds (`build/startup-registration-tests.log`). The package test uses
the actual production entry point and bundled dependencies, checks root-loaded,
one window and mutual helper discovery, and stops only its own child. Source
fixtures are separate checks, not a substitute for this production registration
test. This verifies the offscreen packaged startup, not installed-app replacement
or real-model generation. Pre-main MLX/Torch initialization still exists; no
general startup-performance claim is made from this passing run.

## Layers and API

1. iiLocalDiffusion `Generation/ImageParameters.hpp`: Qt-free C++23 values,
   parameter catalog, structural validation, and native capability checks.
2. `ImageParameterCodec`: strict conversion at the QML/JSON boundary. Numeric
   strings, booleans as integers, fractions as integers and unknown fields fail.
3. `AdvancedImageParameters` (`Dreamscapes.Storage 1.0`): observable detached
   parameter document, collection operations, atomic edits, and durable presets.
4. `GenerationController::enqueueAdvanced(QVariantMap)`: validated snapshot into
   the existing serial queue; never changes the selected model or QuickGenerate.
5. `AdvancedImageOutput`: output color profile and metadata policy.
6. `AdvancedImageInputs`: local reference decoding on the generation worker.
   Submission canonicalizes local paths/file URLs (relative paths resolve in
   Society Files) and checks readable image headers before publishing any jobs.
   Worker decoding applies orientation, bounds each input to 2048 per side,
   rejects sources over 64 megapixels, and produces tightly packed owned RGB.
   It honors the runtime pause/cancellation boundary between decodes and commits
   the whole list atomically. An already-running image decode finishes first.
   File contents are read at worker start, not snapshotted as bytes at enqueue;
   deleting or replacing a queued file can therefore fail/change that job.
   The first reference initializes img2img; Image Strength controls denoising.
   Editing models also receive all ordered references. Multiple images on a
   model without multi-reference support fail explicitly, never drop inputs.

Applied ControlNet Canny/Tile entries now use the native SD 1.5/SDXL route. Up to
64 entries are supported; unapplied drafts remain editable and are never executed.
Submission canonicalizes the model (Society Models-relative/local URL/absolute
safetensors or GGUF) and hint image (Society Files-relative/local URL/absolute),
validates headers, and rejects unavailable/remote inputs before publishing jobs.
`AdvancedImageInputs` decodes the hint on the worker with the same bounds,
orientation and atomic-commit rules as references. The SDK performs Canny edge
detection or passes Tile RGB, then supplies the hint and [0,2] weight to actual
ControlNet conditioning. The model joins runtime-resident anonymous weight
storage and its identity is part of context caching. Changing only the hint or
weight does not reload weights. Source bytes are read at worker start.
Other detectors remain explicitly rejected; those controls are not simulated by
passing unprocessed RGB. IP-Adapter has a separate raw-image conditioning route.

### Per-control IP-Adapter

The existing LVRS IP-Adapter toggle selects local adapter weights followed by
CLIP vision weights. Only acceptance of both files commits enabled state and
`ipAdapterModel`/`ipAdapterVision` together. Cancel at either stage leaves the
previous document unchanged. Disable retains both paths; disable/re-enable
permits replacement. The existing row/layout and LVRS styling are preserved.

Apply with process `IP-Adapter` or `None` plus the IP toggle performs image
conditioning without requiring a ControlNet checkpoint. With `Canny` or `Tile`,
both kinds of conditioning execute independently. The reference RGB goes to
IP-Adapter before any Canny preprocessing, and the row's [0,2] weight and optional
regional mask apply independently to both branches. A different detector is not
silently bypassed just because IP is enabled. Editing invalidates Apply.

Submission requires both selected IP weight sources, accepts local URLs,
absolute paths and Society Models-relative safetensors/GGUF files, canonicalizes
them in the immutable snapshot and rejects missing/remote resources before any
job is created. Disabled IP paths are retained but not read/validated. The worker
decodes all applied images/masks atomically; failure cannot replace just one of
the ControlNet/IP input vectors. Unsupported old preset effects remain editable;
old presets without the additive IP fields get empty defaults and must select
resources before active IP can be submitted.

The SDK prepares ordered adapter/vision slots in anonymous model memory, uses
the same image and mask bounds as ControlNet, checks SD 1.5/SDXL architecture,
and rejects any resource/preparation failure instead of publishing an image
without the condition. Input edits reuse resources. Turning all IP inputs off
retains their context when the Base/other resource identity is unchanged;
legacy resident requests also explicitly clear IP inputs. Re-enabling checks
both adapter and vision file identities. Source replacement during inference
invalidates the result. This is source/API and fixture-test evidence, not yet
an installed-app or real-checkpoint image-quality/performance claim.

Detailer crop regeneration clears whole-image IP/ControlNet inputs and regional
masks before inference, retaining model resources. The next request restores
its own inputs. This prevents applying normalized whole-image masks to unrelated
crop coordinates; a combined IP + multiple ControlNets + Detailer regression
covers both isolation and subsequent warm reuse.

Verification (2026-09-29): SDK 22/22 selected suites passed in 32.42 seconds;
product AdvancedParameters/Generation passed 2/2 in 44.01 seconds (12 and 61
cases respectively, two optional real-model cases skipped). Focused existing-view
regressions passed both cases, including picker cancellation and independent
workspace scrolling. Logs are `build/ip-forward-final-product-tests.log` and
`build/ip-forward-final-gui-tests.log`. Source-view verification is not a claim
that the installed app was updated. The earlier watchdog failure is retained
in `build/ip-forward-product-tests.log`; its unchanged rerun passing does not
establish that its timing sensitivity was fixed.

All applied entries are decoded in order and committed atomically: a bad image
or mask in a later entry cannot leave a partially replaced list. Every selected
model is prepared in resident memory before sampling. Each produces independent
residuals, multiplied by its own resized mask and [0,2] weight, then summed without
averaging. The UNet receives this sum with strength 1; no entry's weight is applied
twice. Hires resizes all hints independently, and Base controls do not execute on
Refiner steps. Failure/cancellation in any model aborts the request rather than
generating with a successful subset. Input edits reuse weights; replacement of
any selected model invalidates the cached context. Model resources survive
end-of-pass scratch cleanup. Existing row editing/apply contracts are unchanged.

The existing regional-mask toggle and source picker now execute on the supported
Canny/Tile ControlNet. Applying requires a mask source; submission canonicalizes
it as a local image with the same 64-megapixel safety bound before creating jobs.
Worker decoding retains orientation and limits each side to 2048. RGB luminance
multiplied by alpha becomes grayscale coverage, rounded to 8 bits. Mask decode
failure/cancellation cannot commit a partial replacement. Disabled masks retain
their saved path but neither validate nor read it during execution.

The native engine multiplies ControlNet residuals by resized coverage: white
applies, black excludes, gray/transparency attenuate. This is regional control
influence, not a promise of unchanged output pixels outside the region. Masks
follow normalized image coordinates at each base/Hires resolution. The original
hint and final output dimensions remain unchanged. Replacing or disabling a mask
does not reload model weights; each request resets cached mask state.

Textual embeddings use an LVRS selector with None, the selected file, and Choose
file. The file dialog accepts local safetensors/GGUF weights. Submission resolves
file URLs, absolute paths or Society Models-relative paths and rejects missing,
remote, empty, unsupported-format or over-100-MiB files before adding jobs.
The snapshot stores its canonical source. Its native token is `user_` plus the
lowercase file stem (characters outside ASCII letters, digits and underscore become `_`,
stem limited to 100 characters). The token is appended to positive conditioning
unless already explicitly present in either prompt; placing it in the negative
prompt applies it there instead. Presets retain selection without copying weights.
The native encoder validates actual compatibility before sampling; a selectable
weight file is not a promise that every model supports textual inversion.
Prompt weighting off preserves the literal prompt instead of interpreting its
weight syntax; toggling does not rebuild a warm model context.

The FreeU toggle is now forwarded to native UNet generation. It amplifies the
first half of backbone channels and frequency-filters skip features in the first
two decoder stages, using the SDK's documented SD 1.5/SD 2/SDXL profiles. It composes
with ControlNet residuals and Hires without changing the separate QuickGenerate
draft. Each request reapplies enabled/disabled state so a cached context cannot
leak the option into legacy generation. Unsupported model architectures fail
before sampling rather than ignoring the toggle. No extra model is downloaded.
The numerical CPU kernel may introduce CPU/GPU transfers; this is not a promise
of faster inference or improved quality for every checkpoint.

The existing Upscaler selector now supports learned `4× Ultra`. Choosing it opens
a local safetensors/GGUF weight picker; accepting atomically stores `upscaler` and
`upscalerModel`, while cancelling preserves both prior values. Choose weights
allows replacement without adding another panel row. Presets retain the source;
switching to a pixel mode or disabling Hires retains but does not load it.
Submission validates/canonicalizes the active source before publishing jobs.
The SDK validates real 4x ESRGAN compatibility and prepares anonymous-memory
weights before base generation, reusing the cached runner on repeated requests.
Load/inference failure is explicit, never replaced by pixel interpolation.
The submitted output dimensions remain unchanged: learned 4x intermediate
upscaling is fitted to the Hires canvas before the selected refinement pass.
No checkpoint is downloaded and no particular commercial/model brand is implied.

Enabling Detailer opens a local converted-YOLOv8 detector picker in the existing
LVRS toggle row. Accept commits enabled state and `detailerModel` together;
cancelling preserves the previous state. Disabling retains the source without
loading it; enabling again permits model replacement. Presets persist both fields.
The same local-weight validation used by learned upscaling canonicalizes the
active detector before any job is created. Raw `.pt`/pickle files are not accepted.

The native detector is prepared in anonymous memory before base generation and
retained in the runtime cache. After base/Hires generation it detects regions,
creates masks, regenerates 512x512 crops using the main conditioning and submitted
Denoise strength, and feathers them back into the full image. Whole-image
references/ControlNet hints/Hires are not reapplied to the crops. No detections is
a successful unchanged image, but detection/inpainting failure or cancellation
cannot silently publish the base image. Cropped previews are suppressed rather
than shown as if they were the whole canvas. Final output dimensions are preserved.

Refiner controls are also available through the document API:
`updateParameters({{"refiner", true}, {"refinerSwitch", 0.8},
{"refinerModel", "Refiner/sdxl-refiner.safetensors"}})`.
Presets persist all three values. Submission accepts Society Models-relative paths,
absolute paths or local file URLs and canonicalizes a nonempty safetensors/GGUF
source before adding jobs. Remote/missing sources fail atomically. Generation
forwards the immutable snapshot to the SDK's same-latent SDXL Refiner route.
The SDK checks actual architecture, requires the Refiner bigG encoder, prepares
the model in runtime-owned anonymous memory before sampling, and reuses it across
switch changes and inactive requests. Explicit release and existing native failure
cleanup govern context disposal. The raw user negative prompt is separate from
Base-only automatic embeddings. Selected explicit embeddings must fit both encoders.
The process worker remains unsupported.

The Figma Enhancement Refiner row now uses the existing LVRS Toggle contract to
open a local safetensors/GGUF checkpoint picker. Acceptance commits enabled state
and source atomically, cancellation preserves the prior pair, and disabling
retains the source. Disable/re-enable permits replacement. This does not add
rows, override LVRS styling, or modify the Figma source. Tests cover the accepted
signal, initial selection, replacement, disable/cancel, and checked-state binding.
The source-workspace screenshot capture is `DREAMSCAPES_REFINER_CAPTURE`.
Source UI tests do not establish installed-app behavior or full-model inference.

Example C++ usage (the equivalent methods are QML-invokable):

```cpp
AdvancedImageParameters draft;
draft.updateParameters({{"prompt", "coastal house"}, {"negativePrompt", "blur"},
    {"model", "Checkpoint/model.safetensors"}, {"width", 1024}, {"height", 1536},
    {"outputCount", 2}, {"seed", 42}, {"steps", 30}, {"cfgScale", 7.0}});
draft.savePreset("Portrait");
const auto errors = draft.submissionIssues(/*desktopWorker=*/false);
if (errors.isEmpty()) generationController.enqueueAdvanced(draft.parameters());
```

`parameters` is a value snapshot, not a mutable shared map. Use
`updateParameters(patch)` to edit one or several fields together. All edits are
validated before publication; invalid patches preserve the entire previous draft.
`parametersChanged` is emitted only for effective changes. `schema` exposes the
scalar catalog. `reset()` restores defaults and empties all collections, without
deleting saved presets. `errorString` reports rejected operations.

Collection API:

- `addReferenceImage(source)`, `removeReferenceImage(index)` (maximum 20).
- `addControlNet(process)` returns a stable ID; `updateControlNet(id, patch)`,
  `applyControlNet(id)`, `resetControlNet(id)`, `removeControlNet(id)`.
  Effective edits invalidate applied state. Empty patches and unchanged typed
  values retain Apply and emit no `parametersChanged` signal; invalid patches
  preserve the entire document. Numeric `1` and `1.0` are equivalent weights,
  but bool/number coercion is rejected. Removing a required source is permitted
  as a draft edit and requires re-Apply after correcting it.
  Apply requires an image and a ControlNet
  model/process or IP-only mode, and an actual mask source when masking is selected.
  Submission additionally requires the active IP model/vision pair. Reset clears settings and sources,
  retaining the row ID. Control-only view expansion is deliberately not stored.
- `addLora(source, name)` returns an ID; `updateLora(id, patch)`, `removeLora(id)`.
  Signed weights [-4,4] and zero remain actual values, not missing values.

The scalar fields and native limitations are listed in the SDK's
`docs/image-parameters.md`. All Figma parameter groups can be edited, validated,
reset and saved even when the current engine does not execute their algorithms.

## Preset storage

Presets contain the complete document, including collection weights and inactive
values, not just a display name. `savePreset(name)` creates/replaces by trimmed
name; `loadPreset(name)` replaces the full draft; `removePreset(name)` removes only
that preset. Names are 1–100 characters; maximum 100 presets / 8 MiB.

Storage is `QStandardPaths::AppDataLocation/advanced-image-presets.json`, versioned
as `{schemaVersion:1,presets:{name:parameters}}`. Writes use `QLockFile` plus
`QSaveFile`. Each mutation re-reads under the lock, so a second editor's unrelated
presets are preserved. Invalid/newer/corrupt files and write failures never replace
the previous draft or overwrite the invalid file. Tests inject paths below `build/`.

## Generation behavior

- Resolve all values and collection sources before adding any batch jobs. Each
  queued image receives its own seed; later draft or model-selector changes do
  not change those jobs. `advancedParameters` retains the submitted recipe;
  `seed` on each job is the actual resolved per-image seed.
- Empty `model` uses the controller's current model at submission. Explicit model
  and VAE values are Society inventory IDs; empty `vae` means embedded/default
  VAE, independent of the quick-generation selector. LoRA sources may be local
  file URLs, absolute file paths, or relative Society Models paths. No remote URLs
  or implicit model downloads are introduced.
- Forward prompt, negative prompt, width/height, steps, seed, CFG, native sampler,
  VAE and LoRAs to the native execution API or native checkpoint worker. Worker
  requests use argument arrays (no shell interpolation). The runtime retains its
  anonymous source-memory lifetime and existing cancellation/progress behavior.
  The existing generation-resource default-modifier policy is retained: an empty
  explicit LoRA list may select the SDK's compatible bundled default LoRAs, and
  compatible default negative embeddings can be appended by the engine.
- The in-process route additionally forwards all catalog samplers/schedulers,
  seamless tiling, CLIP skip, Eta, Hires, nearest/bilinear/bicubic/Lanczos or learned 4x ESRGAN upscaling and the chosen refinement
  strength through `generateNativeAdvancedImage`. The worker still rejects those
  additional controls rather than silently ignoring them. Native progress uses
  the submitted refinement strength instead of the legacy fixed 0.35 value.
  Textual embeddings and prompt-weighting policy also use the in-process route.
- Native max canvas is 2048 per side. Currently unsupported active settings are
  rejected before queue insertion. Remote Society and packaged worker routes do
  not yet advertise this advanced contract, so they are not used as a silent
  fallback; a model becoming remote after enqueue fails with a specific error.
- `colorProfile`: convert pixels to sRGB or Display P3 and embed the ICC profile.
  Untagged native RGB is treated as sRGB. `preserveMetadata=true` retains existing
  image metadata and adds `Dreamscapes.Parameters` with the recipe and actual seed;
  `false` writes pixel-only content plus required ICC profile. Dimensions are not
  resized. These controls run only for advanced submissions.
- `watermark`: the existing common Dreamscapes brand PNG is composited into the
  bottom-right of every final image. Its square is 1/16 of the shorter side,
  clamped to 1..128 pixels, with a 1/64 margin and 55% opacity. Tiny images fit
  the margin to the available area. The SDK performs stride-aware, alpha-correct
  bilinear resampling/source-over blending without Qt; the existing product
  codec boundary decodes the asset and converts it into the selected profile.
  This is a visible brand mark, not an invisible provenance signature. The
  default remains off. Previews and engine output files remain untouched;
  metadata retention is independent, so removing metadata does not remove
  the visible mark. Missing asset/encoding failures fail publication. The queued
  immutable switch applies to all outputs even if the draft changes meanwhile.

Watermark verification (2026-09-29): all 23 selected SDK suites passed in 27.20s.
After packaging finished, product AdvancedParameters/Generation passed 2/2 in
56.32s (13 + 63 individual passes, two optional real-model skips), recorded in
`build/watermark-isolated-product-tests.log`. The output test checks exact pixels,
ICC/metadata and byte-for-byte source-file preservation; the queue test checks
both native and worker publication and queued-toggle isolation. The original
test-only pixel-format assertion failure and concurrent-packaging watchdog
timeouts are retained in the earlier watermark logs; the watchdog sensitivity
has not been changed or claimed fixed. Source and staged SDK are updated, not
the global SDK or installed application.

The watermark-phase canonical bundle passed deployment's deep strict signature
verification and source GUI toggle/scroll tests, but packaged startup did not
reach root-loaded in either initial run or unchanged retry. These historical
failures are retained; the subsequent production-registration section above
records the missing-type cause, fix and passing rebuilt-bundle regression.
The old retry reached root-load-request. A sampled early phase showed dyld
initializing MLX/Torch before main, which did not establish the later load error. See
`build/watermark-package-retry.log` and `build/watermark-package-startup-sample.txt`.

Structural/API tests do not prove real model output quality or support for
unimplemented inference features. In particular there is no connected
additional control detector, face restoration or safety-classifier
execution in this change; the API reports that limitation rather than dropping it.
Multi-ControlNet numerical/production-sampler tests use controlled neural compute,
and product tests substitute inference after checking ordered decoded inputs.
Real multi-checkpoint image quality and throughput still require separate validation.

The 2026-09-29 multi-ControlNet verification rebuilt the SDK/native targets and
passed 20 SDK suites plus both product suites (`AdvancedParameters`, `Generation`).
After rebuilding stale consumer objects from an overlapping initial stage/build,
the final product run passed in 45.59 seconds. Its optional real-model fixture
tests were not enabled. This is source/staged-SDK validation, not installation
or packaged-app runtime proof; no layout changes were required for this phase.

## Verification

```sh
env -u CPATH -u CPLUS_INCLUDE_PATH cmake --build build --target DreamscapesAdvancedParametersTests DreamscapesGenerationTests -j 4
DYLD_LIBRARY_PATH=/Volumes/Storage/Workspace/SDK/iiLocalDiffusion/build/install/lib ctest --test-dir build -R '^Dreamscapes.AdvancedParameters$' --output-on-failure
DYLD_LIBRARY_PATH=/Volumes/Storage/Workspace/SDK/iiLocalDiffusion/build/install/lib build/DreamscapesGenerationTests advancedSubmissionSnapshotsAndForwardsParameters advancedNativeReceivesSamplingComponentsAndAdapters
DYLD_LIBRARY_PATH=/Volumes/Storage/Workspace/SDK/iiLocalDiffusion/build/install/lib ctest --test-dir build -R '^Dreamscapes.Generation$' --output-on-failure
```

Coverage includes rollback, numeric/enum types, collection limits, stable IDs,
Draft/Apply, full preset round-trip, corrupt files, concurrent editors, write
failures, native component/sampling forwarding, worker argv/provenance, immutable
batch snapshots, real PNG encoding, metadata removal and ICC color-space output.
Use the updated SDK header in the configured SDK prefix when building this source.
The shell's global `CPATH` can otherwise put an older `~/.local` SDK header before
CMake's explicit system include directory. Clear it for this configured build;
do not mix the staged new library with globally installed old headers.
For this staged-prefix developer setup, the test commands also select the staged
library explicitly: another installed Society dependency can otherwise preload
the older global copy of the same dylib identity. Packaged app verification must
remove these overrides and use only the bundle's own runtime.

Verified on 2026-09-29: SDK `ImageParametersTests` and a standalone C++23 consumer
compiled against `build/install/include` passed. Dreamscapes generation library,
advanced-parameter, generation, and LocalSociety test targets built successfully.
The final CTest run passed all 3 suites (`AdvancedParameters`, `Generation`,
`LocalSociety`) in 75.59 seconds. Real-model smoke cases remain opt-in and were not
run; no additional image generation was started in the user's installed app.
