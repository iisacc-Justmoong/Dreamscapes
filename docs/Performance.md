# Runtime size and UI responsiveness

Dreamscapes deploys the runtime dependency closure used by its executable, QML
plugins and plain helpers. It does not ship the complete Qt SDK. The macOS
application selects the Basic Controls style in `main.cpp`; its deployment
selects the same style through `product_deploy_app(COMPACT QUICK_STYLE Basic)`.

Compact deployment excludes unrelated child QML modules, unselected Controls
styles, designer metadata, QML tooling type descriptions and debug symbol
bundles. LVRS fonts, icons and image resources are embedded in `libLVRS`; their
duplicate filesystem copies are excluded while license files and runtime QML
remain. Native libraries outside the executable/plugin/helper dependency closure
are removed. Release binaries retain the application's architectures and public
symbols while local/debug symbols are stripped. Signing happens after all these
changes. Missing libraries or incompatible architectures fail deployment.

`iiCSMIDI`, `iiLicenseManager` and `iiUpdateManager` are no longer direct product
dependencies because the application does not call them. A dependency required
transitively by an active SDK still belongs to the runtime closure.

## Apple inference profile

Build the product-owned macOS inference runtime before the release application:

```sh
python3 scripts/build-macos-runtime.py --jobs 8
cmake --build build --target Dreamscapes --parallel 8
```

The script builds the iiLocalDiffusion checkout under
`build/apple-runtime/build`, installs it into
`build/runtime-sdk/iiLocalDiffusion-apple`, runs its compute/native backend tests,
checks the dylib links and configures the application to use that package. All
build outputs remain below `build/`. `--sdk-source` selects another SDK checkout;
`--configure-only` prepares the SDK without building or changing the app cache.
The existing app build's iiFileProvider package selection is retained.

This profile disables the optional LibTorch backend used for AMD ROCm or an
alternative CPU implementation. MLX CPU, Metal, Core ML, native diffusion and
ONNX pose support remain enabled. Windows/Linux backend selections are unchanged.
Fetched native sources that require compatibility patches are copied into the
product build first; shared SDK source trees and global installations are not
modified. The private copies explicitly include their standard library headers
and prefer matching jaccl headers over an unrelated installed include path.

The recipe does not download model weights or generate content. Backend tests
and launch checks do not prove trained-model output quality.

## UI lifecycle and invalidation

The Home screen loads `CanvasEditor` on the first operation that needs it: a new
canvas, a native/image file or selected generation results. Cancelling the open
dialog does not instantiate the editor. The loaded editor remains alive when
returning Home so its document, undo history and route behavior are preserved.
Video results do not instantiate the image editor.

The tool panel reads one shared `engineState` binding per tool-state change.
Timeline selection changes update only selection; brush/tool settings update
lock flags only when those flags actually change. Neither operation discards
cached thumbnails or reconstructs the track list. Document/revision/artboard
changes still invalidate thumbnails, including pixel edits and frame changes.

## Verification and measurements

Recurring application-wide storage work is kept out of the input path. Society
Helper publishes presence and reads peer records on one coalescing native C++
worker; the event loop only applies completed value snapshots. Automatic activity
and peer-history commits use a separate lazy worker, so history cannot delay the
heartbeat or focus changes. Normal shutdown drains those commits. Atomic publication,
permissions and directory validation are preserved. Stopping a Helper drains its
active operation and invalidates queued results before removing its own record.

The application requires iiSocietyHelper 0.9.2 or later. A tested package installed
under `build/runtime-sdk/iiSocietyHelper` is supported alongside the normal SDK
installation; `iiSocietyHelper_DIR` selects the exact package. Keep its
iiFileProvider, iiSocietyContainer, iiSharedCanvas Core/Quick and iiSocietySync
dependencies from the same verified package graph when configuring the product.
Generation-history assertions count content through the provider's control-file
classification, while retaining exact PNG names, dimensions and temporary-file
cleanup checks. Storage-selection fixtures scope both the settings path and the
container override, so an isolated test drive cannot override that selection test.
The complete LocalSociety fixture has a 180-second runner budget for native
process/loader setup on shared volumes. Initial asynchronous replica creation has
a separate 15-second setup budget with both peers' errors reported on failure.
Its per-job progress, stall, cancellation and background-expiration deadlines
are unchanged and remain asserted.

