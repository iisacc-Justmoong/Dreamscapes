# Dreamscapes

Models that exist only on the host run first on the Society host without waiting for download completion. The iiSocietyGeneration SDK handles creation requests, progress, and result reception via an authenticated Society connection, and after host acceptance, asynchronously requests the download of model and shared generation resources to Society. Download errors or generation cancellations do not cancel each other's work. Fully synchronized models use existing local inference from subsequent requests.

Remote results are saved to the local Society's Generation History after checksum and size verification, and then transferred to other devices via Society synchronization. iOS continues transmission during the allowed background execution time and preserves the download request upon expiration. Even if the app is suspended, accepted host work continues and resumes result reception upon return. If the host is restarted, the running queue is not recovered and failure is indicated. Both the host Society and the client Dreamscapes must be updated together.

Bundles submitted remotely maintain the host path even during download. After download completion, new submissions recheck the disk's actual readiness status to select the local path. Remote generation and replication iOS background requests do not require GPU resources. While OS permission is granted, subsequent host work also runs, and upon permission expiration, pending unsubmitted work and transmission wait for foreground return.

Mobile builds require the Remote libraries for iiSocietyClient and iiSocietyGeneration. The Android build script also prepares this dependency in the order ServerHost → Sync → Client → Generation. The Host library that provides inference is included only in the desktop Society.

Dreamscapes uses `LV.ApplicationWindow` and `LV.Theme.defaultPrimary` ( `#0A84FF` ). The entire window is filled with `#0B0B0B`, almost black without gradient, and fill opacity is 50%. The window background is separated from Primary, and button and selected states maintain the app accent color. 64px material blur and macOS native background blur are maintained, and text and button opacity is not reduced. `Dreamscapes.Gui`'s `mainCreatesOneSharedWindow` validates color, opacity, and gradient removal together with the actual window material.

`Views/Home/AdvancedGenerate.qml` is an advanced image generation form separated from the entry point. `expanded` represents both Figma's compact frame and full expanded frame states. It reuses 402px as the base frame and LVRS · ListItem · InputField · ToggleSwitch, distinguishing between categories with `Theme.gap12` and between cards/groups within the same category with `Theme.gap6`. The default seed `-1` means random, and reference images are limited to a maximum of 20. ControlNet always provides the next numbered folded additional row behind the default two layers, and when opened, a new layer and the next numbered row are created. If LoRA is missing, a large `+` card is displayed and added via Fine-tuning's Add or through the corresponding card. The preset save popup accepts only one name. This view is intentionally not connected to `Main.qml`, and subsequent entry point tasks display it and map it to the actual generation request contract. `advancedGenerationDefaultsAndDynamicCollections` validates two display states, default, ControlNet consecutive numbers, 20 reference limit, empty LoRA addition, and name-only preset save.

You must use the latest LVRS package. The current Workspace instance shares the framework installed at `SDK/LVRS/build/material-runtime` with Society, and the `build/` configuration of `LVRS_DIR` points to that `lib/cmake/LVRS`.

At app startup, iiSocietyHelper is launched at `com.iisacc.dreamscapes`. Helpers using the same public observation location on the same device, including Society, discover each other's execution instances. LVRS connects the Helper to the engine's lifecycle, reflects foreground/background status, and provides `societyHelper.observedApplications` and events to QML. If observation is interrupted, it attempts to restart during app state transition. The generation queue or model save location is not used for this observation registration. iOS reuses the existing Society App Group, and observation for the interrupted app expires.

Package execution validation for `Dreamscapes.Gui` checks whether the actual app and separate test Helpers discover each other. `SOCIETY_HELPER_DIRECTORY` is specified by test-specific `build/` path. SDK installation location is specified as `iiSocietyHelper_DIR`, and the current Workspace validation is `SDK/iiSocietyHelper/build/install/lib/cmake/iiSocietyHelper`.

It is a desktop/mobile Qt Quick app using Qt 6.8.3 installation and local LVRS framework. All platforms share a single UI. The home screen displays QuickGenerate at the top, and after generation completion, it displays the image result and a top navigation button, placing the same QuickGenerate at the bottom. Platform detection and responsive layout are delegated to LVRS. The following product dependencies are declared as required packages, and CMake does not download or install additional packages.

Helper's start/foreground/background mutual observation events are also recorded in the common persistent emission queue. SocietyDaemon Since this is received by Society, even if the window is closed, it can be read later by the main app. Creating a request to be delegated to another process or cloning the model is not a change. App-specific data can be connected via `Helper::sendData(topic, payload)`. Qt Sql and QSQLITE drivers are transitive dependencies of the SDK.

<a id="제품-의존성"></a>

## Product dependencies

Explore the next 9 packages with `find_package(... CONFIG REQUIRED)` and specify `Dreamscapes` app's `PRIVATE` link dependencies. The default exploration hint is each `$HOME/.local/SDK/<package name>`, and other installation locations can be specified as `CMAKE_PREFIX_PATH` or `<package name>_DIR`.

|Package|CMake link target|
| --- | --- |
| iiCSMIDI | `iiCSMIDI::iiCSMIDI` |
| iiLicenseManager | `iiLicenseManager::iiLicenseManager` |
| iiLocalDiffusion | `iiLocalDiffusion::iiLocalDiffusion` |
| iiPaintEngine | `iiPaintEngine::iiPaintEngine` |
| iiSocietyClient | `iiSocietyClient::iiSocietyClient` |
| iiSocietyContainer | `iiSocietyContainer::iiSocietyContainer` |
| iiSocietyHelper | `iiSocietyHelper::iiSocietyHelper` |
| iiUpdateManager | `iiUpdateManager::iiUpdateManager` |
| LVRS | `LVRS::LVRS` |

The actual installed package name of the library denoted as `iiLisenseManager` in the request is `iiLicenseManager`. If the package is missing, it fails in the CMake configuration step, and optionally it is omitted or does not create an empty replacement target. This declaration alone does not implement the product feature that uses each library.

Android's `scripts/build-android.sh` builds and installs iiAcountManager, which is a transitive dependency of Helper, first with the same ABI. In Dreamscapes, since it uses the Core/Network part required by the account model, the Quick component of this dedicated installation is disabled. At the end of packaging, `tests/verify_android_bundle.py` checks the arm64 format of the Helper/account library in APK and the actual ELF connection. This check does not run the app or send a login request.

<a id="소스-구조"></a>

## Source structure

It uses the project root, `src/App`, `src/App/Views`, and the `src/App/Generation` dedicated to the creation queue. QWidget is not used.

- `src/main.cpp`: Initializes LVRS runtime and always keeps `Main.qml` open.
- `src/App/Main.qml`: A single `LV.ApplicationWindow` used by all platforms connects the top/bottom placement of creation completion and go back and QuickGenerate.
- `src/App/Views/Home/QuickGenerate.qml`: A common creation panel combining LVRS input fields, buttons, menus, and a responsive stack layout.
- `src/App/Views/Result/GenerationResult.qml` : Scroll gallery of multiple results, Fit zoom view of selected images, and Back/New Canvas buttons with creation in-progress/error status.
- `src/App/Views/Editor/CanvasEditor.qml` : Common editor entry screen for Home/Creation results and Back/Escape return.
- Home opens local `.iisc` documents, `.iiscp` projects and image originals through the shared editor. Cmd/Ctrl and Shift select generated images for independent canvases in one project, with page navigation and complete project persistence. Returning preserves the prompt draft and selection. See [multi-canvas projects](docs/MultiCanvasProject.md) and [Home-to-editor routes](docs/HomeEditorRoutes.md).
- `src/App/Views/Result/Assets/right.svg` : Exact arrow original downloaded from Figma, and LVRS IconButton rotates 180 inside.
- `src/App/tst_Gui.cpp` : Maintains status on Main entry point/window count/screen size change, common panel layout, input and menu selection, and request delivery verification.
- `src/App/Generation/GenerationController.h/.cpp` : Manages app memory queue with fixed Society model reference and existing iiLocalDiffusion executor connection.
- `src/App/Generation/tst_Generation.cpp` : Checks queue isolation, termination on re-run, model change, sequential execution, result verification, cancellation, concurrent windows, and boundary exit.

Creation isolation test allows the default folder prepared by Society under `Files`, but iterates through all subfolders to verify that created files and hidden files are not leaked. Completed images must be saved only in `Generation History`.

Register only sources existing in this repository for GUI test targets. There is currently no `src/App/AI` directory; registering Society's AI integration class path as Dreamscapes test source will cause CMake creation to fail. This source list is verified by the above macOS configuration/build and existing GUI regression tests.

Removed platform-specific window files and the Loader that selected them. Use `Main.qml`'s `LV.ApplicationWindow` provided `isMobilePlatform` ·size class·basic adaptive scaffold policy as is. Initial size is desktop 960×640, mobile 390×844, and actual mobile window size/system transition follows LVRS platform policy. Minimum size is 320×480, allowing narrow layouts even on desktop. UI is not regenerated when window size changes, preserving prompt and screen ratio selection.

Place all views within a single `appContent` area and limit boundaries with `clip: true`. On desktop, start under LVRS's `windowDragHandleTopMargin + windowDragHandleHeight` with default top reserved height 28px. This area is exclusive for macOS traffic light button and window move handle. On mobile, follow LVRS system safe area, and in full-screen or native titlebar mode, do not add unnecessary custom handle margins. When input device is visible, reduce only the bottom available area.

Register only `appContent` in `windowDragExclusionItems`. Content click does not move the window, and top handle is excluded from the exclusion area, making it draggable. LVRS's default edge/corner resize area is also maintained. `viewsLeaveWindowChromeAvailable` checks whether the same boundary is maintained after Home/result screen transition and handle height change.

2026-09-08 This change's macOS build and QML static inspection, GUI test 27 cases and 9 generation controller test cases passed. The 1 selective tests for actual inference were omitted in this UI verification. Metal Renderer also passed screen layout, backward navigation, and continuous generation tests. Signals and Back access and `windowMoveAttempted(true)` were verified in the running app, and no actual window coordinate changes were observed with the auto-drag tool. Therefore, the system distinguishes between accepting a move request and completing actual pointer movement. Verification logs and captures are at `build/chrome-verification/`.

<a id="공통-quickgenerate"></a>

## Common QuickGenerate

The QuickGenerate immediately below Home uses the `Generate history` Society dashboard-like `iiSocietyContainer::DashboardFiles`. It reads the latest `Generation History/` images stored immediately below the connected Society's 20 in descending order by modification time, and previous generation results are displayed even after app re-execution. JSON Hidden files, symbolic links, and previous app-specific subfolders are excluded. It updates on file add, replace, and delete, and on home return and window reactivation, and maintains the list and horizontal scroll position if the content is the same.

`src/App/Views/Home/GenerationHistory.qml` uses the same LVRS Small/Brief file card (140×160, spacing 8) as Society. Horizontal navigation is performed via touch drag, trackpad, mouse wheel, scrollbar, arrow keys, Home, and End. `View all` and card activation open Society's Storage → Generation History via `society://generation-history`. The Society app must also be built with the version that registered this URL, and iOS Dreamscapes includes `LSApplicationQueriesSchemes` `society`. If the execution request fails, guidance is displayed on Home. GUI tests check position, 320px window, 20 count limit, thumbnail loading, auto refresh, horizontal scroll, and URL transmission.

<a id="모바일-home"></a>

### Mobile Home

`src/App/Views/Home/MobileHome.qml` implements the Figma `Dreamscapes/Home` mobile frame `103:1143`. It is used only on mobile platforms and the desktop Home maintains the existing configuration. A single vertical scroll contains compact `QuickGenerate`, new canvas, image, video, board 2×2 quick start, Recent files, Recent published, and Generation History, with `LV.MobileNavigationBar` fixed at the bottom. There are 5 tabs: Home, Tools, Storage, Notification, and Account, and Search is an independent action excluded from the tab count. Since unimplemented destination screens are not arbitrarily created, tabs other than Home emit only destination signals.

