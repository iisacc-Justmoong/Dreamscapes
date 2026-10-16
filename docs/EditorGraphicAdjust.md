# Editor graphic processing adapter

`EditorGraphicAdjust.h/.cpp` is Dreamscapes' product adapter for the installed
**iiGraphicAdjust 0.4.0** package. It uses the SDK's typed public options and
functions; image algorithms remain in the Qt-free C++23 leaf SDK. It does not
replace the document, canvas selection, history, timeline or drawing engine.

The editable Figma Color, Adjust, Mask and Effects views use the API and option
contracts recorded in `Design/Dreamscapes/figma-adjust-filter-20261008/`.
`SDK/iiGraphicAdjust/docs/filter-catalog.json` supplies the Filter command aliases.
The current adapter has 250 image processing API entries, eight mask operations,
and eight automatic or sample settings actions. Its image entries include all
47 Gallery effects, the 22 Lightcraft looks and 41 Lightcraft presets. A command
alias refers to the same operation; it is not an additional algorithm.

## Calls and ownership

```cpp
const auto result = EditorGraphicAdjust::process(
    originalImage, "developLight", {{"exposure", .5}, {"shadows", 20.0}});
if (!result.ok()) {
    // Present result.error. A failure contains no output image.
    return;
}
// Commit result.image through the existing EditorCanvas document/history path.
```

`process(source, operation, parameters)` borrows its QImage argument and returns
a detached QImage. All settings are values and the SDK only borrows converted
pixels for the synchronous call. The adapter keeps reference images, masks and
other span resources alive throughout processing. Calls have no shared mutable
state and can run on separate workers while their supplied values remain alive.

The source is converted from its declared color space to sRGB when necessary,
then to straight-alpha ARGB32. Numeric words pass through the SDK's
`fromArgb32`/`toArgb32` conversion; no premultiplied Qt scanline is passed as
straight-alpha SDK input. Output images are tagged sRGB. Untagged images are
interpreted as sRGB. Source storage is never overwritten. Alpha and invisible
RGB follow the chosen SDK operation's actual contract: color operations preserve
coverage, whereas geometric and spatial filters may transport it.

The host must retain its source snapshot before a preview, blend the result
against its existing layer/selection mask, and commit through its existing undo
path. The adapter never writes a document or a file, never creates a layer and
never records history. Preview cancellation and Reset restore the host snapshot;
calling the adapter repeatedly with the same original gives a fresh evaluation.
`EditorCanvas` must avoid applying a second time to an already-previewed image.
`applyLocalAdjustments` already applies supplied coverage inside the SDK; the
host must not blend that result against the same selection a second time.

## EditorCanvas integration

`previewGraphicOperation` returns a detached encoded preview and any estimated
settings. It does not replace layer pixels, change the document revision, add
history entries or replace the active selection. `applyGraphicOperation` commits
image results through `replaceSelectedPixels`, preserving the editor's existing
Undo path. Repeated edits of the same tool/API/layer/revision session evaluate
the retained original rather than accumulating the previously applied effect.

Mask generation commits an editor selection only. The eight mask APIs use
`makeMask`; diagnostics use the five `maskColorOverlay`,
`maskColorOnBwOverlay`, `maskImageOnBlack`, `maskImageOnWhite` and
`maskWhiteOnBlack` APIs and remain preview-only. Local adjustments, diagnostics,
invert and refine require an existing selection and receive that selection in
raster-local `mask` coordinates. `combineMasks` receives the active selection as
`base`. When a second generated mask replaces an existing active selection,
the editor retains the previous raster-local mask as the default `operand`.
Thus Combine uses the current selection as `base` and the previous selection as
`operand`; the caller supplies `operation` and may override `operand` explicitly.
The retained operand is tied to the current document generation and selected
layer. A different layer or reopened/replaced document cannot consume it.
Without a retained or explicit operand, Combine asks the user to create a second
mask. Invert, Refine and Combine preserve the retained operand so they do not
silently replace it with their own previous result.

Layer transforms are document-world coordinates. Mask-to-selection mapping
subtracts the active artboard origin, and selection-to-layer mapping uses the
same origin before reading coverage. This keeps masks and subsequent image
adjustments aligned on artboards located away from `(0,0)`. SDK coverage is
quantized to the existing editor's 8-bit selection representation once. General
image filters blend against this selection; local adjustments use SDK coverage
directly. Unsupported processors, missing raster inputs, hidden layers or
artboards, pixel locks and invalid
parameters return explicit errors before document replacement.

Canvas color sampling exposes `layerColorSample.sequence`. Only a successful
sample increments it; entering or canceling picking and rejected coordinates
leave it unchanged. A canceled pick retains the previous valid color, so sample
helpers must compare the captured sequence with the current sequence before
consuming that color. The sample state remains editor UI state and never rewrites
document pixels.

## Operation and parameter names

Use the exact public API name, for example `developLight`, `developColor`,
`developCurve`, `developDetail`, `correctOptics`, `cameraRawFilter`,
`filterGaussianBlur` or `lightcraftPresetWarmGlow`. Filter command names such as
`filter.blur.gaussianBlur` also work. `look.CineTealAmber` and `preset.WarmGlow`
are explicit aliases for named Lightcraft APIs.

`supports(operation)`, `canonicalOperation(operation)`, `operations()` and
`parameterNames(operation)` describe the adapter's executable contract.
`parameterNames` lists top-level typed option/resource fields. Automatic actions
and mask APIs are listed as supported; call `makeMask` for mask generation.
An unknown operation returns a reason and no image.