The generation controller's two-second inventory poll reads storage/model
metadata on a background task. An unchanged inventory does not reopen the
FileSystem or reconstruct its complete Files/Published/History mirror. Changed
models update existing selections and bindings on the UI thread. Storage epochs
discard results from superseded selections, and active generation/background
states reject obsolete polling results. Explicit storage selection and Refresh
retain their synchronous completion contract. Background polls preserve an
existing generation error. `backgroundModelInventoryDoesNotReopenTheFileMirror`
covers repeated polls, discovery of a newly added model, selection retention and
error retention. Presence worker tests also deliberately stall an operation
while checking that application timers continue to receive events.

`GuiTests::homeDefersEditorUntilNeeded`, Home/editor route tests and multi-canvas
tests cover delayed creation and retained documents. `mainStartupPerformance`
reports the time spent in `QQmlApplicationEngine::load` and the resulting object
count for three loads of an isolated empty Society fixture. It is a QML load
measurement, not the complete operating-system launch or first-frame latency.

`EditorLayerTimelineTests::interactionPerformance` creates 32 patterned static
layers, warms their thumbnails, then repeats 20 brush-setting changes and
thumbnail queries. It reports elapsed time and model notifications.
`toolSettingsDoNotRebuildTimeline` checks unchanged thumbnails across tool and
selection changes and renewed thumbnails after actual pixel changes. Fixtures
are test-only and do not populate production timelines.

Run relevant native/UI tests with the same renderer and no concurrent compiler
load when comparing timings. Native bundle tests cover real Mach-O pruning,
architecture thinning, exported symbols and execution after stripping. A final
bundle must also pass deep signature verification and an actual application
launch without loader path overrides. Source fixture timings, signed package
size and installed application behavior are separate evidence.

MCP process tests use an actual `.societycontainer` package, with Helper and app
endpoint directories below that package's `.society-runtime`. A bare directory
does not satisfy the current Society storage contract. These fixtures isolate
queue, cancellation and local UI question checks from a running user's store.

## Measured macOS result (2026-10-16)

The installed arm64 application decreased from 1,061,195,842 bytes (1,012.0 MiB)
to 291,063,959 bytes (277.6 MiB), a 72.6% reduction in real-file payload. Allocated
filesystem size can differ because of block allocation, directories and links.
The previous application is retained as a ZIP backup in the verification folder.

With an isolated empty Society fixture and software rendering, Home created
6,551 objects instead of 28,349. The mean of the two warm QML engine loads was
617.3 ms instead of 1,124.5 ms. The before fixture freezes the original Main.qml;
both final Home measurements use the same compact installed runtime. These are
source-QML load measurements and do not include operating-system process launch.

For 32 layers and 20 tool-setting/thumbnail-query iterations, the native model
benchmark decreased from 235.058 ms with 20 model notifications to 0.942 ms with
zero notifications. This is a targeted cache/invalidation benchmark, not an
application-wide FPS measurement. Real trained-model generation speed and output
quality were not benchmarked in this change.

Evidence, command logs, exact payload checksums and installation verification are
under `build/performance-20261016/`. The compact candidate passed the native Cocoa
entry-point and Helper discovery check without child loader-path overrides. The
installed app was also opened normally and checked with an existing IISC canvas,
frame navigation, timeline mode changes and Elements/Brush panel switching.

## Interaction-path follow-up (2026-10-16)

A three-second sample of the long-running production process found 85 of its
237 main-thread samples inside Helper publication's `QSaveFile::commit` and
`fsync`. The periodic generation-model refresh also reopened the complete file
mirror on that thread. These shared services run independently of the selected
screen, making them application-wide input bottlenecks under storage contention.
The changes described above move recurring presence, automatic history and model
inventory work off the input thread while retaining explicit durability contracts.

In a three-second, two-participant presence fixture with 100-ms heartbeats, the
95th-percentile timer interval changed from 15.8922 ms to 12.6121 ms and the
maximum from 36.0853 ms to 27.229 ms. Both runs use the same Qt runtime without
duplicate Qt images. This is an isolated timer benchmark, not overall app FPS,
full application launch time or trained-model generation throughput.

The signed candidate also started using Cocoa and its normal Metal selection
without child loader-path overrides. A separate post-startup sample loaded the
verified Helper binary and contained no main-thread disk commit or model-mirror
reopen stack. This candidate used an isolated storage fixture; its memory usage
and sample counts are not directly comparable to the user's open document.

Evidence, package manifests, regression logs and the reversible installation
record are under `build/interaction-performance-20261016/`. The user's existing
process was launched before this candidate and retains its older executable.
Replacing the installed bundle preserves that session; the new code takes effect
on the next application launch.
