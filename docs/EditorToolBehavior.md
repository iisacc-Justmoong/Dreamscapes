# Editor tool execution

The editor now connects its tool controls to native document edits instead of
showing a generic action acknowledgement. The visual controls remain installed
LVRS controls. `EditorCanvas` is the application bridge; detached color, effect,
fill and mask operations and native brush dynamics belong to iiSharedCanvas
0.26.0. The document format remains `.iisc` 1.17.

## Executed operations

| Tool | Native behavior |
| --- | --- |
| Elements | Drag to create rectangles, ellipse sectors/rings, polygons/stars and marked lines. Geometry, outline placement/caps, color, opacity and blend update the selected session-created element. Solid geometry is vector; gradient/image fills are committed raster content. |
| Text | Click to place glyph outlines. Text, typography, alignment, caption timing/backdrop, heading scale, callout shape/pointer and footer placement update the session-created text. Glyph outlines persist independently of the font installation. |
| Camera/Photo | Full-resolution camera capture uses Qt's camera permission/capture boundary; local photo import and scan color conversion produce real layers. RAW/depth and exposure-lock controls are unavailable when the capture backend cannot provide them. |
| Asset | Search/type/sort controls filter a real local media directory, supplied from Society's connected root. A valid local source directory and search/type/sort filter the actual files. A chosen item imports image, SVG, WAV or bounded video content. Licensing, sharing, cloud versions and collection management require a connected service; no example license/version is asserted. |
| File | Native open/copy, Fit/Fill/1:1 image placement, layered `.iisc`/project saving and flattened `.iisc`/PNG/JPEG/TIFF frame export. Unsupported persistent links/smart objects are explicitly inactive. |
| Background | Solid, linear and radial background pixels update immediately. Supplied semantic regions can produce masks or remove a background. Generated replacements use the existing generation controller and are placed only after choosing a result. |
| Audio | WAV import, mono/stereo/split tracks, sample-rate conversion, trim, fades, gain, pan, activity-based ducking, offset and stretching write real PCM/tracks. The waveform displays edited PCM. Preview plays the chosen edited track. Timeline playback mixes enabled clips and unmuted tracks from the current frame, including source offsets and track/clip gains, into stereo 48 kHz PCM. Beat sync aligns an analyzed onset to the current frame. |
| Canvas | Extent changes scale or anchor existing content; rotate/mirror transform real layers. Mirror canvas only reflects the complete interactive preview, including overlays, without writing pixels. Crop, bleed and safe-inset guides use document coordinates and zoom. Resolution controls mm-to-pixel guide conversion and exported image density. Format-level ICC/profile and destructive crop options remain inactive. |
| Generative | Text/image generation queues the existing advanced runtime. Current model, negative prompt, image reference, denoise, LoRA and VAE are passed to its typed contract. Inpaint/fill blend a chosen generated result through the active mask. Outpaint prepares an expanded reference, changes extent on placement and places generated content behind the existing composition. Upscale performs the selected native resampling method. |
| Layers | Inspect/select actual layers, edit opacity/blend and enforce session pixel/position locks. Rasterize native geometry/dynamic content at the current frame; inspect/move actual vector paths; apply Union/Subtract/Intersect to the selected vector asset's path footprints; apply an actual pixel mask. Boolean output uses the first path's paint and commits editable filled outlines. Group/adjustment-layer semantics absent from the format are inactive. |
| Select | Rectangle, triangle, lasso, connected/global color wand, object-layer hit testing and supplied semantic selection. Selection coverage is rendered on the canvas and constrains supported pixel edits. |
| Color | Nonaccumulating exposure/contrast/tone/white-balance/saturation/grading/curve edits on the selected layer. Histogram, clipping and tone response use real pixel data. White/black clipping overlays never rewrite artwork. A neutral picker samples the actual frame. |
| Effects | Native blur, texture/clarity, haze, vignette (including manual vignette), deterministic grain/uniform noise, sharpening and luminance/color denoise, manual distortion and color-fringe reduction. The attached UI names Uniform noise honestly; unimplemented Gaussian/Poisson distributions and calibrated lens profiles are not advertised. |
| Retouch | Clone source picking, source scale/rotation and local color-matched healing. Selected repair regions use boundary propagation and relaxation. This is a local repair algorithm, not learned content synthesis. Inpaint uses generation and mask placement. Face detection/refinement requires a separate model and is inactive. |
| Fill | Connected/selected solid, gradient and pattern fills, with opacity and blend applied to pixels. Palette choices update the actual fill color. A click in Generative mode selects the connected region without changing artwork; Generate fill queues its replacement and later uses mask placement. |
| Brush | Installed iiPaintEngine tip shape, spacing, hardness, opacity/flow, blend, pressure size/opacity, pressure inversion, color dynamics and wet mix. Rope, predictive and pulled pointer filters use the configured strength/delay, with streamline filtering, optional catch-up and 45-degree angle snapping. Only supported brush shapes/blends are offered. Stylus tilt forwarding and build-up control remain inactive. |
| Auto enhance | Analyze actual luminance/color statistics and apply tone/color corrections; denoise and sharpen detail. Manual geometric correction transforms the canvas. Learned face refinement/calibrated lens detection is inactive. |
| Masking | Brush/linear/radial masks, edge-aware brush restriction, luminance/color ranges, supplied semantic subjects/regions, feather/contrast and mask combination. |
| Eraser | Real pixel erasing, transparent object removal, local repair, vector split/trim/stroke removal, and recorded-pixel restore/protection. Boundary expansion grows the actual object mask before local removal or generation. Restore/Protect uses the configured feather on its painted coverage. |