The file area connects directly to `DashboardFiles`. Recent files and Generation history expose only the latest 20 items each, while Recent published exposes only the latest 4 items. QML also applies the same upper limit, but actual navigation, sorting, and monitoring are handled by `iiSocietyContainer::DashboardFiles`. The mobile Home and result screens reuse the same `QuickGenerate` instance, so prompts, aspect ratio, and generation count are maintained during scrolling and result transitions. `mobileHomeUsesFigmaSectionsLimitsAndLvrsNavigation` checks the 402×844 iOS theme's 20/4/20 list upper limit, 5 tab+search, 370px content width, bottom 88px LVRS navigation, and vertical scroll.

We restored the existing compact layout based on [Figma QuickGenerate and 203:6930](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=203-6930). The top is the full-width Prompt input field, with Image, aspect ratio, and generation count dropdowns on the left row below 8px, and the Generate button on the right. The one line input field and button use LVRS default 22px, while one line content height is 52px. All generation [prompt input fields](docs/PromptFields.md)automatically wrap text exceeding the width and increase height according to the actual number of lines. Deleting text reduces it again. We do not use the rounded outer composer panel. The outer margin of the mobile Home is 0, the remaining entry points are 10px, and we reuse LVRS input material, color, and Pretendard Medium 13px. The three dropdowns use the original 18×18 arrow SVG of the specified node.

Aspect ratios 1:1 · 4:3 · 3:4 · 16:9 · 9:16and quantities 1 to 10 · 15 · 20 · 25 · 30 · 40 · 50 · 100 · 200 · 500 · 1000 can be reselected. Defaults are empty prompt, Image, 1:1, and 1 items. Figma's 100 indicator is in a selectable state, and the app does not default to creating 100 items when opened. The quantity list supports scrolling and keyboard navigation. On narrow screens, we reduce button spacing and ensure buttons do not overlap with Generate.

We maintain `generateRequested(prompt, mediaType, aspectRatio, count)` contract, Enter submission, space removal, empty input focus, draft preservation, and the top menu and error guidance on the result screen. Image connects to the existing generation queue. We also maintain the current Image/Video selection and the Video unsupported notice, and do not put Video requests into the image queue. We apply the same layout and selection controls to Society as well.

`sharedPanelLayout` checks two-row placement, overlap prevention, and loading of three source icons and 18px size on desktop and mobile 9 sizes. `sharedControlsSubmitCurrentSelection` checks type/ratio/quantity selection, keyboard navigation to the end of 1000 items, and preservation of spaces/Enter/drafts and blocking of Video requests. It also performs regression verification against the existing result screen and 3 fixture creations.

<a id="생성-결과-화면"></a>

## Generated result screen

The multi-result gallery extends the generation results while retaining the toolbar and QuickGenerate from [Figma generation results, 31:81](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=31-81). The default desktop window is 960×640. In the reference 402×575 window, the single-image area retains the existing 402×242px Fit display. Buttons at the top of the content are 22px, and the bottom QuickGenerate is 72px including outer margins for a one line prompt. Its height grows automatically for multiline input, and input taller than the window scrolls vertically within the composer. The background, buttons, inputs, and text reuse the existing LVRS theme and components.

If multiple completed images exist, the entire space between the top toolbar and bottom QuickGenerate is used as a scrollable gallery. Like the photo app, it fills with `PreserveAspectCrop` square thumbnails at 2px spacing, arranging at least 2 columns with one column per available width 160px. For example, 390px window has 2 columns and 960px window has 6 columns, changing according to resize. Clicking a thumbnail displays the original proportionally large in the entire available area, and Back returns to the gallery at the same scroll position. Pressing Back in the gallery navigates home, ending the lifecycle of that result screen. Submitting a new generation from home or the zoomed screen clears previous images, selections, zoom, and scroll states, displaying the new request's progress and results. Selection is done with arrow keys, Home, and End, and zooming with Enter. Right-click and long-press open the existing save menu for the selected image.

`GenerationController.completedResults` provides all output images of all completed tasks in the current app session in generation order. Each item's `imageSource` · `image` and prompt, ratio, and model information point to the same image. Failed, cancelled, and in-progress tasks, deleted files, and symbolic links are excluded, and reconnecting the repository clears the list. The existing first image contract of `latestResult`'s recent tasks is maintained. `enqueue()` adds the entire verified generation bundle to the queue and exports `submissionQueued(QStringList jobIds)` to one time. The result screen uses only tasks and completed images from this ID list. When completed images are added within the same request, scroll position and the currently zoomed selection are preserved, and editing the next prompt does not change the saved selection image or New Canvas input. Closing the screen maintains the actual generation queue and Society's completed image files.