Options use the SDK's **actual units**. `developLight.exposure` is EV and its
other signed sliders are `[-100,100]`. `hueSaturation.saturation` is normalized
`[-1,1]`. `whiteBalance.temperature` is Kelvin, while
`cameraRawFilter.temperature` is a relative `[-100,100]` slider. The adapter
never guesses a conversion between these operations. The Figma spec's `sdkUnit`
conversion must occur before the host submits a value.

Parameters may be a nested object, a dotted or indexed property path, or an
`options` object together with explicit resources:

```cpp
EditorGraphicAdjust::process(image, "developColor", {
    {"mixer[3].hue", 35.0},
    {"grading.shadows.hue", 210.0},
    {"grading.shadows.saturation", 25.0}
});
EditorGraphicAdjust::process(image, "developCurve", {
    {"master.points[0].output", .08},
    {"master.points[1].output", .94}
});
```

Partial indexed edits preserve every unedited SDK initializer. Full arrays must
have the exact length required by a fixed SDK array. A collection accepts a
QVariantList of typed option objects, or bounded indexed edits of its current
SDK default collection. Curves, pins, paths, points, lights, strokes, mesh quads,
LUT rows and normalized coverage reach the corresponding typed structs.
Colors accept normalized RGB/RGBA arrays, `QColor`, color strings, or an RGBA
object where that SDK field is `Rgba`. Enum strings use public enum values;
case, spaces and punctuation are ignored. Valid integer enum values also work.
Unknown enums, fractional integers, nonfinite values, unsupported parameter
names, malformed arrays and SDK range/geometry violations return explicit
errors. Unsupported fields do not become no-op success.

Compatibility aliases are limited to known source contracts: Gaussian menu
`radius` becomes `sigma`; `hslSat`/`hslLum` become Camera Raw's
`hslSaturation`/`hslLuminance`; Liquify `meshSize` becomes `cell`; Lens Blur's
`bladeCurvature`, `invert` and named aperture shape become the public SDK fields;
Puppet/Perspective `src`/`dst` become source/destination. Typed API member names
are preferred. Host-only fields such as a document layer ID are not pixel options
and must be consumed by the host before calling this adapter.

## Resources, masks and automatic settings

An `ImageView` resource such as `reference`, `map`, `guide`, `depthMap` or a
Vanishing Point paste image accepts a QImage, a local path/file URL, or an
object with `source`, `path` or `image`. Remote URLs are not decoded. A color LUT
is a typed `lut` object with its kind, size, values and optional domain. The host
can call `iiGraphicAdjust::parseCubeLut` to read supported Cube text and pass the
resulting values; filesystem import remains an application action.

`makeMask(source, operation, parameters)` returns width, height and detached
normalized float coverage. Supported operations are `brushMask`, `linearMask`,
`radialMask`, `colorRangeMask`, `luminanceRangeMask`, `invertMask`, `refineMask`
and `combineMasks`. Source dimensions come from the selected bitmap. Brush
parameters may be a single `BrushStroke` object as shown in Figma, or a `strokes`
array. Stroke coordinates are supplied by the host.

Image operations accepting a mask use `{width,height,coverage}`; width and
height default to source dimensions. `applyLocalAdjustments` accepts the SDK's
`adjustments` array, or one local adjustment with a `mask` and the
`LocalAdjustmentOptions` fields at the parameter root. Diagnostic mask views
process actual supplied coverage instead of a decorative sample.

`autoDevelopLight`, `autoWhiteBalance`, `sampledWhiteBalance`,
`whiteBalancePreset`, `autoBlackAndWhiteMix`, `samplePointColor` and
`autoLensScale` and `estimateChromaticAberration` call the real SDK estimators. Their `Result.parameters` contains
the returned settings for the host to merge into its current draft.
`samplePointColor` returns the detached source and the sampled Point Color
settings; the host chooses which point receives those settings.
`estimateChromaticAberration` returns `redChromaticAberration`,
`blueChromaticAberration` and `autoChromaticAberration=false` for the
`correctOptics` draft, together with the corrected detached image. Disabling
automatic estimation in the returned settings avoids estimating the same
aberration again when the returned manual slider values are applied.

## Validation

`tests/tst_editorgraphicadjust.cpp` checks independently calculated one-stop
exposure, encoded brightness units, original/alpha preservation, premultiplied
input conversion, partial indexed edits, SDK initializer preservation, typed
LUT/reference resources, invalid parameters, real coverage geometry and local
adjustments, automatic settings, named Look/Preset output and Gallery ordering.
Tests compare selected operations against direct installed SDK calls while also
asserting pixel properties and numerical oracles. No product session or active
rendering job is restarted by these checks.

The product CMake target must find `iiGraphicAdjust 0.4.0` and link
`iiGraphicAdjust::iiGraphicAdjust`; tests run from `build/`. Packaged application
loading, editable panel routing and target-device execution
require their separate product checks.

`tests/tst_editortools.cpp` also covers the actual EditorCanvas connection:
nonmutating preview, Apply/Undo and original-pixel reevaluation, alpha retention,
invalid input and pixel-lock preservation, local soft coverage against a
separate sRGB exposure calculation, active mask inversion/refinement/combination,
diagnostic-only views and independent masks on a nonzero second artboard.