## Capability and lifecycle contract

`toolControlState()` separates a `supported` capability from the current
`enabled` state and its reason. The historical
428-field design catalog includes capabilities not exposed by the current
native file format, model resource bindings, camera backend or local library.
The attached editor does not render these unsupported design placeholders as
editing controls. The catalog remains intact for Figma fixture comparisons.
Mode selectors now show the parameters belonging to their actual operation;
RAW Capture, dynamic/group layer authoring and Face Refine are omitted until a
connected implementation exists. A supported operation missing its current
selection, device or semantic input remains visibly inactive with its reason.
The Info button lists the exact missing capability for the current tool.
Omission is not implementation of the removed capability. Advanced generation
opens the existing resource-aware form, including ControlNet, reference and enhancement bindings. This
implementation does not claim that all 428 original catalog fields execute.
Editable layer preservation is offered for IISC; PNG/JPEG/TIFF export disables
that option and exports a flattened frame without rebinding the working file.
The compact generative controls do not silently substitute an unattached
ControlNet/IP-Adapter/lens/face model.

Pixel locks block fill, repair, mask placement, rasterization, generated masked placement and painting before mutating the document. Position locks also block individual path movement. Rectangle selection rotation and rounded triangle coverage are computed from the actual drawn geometry. Non-resampled canvas resizing offers center, top-left and bottom-right anchors.
The Layers controls display the selected layer's actual opacity, blend and session lock; its Reset action applies default opacity/blend and clears session locks. Canvas dimensions display the current document extent. Vector stroke erasing intersects the stroked outline, including open lines. Trim and split commit filled native outlines where a stroke is cut. Generative object erasing queues the masked-generation workflow using the Generative tool's prompt and model settings.
Alpha protection is per layer, initially off for a newly created bitmap. Visiting
Layers or protecting another layer does not make a new transparent paint layer
unpaintable. The displayed switch follows the selected layer, and Reset clears
its protection. The current implementation protects brush-painted alpha.

Manual distortion exposes a signed coefficient and chromatic-fringe reduction
uses a separate native pixel operation. Both restore the original preview when
returned to neutral, without requiring or claiming a calibrated lens profile.
Retouch can sample the selected bitmap or the actual composited frame, mapping
source points through the layer transform. The source overlay shows the chosen
canvas point. Heal adaptation selects color matching, rotation, or both.
Layer name edits write the selected native layer name. File Save as uses the
requested name and directory; the document-level Save button still saves its
existing working file. A generated Remove fill queues generation rather than
silently applying the local repair algorithm.

Edits go through validated native transactions. Full-document undo/redo includes
text, geometry, transforms, audio and raster edits. Undo/redo also restores the session tool drafts so subsequent edits use the restored text and parameter values. History retains at most 32
states and prunes raster-heavy history toward a 128 MiB budget. Working-file
edits commit synchronously; Undo writes the restored state back to that file.
Do not use an original working `.iisc` for destructive verification.

Tool settings, source brush history and editable text/element authoring data are
session state. Saved files preserve the actual native paths, pixels, transforms,
audio and timing; reopened glyph outlines remain editable as native paths.
They do not restore the session's original text string/font authoring controls,
source audio before trimming, or persistent layer locks. These boundaries are
not inferred from a successful raster round trip.
Timeline scrubbing pauses playback. Resuming renders audio from the chosen
frame rather than restarting a selected track at sample zero. Frame advancement
uses elapsed wall time, so a delayed timer does not slow the frame sequence
relative to audio. The detached PCM mix is bounded to 16,777,216 sample frames;
longer remaining timelines report a limit error. Physical output latency and
device A/V synchronization still require hardware verification.
Print resolution and guides are session controls; image export stores pixel
density where supported by its codec. The current IISC format does not store
print density or ICC tags. Bleed describes an outer guide; it does not expand
the saved document by itself. Safe inset accepts percent, mm or px and rejects
malformed values before replacing the draft.

Generation batches retain their owning canvas. Results are shown without
replacing current artwork automatically. Unsupported input/model resources
produce the existing runtime error; queue acceptance alone is not inference
proof. Playback and capture are conditional on the actual device.
Native inference is the Apple/Android platform default. An explicit
`IILD_GENERATOR_EXECUTABLE` selects the process backend on macOS too;
it does not silently load process-protocol fixtures as native model weights.
The application MCP generation/cancellation test uses that explicit backend
and verifies the generated PNG and session queue, independently of learned
inference quality. Queue entries are not restored across new sessions.
Masked generation placement maps the full generated canvas through the selected layer's inverse transform before blending its pixels. A moved or scaled target therefore receives the corresponding canvas region, rather than a shrunken copy of the complete generated image.

## Verification

`Dreamscapes.EditorTools` exercises native text/shape edits, masked fill,
nonaccumulating color processing, effects, transforms, full-document undo,
original image preservation and native save/reopen including PCM audio.
`editorToolbarOperations` exercises toolbar selection, parameter edits, pointer
selection/fill, text placement, actual preview and Undo through the QML view.
Existing native-canvas and render-quality tests continue to cover `.iisc`
mounting and presentation. Camera capture, physical audio output and fresh
learned inference need corresponding runtime/device evidence.
The catalog sweep dismisses real editing surfaces opened by both choices and
actions before testing panel Escape. Selecting Image fill opens a source chooser;
Escape closes that chooser before it can close the underlying tool panel.