An ongoing generation job appears as the last preview tile in the gallery and cannot be selected for saving or project input. The existing Qt Quick [GridView](https://doc.qt.io/qt-6.8/qml-qtquick-gridview.html)provides item reuse and vertical scrolling, and the Qt Quick Controls ScrollBar uses LVRS colors. Thumbnails are generated only near the viewport, and [Image.sourceSize](https://doc.qt.io/qt-6.8/qml-qtquick-image.html#sourceSize-prop)is limited according to tile size and display scale. Originals load in zoom mode; no new image library or external dependency is added.

Generated images are adjusted to fit within the frame while maintaining the original aspect ratio at `Image.PreserveAspectFit`. Images are centered horizontally and vertically, and if the frame and ratio differ, remaining space is left as padding. `clip: true` ensures images do not draw over buttons and panel areas. If a Generate request is validated and enters the app memory queue, it immediately moves to a new result screen without waiting for process start or image completion. Submissions that directly call the controller, like MCP, use the same path. If input or model validation fails, the existing screen is held and the error is displayed. Until the first intermediate image arrives, an empty image area and waiting/preparing status are shown, and then the actual VAE decoded image is updated to Fit at each denoising step of that request. During asynchronous loading of the same request, the previous image is held. If a previous request is running, `Queued` is displayed for the new request, and previous preview/error/completed images are not mixed. Returning to home clears the display status, and the intermediate image, failure, cancel, and queue notification are not reopened. The `View result` button to reopen a finished result screen is not provided.

`GenerationController.previewImage` , `previewStep` , and `previewTotalSteps` update the `IILD_PREVIEW` event of the current process. The iiLocalDiffusion `--preview-dir` callback copies the sampler's latent and decodes it with the VAE at each step. Noise images or progress are not arbitrarily generated in the UI. Step-by-step PNGs saved atomically are transmitted after preparation, and the actual number of steps is displayed on the screen. Previews exist only with long side 512px or less and only in Society's `Models/.society-runtime/iiLocalDiffusion/` temporary work location. They are removed after completion, failure, or cancel, and only images that pass the final file verification remain in `Generation History/` and `latestResult`. Step-by-step VAE decoding incurs additional computation time.

2026-09-08 macOS practical verification executed Society's `redLilyIllu_v10.safetensors` in MPS/FP16 , 512×512, 20 steps. 20 different intermediate PNGs were observed, and the controller also received 20/20 steps. Immediate transition in actual creation, screen update during denoising, final image saving, and temporary directory removal were confirmed. Local verification records and frames are in `build/live-preview-verification/verification.json` and the same directory.

The Back button is placed below the top handle, so it does not overlap with LVRS's default corner resize area.

Right-click or press and hold a completed image to open the LVRS context menu. **Save to File** opens the system save dialog. The menu responds only within the rendered image bounds and stays inside narrow windows. A short primary click and temporary generation previews do not open it. If another generation finishes while the dialog is open, saving still exports the originally selected image.

`ImageFileExporter` copies the original bytes, preserving resolution and metadata. It uses `QSaveFile` for atomic file replacement and accepts Android document-provider `content:` destinations. Cancelling leaves files untouched. Success and failure appear in the result status. The menu, dialog title, file filter, progress, success messages, and application-defined errors use English. The implementation reuses Qt 6.8.3 [FileDialog](https://doc.qt.io/qt-6.8/qml-qtquick-dialogs-filedialog.html), [TapHandler](https://doc.qt.io/qt-6.8/qml-qtquick-taphandler.html), and [QSaveFile](https://doc.qt.io/qt-6.8/qsavefile.html).

On macOS 11+, iOS 14+, and Android, **Save to Photos** adds the original image to the system photo library without a file picker. Duplicate requests are blocked during an import. Permission denials and failures allow a retry. Support follows the native platform, so applying a mobile theme on Windows or Linux does not expose the Photos action.

Apple platforms use [PhotoKit](https://developer.apple.com/documentation/photos/phphotolibrary), `PHAccessLevelAddOnly`, and `PHAssetCreationRequest` to import the original resource. The existing Qt/Society Info.plist receives `NSPhotoLibraryAddUsageDescription`; full-library read permission is unnecessary. **iCloud Photos syncs the saved image when enabled for the system photo library.** [Apple identifies the System Photo Library as the library used by iCloud Photos](https://support.apple.com/en-ie/104946). The app does not change synchronization settings. “Saved to Photos” confirms the local library transaction; cloud upload completion depends on connectivity, account state, and available storage.

Android writes the original bytes to `Pictures/Dreamscapes` through [MediaStore.Images](https://developer.android.com/training/data-storage/shared/media), making the image available to the default Photos or gallery app. Android 10+ needs no photo-read or storage permission for these new images and uses `IS_PENDING` to publish only completed writes. Android 9 requests storage-write permission; the manifest limits that permission to API 28 and earlier. Failed writes remove the incomplete MediaStore entry. Both backends use existing Qt and native OS APIs without an additional external package.

Validation on 2026-09-08: macOS build, qmllint, and all four CTest suites passed, covering menu gestures, asynchronous completion, failures, duplicate prevention, and callbacks after a view closes. A generated flower image was saved through the actual app menu and appeared in Photos with “Synced to iCloud” status. Exporting its unmodified original from Photos produced the same 346,972-byte PNG and SHA-256 as the generated source. Evidence: `build/photo-library-verification.json` and `build/photo-library-original/image.png`.

The Android 16 (API 36) emulator test APK exercised the C++ → JNI → Java → MediaStore path: original bytes matched, the saved row had `IS_PENDING=0`, and failed writes left no row. All four checks passed, and the test-created image was removed. The app's only photo-related manifest permission is `WRITE_EXTERNAL_STORAGE maxSdkVersion=28`. The signed iOS device bundle passed PhotoKit-link, add-only permission-description, and Society App Group checks. Saving to Photos on a physical iOS device and the Android 9 permission dialog have not been exercised.

To rerun the native Android tests, prepare the SDKs with `scripts/build-android.sh`, connect an API 29+ emulator, and run the following commands. Test mode uses separate Qt packaging directories for the app and test APK. The regular build script disables test mode and retains the usual app APK path.

```sh
cmake -S . -B build/android/build -DDREAMSCAPES_BUILD_ANDROID_PHOTO_TESTS=ON
env -u QT_QML_IMPORT_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
  ANDROID_SERIAL=emulator-5554 \
  JAVA_HOME=/Applications/CLion.app/Contents/jbr/Contents/Home \
  GRADLE_USER_HOME="$PWD/build/android/gradle" ANDROID_USER_HOME="$PWD/build/android/user" \
  /Volumes/Storage/Qt/6.8.3/macos/bin/androidtestrunner \
  --path "$PWD/build/android/build/android-build-DreamscapesPhotoNativeTests" \
  --apk "$PWD/build/android/build/android-build-DreamscapesPhotoNativeTests/DreamscapesPhotoNativeTests.apk" \
  --make "cmake --build $PWD/build/android/build --target DreamscapesPhotoNativeTests_make_apk" \
  --skip-install-root --adb /opt/homebrew/bin/adb --timeout 60 \
  -- -o "$PWD/build/photo-native-results.txt,txt"
```

`resultImageContextMenu` checks right-click, mouse/touch press-and-hold, and narrow-window placement. `resultImageSaveDialogPreservesSelectionAndCancel` checks the English menu and dialog flow, cancellation, the frozen source selection, and full-resolution output. `resultImageSaveToPhotos` checks the English Photos action and feedback. `Dreamscapes.ImageExport` covers original bytes, Unicode/spaces/special characters in paths, overwriting an existing file, and preserving files on invalid input. Native mobile file pickers require separate device validation.

The earlier file-export verification passed the build, three CTest suites, and qmllint. The actual macOS app exported the generated “A beautiful flower” image through the native save dialog; the 512×512 PNG matched the Society source and generation manifest hash. Evidence: `build/flower-save-verification.png` and `build/image-save-verification.json`.

QuickGenerate maintains `implicitHeight` when navigating between the Home and Result screens, changing only the vertical position. Simultaneously switching top and bottom anchors prevents the issue where the panel expands to the full window height. The repository integration GUI test also checks the actual clickable area of the panel height and the Home model selection button after Back/New Request.

QuickGenerate is not regenerated during movement or resizing. Since the current app session maintains the previous prompt and ratio/quantity, you can modify from the bottom and generate continuously. When the app is reopened, the queue, prompt, and result screens are not restored, while the completed image file saved in Society is maintained. The next prompt entered by the user during work is not overwritten by the completion event. Upon submission, the keyboard and menu are closed, and in-progress, pending, failed, and image loading errors are displayed within the result screen.

`GenerationController.latestResult` returns the ID ·prompt·ratio·model·save location information of the work along with the verified recent completed image URL (`imageSource`). If selected from the gallery, `selectedResult` maintains the information of the corresponding image. `New Canvas` is activated only when the selected completed image is loaded after waiting and generation is finished, and it delivers `Main.newProjectRequested(url imageSource, var generationResult)`. Intermediate preview is not passed as project input. This value is the information of the generated work displayed on the screen, and is distinguished from the next prompt being modified in the input field. This button delivers the selected image and metadata to `CanvasEditor.qml` and navigates to the editor view. A new canvas in Home opens a blank canvas in the same view. Back or Escape in the Editor returns to the screen before entry, preserving the generation input draft and gallery selection. Existing `newProjectButton` object name and `newProjectRequested` signal are maintained for integration compatibility. The editor uses iiSharedCanvas native documents for blank canvases, .iisc files and imported image layers. See [EditorNativeCanvas.md](docs/EditorNativeCanvas.md) for working-file persistence, brush and layer editing, basic vector creation and current tool limitations.

`canvasRoutesPreserveSelectionAndDraft` validates the New Canvas label in desktop and mobile themes, editor transition via actual button click, transfer of selected images and metadata, draft preservation after return, and entry to the empty canvas on the mobile home and initialization of the previous image.

`resultGalleryLayoutAndSelection` checks the wide grid, access to the last image, bounded thumbnail generation, zoom and project selection, and preservation of selection, scrolling, and drafts after results are appended to the same request, using 320, 390, 960, and 1440px windows and a list of 1,000 images. `countSelectionCreatesThreeImagesInSociety` verifies that submitting an actual QuickGenerate count of 3 produces 3 stored files and 3 gallery entries, and that only the new 3 images appear for the next submission. `newSubmissionReplacesDismissedResults` checks the pending, preview, and result displays for a new request after leaving Home, along with preservation of previous files, for desktop single results and mobile-size multiple results. `dismissedGenerationCannotReopenOrContaminateTheNextSubmission` also checks leaving during generation, direct controller submissions, isolation of late previews and completion from a previous job, and staying on Home. `batchSubmissionKeepsOneModelSnapshotAndRejectsInvalidCounts` verifies one ID notification per successful batch and no notification for invalid submissions. `completedResultsExposeEveryImageAndExcludeUnpublishedFiles` checks multiple outputs from one job, consecutive jobs, exclusion of failed, canceled, deleted, and redirected outputs, and storage switching. These tests use deterministic generation fixtures and do not prove real model inference or execution on physical mobile devices.

`resultScreenLayout` checks dimensions, original aspect ratio, and Fit size preserving the entire image on the reference screen, narrow window, desktop, mobile theme, and landscape screen, and inspects the bottom panel and menus opening upward. It compares the actual display size of horizontal, vertical, and square originals with the expected size fitted to the frame. `generateOpensResultImmediatelyAndDisplaysEveryPreview` checks immediate transition right after Generate, preview replacement at each step, progress step, New Canvas disabled, back/reattempt during generation, and draft preservation. The generation controller test inspects events arriving separately, duplicate/invalid events, failure/cancellation after preview, temporary file cleanup, and final file separation. `generateButtonUsesSocietyStorage` checks completion transition and continuous generation, and project input. The deterministic generator for testing validates the UI ·save protocol and does not substitute actual model inference. Images captured via the following command are also color patterns for Fit inspection.

```sh
QSG_RHI_BACKEND=metal QML_DISABLE_DISK_CACHE=1 DREAMSCAPES_CAPTURE_DIR="$PWD/build/result-verification" \
  build/bin/DreamscapesGuiTests resultScreenLayout:result-figma-402 resultScreenLayout:result-ios
```

<a id="society-모델로-이미지-생성"></a>

## Generate image with Society model

When a container is opened in Society, the original path and UUID are registered in `iiSocietyContainer::SharedStorage`. Dreamscapes automatically opens the same default repository and can also specify a particular original with `--society-container /absolute/source/path`. This path is an original with 8 areas, not the public drive in Finder. If the default repository is changed, reopening Dreamscapes follows the new selection. The app-specific model directory and model copy are not created.

After dropping `.safetensor` or `.safetensors` into the Society window, select the model in Dreamscapes and press Generate. The Diffusers package (including `model_index.json`) is also available for use in Society's `Models/`. The list is a candidate storage format, and the role and required components such as model architecture and LoRA are checked by iiLocalDiffusion. The UI for assembling separate VAE, text encoder, and adapters is not currently included.

The default generation size of QuickGenerate is iiLocalDiffusion SDXL default value and 1024×1024, using 10 steps. Changing the ratio maintains the short side 1024px and rounds the long side to the nearest 8px unit. Therefore, 4:3 is 1368×1024, 3:4 is 1024×1368, 16:9 is 1824×1024, and 9:16 is 1024×1824. The ratio is an approximation aligned to the generator's 8px grid. The same calculation is applied to desktop worker and mobile native generation requests, and pre-model preparation uses the default square 1024×1024. The test `GenerationRuntime.imageExtent` also refers to the short side and maintains the explicit value `steps`. Options not separately specified such as CFG, precision, and scheduler continue to be determined according to iiLocalDiffusion model. `Dreamscapes.Generation` verifies that the short sides of the five ratios are maintained at 1024px and whether the final file of that size is saved from both execution paths.

Completed generated images are not Assets. Results of all apps are saved as image files immediately below `Generation History/`, without creating subfolders for each app or task. Dreamscapes avoids name collisions by naming files with task UUIDs and image sequence numbers like `<UUID>-0001.png`. Files are not added to `Asset Library/` through generation alone.

Queues, prompts, model references, execution status, and result metadata are kept only in the memory of the corresponding app instance. Request JSONs are not recorded in Society or other persistent storage, and other app instances do not restore by reading or re-executing this queue. When the app is closed, pending requests and session information are deleted, while completed images already saved to `Generation History/` are retained.

The generator CLI processes outputs, previews, execution data, and caches that require file paths in Society's `Models/.society-runtime/iiLocalDiffusion/`. A `QTemporaryDir` is created for each task, and `--work-dir`, `--cache-dir`, `--preview-dir`, `--output-dir`, and subprocess temporary paths are placed here. Temporary files are cleaned up upon completion, failure, cancellation, or normal app termination. The feature to specify app-specific temporary paths has been removed. Generation does not start if Society is disconnected or the task path is redirected externally. `.society-runtime` is local derived data in Society and is excluded from synchronization and the model list. Test Society containers are isolated under `build/`.

Even if the model selection is changed, the `{containerId, path, format, fingerprint}` reference of the request already in memory is retained. The system rechecks whether the original model is the same just before and after execution, and does not automatically replace with deleted, modified, or redirected models. Fingerprints are limited change-detection values from the SDK, not full weight snapshots. `.safetensor` and uppercase extensions are normalized in temporary task links without changing the original name.

SD1 · SDXL single checkpoint uses the model settings and tokenizer included in the iiLocalDiffusion installation. No ComfyUI installation, server, or node initialization is required, and `--backend local` is specified to use an independent path.

Finished Anima safetensors also use the same Mac worker and queue. iiLocalDiffusion determines the Anima tensor structure and calls the native SDK engine inside the worker. Files including built-in text encoders and VAEs are not classified as standalone VAE files, and external VAEs are not forced. Model preparation maintains native context without sampling, and actual weight placement is performed during the first generation. Results record `backend: native`, the original model hash, seed, and cache hit status. Other models' Diffusers paths and the app's short variant 1024px · 10 steps and sequential queues are retained. This native path reports generation progress and does not provide per-step latent image previews. The desktop worker does not inherit the default C++ native request total time limit of 15 minutes. Even if processing large images at VAE takes a long time, it is processed to completion like the existing worker, while Cancel and app termination maintain the existing process cancellation paths.

The default seed is randomly determined for each generation request. Dreamscapes does not pass a fixed seed, and the result metadata, including the actual seed chosen by the checkpoint runner, remains in the `generation` field of app memory after job validation. If the SDK specifies a seed, that value is held.

The app instance executes its own memory queue in order. No queue files, status polling, or shared worker locks are placed between app instances. Cancel cancels pending requests or current execution for this instance. On desktop Unix, it terminates the separate process group for that job and its child processes as well.

Start `iild-generate --worker` for each app instance and pass argument arrays such as one time and `--model-path <Society original> --prompt ...` along with the request ID to a single line in stdin at JSON. Python dependency initialization, model hashing, and memory caching for compatible load models are handled by iiLocalDiffusion. After receiving the SDK's `IILD_RESULT`, Dreamscapes passes the next queue. If cancellation or abnormal process termination occurs, restart the executor in the next request. Even after a general request failure, process the next request, and keep only the queue and execution diagnostics in app memory.

Python The temporary directory used by the JIT executor is retained until the process terminates, and images, previews, and request files are placed in separate temporary directories for each job, removing them upon completion, failure, or cancellation. Both directories are located in Society's `.society-runtime`. HF · Transformers · Torch · JIT cache also uses this executor directory, and model downloads are blocked with offline settings. When the app terminates, the executor is stopped and the temporary directory is removed. `PYTHONDONTWRITEBYTECODE=1` is retained, but no new `PYTHONPYCACHEPREFIX` is specified, so the installed Python bytecode cache is read.

For Generate, models not held are first executed on iiSocietyGeneration at Society host. After host acceptance, iiSocietyClient downloads the selected model and shared generation resources to the local Society asynchronously; this replication is not a completion condition. SDK completes by verifying completion events, execution logs, and the size and decoding of all images, then atomically saves only the completed image file to `Generation History/`. Society preserves the image extension and bytes from the temporary location to the final save location. Multiple images are saved directly below the same area. Failure, cancellation, preview, JSON, and cache are added to the results list without overwriting existing files.

The `.dreamscapes/generation/` and `Generation History/Dreamscapes/` of Society created by previous versions are cleaned up when connecting to the repository. Previous pending requests are not executed. If a valid completed request points to a previous Asset Library image, it is moved to Generation History for preservation, and existing completed images, models, and other assets are retained. If the previous worker is in use or the parent path is redirected, the corresponding file is not deleted. In this case only, the previous worker's lock is checked and is not used for saving the new queue.

`Dreamscapes.Generation` validates per-instance queue isolation, queue deletion on retry, preservation of completed images, removal of temporary data on normal, failed, cancelled, and terminated states, rejection of external redirections in Society job paths, and verification of previous data cleanup and model link preservation. It also maintains model references, multiple results, name collisions, partial failures, and preview checks. `Dreamscapes.Gui` checks whether the actual Generate completed image is loaded from Generation History.

The resident executor check includes PID reuse for continuous jobs, handling of general errors and subsequent requests after split received long Korean errors, restart after process interruption, and separation of lifetimes for job temporary files and executor temporary files. `realSocietyInference` executes two actual requests with the installed SDK to verify the same PID and initial model configuration and device placement each 1 times. In the second request, the full model hash, configuration file read, model reconstruction, and device placement must all be 0 times and the cache must hit. Older SDKs without `--worker` are rejected during configuration.

iiLocalDiffusion maintains model configuration and device placement separately. Changes to generation options such as sampler, prompt, and seed do not trigger model reconstruction or reassignment. If only device and placement settings change, existing configuration components are reused and only necessary placements are updated. Configuration file parsing is also cached based on change detection and does not permanently store the corresponding cache or weights separately in Dreamscapes or Society.

When the app reaches `Qt::ApplicationActive`, `GenerationController` passes iiLocalDiffusion status and the selected `foreground` model to Society. From before pressing the generate button, the SDK prepares the model configuration and GPU placement, and maintains it in that executor even if the queue is empty. Prepare-only commands do not create images, previews, or job records. `inferenceStatus` exposes the prepared status, device, GPU resident status, and errors verified by the SDK. The model is not deemed ready solely based on the executor start event.

If the active status or model selection changes while preparing, the latest status is collected and processed after the current command completes. The actual generation queue takes precedence, and the queue and prepare commands are not recorded as the same job. In Background, new pre-preparation is not started, and ongoing generation and existing session caches are maintained. When Foreground resumes, the SDK's existing model is verified and reused. If no model exists, the existing model is not marked as ready. Caches that disappear due to process termination or cancellation are reconfigured on the next prepare/generation.

Before transmission, `IILD_READY.capabilities`'s `foreground-residency` support is verified. Blocking is applied to prevent prepare commands from being passed to older executors as incorrect image generation requests. `foregroundPreparesWithoutAQueueAndReusesTheWorker` and `foregroundModelChangesAndQueuedRequestsRemainSeparate` verify the separation of prepare, actual queue, and model selection, and `foregroundApplicationPreparesBeforeGenerate` in the GUI checks the actual app active status connection. `foregroundPreparationFailureCanRecoverWithAnotherModel` and `foregroundControlRequiresTheSdkCapability` verify error recovery and detection of older SDK. Mobile observes the same app active status but the limitation of native inference not being implemented in existing documents remains applied.

The next selection verification checks GPU pre-preparation and reuse of configuration and placement for the first generation using the installed SDK. Since `Generation History` leaves one result image, a separate verification container with local SD1 Diffusers models is used.

```sh
DREAMSCAPES_REAL_SMOKE_CONTAINER=/path/to/verification-society \
  IILD_GENERATOR_EXECUTABLE="$HOME/.local/SDK/iiLocalDiffusion/bin/iild-generate" \
  build/DreamscapesGenerationTests realForegroundPreparation
```

Reuses the existing Qt Core/Gui's QProcess· QTemporaryDir · QSaveFile and iiLocalDiffusion. Does not introduce an additional queue framework or persistent database. Inference dependencies and licenses follow the existing iiLocalDiffusion Diffusers / PyTorch runtime and bundled setting resources. Linking only the SDK library does not mean the Python inference environment is installed.

The required versions are `iiSocietyContainer >= 0.10.0`, `iiSocietyHelper >= 0.5.0`, and `iiLocalDiffusion >= 0.5.0`. Also checks the `--model-path` contract of the executor found during desktop configuration to reject old installations. `DREAMSCAPES_DIFFUSION_EXECUTABLE` CMake path or `IILD_GENERATOR_EXECUTABLE` execution environment designates the SDK executor. The Python environment defaults to using the SDK's managed environment and can be specified via `DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE` CMake option or a preferred `IILD_PYTHON_EXECUTABLE` environment variable. A single checkpoint uses the current SDK's standalone executor and bundled SD1/SDXL settings and does not require ComfyUI. Missing runtime or unsupported models display the failure cause.

If an `Reference dependency accelerate is missing` error occurs, it is verified from the Python path of the actual generation worker. Python for build and test is separate from Python for inference, and past apps fixed the Python path for the Codex tool to `DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE` take precedence over the SDK's `runtime-python.json` setting. Standard deployment uses the CMake option left empty to use the SDK management environment. It must also be verified whether the installed app is the latest build. Import `accelerate`, `torch`, `diffusers`, and `transformers` from the management environment, and check the default LoRA and the `peft`, `sentencepiece`, and `pip check` required by the tokenizer. The installed version is also aligned with the SDK's fixed version `reference/diffusers/requirements.txt`. Society shared generation resources are separately verified via the following procedure. Generation success is not determined solely by dependency checks or `--print-config` passing, but by verifying completed tasks with the actual model and PNG.

iOS accesses the Society source container of the same device as `iiSocietyContainer_configure_ios_client()` and `SOCIETY_IOS_APP_GROUP` · `SOCIETY_IOS_TEAM`. Connection, authentication, and synchronization for other devices are performed by Society. Finder/ iOS Files continue to expose only `Files/`.

<a id="society-간-동기화와-로컬-생성"></a>

### Synchronization and local generation between Society

1. Desktop Society and iPhone Society connect with the same account.
2. Since Society synchronizes the host's model list and version first, selection is possible even before the original download.
3. Dreamscapes uses the local App Group's storage map and iiSocietyClient's authenticated host connection. The user does not separately enter the host URL.
4. In the state of no original possession, it is immediately submitted to the Society host's iiSocietyGeneration queue, and the model is asynchronously replicated after acceptance. Held models use the existing local engine. Results are saved to the local Society Generation History.
5. Device-to-device replication of the completed image is also performed by Society in the next synchronization. No host connection is required for local generation after model reception.

Dreamscapes uses iiSocietyClient and iiSocietyGeneration SDKs and does not implement direct authentication and transmission. Society host also calls the iiLocalDiffusion engine of the SDK instead of the Product executor.

Native generation requires iiLocalDiffusion 0.5.0's `IILD_ENABLE_NATIVE_DIFFUSION=ON` package. The current API takes a single checkpoint file as input, and model loading and inference are performed on the app worker thread. There is no guarantee that any model will fit in iPhone memory, and upon failure or cancellation, the completed image is not posted. Builds without the engine display a local generation unavailable status. Refer to [SDK contract](../../SDK/iiLocalDiffusion/docs/native-image-generation.md).

For iOS, iiLocalDiffusion places VAE rendering and weights on the CPU and performs denoising on Metal. This is a policy to avoid the impact of VAE observed Metal command buffer failures and GPU recovery, maintaining existing VAE tiling and default short side 1024px · 10 step · sequential queue. 9:16 completes in 1024×1856 internal canvas in 1024×1824 RGB. All built-in VAE and Qwen Image RGB · SDXL · FLUX.1 · FLUX.2's automatic VAE fallbacks use the same execution layout. CPU render time and actual device generation success must be verified separately from build and bundle checks.

With explicit VAE batching, text encoder and denoiser weights can be loaded to the GPU in segments from mmap files and reclaimed according to memory budget. If only VAE batching is specified, the engine's automatic memory layout is disabled and GPU weights may accumulate during denoising, so iOS bundle checks require CPU VAE and reclaimable weight batching together.

iOS GPU budget is limited to Metal or less of the recommended 60% work set, leaving at least 1.5GiB or 40% of the app's remaining memory for CPU rendering, file mapping, and system. The smaller value of the two constraints is emitted in 256MiB units without hardcoding device model names. Instead of executing until memory is full, the engine can choose segment splitting, and image size, step, and precision remain unchanged.

`Dreamscapes.LocalSociety` synchronizes the actual loopback TLS of the two Society role 700,000 byte models, verifies the local Helper path and container UUID, then disconnects all connections. It then verifies that the path passed to the Dreamscapes creation fixture is perceived by the client `Models/` and that results are stored only in the client `Generation History/`. This fixture is a process and storage contract verification, not actual model inference or physical iPhone success evidence. The iOS bundle check verifies App Group, native path, signature, and the absence of remote client/Sync.

2026-09-10 verification passed Society, 16/16, Dreamscapes, 5/5, local synchronization and creation regression, and both app qmllint tests. The new native SDK was built with macOS and iOS arm64, and macOS confirmed actual `redLilyIllu_v10.safetensors` 512×512 20 step local image generation. The iOS bundle passed signature, App Group, local engine, and remote Sync library absence checks. In the final check, iPhone connections were restored, the latest development signed app was installed and executed, and the process was verified. It was also confirmed that 6.46GB checkpoints exist in the Models of iPhone Society. The device connection was broken during the additional creation verification build installation step, so iPhone native inference completion has not yet been verified. The detailed log is `build/society-local-generation/REPORT.md`.

`DREAMSCAPES_LOCAL_RUNTIME_PROBE=ON` is an option for real-device verification and is OFF by default. This build's `--verify-local-generation --local-model <local model ID> --local-prompt <sentence>` selects a prepared local model to run the actual controller. It does not receive a host address or pass a model, and records only the result status and app screen in Documents. After verification, the regular build is reinstalled.

Previous `build/iphone-remote-generation/` logs are evidence of the past method of transferring desktop inference result PNGs to the iPhone. They are not used as evidence of model synchronization and iPhone native creation in the current structure.

<a id="macos-빌드와-실행"></a>

## macOS build and execution

Qt installation path is `/Volumes/Storage/Qt/6.8.3/macos`, and all 9 product package installations are required. CMake, 3.31 or more, Ninja, C++23 compiler is required. `find_package`'s `EXACT` condition prevents selection of other Qt versions.

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug --parallel
ctest --preset macos-debug
open build/bin/Dreamscapes.app
```

All creation files are placed in `build/`. Existing `cmake-build-debug/` is preserved but not used. CLion's existing Debug profile is configured to use `build/` and Qt LVRS installations.

The macOS development app records the installed LVRS library directory as the build RPATH, so it can be executed without separate `DYLD_LIBRARY_PATH` settings. The current output is a development build referencing the installed Qt ·LVRS and is not a redistributable standalone package.

<a id="다른-플랫폼"></a>

## Other platforms

Mobile bottom toolbar icons include the original SVG of Figma `103:1211` in the bundle and render using the LVRS original color preservation option. Mapping and verification methods are recorded in the [mobile icon contract](docs/MobileNavigationIcons.md).

<a id="ios-기기-패키지"></a>

### iOS Device package

iOS build maintains the required SDK 9 items. Connect static LVRS · Society Container·Helper and the remaining target iOS SDK, and include the dynamic product library and iiCSMIDI's transitive dependency iiFileProvider in the app's `Frameworks/`, signing and linking them together via the executable's rpath. Since static Qt's SQLite plugin is explicitly registered, the Helper's persistent queue can open the DB on devices as well. The app ID is `com.iisacc.dreamscapes`, and the shared group is a `group.com.iisacc.society` like Society. Xcode automatic signing requires `SOCIETY_IOS_TEAM` and a device-registered profile. All iOS artifacts go inside `build/ios-device/`.

The actual signed device package verifies the completeness of platform·arm64 ·shared permissions·device profile·embedded dynamic library, and LVRS registration·resources, using the following command. After this verification, device installation and execution are checked separately.

```sh
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer \
python3 -B tests/verify_ios_bundle.py build/bin/Dreamscapes.app \
  --device <iPhone-UDID>
```

iOS uses a native artifact path that reads the device's Society repository and does not run the desktop executor directly on the device. Mobile native inference is not connected merely by app installation or SDK linking.

2026-09-08   Xcode   27 beta 6 and Qt 6.8.3 to iPhone   15 Pro Max ( iOS   27 ) for development signing build 0.1.0 was installed. Verified the app list, running processes, and actual home screen, then re-executed Society to confirm connection to the same App Group repository. Verification logs and screenshots are in `Workspace/build/ios-install/`. Since a warning for Objective-C class duplication statically included in the dynamic SDK remains, it is distinguished from long-term usage stability verification.

Installation packages for 9 product dependencies of the ABI, such as the target platform's Qt   **6.8.3** kits, are required. Configure the project with the kit's `qt-cmake` and specify the installation root in `LVRS_DIR`. The LVRS installation package selects the library for the target platform. Other product packages must also provide installation packages for that platform. Windows · Linux kits are used on that operating system.

Android uses Android   SDK / NDK / JDK, while iOS requires libraries for Xcode and the target device or simulator separately. In cross builds, no host GUI tests are generated. When switching platforms, do not mix CMake caches from different toolchains; instead, clean the artifact-specific `build/` and reconfigure in the same path.

The current Android LVRS build is for `arm64-v8a`, so it matches the `android_arm64_v8a` kit. The iOS LVRS static archive does not have a separate QML plugin, so apply `WHOLE_ARCHIVE` from the app link to ensure QML type registration and resource initialization objects are not removed. Bundle checks also verify whether the static generator for the corresponding qrc translation unit remains when Release LTO inlines the initialization function.

The host validates the single window of Main and the actual runtime platform value of LVRS. Layout checks with iOS · Android themes also use the same Main. Since the read-only runtime platform is not forcibly changed, it does not replace mobile binary build, system safe area, or device execution.

<a id="android-에뮬레이터-실행"></a>

### Android Emulator execution

`scripts/build-android.sh` creates a signed debug APK using installed Qt 6.8.3 Android arm64 kit, NDK, SDK, and JDK 21. App artifacts are placed at `build/android/`, and per-SDK builds are placed at each SDK's `build/dreamscapes-android/`. While maintaining the host build and mobile builds for other products together, it adheres to the SDK's build path constraints. It includes 9 essential product dependencies and transitive dependencies in the APK and does not create empty stub libraries.

The Android build for iiCSMIDI · Society family 3 items, iiLocalDiffusion, and iiCSMIDI's transitive dependencies iiFileProvider is built from the actual source of `Workspace/SDK` and installed at `build/android/sdk`. iiFileProvider is built first and also included in the APK. The existing macOS SDK build is maintained. iiLocalDiffusion disables Apple-specific Core ML ·MLX and optional LibTorch with existing options, and after verifying existing dependencies [json-c 0.18](https://github.com/json-c/json-c/releases/tag/json-c-0.18-20240915)with SHA-256, it statically links them. The connection scope between the generation engine and app UI remains unchanged.

The environment checked on the current machine is Android SDK `/opt/homebrew/share/android-commandlinetools`, NDK r29 `/opt/homebrew/share/android-ndk`, JDK 21 `/Applications/CLion.app/Contents/jbr/Contents/Home`. SDK ·NDK can be changed to `ANDROID_SDK_ROOT` · `ANDROID_NDK_ROOT`, JDK to `DREAMSCAPES_JAVA_HOME`, Qt to `DREAMSCAPES_QT_ROOT`, and product SDK path to `DREAMSCAPES_SDK_SOURCE_ROOT` · `DREAMSCAPES_SDK_INSTALL_ROOT`. Gradle cache and Android build settings are also placed under `build/android`.

```sh
./scripts/build-android.sh
adb -s emulator-5554 install -r build/android/build/android-build/Dreamscapes.apk
adb -s emulator-5554 shell am start -W \
  -n com.iisacc.dreamscapes/com.iisacc.dreamscapes.DreamscapesActivity
```

The existing `WeUs_API_36` AVD is configured as Pixel 9 Pro, Android 16 (API 36), and arm64-v8a. If the emulator is off, turn it on via the following command in a separate terminal. `-read-only -no-snapshot` preserves existing AVD data and snapshots, and apps installed in this run are maintained only in this emulator session.

```sh
ANDROID_SDK_ROOT=/opt/homebrew/share/android-commandlinetools \
ANDROID_AVD_HOME="$HOME/.config/.android/avd" \
  /opt/homebrew/share/android-commandlinetools/emulator/emulator \
  -avd WeUs_API_36 -read-only -no-snapshot -no-audio -gpu auto
```

2026-09-06 Android emulator verified APK signature validation, installation, Activity execution, LVRS's Android / Vulkan initialization, and mobile top panel display. It confirmed that even when entering the prompt with a real Android keyboard and selecting 16:9 from the menu and pressing Generate, the app remains held. Execution screen and logs are saved to `build/android/emulator-*.png` · `build/android/emulator-runtime.log`. The installed LVRS outputs warnings for 4 window padding properties missing at Qt 6.8, but does not block app loading and display. Physical device and iOS execution require separate verification.

<a id="검증"></a>

## Verification

Dependency verification starts from  `cmake --preset macos-debug` . When essential packages are missing, check if configuration failure and guidance for the package's Config file are outputted. After all packages are installed, configuration, build, and CTest must succeed in order. Even if existing  `build/`  test binaries pass in a state where configuration or build failed, it is not treated as the verification result of the new dependency configuration.

`ctest --preset macos-debug`  inspects single top-level window of Main,  LVRS  platform determination, last window close signal, and window cleanup upon engine release in offscreen/software environment. Changing window size from  960  to  320  to  800  to  1440  to  390px  verifies  LVRS  size class change, holding same panel and input/ratio state, and delivering requests after change. Common panels are verified for top placement and button overlap at desktop  320  · 960  · 1440px,  402px  original dimensions, and  iOS  · Android  theme's  320  · 360  · 390  · 844px  widths. Key input, menu opening and selection, Generate, Enter request, and space input blocking are also checked in both desktop and mobile themes.  QML  disk cache is turned off, so no  QML  cache is left in user cache.

Test is executed within  `QGuiApplication::exec()` .  [Qt's last window close signal](https://doc.qt.io/qt-6.8/qguiapplication.html#lastWindowClosed)occurs while this main event loop is running, so simple event handling alone does not substitute for window termination check.

To verify and capture common UI using the host's actual renderer, use the command below. During test execution, the window is briefly displayed and  `build/desktop-960.png`  and  `build/ios-390.png`  are saved. The latter is a preview with mobile  LVRS  theme applied to the host window.

```sh
QML_DISABLE_DISK_CACHE=1 DREAMSCAPES_CAPTURE_DIR="$PWD/build" \
  build/bin/DreamscapesGuiTests sharedPanelLayout:desktop-960 sharedPanelLayout:ios-390
```

Also, run the actual app in a separate process with runtime library and  QML  search path environment variable removed, to verify that embedded  `Main`   QML  entry points load normally. The process start is also checked to load native dependencies from external disk for the first time, waiting up to  30  seconds for start and up to  Society  seconds for bidirectional  15  observation. The limit for entire  GUI  inspection is  120  seconds, and the expiration time of  5  seconds for the observation target is maintained. If start or observation fails, the output of the corresponding app is also reported.

LVRS The `windowCount` of the start log counts only objects that are direct QML root window objects. Since Main itself is a window, it must be `windowCount: 1`. The display and input actions of the common panel are verified with GUI tests and actual desktop windows.

QML module is registered as a LVRS consumer helper following the CMake official](https://doc.qt.io/qt-6.8/cmake-build-qml-application.html)configuration [Qt 6.8. The app build and QML static check commands are as follows.

```sh
cmake --build build --target Dreamscapes Dreamscapes_qmllint --parallel
```

SDK The new location of the source is `Workspace/SDK`. CMake Default hints, presets, and CLion profiles use the `~/.local/SDK` installation package. The configuration failure policy for missing essential SDKs is maintained. Verify the new path with `cmake --fresh --preset macos-debug`.

2026-09-06 All required SDK 9 items were explored, and the missing GUI test source was also recovered. Host verification using mobile themes does not replace system safe area, keyboard, and touch verification on actual iOS · Android devices.

2026-09-07 After UI common macOS Debug configuration, build, GUI test 17/17, and QML static check were passed. In the actual desktop app, QuickGenerate display, prompt input, 16:9 selection, and LVRS Generate manipulation, and `windowCount: 1` of the start log were checked. Android The APK was also rebuilt with the integrated Main and signature verification was performed. The Android emulator execution and iOS build and execution of this change were not performed.


2026-09-08 In the storage structure at that time, Society validated the common storage connection. The next path is a pre-modification validation artifact, and the current generation storage specification follows the above section. macOS In the actual Dreamscapes window, Generate was pressed to create a 512×512 · 20 step image using Society's small random weight SD 1.x validation package. The execution device for `generation.json` is `mps`, GPU acceleration is `true`, and the model source file hash and storage path were recorded together. The results are `build/society-inference-verification/Asset Library/Dreamscapes/af21b3b3-4743-463e-8d11-7011697b42bc/`, and the request is in `Generation History/Dreamscapes/af21b3b3-4743-463e-8d11-7011697b42bc/request.json` of the same validation container. CPU 64×64 · 1 step actual execution also passed separately. The validation model does not prove image quality or user checkpoint compatibility.

This build's iiLocalDiffusion package was installed at `SDK/iiLocalDiffusion/build/society-consumer/install`. The Python environment uses the existing `SDK/iiLocalDiffusion/reference/diffusers/.venv`, and checkpoint ComfyUI was also prepared in the installed package's managed path. Because the host lacks a Metal compiler, the package and 70 CTests were verified in a separate `build/society-consumer/build` with the optional C++ backends MLX, LibTorch, and Core ML disabled. The actual GPU inference above uses the Python PyTorch MPS path. The iOS storage bridge and the generation controller's iOS conditional code were checked for syntax and types against the host's Catalyst headers. Since the Xcode/iOS SDK is absent, iOS app linking and device execution were not verified.


2026-09-08 Independent Inference Verification: After replacing the SDK's standalone Diffusers / PyTorch runner, `redLilyIllu_v10.safetensors` was used to generate 512×512 · 20 step MPS FP16 images from the actual Generate button and display them on the native result screen. ComfyUI was not executed. The work ID is `4e4f253e-33e2-4ee8-8c0e-89c176883dc8`, and previous failure records were preserved. After rebuilding, GUI · Generation Test 2/2 passed. Detailed SDK verification was recorded at `SDK/iiLocalDiffusion/docs/standalone-validation.md`.

<a id="iphone-생성-진행-및-종료-계약"></a>

### iPhone Generation Progress and Termination Contract

Native generation distinguishes between model loading, prompt preparation, noise removal, and image rendering. The number of tensor loads and VAE tile count are not summed with the requested generation steps. The result screen displays elapsed time and a cancel button, and completion is processed only after file posting is finished.

iOS sets `UIApplication.idleTimerDisabled` only during generation and restores its previous value on completion, failure, cancellation, or a transition to the background. A brief inactive state does not cancel the job. In the actual background, generation continues according to the system execution permissions below. The default limit is **15 minutes of active time without actual progress**. Valid throughput reported during tensor preparation and loading, prompt encoding, denoising, or image rendering resets the timeout. Engine waiting or status notifications without throughput do not extend it. To avoid canceling a progressing job on a slow CPU merely because its total duration exceeds 15 minutes, the SDK's separate total execution timeout is set to its maximum value; termination is controlled by the app's no-progress monitor and cancellation signal. The engine checks cancellation between tensor-loading and computation segments and waits for an ongoing GPU call to return. `Dreamscapes.LocalSociety` checks both that repeated progress across multiple stages can continue beyond the previous total timeout to completion and that notifications or waiting without progress are stopped.

<a id="ios-백그라운드-생성"></a>

#### iOS Background Generation

iOS determines the generation lifecycle based on the actual UIKit `DidEnterBackground` / `WillEnterForeground` notification instead of the Qt window's active status. Temporary inactivation by Control Center, Notification Center, or System Dialog is not a reason for generation cancellation or model cache release. Front/back switching during execution does not overwrite the current generation step and progress.

At iOS 26 and above, `BGContinuedProcessingTask` is registered for the image generation started by the user. If `BGTaskScheduler.supportedResources` includes GPU, existing automatic scheduling is used and GPU permission is requested. Even devices that do not support background GPU use automatic scheduling ( Metal denoiser, existing CPU VAE policy) on the front. This device does not request CPU persistent execution tasks, and when actually entering the background, it pauses the same tensor and seed, and resumes upon returning to the foreground. Background GPU support alone does not force the entire front inference to CPU. `Background GPU Access` entitlement, `processing` mode, and task identifier are declared in the bundle, and background execution is judged to be allowed only after receiving the actual system work. Progress of model preparation, loading, generation, and rendering is reported to the system Live Activity, and the work is completed after the final image storage in Society succeeds. Task registration, permission acquisition, and the app's screen hold feature are separated. OS task targets a single image currently, and the remaining generation queue starts when returning to the app.

For background GPU unsupported, outdated iOS , or system request rejection, the task is paused at the `NativeExecutionControl` boundary of the existing tensor/computation segment when actually entering the background. When returning to the app, it resumes with the same request, model, latent, and seed without regenerating from the beginning. Submitted GPU calls wait for a return. Stop time is excluded from the time limits of both the app and SDK , and user cancellation during pause is also handled. The finite UIKit background assertion secures handoff, storage, and cleanup time, and is released by the OS to suspend the terminated process when it expires. This assertion does not substitute GPU permission. Execution permission is returned when the OS persistent execution task expires or is interrupted in the system UI. Generation continues in the foreground, and in the background, the same request is paused at the computation boundary to continue upon the next foreground return. App Cancel actually cancels the request. Restoration after process termination due to force quit or memory pressure is not provided.

The existing Qt/iiLocalDiffusion and Apple system frameworks are used without adding external packages. Background `URLSession`, used for game-resource downloads, delegates file transfers to the operating system and does not grant execution permission for local GPU inference. References: [Apple long-running tasks](https://developer.apple.com/documentation/backgroundtasks/performing-long-running-tasks-on-ios-and-ipados), [GPU entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.background-tasks.continued-processing.gpu), [finite background execution](https://developer.apple.com/documentation/uikit/extending-your-app-s-background-execution-time).

`Dreamscapes.LocalSociety` checks the actual controller's progress, completion, and storage in allowed background, and GPU · CPU each allowed execution and request rejection for the same request's pause, resume, pause cancellation, time limit preservation, progress preservation upon return to the front, and preservation and execution permission return of the same request after foreground and background expiration. `verify_ios_bundle.py` checks the GPU permissions of the framework link, Info.plist, signature, and provisioning profile, and verifies the resume API for the app and included SDKs. opt-in device probe records `backgroundExecution` and time series `Documents/local-generation-lifecycle.jsonl`, and does not attempt screen capture in the background Qt. `python3 tests/verify_ios_lifecycle.py <timeline.jsonl> --mode paused` verifies the actual UIKit background state, transition of 30 seconds or more, engine wait confirmation, re-resuming the same request, and completed image. `--mode continued` separately requires OS permission and backface creation step increase. Verifier regression runs with `python3 -m unittest discover -s tests -p test_ios_lifecycle.py`. Host regression test distinguishes from actual device OS execution allowance evidence.

`DreamscapesLocalSocietyTests` checks step confusion, cancel/pause/background/limit time during loading, next request retry, and screen hold release. Actual device verification uses `DREAMSCAPES_LOCAL_RUNTIME_PROBE=ON` build's `--verify-local-generation --local-model <model> --local-prompt <prompt>`, and checks steps, elapsed observation time, foreground, screen hold, and final results in Documents' `local-generation-verification.json`. Test engine results and real device results are recorded as separate evidence. Screen hold API: [Apple UIKit](https://developer.apple.com/documentation/uikit/uiapplication/isidletimerdisabled).

Shared QuickGenerate uses the compact configuration of Figma   203:6930. Input row 22px + spacing 8px + button row 22px = 52px, and the result screen adds outer margin 20px to make 72px. Result image center placement verification also uses this height as the baseline.

LVRS common motion applies to button press and restore, and context menu entry and close. Image save GUI regression test sends the next click after the menu's `visible` becomes false and the close animation completes, and checks for preventing duplicate requests during file save cancel and retry and photo save without fixed time wait.

Real device cancel reproduction can add probe argument `--local-cancel-after-ms 3000`. Probe report's `ui` also records actual input height, result screen, and image loading status. The final app is rebuilt with probe OFF, and `verify_ios_bundle.py` causes the diagnostic probe to reject the remaining binary. `--allow-runtime-probe` is specified only when inspecting the diagnostic bundle itself.

After the 2026-09-11 fix, generation of a 512×512, 20-step image with `redLilyIllu_v10.safetensors` on an iPhone 15 Pro Max, screen display, and saving to Society completed in 294.647 seconds. A cancellation requested after 3 seconds of loading completed normally at 3.162 seconds from the start. The app's CTest 5/5, native SDK 79/79, installed iOS LVRS QML 62/62 hashes, and the device's input height of 22px were checked. Detailed reproduction steps, logs, and images are in the [verification report](build/iphone-generation-hang/REPORT.md).


QuickGenerate default output is short edge 1024px · 10 step. Common `GenerationRuntime` default step is lowered from 20 to 10 and passed identically to native engine and desktop worker. `DreamscapesGenerationTests` checks default resolution and 10 step passed from two backends at 5 aspect ratios, and `DreamscapesLocalSocietyTests` checks 10 step progress. iOS local native engine preserves Society Models original and prepares Q8_0 / VAE F16 GGUF derived files in Society's `Models/.society-runtime/iiLocalDiffusion/q8/`, reusing context and file mapping of the last successful model in the next image. iOS uses Metal budget based on device memory availability and full CPU cores. Idle cache is released on background entry, memory warning, and controller exit, and in-progress work is released on exit. Native job's `generation.performance` records cache hit, budget, thread count, model preparation, and inference time.

`DREAMSCAPES_PROBE_EXTENT` can only be compared for identical initial creation and memory cache reuse under the same conditions with `DREAMSCAPES_PROBE_SEED` and `--local-repeat 2` in performance verification builds only. Regular apps do not include this diagnostic input. Build, test, and real-device results and limits are recorded in `build/quickgenerate-acceleration/REPORT.md`.


iOS native inference uses `Platform/iOS/Dreamscapes.entitlements.in` Extended Virtual Addressing and Increased Memory Limit capability. 6.94GB checked real-device path where checkpoint fails to mmap at default virtual address limit. `-allowProvisioningUpdates` provisions and builds updated profile including that capability, and bundle check verifies permissions for both signature and profile. Additional memory is provided only to supported devices, so budget is limited to actual `os_proc_available_memory()` and Metal recommended workload set. Rationale: [Apple memory limit](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.increased-memory-limit), [extended address space](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.kernel.extended-virtual-addressing).

Native 0.5 reuses checkpoint mmap and reduces duplicate allocation and copying of GPU temporary buffer for upload. Upon loading completion, it proceeds immediately using a condition variable, eliminating the delay that waited for 200ms in each segment. Existing generation quality settings are maintained.

Device verification at `IILD_NATIVE_DIAGNOSTICS=1` records cache invalidation due to UIKit memory warnings. Models with insufficient available memory may have their context cache invalidated, so the `modelCacheHit` of continuous generation is verified against the actual result.

QuickGenerate uses iOS iiLocalDiffusion 0.6.1 Q8 cache by default. Existing app CacheLocation `iiLocalDiffusion/q8` migrates from Society connection to Society Q8 path on the work thread. Generation request runs after previous completion, and if it previously failed, it re-checks right before generation. The original model is not copied. Caches with the same name are deduplicated only when SHA-256 are equal, preserving remaining files in case of content collision, external links, or cancellation, and reporting errors. The existing app cache does not write new files. Model conversion time is added only on the first request, and a ready step is displayed on the screen. Original changes and conversion result corruption invalidate the cache, and partial files that are cancelled or failed are not reused. Q8 may have different pixels for the original FP16 with the same seed. The default resolution and step count follow the above common settings. Disk preparation and reuse are verified separately for `generation.performance`, `q8CacheUsed`, `diskCacheHit`, `modelBytes`, and `preparationMilliseconds`, and memory reuse is verified for `modelCacheHit`.

2026-09-11 Q8 measurements on a physical device: On an iPhone 15 Pro Max, the same 512×512, 20-step request decreased from 294.647 seconds with the original FP16 to 140.366 seconds including the initial conversion, a reduction of approximately 52.4%. Consecutive generation took 149.795 seconds, with both disk and memory cache hits. The then-default 1024×1024, 20-step request completed in 643.309 seconds. These measurements preceded reducing the default to 10 steps; generation time for 10 steps must be measured separately. Disk-cache preparation after restart was confirmed at 1.287ms. Original preservation, actual results, cache release, and the limits of device measurements are described in the [verification report](build/quickgenerate-acceleration/REPORT.md).

Device probe handles cases where Qt Image status is serialized as a number or enum name `Ready` . At execution start, past screen captures are deleted, and capture completion is indicated only after PNG save success. UI's Ready/source observation is distinguished from actual screen PNG verification.

<a id="iphone-생성-완료-후-결과-처리"></a>

## iPhone Result processing after generation completion

iOS diagnostic probe records the actual UIKit's `idleTimerDisabled` , `applicationState` , and device `thermalState` ·low power mode separately from the app's logical screen hold state. It is an observation value to distinguish automatic lock, background transition, and generation engine errors, and does not bypass the device's thermal protection or lock policy.

The native SDK returns the requested output size as is. When SDXL rounds up the internal canvas to 64 pixel units, it crops only the overflow edge based on the center and does not interpolate. For example, QuickGenerate 3:4 of 1024×1368 removes 20 pixels vertically and horizontally from the internal 1024×1408 result for storage and display. The path where the VAE's `using Conv2D scale 0.031` warning used to display size validation failure has also been fixed in the SDK. It uses existing stable-diffusion.cpp, standard C++ RGB copy, and Qt image storage without new external dependencies.

The `--verify-local-generation --local-aspect-ratio 3:4` of the optional diagnostic build passes that ratio to the actual controller. If the ratio is omitted, it defaults to 1:1 and applies the same ratio even in repeated execution. If `DREAMSCAPES_PROBE_EXTENT` is not specified, it uses the product's default size and step. Regular installations are rebuilt with `DREAMSCAPES_LOCAL_RUNTIME_PROBE=OFF` and validate `tests/verify_ios_bundle.py` diagnostic code exclusion/inclusion SDK, signature, and device profile. Pre- and post-fix regression and manual test results are recorded in `build/iphone-result-fix/`.

<a id="society-생성-리소스와-앱-패키징"></a>

## Society generation resources and app packaging

Dreamscapes bundles include only executable code, UI, and icons. Checkpoints, VAE, LoRA, embeddings, and `generation-defaults.json` are not copied to the app. Incremental builds also remove the previous version's `iiLocalDiffusion/resources` directory before signing. `Dreamscapes.GenerationResources` and iOS Android package checks fail if a model extension or generation resource spec is found. The same rules apply even to compressed APKs on Android. `Dreamscapes.GenerationResourceVerifier` checks for nested models, incorrectly included specs, and normal executable code/icons versus missing bundles.

Society owns the generation resources. The contents of `share/iiLocalDiffusion/resources/` prepared by the SDK are stored in the Society container's `Models/.generation-resources/iiLocalDiffusion/` with the same relative path. Society synchronizes the `generation-defaults.json` in that directory that reference VAE, LoRA, embeddings, and config files. Hidden resources are excluded from the generation model selection list and are synchronization targets, unlike `.society-runtime` which stores local derived files. App builds do not modify the actual Society container.

Creating only a path does not mean resources are prepared. Reading empty shared resources from Anima that requires an external VAE caused the old SDK to fail with `filesystem error: in file_size`. Install resources to the local Society source and synchronize them between Societies. Using existing Python standard libraries, check the size and SHA-256 of all files referenced in the spec and the license notice, then publish the spec after resource copying is complete. The same installation is reused, missing files are recovered, and existing files or symbolic links with different content are preserved and reported as errors.

```sh
python3 -B scripts/provision_generation_resources.py \
  --source /absolute/SDK/iiLocalDiffusion/share/iiLocalDiffusion/resources \
  --society /absolute/Society
# Add --check to the command above to verify the entire installation without changing files.
```

`Dreamscapes.GenerationResourceProvisioning` checks for non-publication of specifications when first installation, recursive, partial recovery, hash mismatch, existing file collision, path deviation, redirection, or copy interruption occurs. This installation tool prepares only local shared storage. Delivery from other devices is Society synchronization, and actual image generation success on the device is verified separately. Anima's default checkpoints require Qwen3 text encoder and Qwen Image VAE, and the built-in component can be used as a complete integrated version without separate VAE. [follows the official Anima configuration](https://huggingface.co/circlestone-labs/Anima#installing-and-running).

Mobile specifies the `NativeGenerationOptions.resourceDirectory` path, desktop specifies the `--generation-resources` path, and Society is selected. This option is maintained even during background execution. SDK does not bypass to other resources such as installation package, app bundle, or environment variables. If no resource specification exists, the optional default style adapter is not used, and models with a normal built-in VAE can continue to be generated. Models requiring an external VAE search for the same family of VAEs in Society, verify existing tensors and settings, and then automatically mount them. If no mandatory VAE exists, an error is returned.

`Dreamscapes.Generation` checks whether CLI, environment variables, temporary output, and cache path point to Society. `Dreamscapes.LocalSociety` verifies native resource options, Q8 cache, previous app cache migration, file collision preservation, exclusion of hidden data, and completion image storage. Actual device storage capacity must be measured separately from build verification.

<a id="앱-아이콘"></a>

## App icon

Generate icons for `resources/Appicon/Artboard 1.png` as source, macOS, iPhone/iPad, Android, Windows, Linux, WebAssembly, and connect them to CMake and Qt runtime. The original Illustrator file is `resources/Appicon/Appicon.ai`. Refer to [app icon documentation](resources/Appicon/README.md)for regeneration methods, platform-specific masks, sizes, and packaging descriptions. `Dreamscapes.AppIcons` checks the original hash, asset specifications, Android's Activity and FileProvider preservation, and web's recursive packaging.

Current Qt iOS deployment has no FFmpeg native dependencies, so select AVFoundation-based `QDarwinMediaPlugin` for the bundle. `verify_ios_bundle.py` rejects links to missing FFmpeg plugins. When verifying by specifying SDK path, build with `env -u CPATH -u CPLUS_INCLUDE_PATH -u C_INCLUDE_PATH` so that the shell's `CPATH` does not take precedence over the previous installation header.

<a id="iphone-vae-마운트와-디코딩-품질"></a>

### iPhone VAE mount and decoding quality

Native SDK checks the VAE full tensor structure of the same SDXL checkpoint as redLilyIllu. If using a normal built-in VAE, replace missing, incomplete, or shape mismatch with the same family of VAEs stored in Society. `adaptive-sdxl-v1` adjusts tiles according to memory budget, and small canvases decode without splitting. Apply fp16 active value protection to SDXL built-in and external VAEs, and treat NaN/Inf latent values and pixels as errors before saving. iOS bundle inspection also checks whether this mount verification, policy, and error blocking are included in the actual built-in library.

`auto-mount-v2` reads the actual safetensors / GGUF content of the selected model to determine the model family and VAE family. There is no need for the user to separately choose a VAE, and if Q8 cache is used, the cache to be actually loaded is also checked. Anima automatically connects Qwen VAE, Z-Image is FLUX.1 VAE along with SDXL ·Qwen Image RGB · FLUX.1 · FLUX.2. Even external VAEs must pass the entire tensor·RGB /latent channel·scale·shift·normalization settings to be mounted, and setting changes invalidate the generation context cache. Distinguishes normal built-in VAE, missing, structural mismatch, and out of support range. The normal built-in SD1/2 /SD3 VAE can be used, but since the default VAE of that family is not in the current SDK resource specification, a clear error is returned when the built-in VAE is missing. It is not a feature that provides other essential components such as a separate text encoder.

Do not modify the original model or existing generation records. VAE Numerical accuracy validation is separate from the denoiser's figure form, step, LoRA quality validation. The validation procedure is in document [SDK](../../SDK/iiLocalDiffusion/docs/native-image-generation.md#vae-마운트-검증과-자동-최적화).

<a id="로컬-mcp-제어"></a>

## Local MCP control

Desktop POSIX builds provide the Dreamscapes controller running iiLocalLLM 0.10.0 as an authenticated local MCP tool. Automatic discovery, input/permission/cancellation contracts, and actual app execution tests are recorded in [Mcp.md](docs/Mcp.md). It can be disabled via `IILOCALLLM_DISABLE_APP_MCP=1`.

<a id="ios-지속-실행-진행-보고"></a>

### iOS continuous execution progress reporting

CPU Inference reports the actual number of 16 node-level operations completed via the upstream graph evaluation callback as `Computing`. Even if stages such as model loading, default generation, and Hires regeneration repeat, the completed workload is accumulated, and progress is not increased merely by duplicate events or elapsed time. The total workload of Live Activity is an estimate including currently known remaining work, and final completion is reported only after image storage. Operation events are not displayed as generation steps or screen stages. If continuous execution permission is granted, a short UIKit assertion is returned, and a separate finite assertion to clean up until the expiration boundary is secured upon expiration.

<a id="앱-종료와-독립적인-live-activity-표시"></a>

### Live Activity display independent of app termination

At iOS 16.2 and above, `iiSocietyContainer_add_ios_live_activity`'s ActivityKit / WidgetKit extension displays the generation card and Dynamic Island. It has local execution permission BGContinuedProcessingTask and a separate lifespan. Even if the app terminates, the last actual progress is displayed. The refresh validity period is 90 seconds, and after reflecting the system's stale status, it changes to a status confirmation notice. There may be a delay in system screen refresh. Permission expiration, temporary suspension, and failure do not terminate the card. Actual image storage completion leaves the completion status, and the user's Cancel immediately terminates. Even if execution permission expires first, the actual completion event is delivered until display. Cards deleted by the user are not automatically restored.

If re-executed, the ID of the remaining card and the last progress are restored. Local tensor operations lost by process removal cannot be resumed, so the corresponding card displays a suspended status. Unlike the service that updates server work via APNs, it does not generate local generation progress while the app is terminated. The maximum iOS lifespan and display policy of ActivityKit are applied, and a continued-processing system card may appear together during execution. It uses Apple system frameworks without additional external libraries.

`Dreamscapes.LocalSociety` separates execution permission expiration and display completion, and checks for temporary suspension display, cancellation, and storage completion after resumption. `verify_ios_bundle.py` checks the actual app's ActivityKit link and signed WidgetKit extension.

<a id="quickgenerate-큐와-네이티브-라이브-프리뷰"></a>

### QuickGenerate queue and native live preview

Desktop  SDXL  loader sets prediction method and noise schedule even for checkpoints without metadata via  `v_pred` · `ztsnr`  tensor display. It does not guess model type by filename, and generates v-prediction models in epsilon mode to prevent outputting only noise images.  Diffusers  preview also passes  `sequence`  exceeding the interval and  `num_timesteps`  to actually execute, so preview continues after default  10  steps and Hires correction  3  steps.

Changing the selected model terminates the pending preparation worker of the previous model and proceeds with the queue of the newly submitted model. Preparation and GPU cache for the same model are reused. At execution start, the status is updated to the current model, and the initial full model hash read displays progress as actual byte count `Checking model`. The hash of unchanged files reuses the SDK's existing cache.

The native paths for Mobile and Desktop Anima use the actual denoised latent RGB projection of SDK `generateNativeImageWithPreview`. No decoding is added at each step VAE, and the color and detail of the low-resolution intermediate preview may differ from the final image. Even if the step numbers for base and Hires restart, the preview is updated as a separate continuous sequence. Even if the progress of the same step arrives first, the image is not missing. The preview is saved only as a temporary PNG per job and deleted upon completion, failure, or cancellation. The generation quality settings ( 1024px short variation · 10 steps · half-resolution base · Hires) are maintained.

Regression verification includes `Dreamscapes.Generation` slow prior model preparation during model change, `Dreamscapes.LocalSociety` actual preview pixel·Hires number restart·temporary file cleanup, and SDK native RGB ownership and Python worker preview output. Host test·bundle inspection and actual device generation time are separate evidence.

The default SDXL LoRA text encoder keys are also loaded from the SDK to match the actual Transformers CLIP structure. Errors are not hidden by omitting the default adapter or lowering generation settings.

2026-09-16 iPhone 15 Pro Max benchmark: redLilyIllu _v10, 1024×1024, 10+3 steps, first preview 33.350 seconds, final save 957.386 seconds, and all 13 previews were displayed on the screen. Approximately CPU VAE decoding and saving after the final correction takes about 468 seconds, so this performance bottleneck remains. macOS Queue execution · v-prediction results · Hires 3/3 received, actual venv's Python 128 instances, and evidence of app GUI and device production restoration are recorded in [QuickGenerate verification report](build/quickgenerate-fix/REPORT.md). The approximately 3000 seconds reported by the user cannot be confirmed as having the same execution conditions, so it is not used for direct before-and-after performance comparison.

On the same day, the desktop's rinFlanimeNoobHigh _vPred10 1024×1824 follow-up generation was the first preview 9.926 seconds·save 106.317 seconds. Worker statistics confirmed that the model hash was not reread or the pipeline was not reloaded. Execution time including initial model inspection·GPU preparation and this cache reuse time are distinguished.
# Unified model objects

A model can be selected as a single model from the `.iildmodel` integrated object created by Society. The latest iiSocietyContainer exposes it in `unified` format and the native generator passes the package to iiLocalDiffusion. The SDK sequentially refines previous RGB results through the VAE of each model. The feature of converting weights of different structures into a single neural network is not. `GenerationTests::unifiedModelReachesTheNativeRuntimeAsOnePackage` validates package selection and native request passing, while actual image connection·cancellation checks are in the SDK's NativeResultTests.

<a id="호스트-목록과-선택-다운로드"></a>

## Host list and select download

Container 0.14.0, Sync 0.8.0, Client 0.1.0 are used together. The model list can be selected before original download as it reads local Society's host metadata. Generate fixes the model version and submits it to the host first, while download is performed independently after acceptance. Download cancellation or failure does not stop the already accepted host generation. Devices holding the model can generate even offline.

Connection, replication, account restoration, and BLE /Bonjour search and TLS /server transfer are handled by the app's internal iiSocietyClient. There is no need to switch to Society app. Initial Society login and host repository registration are required. iOS shares the encrypted account status and container of the existing App Group and includes descriptions for local network and Bluetooth usage in the bundle. Downloads proceed during foreground and OS-permitted background execution time. Upon permission expiration, requests and partial files are preserved and resumed in the next execution. Host inference already accepted continues independently of the client's suspension.

Verify size and decoding of completed images, atomically save to local Generation History, then immediately request synchronization to SDK. Even if host connection is broken, original remains and is submitted in next connection. Generation queue and prompt maintain existing app session lifecycle policy. Public  `.generation-resources/iiLocalDiffusion`  is received together as current runtime resource unit, and does not receive other Models items and  `.society-runtime`  cache.

`Dreamscapes.LocalSociety`  checks list prior to actual local  TLS  transfer and injected image generation, model transfer pending host generation complete, subsequent model download, host upload, and offline generation using cache. This test distinguishes from  iPhone  inference performance evidence of large actual model.  `iiSocietyClient.Session`  verifies stored server and automatic  LAN  connection without  Society  app process.

Specify absolute path of existing checkpoint in  `DREAMSCAPES_REMOTE_TEST_MODEL`  and set to  `QTEST_FUNCTION_TIMEOUT=1200000` , then directly execute  `DreamscapesLocalSocietyTests actualModelRunsOnHostWithoutClientWeights`  to verify actual host engine and  TLS  result transfer. Create hard link not modifying original in separate  Society  test container, and leave client model directory empty.  256×256 · 2  step results and execution information are left in  `society-local-generation/remote-real.png` ,  `remote-real.json`  of build directory. This is connection verification of actual host inference, and does not evaluate image quality or iPhone OS background permission.

## Source layout

Implementation files and their headers live together under `src/`. Existing feature and platform subdirectories retain their responsibilities. Build configuration, tests, documentation, resources, and maintenance scripts remain at the project root. Configure and build using the repository-local `build/` directory.

<a id="모델-검사-대기-단축"></a>

### Model inspection wait shortcut]

`Checking model` is the SHA-256 calculation of the model content. iiLocalDiffusion stores the verified hash in process memory and persistent SQLite cache. If the file path, device, inode, size, mtime, and ctime match, the model body is not read even after worker restart. The initial check and modified files calculate the full hash. If the completed bytes equal the total bytes, it immediately switches to Preparing to prevent incorrectly displaying model loading as Checking afterwards. SDK PersistentModelHashTests and Dreamscapes.Generation's completedModelCheckDoesNotRemainCheckingDuringPreparation verify reuse, change detection, and state transition.

<a id="생성-전-최소-모델-확인"></a>

### Pre-generation minimum model check

Dreamscapes' worker uses the `IILD_MODEL_VALIDATION=metadata` policy. From the initial selection, it does not calculate the full hash of checkpoints, package members, LoRA, VAE, and ControlNet. It only checks the path, file existence, non-empty status, and necessary small manifest/format information, then passes it to the actual loader. Unchecked SHA-256 are null and are not recorded as verification success. Model issues are revealed by actual loading, generation errors, or generation results, and the loader's failure is not hidden or changed to a successful result. The existing persistent hash cache is used only at the explicit full verification SDK path. The model read, conversion, and GPU loading time required for generation is separate from this pre-check omission.
# Desktop window and menu shell

The desktop global menu bar provides File, Edit, Window, and Help. `Edit → Preferences…`, ⌘, on macOS, or Ctrl+, on Windows/Linux opens a separate LVRS preferences window with the Account profile row at the top, followed by General/Appearence/Storage, Image/Video/Audio/Agents, Share/Intergration/Publish, and About/Accessibility/Keyboard Shortcut. The 207px sidebar uses a 44px LVRS profile row, 24px menu rows, 10px padding, and 12px group gaps. It scrolls in small windows and reveals the focused row for keyboard navigation. Profile text retains the design sample. Each menu uses the exact LVRS icon name read from its Figma component. The Account image binds to iiAccountManager Account.avatarUrl through the same SDK AccountSession borrowed by the Society client; updates appear immediately and absent photos fall back to the LVRS user icon. On the current macOS version the native action is displayed as `Edit → Settings…`. The window is reused and hidden by Escape, ⌘W, or closing the window, and closes along with the main window when the main window is closed.

Image and Video provide their respective default generation model dropdowns backed by Society's existing model inventory. Choices are saved immediately and restored after app restart. Changing defaults updates the current selection while preserving the model references in previously submitted jobs. Missing defaults retain their saved IDs and fall back to an available model; Automatic resets a default to the first model of its type. Storage contains the existing Society drive settings. Account links to account management in Society. Other categories currently show their selected heading. See [preferences](docs/Preferences.md) for persistence, fallback, and keyboard behavior.

All preferences visual components use the installed LVRS library: HStack/VStack/Spacer for layout, List for scrolling and model options, and Modal/InputField/ListItem/buttons for folder selection. Application preferences views do not import Qt Quick Controls or Qt Quick Dialogs or directly instantiate raw Qt visual/layout types. The folder picker supports subfolder navigation, Up, path input, Choose, Cancel, and Escape; selecting a folder updates the input until Apply validates and saves the existing drive. FolderListModel supplies filesystem data only. The preferences suite enforces this component contract and verifies long model-menu scrolling and the folder workflow.

In General, check the current Society drive location and enter the existing Society drive root path, or select Choose folder… and apply with Apply. `GenerationController::selectStorageLocation` validates the existing container and saves it to the SDK share default location before connecting. Empty paths, relative paths, and regular folders are rejected, preserving existing connections. Changes during creation/storage operations are rejected. sparsebundle is mounted first, then the mounted root folder is selected. File move/deletion or new drive creation is not performed. The save location is shared with Society, and no running app is forcibly switched.

`Dreamscapes.Preferences` verifies the profile-first layout, all 13 menu rows, group geometry, exact LVRS icon names and SVG decoding, SDK WebP avatar replacement and account-clear fallback, category routing, keyboard model selection, and focused-row scrolling at minimum window size. Test GUI `mainCreatesOneSharedWindow` validates the Edit menu, preferences shortcut, category/detail area, Apply behavior, share location save, connection preservation from invalid paths, and window lifecycle. Generation test `driveLocationSelectionPersistsAndRejectsInvalidFolders` validates file URL input, reconnection, and rejection of invalid folders.

macOS window close, app termination, and Dock reopen actions follow the [application lifecycle policy](docs/ApplicationLifetime.md).

### Explicit VAE selection

Desktop generation can select an external VAE from Society `Models/VAE/` with the VAE menu or the `vaes`/`select_vae` MCP tools. Each submitted job pins the VAE's container reference independently of later selection changes and resolves it again before passing `--vae` to iiLocalDiffusion. The SDK validates the model latent contract. Empty selection preserves embedded/fallback behavior. Explicit selection currently requires the local desktop worker; unsupported native/remote requests fail instead of silently ignoring the VAE.

Desktop worker requests also forward the job seed explicitly so recorded seeds reproduce the actual request, including VAE/HiRes comparisons. Foreground preparation includes the selected VAE.

The MCP `generate` tool accepts an optional unsigned 32-bit seed; batches increment a supplied seed in submission order and reject overflow. This supports repeatable comparison and queue transfer without changing prompts or seeds.

### Society model inventory refresh (0.1.1)

Dreamscapes reads generation model snapshots through iiSocietyHelper 0.7.2 `FileSystem::models()` on startup, activation and foreground polling. iiSocietyContainer 0.14.1 reconciles native owner files with the asynchronous catalog so Deleted models disappear and newly published checkpoints/packages appear before indexing completes. Replica-only models remain visible for download. Removing a selected checkpoint chooses the next valid model; removing a selected VAE resets it to Model default. Hidden conversion resources are excluded. The `modelInventoryFollowsSocietyOwnerAtStartupAndRefresh` regression test covers stale startup catalogs, changes without restart, stable refresh and empty selections.

The MCP process test retains a 25-second default startup limit. For a cold signed app on slow external storage, run the test binary with `DREAMSCAPES_TEST_STARTUP_TIMEOUT_MS=60000` (accepted range 25000–120000 ms). Only root-window startup uses this budget; protocol requests, generation and cancellation assertions keep their existing limits. Run the binary directly when the configured startup budget could exceed CTest's 90-second suite limit.

<a id="krea-2-quickgenerate-비율-호환"></a>

### Krea 2 QuickGenerate ratio compatibility

Native Krea 2 also uses QuickGenerate's five ratios and the existing 8px output grid. The SDK rounds up the internal canvas only in 64px units and returns the requested size by center cropping the final RGB. 9:16 is the 1024×1824 result obtained by removing 16px from top and bottom from the 1024×1856 internal canvas. Krea's resolution-based schedule is calculated based on the internal canvas and does not forcibly change the output ratio or stretch the image.

<a id="모바일-에디터-툴바"></a>

## Mobile editor toolbar

Figma [Editor Toolbar · Full 19](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=143-1652)'s 19 items are implemented in `Views/Editor/EditorToolbar.qml`. It is displayed with left/right 8px and bottom 8px margins within the system safe area at the bottom of the mobile editor. Since the canvas ends above the toolbar, the image and tools do not overlap. The updated desktop [Editor layout, Figma 353:22614](https://www.figma.com/design/bn8O4AHKr1X9DWnhR1TgEy/Dreamscapes?node-id=353-22614&m=dev) uses a flat, opaque, borderless 54px top toolbar with 36px icon tabs and 8px padding, a 342px default right panel, and a native canvas centered in the remaining work area. Desktop document commands remain available through standard shortcuts and a canvas context menu. Layout, responsive behavior and verification are described in [EditorLayout.md](docs/EditorLayout.md).

The original height  84px,  86×68px tool,  4px spacing,  22px icon area, and  9px  Pretendard Medium label are held.  `LV.Tab` 's selection, accessibility, and keyboard actions are reused, and  `ListView` handles horizontal touch/mouse drag, inertial scroll, and item snapping. Tools are not selected by scrolling alone, and tools selected by tab are held even after size changes. Navigation is possible with arrow keys and Home/End.  `CanvasEditor.selectedTool` and  `toolSelected(toolId)` expose the current selection and tool switching. Pressing a tool button raises the  LVRS bottom sheet of that tool. It provides  19 integrated panels'  428 items, per-tool input state preservation, Reset, vertical scroll, color selection, and Escape·background tab·bottom drag close. On narrow screens,  2 column items are arranged in  1 columns. Native document opening, saving, image placement, basic vector creation, brush/pixel eraser input and layer properties are connected as described in [EditorNativeCanvas.md](docs/EditorNativeCanvas.md). Other engine actions show their current unavailable status. The implementation structure and verification contract are in  [EditorToolPanels .md](docs/EditorToolPanels.md).

19 original SVGs are saved as-is in  `Views/Editor/Assets/` and included in app resources.  `manifest.json` recorded  Figma nodes, files, original size, and position within the icon area. No temporary  Figma URLs are used at runtime.  `mobileEditorToolbarSlidesAndSelects` inspects bottom placement, safe area, selection, touch/mouse drag, last item reach, keyboard navigation, and load/file/display dimensions for all SVGs in  320/390/402px  iOS theme,  360px  Android theme, horizontal screen, and full original width.

<a id="추론-진단-기록과-데스크톱-무진행-감시"></a>

### Inference diagnostic log and desktop no-progress monitoring

The desktop  iiLocalDiffusion worker writes native inference logs to  Society repository's  `Models/.society-runtime/iiLocalDiffusion/diagnostics/inference-*.jsonl` . Logs are held even if the temporary creation folder is cleaned up or the worker is terminated. Recent logs and file paths are passed to each task's  `telemetry` and  `inferenceStatus.telemetry` .  SDK update is required, and the app does not infer logs not sent by the previous SDK.

The log range is Model load → Text encode → Denoise step →  VAE decode → Postprocess. It distinguishes the actual module's execution backend, step time, weight loading interval, process memory, and system swap.  Metal display is module placement and is not evidence that all operators'  CPU fallbacks were absent. Detailed measurement definitions follow the SDK's  `docs/krea2.md` .

Background monitoring with a default duration of `nativeTimeoutMilliseconds` (initially 15 minutes) is also applied to desktop creation requests. The limit is refreshed when the step, step, model validation bytes, or the actual completed CPU graph batch changes, or when a valid new preview arrives. Repeated progress values and telemetry heartbeats are not refreshed. If the limit is exceeded, the corresponding worker process group is terminated, and if it remains alive after 2 seconds, it is forcibly terminated. The job is treated as failed, and the last step and diagnostic file path are recorded in the error. A fixed 15 minute limit on the total creation time is not imposed. Remote Society jobs and mobile in-process cancellation policies are managed separately for each execution path.

`Dreamscapes.Generation` verifies whether a worker that only sends heartbeats fails, whether the entire duration exceeds the monitoring interval even if the actual step progresses, and whether diagnostic files are preserved even after failure.


The `Dreamscapes.Generation` CTest suite has a 90-second outer limit to cover
worker startup, cancellation, and history fixtures while real inference shares
external storage. Individual generation/watchdog deadlines are unchanged; this
is test-runner headroom, not a longer application stall timeout.

QuickGenerate restore regression tests are executed with `ctest --test-dir build -R "^Dreamscapes.QuickGenerate$" --output-on-failure`.

## Desktop Home — Figma 261:3148

The desktop home uses an LVRS menu sidebar and native title-bar search,
notifications and account controls. Recent files, publications and generation
history are backed by Society's `DashboardFiles`, including search-before-limit
filtering and automatic file-change refresh. Layout, asset provenance,
interaction contracts and focused tests are documented in
[DesktopHome.md](docs/DesktopHome.md).

The Home sidebar follows Figma `243:8376`: a 181px width with 8px padding on all
four sides, 24px menu rows, and 3px divider areas after Home and Tools. Its views
use installed LVRS components, including the scrollable list and compact-rail
tooltips. The existing 48px compact rail keeps the same padding. Keyboard focus
reveals each menu within the padded viewport, and the original Figma icon
exports retain their dimensions and colors. Selection and press use the LVRS
`accentBlueMuted` token matching Figma's `#25324D` row fill. Focused GUI coverage checks the
exact row geometry, asset loading, activation, and scrolling in short windows.

The desktop Home includes the [interactive paint canvas](docs/HomePaintCanvas.md): LVRS icon tools and ColorPicker, draggable image references, undoable pixel paste, and immutable generation inputs.

The desktop [Home body](docs/DesktopHome.md) follows Figma `261:3148`: a canvas-first
composer, compact icon/slider/color tools, reference-image attachments, and five
continuous content rows. Inspiration cards fill the editable prompt; persisted
rows are backed by Society. Native window chrome remains unchanged.

The Home [New canvas chooser](docs/NewCanvas.md) provides 300 Figma-derived canvas presets, global search, custom physical dimensions, and LVRS controls before opening an iiSharedCanvas editor document.

## Video generation

The desktop Video sidebar entry opens the [Video generation workspace](docs/VideoGenerationWorkspace.md): an aspect-correct preview, independent 300 px parameter inspector, and 406 px shot/keyframe timeline. Kept shots, per-shot prompts and immutable weighted image conditions are submitted through the existing iiLocalDiffusion LTX queue. The workspace retains its draft, shows every output, plays MP4 and exports verified bytes. Home QuickGenerate and mobile Video retain their existing path. Runtime/model preparation and output verification follow [VideoGeneration](docs/VideoGeneration.md).

Editor tool execution, native edit/undo behavior and capability boundaries are documented in [EditorToolBehavior.md](docs/EditorToolBehavior.md).
