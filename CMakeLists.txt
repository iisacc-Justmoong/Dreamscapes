cmake_minimum_required(VERSION 3.31)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/single-app/Defaults.cmake")
project(Dreamscapes VERSION 0.1.1 LANGUAGES CXX)
find_package(iiSDKCore 0.18.1 CONFIG REQUIRED COMPONENTS Runtime HINTS "$ENV{HOME}/.local/SDK/iiSDKCore")
# File creation is owned by iiFileProvider.
find_package(iiFileProvider 0.6 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")
find_package(iiFileProviderNative 0.6 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")
find_package(iiJson 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiJson")
find_package(iiImageCodec 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiImageCodec"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiImageCodec/build/install")





set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 Concurrent Multimedia)
find_package(Qt6ShaderTools 6.8.3 EXACT REQUIRED)

# Link the runtime capabilities used by this product.
list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_BINARY_DIR}/runtime-sdk/iiWebServer")
find_package(iiLocalDiffusion 0.6.2 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLocalDiffusion")
if(IOS OR ANDROID OR EMSCRIPTEN)
    find_package(iiMultimodalAgent 0.1 CONFIG REQUIRED COMPONENTS FailureDiagnostics HINTS "$ENV{HOME}/.local/SDK/iiMultimodalAgent")
else()
    find_package(iiMultimodalAgent 0.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiMultimodalAgent")
endif()
find_package(iiSharedCanvas 0.32.0 EXACT CONFIG REQUIRED COMPONENTS Quick HINTS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiSharedCanvas/build/install" "$ENV{HOME}/.local/SDK/iiSharedCanvas")
find_package(iiGraphicAdjust 0.4.0 EXACT CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiGraphicAdjust")
find_package(iiColorManager 0.1.0 EXACT CONFIG REQUIRED
    HINTS "$ENV{HOME}/.local/SDK/iiColorManager" "${CMAKE_CURRENT_BINARY_DIR}/runtime-sdk/iiColorManager")
find_package(iiPaintEngine 0.5.1 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiPaintEngine")
find_package(iiPhotoLibrary CONFIG REQUIRED COMPONENTS Library HINTS "$ENV{HOME}/.local/SDK/iiPhotoLibrary")
find_package(iiSocietyContainer 0.16.0 CONFIG REQUIRED COMPONENTS Gui HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")

find_package(iiSocietyHelper 0.11.0 CONFIG REQUIRED HINTS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiSocietyHelper/build/install"
    "${CMAKE_CURRENT_BINARY_DIR}/runtime-sdk/iiSocietyHelper"
    "$ENV{HOME}/.local/SDK/iiSocietyHelper")
find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/LVRS")
find_package(iiSocialPublish 0.2.0 CONFIG REQUIRED HINTS
    "$ENV{IISOCIALPUBLISH_PREFIX}" "$ENV{HOME}/.local/SDK/iiSocialPublish"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiSocialPublish/build/install")
qt_standard_project_setup(REQUIRES 6.8)
if(IOS AND NOT iiLocalDiffusion_NATIVE_DIFFUSION)
    message(FATAL_ERROR "Dreamscapes iOS requires iiLocalDiffusion with IILD_ENABLE_NATIVE_DIFFUSION=ON")
endif()
if(iiLocalDiffusion_NATIVE_DIFFUSION)
    get_filename_component(dreamscapes_diffusion_prefix "${iiLocalDiffusion_DIR}/../../.." ABSOLUTE)
endif()

# Run the existing SDK's inference launcher; no second engine or weight cache.
if(NOT IOS AND NOT ANDROID)
    find_program(DREAMSCAPES_DIFFUSION_EXECUTABLE NAMES iild-generate
        HINTS "${iiLocalDiffusion_DIR}/../../../bin")
endif()
if(NOT DREAMSCAPES_DIFFUSION_EXECUTABLE)
    set(DREAMSCAPES_DIFFUSION_EXECUTABLE "")
endif()
set(DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE "" CACHE FILEPATH "Optional Python environment for the installed iiLocalDiffusion runtime")
set(_dreamscapes_diffusion_command "${DREAMSCAPES_DIFFUSION_EXECUTABLE}")
if(WIN32 AND NOT DREAMSCAPES_DIFFUSION_EXECUTABLE MATCHES "\\.[eE][xX][eE]$")
    if(NOT DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE)
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        set(DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE FILEPATH
            "Python environment for the installed iiLocalDiffusion runtime" FORCE)
    endif()
    list(PREPEND _dreamscapes_diffusion_command "${DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE}")
endif()
if(DREAMSCAPES_DIFFUSION_EXECUTABLE AND NOT CMAKE_CROSSCOMPILING)
    execute_process(COMMAND ${_dreamscapes_diffusion_command} --help
        OUTPUT_VARIABLE diffusion_help ERROR_VARIABLE diffusion_help_error
        RESULT_VARIABLE diffusion_help_result TIMEOUT 15)
    if(NOT diffusion_help_result EQUAL 0 OR NOT diffusion_help MATCHES "--model-path" OR NOT diffusion_help MATCHES "--worker" OR NOT diffusion_help MATCHES "foreground-residency")
        message(FATAL_ERROR "The installed iild-generate is outdated or unavailable. Reinstall the current iiLocalDiffusion package and set DREAMSCAPES_DIFFUSION_EXECUTABLE to its launcher. ${diffusion_help_error}")
    endif()
    execute_process(COMMAND ${_dreamscapes_diffusion_command} --backend local --help
        OUTPUT_VARIABLE diffusion_local_help RESULT_VARIABLE diffusion_local_result TIMEOUT 15)
    if(NOT diffusion_local_result EQUAL 0 OR NOT diffusion_local_help MATCHES "--work-dir" OR NOT diffusion_local_help MATCHES "--cache-dir" OR NOT diffusion_local_help MATCHES "--preview-dir" OR NOT diffusion_local_help MATCHES "Diffusers/PyTorch")
        message(FATAL_ERROR "The installed iiLocalDiffusion must provide the standalone Diffusers/PyTorch checkpoint runtime with --work-dir, --cache-dir and --preview-dir. Reinstall the current SDK runtime.")
    endif()
endif()
if(NOT IOS AND NOT ANDROID)
    find_program(DREAMSCAPES_FFMPEG_EXECUTABLE NAMES ffmpeg HINTS /opt/homebrew/bin /usr/local/bin REQUIRED)
    find_program(DREAMSCAPES_FFPROBE_EXECUTABLE NAMES ffprobe HINTS /opt/homebrew/bin /usr/local/bin REQUIRED)
else()
    set(DREAMSCAPES_FFMPEG_EXECUTABLE "")
    set(DREAMSCAPES_FFPROBE_EXECUTABLE "")
endif()
add_library(DreamscapesNativeGeneration STATIC src/App/Generation/NativeGenerationWork.cpp)
set_target_properties(DreamscapesNativeGeneration PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesNativeGeneration PUBLIC cxx_std_23)
# Inference is injected. This worker uses the SDK's value contracts without
# pulling its model engines into the preview codec's link boundary.
target_include_directories(DreamscapesNativeGeneration PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Generation"
    "$<TARGET_PROPERTY:iiLocalDiffusion::iiLocalDiffusion,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(DreamscapesNativeGeneration PUBLIC iiFileProvider::Native iiImageCodec::iiImageCodec)
add_library(DreamscapesViewModelsNative STATIC
    src/App/ViewModels/Native/ViewValue.cpp
    src/App/ViewModels/Native/ViewCatalog.cpp
    src/App/ViewModels/Native/VideoTimeline.cpp
    src/App/ViewModels/Native/EditorToolState.cpp
    src/App/ViewModels/Native/GenerationPresentation.cpp
    src/App/ViewModels/Native/GenerationDraft.cpp)
set_target_properties(DreamscapesViewModelsNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesViewModelsNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesViewModelsNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DreamscapesViewModelsNative PUBLIC iiSDKCore::Runtime iiJson::iiJson)
add_library(DreamscapesViewModels STATIC src/App/ViewModels/ViewData.h src/App/ViewModels/ViewData.cpp
    src/App/ViewModels/VideoTimelineModel.h src/App/ViewModels/VideoTimelineModel.cpp
    src/App/ViewModels/GenerationProjection.h src/App/ViewModels/GenerationProjection.cpp)
target_link_libraries(DreamscapesViewModels PUBLIC DreamscapesViewModelsNative Qt6::Qml)
target_include_directories(DreamscapesViewModels PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_library(DreamscapesGeneration STATIC)
target_link_libraries(DreamscapesGeneration PUBLIC DreamscapesViewModels)
set(_native_view_catalogs tools text text-definition photo file compact elements graphic view-options field-groups)
set(_native_view_catalog_files)
foreach(_catalog IN LISTS _native_view_catalogs)
    set(_path "resources/viewmodels/${_catalog}.json")
    set_source_files_properties("${_path}" PROPERTIES QT_RESOURCE_ALIAS "${_catalog}.json")
    list(APPEND _native_view_catalog_files "${_path}")
endforeach()
qt_add_resources(DreamscapesViewModels native_view_catalogs PREFIX "/viewmodels" FILES ${_native_view_catalog_files})
target_link_libraries(DreamscapesGeneration PRIVATE DreamscapesNativeGeneration)
# Publication uses the same immutable, cross-platform brand asset as the app.
set(_watermark_icon "${CMAKE_CURRENT_SOURCE_DIR}/resources/Appicon/generated/common/Dreamscapes.png")
set_source_files_properties("${_watermark_icon}" PROPERTIES QT_RESOURCE_ALIAS "Dreamscapes.png")
qt_add_resources(DreamscapesGeneration dreamscapes_watermark PREFIX "/generation/watermark"
    FILES "${_watermark_icon}")
set_source_files_properties(resources/editor/ViewOptions.json PROPERTIES QT_RESOURCE_ALIAS "ViewOptions.json")
qt_add_resources(DreamscapesGeneration editor_view_options PREFIX "/editor" FILES resources/editor/ViewOptions.json)
qt_add_resources(DreamscapesGeneration social_publishing_licenses PREFIX "/licenses/iiSocialPublish"
    BASE "${iiSocialPublish_LICENSE_DIR}" FILES
    "${iiSocialPublish_LICENSE_DIR}/nlohmann-json/LICENSE.MIT"
    "${iiSocialPublish_LICENSE_DIR}/dependencies.json")
qt_add_resources(DreamscapesGeneration editor_view_option_icons PREFIX "/editor/view-options" BASE resources/editor/view-options FILES
    resources/editor/view-options/show.svg resources/editor/view-options/rulers.svg
    resources/editor/view-options/grid.svg resources/editor/view-options/guides.svg
    resources/editor/view-options/snap.svg resources/editor/view-options/outlines.svg
    resources/editor/view-options/transparency.svg resources/editor/view-options/pixel-preview.svg
    resources/editor/view-options/close.svg resources/editor/view-options/checker.svg)
qt_add_shaders(DreamscapesGeneration editor_channel_shader PREFIX "/editor/shaders" BASE resources/editor/shaders
    FILES resources/editor/shaders/channel.frag)
qt_add_qml_module(DreamscapesGeneration
    URI Dreamscapes.Storage VERSION 1.0 NO_PLUGIN
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Dreamscapes/Storage"
    SOURCES src/App/Views/Home/SocietyTypes.h
        src/App/ViewModels/WorkspaceSession.h src/App/ViewModels/WorkspaceSession.cpp
        src/App/Generation/VideoDraftModel.h src/App/Generation/VideoDraftModel.cpp
        src/App/Generation/GenerationComposerModel.h src/App/Generation/GenerationComposerModel.cpp
        src/App/StorageViewTypes.h src/App/StorageViewTypes.cpp
        src/App/Views/Home/HomeCanvas.h src/App/Views/Home/HomeCanvas.cpp
        src/App/Views/Home/CanvasPresets.h src/App/Views/Home/CanvasPresets.cpp
        src/App/Views/Home/CanvasPreview.h src/App/Views/Home/CanvasPreview.cpp
        src/App/Views/Home/HomeFileDetails.h src/App/Views/Home/HomeFileDetails.cpp
        src/App/Views/Home/ImageOpenRequest.h src/App/Views/Home/ImageOpenRequest.cpp
        src/App/Views/ColorManagerModel.h src/App/Views/ColorManagerModel.cpp
        src/App/Views/Editor/EditorToolStateModel.h src/App/Views/Editor/EditorToolStateModel.cpp
        src/App/Views/Editor/EditorCanvas.h src/App/Views/Editor/EditorCanvas.cpp
        src/App/Views/Editor/EditorCanvasViewport.cpp
        src/App/Views/Editor/EditorCanvasAlignment.cpp
        src/App/Views/Editor/AlignmentGeometry.h src/App/Views/Editor/AlignmentGeometry.cpp
        src/App/Views/Editor/EditorViewOptions.h src/App/Views/Editor/EditorViewOptions.cpp
        src/App/Views/Editor/EditorAlignmentOverlay.h src/App/Views/Editor/EditorAlignmentOverlay.cpp
        src/App/Views/Editor/EditorFrameSnapshot.h
        src/App/Views/Editor/EditorClippingOverlay.h src/App/Views/Editor/EditorClippingOverlay.cpp
        src/App/Views/Editor/EditorFrameExporter.h src/App/Views/Editor/EditorFrameExporter.cpp
        src/App/Views/Editor/EditorTimelineModel.h src/App/Views/Editor/EditorTimelineModel.cpp
        src/App/Views/Editor/EditorTimelinePlayback.cpp
        src/App/Views/Editor/EditorTimelineNavigation.cpp
        src/App/Views/Editor/EditorTimelineViewport.h src/App/Views/Editor/EditorTimelineViewport.cpp
        src/App/Views/Editor/EditorTimelineThumbnails_p.h
        src/App/Views/Editor/EditorCanvasDocument.cpp src/App/Views/Editor/EditorCanvasTools.cpp
        src/App/Views/Editor/EditorCanvasPresentation.cpp
        src/App/Views/Editor/EditorArtboards.cpp
        src/App/Views/Editor/EditorShapes.cpp src/App/Views/Editor/EditorPixelTools.cpp
        src/App/Views/Editor/EditorElementTransform.cpp
        src/App/Views/Editor/EditorCanvasInteraction.cpp
        src/App/Views/Editor/EditorCanvasFeedback.h src/App/Views/Editor/EditorCanvasFeedback.cpp
        src/App/Views/Editor/EditorText.cpp
        src/App/Views/Editor/EditorGraphicAdjust.h src/App/Views/Editor/EditorGraphicAdjust.cpp
        src/App/Views/Editor/EditorGraphicHelperValues.h src/App/Views/Editor/EditorGraphicHelperValues.cpp
        src/App/Views/Editor/EditorGraphicHelper.h src/App/Views/Editor/EditorGraphicHelper.cpp
        src/App/Views/Editor/EditorGraphicOperations.cpp
        src/App/Views/Editor/EditorGraphicImage.h src/App/Views/Editor/EditorGraphicImage.cpp
        src/App/Views/Editor/EditorRasterSource.h src/App/Views/Editor/EditorRasterSource.cpp
        src/App/Views/Editor/EditorGraphicCommand.h src/App/Views/Editor/EditorGraphicCommand.cpp
        src/App/Views/Editor/EditorToolActions.cpp src/App/Views/Editor/EditorSelection.cpp src/App/Views/Editor/EditorBackgroundMask.cpp
        src/App/Views/Editor/EditorBackground.cpp
        src/App/Views/Editor/EditorMedia.h src/App/Views/Editor/EditorMedia.cpp
        src/App/Views/Editor/EditorAudioPlayback.h src/App/Views/Editor/EditorAudioPlayback.cpp
        src/App/Views/Editor/TimelineAudioMix.h src/App/Views/Editor/TimelineAudioMix.cpp
        src/App/Views/Editor/EditorCameraPhoto.h src/App/Views/Editor/EditorCameraPhoto.cpp
        src/App/Views/Editor/EditorPhotoGalleryModel.h src/App/Views/Editor/EditorPhotoGalleryModel.cpp
    src/App/Views/Editor/EditorOperations.cpp
    src/App/Views/Editor/EditorLayerPanels.cpp
    src/App/Views/Editor/EditorLayerHistogram.h src/App/Views/Editor/EditorLayerHistogram.cpp
    src/App/Views/Editor/EditorColorSampler.h src/App/Views/Editor/EditorColorSampler.cpp
        src/App/Views/Editor/EditorProject.h src/App/Views/Editor/EditorProject.cpp
        src/App/Views/Editor/EditorDocumentOpen.h src/App/Views/Editor/EditorDocumentOpen.cpp
        src/App/Views/Editor/EditorDocumentOpenRequest.h src/App/Views/Editor/EditorDocumentOpenRequest.cpp
        src/App/Views/Editor/EditorPhotoImport.h src/App/Views/Editor/EditorPhotoImport.cpp
        src/App/Views/Editor/EditorPhotoImportCommand.h src/App/Views/Editor/EditorPhotoImportCommand.cpp
        src/App/Views/Editor/EditorImageImport.h src/App/Views/Editor/EditorImageImport.cpp
        src/App/Views/Editor/EditorImageImportRequest.h src/App/Views/Editor/EditorImageImportRequest.cpp
        src/App/Views/Editor/EditorStillTiming.h
        src/App/Views/Editor/EditorPagePreviews.h src/App/Views/Editor/EditorPagePreviews.cpp
        src/App/Views/Editor/EditorFileInfo.cpp
        src/App/Views/Editor/EditorProjectCodec.h src/App/Views/Editor/EditorProjectCodec.cpp
        src/App/Generation/AdvancedImageParameters.h src/App/Generation/AdvancedImageParameters.cpp
        src/App/Generation/ImageParameterCodec.h src/App/Generation/ImageParameterCodec.cpp
        src/App/Generation/AdvancedImageOutput.h src/App/Generation/AdvancedImageOutput.cpp
        src/App/Generation/AdvancedImageInputs.h src/App/Generation/AdvancedImageInputs.cpp
        src/App/Generation/VideoGeneration.h src/App/Generation/VideoGeneration.cpp
        src/App/Generation/VideoPresets.cpp
        src/App/Generation/GenerationController.h src/App/Generation/GenerationController.cpp
        src/App/Generation/GenerationRequests.cpp
        src/App/Generation/GenerationExecution.cpp
        src/App/Generation/GenerationProgress.cpp
        src/App/Generation/FailureEvidence.h
        src/App/Generation/FailureAgent.h src/App/Generation/FailureAgent.cpp
        src/App/Generation/GenerationPublication.h src/App/Generation/GenerationPublication.cpp
        src/App/Generation/NativeGenerationWork.h
        src/App/Generation/GenerationProcess.h src/App/Generation/GenerationProcess.cpp
        src/App/Publishing/SocialPublishingController.h src/App/Publishing/SocialPublishingController.cpp
        src/App/Publishing/PublishDraft.h src/App/Publishing/PublishDraft.cpp
        src/App/Publishing/PublishReview.h src/App/Publishing/PublishReview.cpp
        src/App/Generation/ModelPreferences.h src/App/Generation/ModelPreferences.cpp
        src/App/Generation/SocietyGenerationStorage.h src/App/Generation/SocietyGenerationStorage.cpp
        src/App/Generation/ImageCanvas.h src/App/Generation/ImageCanvas.cpp
        src/App/Views/Result/ImageFileExporter.h src/App/Views/Result/ImageFileExporter.cpp
        src/App/Views/Result/ReviewSelection.h
        src/App/Views/Result/GenerationReviewModel.h src/App/Views/Result/GenerationReviewModel.cpp
        src/App/Views/Result/GenerationReviewExport.h src/App/Views/Result/GenerationReviewExport.cpp
        src/App/Views/Result/PhotoLibraryExporter.h src/App/Views/Result/PhotoLibraryExporter.cpp)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(DreamscapesGeneration PRIVATE src/App/Views/Result/PhotoLibraryApple.mm)
    set_source_files_properties(src/App/Views/Result/PhotoLibraryApple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(DreamscapesGeneration PRIVATE "-framework Photos" "-framework Foundation")
    target_sources(DreamscapesGeneration PRIVATE src/App/Views/Editor/EditorCaptureApple.mm src/App/Views/Editor/EditorPhotoImageApple.mm)
    set_source_files_properties(src/App/Views/Editor/EditorCaptureApple.mm src/App/Views/Editor/EditorPhotoImageApple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(DreamscapesGeneration PRIVATE "-framework ImageIO")
    if(NOT IOS)
        target_link_libraries(DreamscapesGeneration PRIVATE "-framework ScreenCaptureKit" "-framework CoreGraphics" "-framework ImageIO")
    endif()
elseif(ANDROID)
    target_sources(DreamscapesGeneration PRIVATE src/App/Views/Editor/EditorCaptureDesktop.cpp src/App/Views/Editor/EditorPhotoImageDesktop.cpp)
    target_sources(DreamscapesGeneration PRIVATE src/App/Views/Result/PhotoLibraryAndroid.cpp)
else()
    target_sources(DreamscapesGeneration PRIVATE src/App/Views/Editor/EditorCaptureDesktop.cpp src/App/Views/Editor/EditorPhotoImageDesktop.cpp)
    target_sources(DreamscapesGeneration PRIVATE src/App/Views/Result/PhotoLibraryUnsupported.cpp)
endif()
target_link_libraries(DreamscapesGeneration PUBLIC iiFileProvider::iiFileProvider iiFileProvider::Temporary
    iiPhotoLibrary::iiPhotoLibrary iiSharedCanvas::Quick iiGraphicAdjust::iiGraphicAdjust iiSocietyHelper::iiSocietyHelper Qt6::Gui Qt6::Qml iiSocietyContainer::iiSocietyContainer iiSocietyContainer::Gui iiLocalDiffusion::iiLocalDiffusion Qt6::Concurrent Qt6::Multimedia)
target_link_libraries(DreamscapesGeneration PRIVATE iiFileProvider::Native iiLocalDiffusion::ModelPackaging)
target_link_libraries(DreamscapesGeneration PRIVATE iiJson::iiJson)
target_link_libraries(DreamscapesGeneration PUBLIC iiSDKCore::Runtime)
target_link_libraries(DreamscapesGeneration PRIVATE iiMultimodalAgent::FailureDiagnostics)
target_link_libraries(DreamscapesGeneration PRIVATE iiFileProvider::CoreIO)
target_link_libraries(DreamscapesGeneration PUBLIC iiSocialPublish::iiSocialPublish)
target_compile_features(DreamscapesGeneration PUBLIC cxx_std_23)
target_link_libraries(DreamscapesGeneration PUBLIC iiColorManager::iiColorManager Qt6::Quick)
set_source_files_properties(src/App/Views/Home/Data/canvas-presets.json PROPERTIES QT_RESOURCE_ALIAS "catalog.json")
qt_add_resources(DreamscapesGeneration canvas_presets PREFIX "/canvas"
    FILES src/App/Views/Home/Data/canvas-presets.json)
target_include_directories(DreamscapesGeneration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(DreamscapesGeneration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Generation")
target_include_directories(DreamscapesGeneration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Publishing")
target_include_directories(DreamscapesGeneration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Result")
target_include_directories(DreamscapesGeneration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Home")
target_include_directories(DreamscapesGeneration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor")
if(IOS)
    enable_language(Swift)
    target_sources(DreamscapesGeneration PRIVATE src/App/Generation/GenerationScreenActivity.mm
        src/App/Generation/GenerationBackgroundActivity.mm)
    set_source_files_properties(src/App/Generation/GenerationBackgroundActivity.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(DreamscapesGeneration PRIVATE "-framework UIKit" "-framework BackgroundTasks")
endif()
target_compile_definitions(DreamscapesGeneration PRIVATE
    DREAMSCAPES_FFMPEG_EXECUTABLE="${DREAMSCAPES_FFMPEG_EXECUTABLE}"
    DREAMSCAPES_FFPROBE_EXECUTABLE="${DREAMSCAPES_FFPROBE_EXECUTABLE}"
    DREAMSCAPES_DIFFUSION_EXECUTABLE="${DREAMSCAPES_DIFFUSION_EXECUTABLE}"
    DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE="${DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE}")

# Keep build-tree launches independent of shell-only DYLD_LIBRARY_PATH settings.
if(APPLE AND NOT IOS)
    list(APPEND CMAKE_BUILD_RPATH "$<TARGET_FILE_DIR:LVRS::LVRS>")
endif()

# Source location must not change the QML module's existing resource URLs.
file(GLOB_RECURSE _app_source_resources CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/*.svg")
foreach(_app_resource IN LISTS _app_source_resources)
    string(REGEX REPLACE "^src/" "" _app_resource_alias "${_app_resource}")
    set_source_files_properties("${_app_resource}" PROPERTIES QT_RESOURCE_ALIAS "${_app_resource_alias}")
endforeach()

lvrs_add_qml_app(
    TARGET Dreamscapes
    URI Dreamscapes
    VERSION 1.0
    SOURCES src/main.cpp
    QML_FILES
        src/App/Main.qml
        src/App/Views/FailureDetails.qml
        src/App/Views/PanelRow.qml
        src/App/Views/PanelFrame.qml
        src/App/Views/PanelSheet.qml
        src/App/Views/ColorPicker.qml
        src/App/Views/Preferences/PreferencesWindow.qml
        src/App/Views/Preferences/ModelPreferenceCombo.qml
        src/App/Views/Preferences/PreferenceList.qml
        src/App/Views/Preferences/SocietyFolderPicker.qml
        src/App/Views/Home/QuickGenerate.qml
        src/App/Views/Home/DesktopGenerationComposer.qml
        src/App/Views/Home/HomeQueueBar.qml
        src/App/Views/Home/PromptField.qml
        src/App/Views/Home/NewCanvasDialog.qml
        src/App/Views/Home/CanvasSizePreview.qml
        src/App/Views/Home/HomePaintCanvas.qml
        src/App/Views/Home/QuickActionCard.qml
        src/App/Views/Home/FileCarousel.qml
        src/App/Views/Home/PublishedList.qml
        src/App/Views/Home/MobileHome.qml

        src/App/Views/Home/HomeRecentFiles.qml
        src/App/Views/Home/HomeSelectedFile.qml
        src/App/Views/Home/HomeRecentGenerations.qml
        src/App/Views/Home/HomeContentRow.qml
        src/App/Views/Home/StorageBrowser.qml

        src/App/Views/Home/ImageSelectionBar.qml

        src/App/Views/Home/HomePromptStarters.qml
        src/App/Views/Home/DesktopHome.qml
        src/App/Views/Home/DesktopHomeSidebar.qml
        src/App/Views/Home/DesktopHomeToolbar.qml
        src/App/Views/Home/AdvancedGenerate.qml
        src/App/Views/Home/ImageGenerationWorkspace.qml
        src/App/Views/Video/VideoGenerationWorkspace.qml
        src/App/Views/Video/VideoParameterPanel.qml
        src/App/Views/Video/VideoTimeline.qml
        src/App/Views/Video/VideoTimelineState.qml
        src/App/Views/Home/GenerationHistory.qml
        src/App/Views/Result/GenerationResult.qml
        src/App/Views/Result/ReviewQueue.qml
        src/App/Views/Result/ReviewInspector.qml
        src/App/Views/Result/ReviewMediaView.qml
        src/App/Views/Result/ReviewCompare.qml
        src/App/Views/Editor/CanvasEditor.qml
        src/App/Views/Editor/EditorTimeline.qml
        src/App/Views/Editor/EditorPageDeck.qml
        src/App/Views/Editor/EditorPageStrip.qml
        src/App/Views/Editor/EditorToolbar.qml

        src/App/Views/Editor/EditorToolControl.qml
        src/App/Views/Editor/EditorCompactPanel.qml

        src/App/Views/Editor/EditorButtonState.qml
        src/App/Views/Editor/EditorViewBar.qml
        src/App/Views/Editor/EditorViewOptionsPanel.qml
        src/App/Views/Editor/EditorViewOptionRow.qml

        src/App/Views/Editor/EditorCanvasRulers.qml
        src/App/Views/Editor/EditorBackgroundPanel.qml
        src/App/Views/Editor/EditorBackgroundParameter.qml
        src/App/Views/Editor/EditorBackgroundSection.qml
        src/App/Views/Editor/EditorBackgroundGradient.qml
        src/App/Views/Editor/EditorBackgroundColorPanel.qml
        src/App/Views/Editor/EditorBackgroundRemovePanel.qml
        src/App/Views/Editor/EditorBackgroundReplacePanel.qml
        src/App/Views/Editor/EditorToolPanel.qml

        src/App/Views/Editor/EditorFilePanel.qml
        src/App/Views/Editor/EditorDesktopPanel.qml
        src/App/Views/Editor/EditorCameraPhotoPanel.qml
        src/App/Views/Editor/EditorCaptureRegion.qml

        src/App/Views/Editor/EditorElementsPanel.qml
        src/App/Views/Editor/EditorElementTransform.qml

        src/App/Views/Editor/EditorLayersPanel.qml
        src/App/Views/Editor/EditorTextPanel.qml


        src/App/Views/Editor/EditorGraphicPanel.qml
        src/App/Views/Editor/EditorGraphicRow.qml
        src/App/Views/Editor/EditorDocumentControls.qml
        src/App/Views/Editor/EditorToolSheet.qml
        src/App/Views/Publishing/PublishWorkspace.qml
        src/App/Views/Publishing/PublishInspector.qml
        src/App/Views/Publishing/PublishField.qml
        src/App/Views/Publishing/PublishChoice.qml
        src/App/Views/Publishing/PublishCanvasPreview.qml
        src/App/Views/Publishing/PublishDestinationReview.qml
    NO_PLATFORM_RUNTIME_TARGETS
)
qt_add_resources(Dreamscapes result_assets
    PREFIX "/qt/qml/Dreamscapes"
    FILES src/App/Views/Result/Assets/right.svg
        src/App/Views/Home/Assets/Paint/brush.svg
        src/App/Views/Home/Assets/media-chevron.svg
        src/App/Views/Home/Assets/queue-ready.svg
        src/App/Views/Home/Assets/home-canvas.svg
        src/App/Views/Home/Assets/home-image.svg
        src/App/Views/Home/Assets/home-video.svg
        src/App/Views/Home/Assets/home-board.svg)
file(GLOB desktop_home_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Home/Assets/Desktop/*.svg")
qt_add_resources(Dreamscapes desktop_home_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${desktop_home_assets})
file(GLOB home_direction_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Home/Assets/Directions/*.svg")
qt_add_resources(Dreamscapes home_direction_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${home_direction_assets})
file(GLOB video_control_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Video/Assets/*.svg")
qt_add_resources(Dreamscapes video_control_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${video_control_assets})
qt_add_resources(Dreamscapes mobile_navigation_assets
    PREFIX "/qt/qml/Dreamscapes"
    FILES
        src/App/Views/Home/Assets/Navigation/home.svg
        src/App/Views/Home/Assets/Navigation/tools.svg
        src/App/Views/Home/Assets/Navigation/storage.svg
        src/App/Views/Home/Assets/Navigation/notification.svg
        src/App/Views/Home/Assets/Navigation/account.svg
        src/App/Views/Home/Assets/Navigation/search.svg)
file(GLOB editor_toolbar_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor/Assets/*.svg")
qt_add_resources(Dreamscapes editor_toolbar_assets
    PREFIX "/qt/qml/Dreamscapes"
    FILES ${editor_toolbar_assets})
qt_add_resources(Dreamscapes editor_panel_assets
    PREFIX "/qt/qml/Dreamscapes"
    FILES src/App/Views/Editor/Assets/Panel/general-chevron-right.svg)
file(GLOB editor_background_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor/Assets/Background/*.svg")
qt_add_resources(Dreamscapes editor_background_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${editor_background_assets})
file(GLOB editor_file_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor/Assets/File/*.svg")
qt_add_resources(Dreamscapes "EditorFileAssets" PREFIX "/qt/qml/Dreamscapes" BASE "src" FILES ${editor_file_assets})
qt_add_resources(Dreamscapes publish_workspace_assets PREFIX "/qt/qml/Dreamscapes" FILES
    src/App/Views/Publishing/Assets/chevron-down.svg
    src/App/Views/Publishing/Assets/stepper.svg
    src/App/Views/Publishing/Assets/checkbox.svg
    src/App/Views/Publishing/Assets/add.svg)
file(GLOB editor_layer_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor/Assets/Layers/*.svg")
qt_add_resources(Dreamscapes editor_layer_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${editor_layer_assets})
file(GLOB editor_text_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor/Assets/Text/*.svg")
qt_add_resources(Dreamscapes editor_text_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${editor_text_assets})
file(GLOB editor_elements_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor/Assets/Elements/*.svg")
qt_add_resources(Dreamscapes editor_elements_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${editor_elements_assets})
file(GLOB editor_camera_photo_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor/Assets/CameraPhoto/*")
foreach(asset IN LISTS editor_camera_photo_assets)
    file(RELATIVE_PATH asset_alias "${CMAKE_CURRENT_SOURCE_DIR}/src" "${asset}")
    set_source_files_properties("${asset}" PROPERTIES QT_RESOURCE_ALIAS "${asset_alias}")
endforeach()
qt_add_resources(Dreamscapes editor_camera_photo_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${editor_camera_photo_assets})
file(GLOB editor_graphic_assets CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Editor/Assets/Graphic/*.svg")
qt_add_resources(Dreamscapes editor_graphic_assets
    PREFIX "/qt/qml/Dreamscapes" FILES ${editor_graphic_assets})
set_source_files_properties(src/App/Views/Video/Assets/coastal-reference.png PROPERTIES
    QT_RESOURCE_ALIAS "App/Views/Video/Assets/coastal-reference.png")
qt_add_resources(Dreamscapes video_workspace_assets
    PREFIX "/qt/qml/Dreamscapes"
    FILES src/App/Views/Video/Assets/coastal-reference.png)
option(DREAMSCAPES_LOCAL_RUNTIME_PROBE "Enable opt-in local device inference verification" OFF)
if(DREAMSCAPES_LOCAL_RUNTIME_PROBE)
    target_compile_definitions(DreamscapesGeneration PRIVATE DREAMSCAPES_LOCAL_RUNTIME_PROBE)
    target_sources(Dreamscapes PRIVATE tests/LocalRuntimeProbe.cpp)
    target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_LOCAL_RUNTIME_PROBE)
endif()
if(iiLocalDiffusion_NATIVE_DIFFUSION)
    file(GLOB dreamscapes_native_notices CONFIGURE_DEPENDS
        "${dreamscapes_diffusion_prefix}/share/iiLocalDiffusion/licenses/*")
    qt_add_resources(Dreamscapes native_inference_licenses PREFIX "/licenses/native-inference"
        BASE "${dreamscapes_diffusion_prefix}/share/iiLocalDiffusion/licenses"
        FILES ${dreamscapes_native_notices})
endif()

set(dreamscapes_product_dependencies
    iiLocalDiffusion::iiLocalDiffusion
    iiPaintEngine::iiPaintEngine
    iiSocietyContainer::iiSocietyContainer
    iiSocietyContainer::Gui
    iiSocietyHelper::iiSocietyHelper
    LVRS::LVRS
)
target_link_libraries(Dreamscapes PRIVATE DreamscapesGeneration Qt6::QuickDialogs2 ${dreamscapes_product_dependencies})
target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_APP_VERSION="${PROJECT_VERSION}")

if(ANDROID)
    set_property(TARGET Dreamscapes PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/android")
    iiFileProvider_add_android_storage(Dreamscapes)
    foreach(dependency IN ITEMS ${dreamscapes_product_dependencies} iiFileProvider::iiFileProvider)
        set_property(TARGET Dreamscapes APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
            "$<TARGET_FILE:${dependency}>")
    endforeach()
endif()

# The installed iOS archive has QML registration/resources but no plugin archive.
# Retain its startup objects even when no C++ symbol directly references them.
get_target_property(dreamscapes_lvrs_type LVRS::LVRS TYPE)
if(IOS AND dreamscapes_lvrs_type STREQUAL "STATIC_LIBRARY")
    set_property(TARGET Dreamscapes PROPERTY LINK_LIBRARY_OVERRIDE "WHOLE_ARCHIVE,LVRS::LVRS")
endif()

set_target_properties(Dreamscapes PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.iisacc.dreamscapes"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    QT_ANDROID_PACKAGE_NAME "com.iisacc.dreamscapes"
)
if(APPLE)
    # Upgrade existing build bundles too: weights are owned by Society, and
    # must not survive an incremental build after removing their copy phase.
    add_custom_command(TARGET Dreamscapes POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "$<TARGET_BUNDLE_DIR:Dreamscapes>/iiLocalDiffusion/resources"
            "$<TARGET_BUNDLE_DIR:Dreamscapes>/Contents/Resources/iiLocalDiffusion/resources"
        VERBATIM)
endif()
if(IOS)
    # The iOS Qt package has no FFmpeg native libraries. Use Apple's media
    # plugin when transitive QML dependencies bring in QtMultimedia.
    qt_import_plugins(Dreamscapes INCLUDE Qt6::QSQLiteDriverPlugin Qt6::QDarwinMediaPlugin
        EXCLUDE Qt6::QFFmpegMediaPlugin)
    set_property(TARGET Dreamscapes APPEND PROPERTY BUILD_RPATH "@executable_path/Frameworks")
    # iOS cannot load the development machine's SDK libraries. Embed and sign
    # every dynamic product library, including FileProvider.
    set(dreamscapes_ios_runtime_dependencies ${dreamscapes_product_dependencies}
        iiFileProvider::iiFileProvider)
    foreach(dependency IN LISTS dreamscapes_ios_runtime_dependencies)
        get_target_property(dependency_type ${dependency} TYPE)
        if(dependency_type STREQUAL "SHARED_LIBRARY")
            # Xcode's embed property requires concrete paths for imported SDKs.
            # Copy the install-name filename, which can differ from VERSION.
            get_target_property(dependency_configs ${dependency} IMPORTED_CONFIGURATIONS)
            list(GET dependency_configs 0 dependency_config)
            get_target_property(dependency_file ${dependency} IMPORTED_LOCATION_${dependency_config})
            get_target_property(dependency_soname ${dependency} IMPORTED_SONAME_${dependency_config})
            get_filename_component(dependency_directory "${dependency_file}" DIRECTORY)
            get_filename_component(dependency_name "${dependency_soname}" NAME)
            set(dependency_runtime_file "${dependency_directory}/${dependency_name}")
            if(NOT EXISTS "${dependency_runtime_file}")
                message(FATAL_ERROR "Missing iOS runtime library: ${dependency_runtime_file}")
            endif()
            set_property(TARGET Dreamscapes APPEND PROPERTY XCODE_EMBED_FRAMEWORKS "${dependency_runtime_file}")
        endif()
    endforeach()
    set_target_properties(Dreamscapes PROPERTIES
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
        XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY YES)
    set(SOCIETY_IOS_APP_GROUP "group.com.iisacc.society" CACHE STRING "Same App Group as the Society app")
    set(SOCIETY_IOS_TEAM "" CACHE STRING "Apple development team shared with Society")
    iiSocietyContainer_configure_ios_client(Dreamscapes APP_GROUP "${SOCIETY_IOS_APP_GROUP}"
        TEAM "${SOCIETY_IOS_TEAM}" DISPLAY_NAME Dreamscapes)
    # Large checkpoint mappings need virtual address space as well as RAM.
    # The runtime still sizes its working set from the actual process limit.
    configure_file(src/platform/iOS/Dreamscapes.entitlements.in
        "${CMAKE_CURRENT_BINARY_DIR}/Dreamscapes.entitlements" @ONLY)
    set_property(TARGET Dreamscapes PROPERTY XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
        "${CMAKE_CURRENT_BINARY_DIR}/Dreamscapes.entitlements")
endif()

if(APPLE)
    # Extend the selected Qt/Society template without discarding iOS App Group settings.
    get_target_property(dreamscapes_plist Dreamscapes MACOSX_BUNDLE_INFO_PLIST)
    if(NOT dreamscapes_plist)
        set(dreamscapes_plist "${Qt6_DIR}/macos/Info.plist.app.in")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dreamscapes_plist}")
    file(READ "${dreamscapes_plist}" dreamscapes_plist_content)
    if(IOS)
        string(REGEX REPLACE "</dict>[ \t\r\n]*</plist>"
            "<key>LSApplicationQueriesSchemes</key><array><string>society</string></array>\n<key>UIBackgroundModes</key><array><string>processing</string></array>\n<key>BGTaskSchedulerPermittedIdentifiers</key><array><string>com.iisacc.dreamscapes.generation.*</string></array>\n</dict></plist>"
            dreamscapes_plist_content "${dreamscapes_plist_content}")
    endif()
    string(REGEX REPLACE "</dict>[ \t\r\n]*</plist>"
        "<key>NSLocalNetworkUsageDescription</key><string>Connect to your Society host to download selected models and upload completed images.</string>\n<key>NSBonjourServices</key><array><string>_society-pair._udp</string></array>\n<key>NSBluetoothAlwaysUsageDescription</key><string>Discover your nearby Society host. Files transfer over Wi-Fi or LAN.</string>\n<key>NSCameraUsageDescription</key><string>Preview your camera and capture photos or silent video for your canvas.</string>\n<key>NSPhotoLibraryUsageDescription</key><string>Browse selected Photos images and import originals into your canvas.</string>\n<key>NSPhotoLibraryAddUsageDescription</key><string>Save generated images to Photos. iCloud Photos syncs them when enabled for your photo library.</string>\n</dict></plist>"
        dreamscapes_plist_content "${dreamscapes_plist_content}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/photo-library")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/photo-library/Dreamscapes.Info.plist.in" "${dreamscapes_plist_content}")
    set_property(TARGET Dreamscapes PROPERTY MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_BINARY_DIR}/photo-library/Dreamscapes.Info.plist.in")
endif()

include(cmake/AppIcons.cmake)
if(IOS)
    iiSocietyContainer_add_ios_live_activity(Dreamscapes TEAM "${SOCIETY_IOS_TEAM}"
        BRIDGE_TARGET DreamscapesGeneration)
endif()
dreamscapes_add_app_icons(Dreamscapes)

include(CTest)
option(DREAMSCAPES_BUILD_ANDROID_PHOTO_TESTS "Build the native MediaStore integration test APK" OFF)
if(ANDROID AND DREAMSCAPES_BUILD_ANDROID_PHOTO_TESTS)
    set(QT_USE_TARGET_ANDROID_BUILD_DIR TRUE CACHE BOOL "Keep Android test and app packages separate" FORCE)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    qt_add_executable(DreamscapesPhotoNativeTests src/App/Views/Result/tst_PhotoLibraryAndroid.cpp)
    target_link_libraries(DreamscapesPhotoNativeTests PRIVATE DreamscapesGeneration Qt6::Test)
    get_target_property(dreamscapes_android_package Dreamscapes QT_ANDROID_PACKAGE_SOURCE_DIR)
    set_target_properties(DreamscapesPhotoNativeTests PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${dreamscapes_android_package}"
        QT_ANDROID_PACKAGE_NAME "com.iisacc.dreamscapes.phototests"
        QT_ANDROID_APP_NAME "Dreamscapes Photo Tests")
    set_property(TARGET DreamscapesPhotoNativeTests APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
        "$<TARGET_FILE:iiSocietyContainer::iiSocietyContainer>")
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeViewModelTests tests/tst_nativeviewmodels.cpp)
    set_target_properties(DreamscapesNativeViewModelTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF MACOSX_BUNDLE OFF)
    target_link_libraries(DreamscapesNativeViewModelTests PRIVATE DreamscapesViewModelsNative)
    target_compile_definitions(DreamscapesNativeViewModelTests PRIVATE DREAMSCAPES_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME Dreamscapes.NativeViewModels COMMAND DreamscapesNativeViewModelTests)
    qt_add_executable(DreamscapesViewModelBridgeTests tests/tst_viewmodelbridge.cpp)
    set_target_properties(DreamscapesViewModelBridgeTests PROPERTIES MACOSX_BUNDLE OFF)
    target_link_libraries(DreamscapesViewModelBridgeTests PRIVATE DreamscapesViewModels Qt6::Test Qt6::Quick)
    target_compile_definitions(DreamscapesViewModelBridgeTests PRIVATE DREAMSCAPES_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME Dreamscapes.ViewModelBridge COMMAND DreamscapesViewModelBridgeTests)
    set_tests_properties(Dreamscapes.ViewModelBridge PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
    add_executable(DreamscapesNativeGenerationTests tests/tst_nativegeneration.cpp)
    set_target_properties(DreamscapesNativeGenerationTests PROPERTIES MACOSX_BUNDLE FALSE AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeGenerationTests PRIVATE DreamscapesNativeGeneration)
    add_test(NAME Dreamscapes.NativeGeneration COMMAND DreamscapesNativeGenerationTests "${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(DreamscapesSocialPublishingTests tests/tst_socialpublishing.cpp
        src/App/Publishing/SocialPublishingController.h src/App/Publishing/SocialPublishingController.cpp)
    set_target_properties(DreamscapesSocialPublishingTests PROPERTIES MACOSX_BUNDLE FALSE AUTOMOC ON)
    target_link_libraries(DreamscapesSocialPublishingTests PRIVATE Qt6::Core Qt6::Qml iiSocialPublish::iiSocialPublish)
    add_test(NAME Dreamscapes.SocialPublishing COMMAND DreamscapesSocialPublishingTests)
    set_tests_properties(Dreamscapes.SocialPublishing PROPERTIES TIMEOUT 30)
    add_executable(DreamscapesHistoryRepair tools/repair_generation_history.cpp)
    set_target_properties(DreamscapesHistoryRepair PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesHistoryRepair PRIVATE DreamscapesGeneration)
    add_executable(DreamscapesApplicationLifetimeTests tests/tst_applicationlifetime.cpp)
    set_target_properties(DreamscapesApplicationLifetimeTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesApplicationLifetimeTests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
    add_test(NAME Dreamscapes.ApplicationLifetime COMMAND DreamscapesApplicationLifetimeTests)
    set_tests_properties(Dreamscapes.ApplicationLifetime PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_property(dreamscapes_directory_links DIRECTORY PROPERTY LINK_LIBRARIES)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/DreamscapesTargetLinks-$<CONFIG>.txt"
        CONTENT "directory=${dreamscapes_directory_links}\ngeneration=$<JOIN:$<TARGET_PROPERTY:DreamscapesGeneration,LINK_LIBRARIES>,|>\nlifetime=$<JOIN:$<TARGET_PROPERTY:DreamscapesApplicationLifetimeTests,LINK_LIBRARIES>,|>\n")
    add_test(NAME Dreamscapes.DependencyBoundary
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_link_dependencies.py"
            "${CMAKE_CURRENT_BINARY_DIR}/DreamscapesTargetLinks-$<CONFIG>.txt"
            "$<TARGET_FILE:DreamscapesApplicationLifetimeTests>")
    if(APPLE)
        add_test(NAME Dreamscapes.MacRuntimeProfile
            COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_macos_runtime_profile.py -v)
        set_tests_properties(Dreamscapes.MacRuntimeProfile PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 60)
        add_test(NAME Dreamscapes.MacDeployLock
            COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_deploy_macos_lock.py -v)
        set_tests_properties(Dreamscapes.MacDeployLock PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30)
    endif()
    add_test(NAME Dreamscapes.EditorGraphicCatalog
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_graphic_panels.py -v)
    set_tests_properties(Dreamscapes.EditorGraphicCatalog PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 60)
    add_test(NAME Dreamscapes.EditorElementsCatalog
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_elements_panels.py -v)
    set_tests_properties(Dreamscapes.EditorElementsCatalog PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 60)
    add_test(NAME Dreamscapes.EditorButtonStatePolicy
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_editor_button_states.py")
    set_tests_properties(Dreamscapes.EditorButtonStatePolicy PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 30)
    file(GLOB DREAMSCAPES_NODE_RUNTIME_HINTS "/opt/homebrew/share/emsdk/node/*/bin")
    find_program(DREAMSCAPES_NODE_EXECUTABLE NAMES node HINTS ${DREAMSCAPES_NODE_RUNTIME_HINTS})
    if(DREAMSCAPES_NODE_EXECUTABLE)
        add_test(NAME Dreamscapes.EditorGraphicState
            COMMAND "${DREAMSCAPES_NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_graphic_state.js")
        set_tests_properties(Dreamscapes.EditorGraphicState PROPERTIES
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 60)
    endif()
    add_test(NAME Dreamscapes.GenerationResourceVerifier
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_generation_resources.py -v)
    add_test(NAME Dreamscapes.GenerationResourceProvisioning
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_provision_generation_resources.py -v)
    add_test(NAME Dreamscapes.FailureAgentProvisioning
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}/tests" -p test_provision_failure_agent.py -v)
    if(APPLE)
        add_test(NAME Dreamscapes.GenerationResources
            COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_generation_resources.py"
                "$<TARGET_BUNDLE_DIR:Dreamscapes>")
    endif()
    add_test(NAME Dreamscapes.AppIcons
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_app_icons.py")
    add_test(NAME Dreamscapes.MobileNavigationAssets
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mobile_navigation_assets.py")
    # Only the integration harness acts as Society peers; the app has no Sync transport.

    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test Widgets)
    find_package(iiSocietyHelper 0.11.0 CONFIG REQUIRED COMPONENTS GenerationHost)
    qt_add_executable(DreamscapesLocalSocietyTests src/App/Generation/tst_LocalSociety.cpp)
    target_link_libraries(DreamscapesLocalSocietyTests PRIVATE iiSocietyHelper::GenerationHost)
    target_link_libraries(DreamscapesLocalSocietyTests PRIVATE DreamscapesGeneration
        iiSocietyHelper::iiSocietyHelper Qt6::Test)
    target_compile_definitions(DreamscapesLocalSocietyTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_FAKE_GENERATOR="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Generation/fixtures/generate.py")
    add_test(NAME Dreamscapes.LocalSociety COMMAND DreamscapesLocalSocietyTests)
    # The complete native fixture includes process/loader setup on shared volumes;
    # individual job watchdogs retain their own short, asserted deadlines.
    set_tests_properties(Dreamscapes.LocalSociety PROPERTIES TIMEOUT 180)
    # Keep this GUI target limited to sources present in Dreamscapes.
    qt_add_executable(DreamscapesGuiTests src/App/tst_Gui.cpp)
    target_link_libraries(DreamscapesGuiTests PRIVATE Qt6::Test Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_link_libraries(DreamscapesGuiTests PRIVATE DreamscapesGeneration)
    target_link_libraries(DreamscapesGuiTests PRIVATE iiSocietyHelper::iiSocietyHelper)
    target_compile_definitions(DreamscapesGuiTests PRIVATE
        DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/App"
        DREAMSCAPES_EXECUTABLE_PATH="$<TARGET_FILE:Dreamscapes>"
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_FAKE_GENERATOR="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Generation/fixtures/generate.py"
    )
    add_dependencies(DreamscapesGuiTests Dreamscapes)
    lvrs_configure_qml_app(DreamscapesGuiTests NO_PLATFORM_RUNTIME_TARGETS)
    qt_add_executable(DreamscapesEditorLayerTimelineTests src/App/tst_EditorLayerTimeline.cpp)
    target_link_libraries(DreamscapesEditorLayerTimelineTests PRIVATE DreamscapesGeneration
        Qt6::Test Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_compile_definitions(DreamscapesEditorLayerTimelineTests PRIVATE
        DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/App"
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    lvrs_configure_qml_app(DreamscapesEditorLayerTimelineTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.EditorLayerTimeline COMMAND DreamscapesEditorLayerTimelineTests)
    set_tests_properties(Dreamscapes.EditorLayerTimeline PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    qt_add_executable(DreamscapesTimelineViewportTests tests/tst_timelineviewport.cpp)
    target_link_libraries(DreamscapesTimelineViewportTests PRIVATE DreamscapesGeneration Qt6::Test)
    add_test(NAME Dreamscapes.TimelineViewport COMMAND DreamscapesTimelineViewportTests)

    add_executable(DreamscapesReviewSelectionCoreTests tests/test_reviewselection.cpp)
    target_compile_features(DreamscapesReviewSelectionCoreTests PRIVATE cxx_std_23)
    target_include_directories(DreamscapesReviewSelectionCoreTests PRIVATE src)
    add_test(NAME Dreamscapes.ReviewSelectionCore COMMAND DreamscapesReviewSelectionCoreTests)
    qt_add_executable(DreamscapesReviewModelTests tests/tst_reviewmodel.cpp
        src/App/Views/Result/GenerationReviewModel.h src/App/Views/Result/GenerationReviewModel.cpp)
    target_compile_features(DreamscapesReviewModelTests PRIVATE cxx_std_23)
    target_include_directories(DreamscapesReviewModelTests PRIVATE src)
    target_link_libraries(DreamscapesReviewModelTests PRIVATE Qt6::Test)
    target_compile_definitions(DreamscapesReviewModelTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ReviewModel COMMAND DreamscapesReviewModelTests)
    qt_add_executable(DreamscapesReviewExportTests tests/tst_reviewexport.cpp)
    target_link_libraries(DreamscapesReviewExportTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesReviewExportTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ReviewExport COMMAND DreamscapesReviewExportTests)
    qt_add_executable(DreamscapesReviewGuiTests src/App/tst_GenerationReview.cpp)
    target_link_libraries(DreamscapesReviewGuiTests PRIVATE DreamscapesGeneration
        Qt6::Test Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_compile_definitions(DreamscapesReviewGuiTests PRIVATE
        DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/App"
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_FAKE_GENERATOR="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Generation/fixtures/generate.py")
    lvrs_configure_qml_app(DreamscapesReviewGuiTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.ReviewWorkspace COMMAND DreamscapesReviewGuiTests)
    set_tests_properties(Dreamscapes.ReviewWorkspace PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    qt_add_executable(DreamscapesPanelFrameTests tests/tst_panelframes.cpp)
    target_include_directories(DreamscapesPanelFrameTests PRIVATE src)
    target_link_libraries(DreamscapesPanelFrameTests PRIVATE DreamscapesGeneration
        Qt6::Test Qt6::Widgets Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_compile_definitions(DreamscapesPanelFrameTests PRIVATE
        DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/App"
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    lvrs_configure_qml_app(DreamscapesPanelFrameTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.PanelFrames COMMAND DreamscapesPanelFrameTests)
    set_tests_properties(Dreamscapes.PanelFrames PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 240)
    add_test(NAME Dreamscapes.FailureDetails COMMAND DreamscapesGuiTests failureDetailsShowFullTextAndScroll)
    set_tests_properties(Dreamscapes.FailureDetails PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 60)

    qt_add_executable(DreamscapesTimelineThumbnailTests tests/tst_timelinethumbnails.cpp)
    target_include_directories(DreamscapesTimelineThumbnailTests PRIVATE src)
    target_link_libraries(DreamscapesTimelineThumbnailTests PRIVATE Qt6::Test Qt6::Concurrent)
    add_test(NAME Dreamscapes.TimelineThumbnails COMMAND DreamscapesTimelineThumbnailTests)
    # The focused groups below cover the other 117 slots. Give these six
    # independent processes so Qt's global shortcut contexts cannot cross tests.
    add_test(NAME Dreamscapes.Gui COMMAND DreamscapesGuiTests mainStartupPerformance)
    add_test(NAME Dreamscapes.GuiMobileHome COMMAND DreamscapesGuiTests mobileHomeUsesFigmaSectionsLimitsAndLvrsNavigation)
    add_test(NAME Dreamscapes.GuiAdvancedDefaults COMMAND DreamscapesGuiTests advancedGenerationDefaultsAndDynamicCollections)
    add_test(NAME Dreamscapes.GuiAdvancedControls COMMAND DreamscapesGuiTests advancedControlsAppearOnlyAfterAdd)
    add_test(NAME Dreamscapes.GuiForegroundGeneration COMMAND DreamscapesGuiTests foregroundApplicationPreparesBeforeGenerate)
    add_test(NAME Dreamscapes.GuiSharedWindow COMMAND DreamscapesGuiTests mainCreatesOneSharedWindow)
    set_tests_properties(Dreamscapes.GuiMobileHome Dreamscapes.GuiAdvancedDefaults
        Dreamscapes.GuiAdvancedControls Dreamscapes.GuiForegroundGeneration Dreamscapes.GuiSharedWindow
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    add_test(NAME Dreamscapes.GuiRegistry COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/verify_gui_registry.py"
        --test-executable "$<TARGET_FILE:DreamscapesGuiTests>"
        --build-dir "${CMAKE_CURRENT_BINARY_DIR}" --ctest "${CMAKE_CTEST_COMMAND}")
    set_tests_properties(Dreamscapes.GuiRegistry PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
    add_test(NAME Dreamscapes.PackagedApplication COMMAND DreamscapesGuiTests packagedApplicationStarts)
    set_tests_properties(Dreamscapes.PackagedApplication PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        RUN_SERIAL TRUE TIMEOUT 120)
    add_test(NAME Dreamscapes.CompactEditorDefaults COMMAND DreamscapesGuiTests applicationLaunchWindowDefaults)
    set_tests_properties(Dreamscapes.CompactEditorDefaults PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    add_test(NAME Dreamscapes.EditorLayout COMMAND DreamscapesGuiTests desktopEditorLayoutCentersCanvas desktopEditorToolbarScrollsAndNavigates desktopEditorElementsMatchesFigma)
    add_test(NAME Dreamscapes.EditorSidebar COMMAND DreamscapesGuiTests editorSidebarKeepsSharedWindowLayout)
    add_test(NAME Dreamscapes.EditorApplicationToolbar COMMAND DreamscapesGuiTests editorSharesApplicationToolbar)
    add_test(NAME Dreamscapes.EditorPublish COMMAND DreamscapesGuiTests editorPublishUsesFigmaActionRole)
    add_test(NAME Dreamscapes.PublishWorkspace COMMAND DreamscapesGuiTests publishWorkspaceRetainsTheEditorAndUsesRealCanvases)
    set_tests_properties(Dreamscapes.PublishWorkspace PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    qt_add_executable(DreamscapesPublishDraftTests tests/tst_publishdraft.cpp)
    target_link_libraries(DreamscapesPublishDraftTests PRIVATE DreamscapesGeneration iiJson::iiJson Qt6::Test Qt6::Widgets Qt6::Quick)
    target_compile_definitions(DreamscapesPublishDraftTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    lvrs_configure_qml_app(DreamscapesPublishDraftTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.PublishDraft COMMAND DreamscapesPublishDraftTests)
    # Includes durable working-file edits and standalone-file migration on disk.
    set_tests_properties(Dreamscapes.PublishDraft PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    set_tests_properties(Dreamscapes.EditorPublish PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 90)
    add_test(NAME Dreamscapes.Tooltips COMMAND DreamscapesGuiTests
        qmlTooltipsDoNotUseQtDefaults applicationTooltipsUseLvrs)
    set_tests_properties(Dreamscapes.Tooltips PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 90)
    set_tests_properties(Dreamscapes.EditorApplicationToolbar PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    add_test(NAME Dreamscapes.CanvasFeedbackGui COMMAND DreamscapesGuiTests canvasToolFeedbackFollowsNativePointer)
    set_tests_properties(Dreamscapes.CanvasFeedbackGui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    set_tests_properties(Dreamscapes.EditorSidebar PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    add_test(NAME Dreamscapes.EditorTimeline COMMAND DreamscapesGuiTests videoTimelineTracks editorTimelineTrackManagement editorTimelineLayout editorTimelineControlsAndPlayback)
    set_tests_properties(Dreamscapes.EditorTimeline PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    set_tests_properties(Dreamscapes.EditorLayout PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    add_test(NAME Dreamscapes.EditorPanelResize COMMAND DreamscapesGuiTests desktopEditorPanelResizes)
    add_test(NAME Dreamscapes.EditorLayers COMMAND DreamscapesGuiTests editorLayerPanelFourStates editorColorSamplingPanelsWaitForCapturedResult)
    add_test(NAME Dreamscapes.GraphicHelperGui COMMAND DreamscapesGuiTests editorGraphicHelpersReturnOnlySettings editorGraphicHelperRetiresWhenThePanelContextChanges editorGraphicQueuedImageCommandRejectsAChangedTarget editorGraphicImageCommandsUseTheNativeWorker editorGraphicImageCommandRetiresWithThePanel editorGeometryMaskUsesFrameTransform editorChunkedMaskMatchesViewport)
    set_tests_properties(Dreamscapes.GraphicHelperGui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    add_test(NAME Dreamscapes.DynamicRasterGui COMMAND DreamscapesGuiTests editorDynamicRasterUsesCurrentFrameInTheGraphicPanel)
    set_tests_properties(Dreamscapes.DynamicRasterGui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    add_test(NAME Dreamscapes.EditorButtonStates COMMAND DreamscapesGuiTests
        editorButtonInteractionStates editorPanelButtonKindsStayFixed)
    set_tests_properties(Dreamscapes.EditorButtonStates PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    add_test(NAME Dreamscapes.EditorText COMMAND DreamscapesGuiTests editorTextPanelFourStates editorTextPanelMobile)
    add_test(NAME Dreamscapes.EditorCameraPhoto COMMAND DreamscapesGuiTests editorCameraPhotoViews editorPhotoImportWorkerBoundary editorGalleryRowsStayStable editorGalleryPermissionContinuation)
    add_test(NAME Dreamscapes.SocietyAliases COMMAND DreamscapesGuiTests societyContainerMirrorIsOwnerProvided)
    add_test(NAME Dreamscapes.StorageBrowser COMMAND DreamscapesGuiTests
        storageBrowserRoutes storageBrowserUnavailableEntriesAndContainerChanges storageBrowserDownloadOpensFreshAlias)
    set_tests_properties(Dreamscapes.StorageBrowser PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    set_tests_properties(Dreamscapes.SocietyAliases PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    set_tests_properties(Dreamscapes.EditorCameraPhoto PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    qt_add_executable(DreamscapesCameraPhotoTests tests/tst_cameraphoto.cpp)
    target_link_libraries(DreamscapesCameraPhotoTests PRIVATE DreamscapesGeneration Qt6::Test Qt6::Widgets)
    target_compile_definitions(DreamscapesCameraPhotoTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.CameraPhoto COMMAND DreamscapesCameraPhotoTests)
    set_tests_properties(Dreamscapes.CameraPhoto PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DREAMSCAPES_EDITOR_SOURCE_CACHE=${CMAKE_CURRENT_BINARY_DIR}/verification/camera-photo-cache" TIMEOUT 60)
    set_tests_properties(Dreamscapes.EditorText PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    set_tests_properties(Dreamscapes.EditorLayers PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    set_tests_properties(Dreamscapes.EditorPanelResize PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    add_test(NAME Dreamscapes.EditorElements COMMAND DreamscapesGuiTests
        desktopEditorElementsMatchesFigma editorElementsTransformAndInspectorStayInSync
        editorToolNumericContracts editorToolSheets
        mobileEditorToolbarSlidesAndSelects canvasRoutesPreserveSelectionAndDraft
        desktopHomeSidebarMatchesFigmaAndScrolls)
    set_tests_properties(Dreamscapes.EditorElements PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 600)
    add_test(NAME Dreamscapes.EditorToolPanels COMMAND DreamscapesGuiTests
        panelRowsKeepLabelsPassive
        desktopEditorToolPanelsMatchFigma desktopEditorElementsMatchesFigma
        editorToolNumericContracts editorToolSheets)
    add_test(NAME Dreamscapes.EditorFilePanel COMMAND DreamscapesGuiTests editorFileThreeViews)
    add_test(NAME Dreamscapes.EditorCompactPanels COMMAND DreamscapesGuiTests editorCompactPanelsMatchFigma)
    set_tests_properties(Dreamscapes.EditorCompactPanels PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 300)
    set_tests_properties(Dreamscapes.EditorFilePanel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    add_test(NAME Dreamscapes.EditorGraphicPanels COMMAND DreamscapesGuiTests
        editorGraphicPanelAllFigmaViews editorGraphicPanelEditsPersistAndReset editorGraphicPanelMobileRoutes editorGraphicPanelCollectionsAndPickers)
    add_test(NAME Dreamscapes.EditorColorPassiveSelection COMMAND DreamscapesGuiTests
        editorColorToolSelectionDoesNotEditDocument editorToolControlsDoNotCommitUneditedFocus)
    set_tests_properties(Dreamscapes.EditorColorPassiveSelection PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 360)
    set_tests_properties(Dreamscapes.EditorGraphicPanels PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 1200)
    set_tests_properties(Dreamscapes.EditorToolPanels PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 300)
    add_test(NAME Dreamscapes.Preferences COMMAND DreamscapesGuiTests
        preferencesCategoriesAndDefaultModels preferencesPromptsWrapAndPersist generationPromptDefaultsSeedNewDrafts
        preferencesLvrsViewsAndFolderPicker preferencesAccountAvatarFollowsSdk)
    set_tests_properties(Dreamscapes.Preferences PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    add_test(NAME Dreamscapes.QuickGenerate COMMAND DreamscapesGuiTests
        homeCanvasMaxWidthAndAutoHeight homeCanvasScalesToFramePreservingPainting
        homePromptDragDoesNotMoveWindow viewsLeaveWindowChromeAvailable
        homeCanvasSliderRanges homeCanvasDragPaintAndColorPicker sharedPanelLayout sharedControlsSubmitCurrentSelection
        referenceGenerateOpensResultImmediately
        countSelectionCreatesThreeImagesInSociety resultScreenLayout
        canvasRoutesPreserveSelectionAndDraft)
    add_test(NAME Dreamscapes.LiveGeneration COMMAND DreamscapesGuiTests
        generateOpensResultImmediatelyAndDisplaysEveryPreview resultCancelsTheActiveGeneration
        newSubmissionReplacesDismissedResults dismissedGenerationCannotReopenOrContaminateTheNextSubmission
        resultGalleryLayoutAndSelection generateButtonUsesSocietyStorage resultCannotOpenProjectWithoutReadableImage)
    set_tests_properties(Dreamscapes.LiveGeneration PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 240)
    add_test(NAME Dreamscapes.DesktopHome COMMAND DreamscapesGuiTests
        desktopHomeContinuousRowsAndPromptStarters desktopHomeSelectionIsPresentationOnly desktopHomeSidebarReflowsAndRoutes
        desktopHomeSidebarMatchesFigmaAndScrolls
        desktopHomeSearchesSocietyAndUpdates desktopHomeRendersFigmaFrame
        historyAppearsBelowQuickGenerateAndScrolls historyShowsAnEmptyState
        sharedContentSurvivesLayoutChanges)
    add_test(NAME Dreamscapes.HomeCanvasPreviews COMMAND DreamscapesGuiTests homeCanvasPreviews)
    set_tests_properties(Dreamscapes.HomeCanvasPreviews PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 90)
    add_test(NAME Dreamscapes.Video COMMAND DreamscapesGuiTests videoHomeSubmitPlaysAndExports
        videoWorkspaceStartsEmptyAndUsesGenerationDefaults videoInspectorStaysWithinViewport
        videoWorkspaceRoutesEditsAndGenerates)
    set_tests_properties(Dreamscapes.Video PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 90)
    add_test(NAME Dreamscapes.PromptFields COMMAND DreamscapesGuiTests
        promptFieldsWrapAndGrow promptCursorStaysVisible
        advancedWorkspacePreservesDraftAndScrolls sharedControlsSubmitCurrentSelection resultScreenLayout)
    set_tests_properties(Dreamscapes.PromptFields PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 180)
    set_tests_properties(Dreamscapes.DesktopHome PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 180)
    set_tests_properties(Dreamscapes.QuickGenerate PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 180)

    set_tests_properties(Dreamscapes.Gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 600
    )
    qt_add_executable(DreamscapesHomeCanvasTests tests/tst_homecanvas.cpp)
    qt_add_executable(DreamscapesImageCanvasTests tests/tst_imagecanvas.cpp)
    qt_add_executable(DreamscapesCanvasPreviewTests tests/tst_canvaspreview.cpp)
    target_link_libraries(DreamscapesCanvasPreviewTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesCanvasPreviewTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.CanvasPreview COMMAND DreamscapesCanvasPreviewTests)
    set_tests_properties(Dreamscapes.CanvasPreview PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)
    target_link_libraries(DreamscapesImageCanvasTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesImageCanvasTests PRIVATE
        DREAMSCAPES_HISTORY_REPAIR="$<TARGET_FILE:DreamscapesHistoryRepair>")
    add_dependencies(DreamscapesImageCanvasTests DreamscapesHistoryRepair)
    target_compile_definitions(DreamscapesImageCanvasTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ImageCanvas COMMAND DreamscapesImageCanvasTests)
    set_tests_properties(Dreamscapes.ImageCanvas PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)
    qt_add_executable(DreamscapesEditorCanvasTests tests/tst_editorcanvas.cpp)
    qt_add_executable(DreamscapesEditorToolsTests tests/tst_editortools.cpp)
    qt_add_executable(DreamscapesGraphicHelperValuesTests tests/tst_graphichelpervalues.cpp)
    set_target_properties(DreamscapesGraphicHelperValuesTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesGraphicHelperValuesTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_include_directories(DreamscapesGraphicHelperValuesTests PRIVATE src)
    add_test(NAME Dreamscapes.GraphicHelperValues COMMAND DreamscapesGraphicHelperValuesTests)
    qt_add_executable(DreamscapesGraphicHelperTests tests/tst_graphichelper.cpp)
    set_target_properties(DreamscapesGraphicHelperTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesGraphicHelperTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_include_directories(DreamscapesGraphicHelperTests PRIVATE src)
    target_compile_definitions(DreamscapesGraphicHelperTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.GraphicHelper COMMAND DreamscapesGraphicHelperTests)
    set_tests_properties(Dreamscapes.GraphicHelper PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesGraphicCommandTests tests/tst_graphiccommand.cpp)
    set_target_properties(DreamscapesGraphicCommandTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesGraphicCommandTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_include_directories(DreamscapesGraphicCommandTests PRIVATE src)
    target_compile_definitions(DreamscapesGraphicCommandTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.GraphicCommand COMMAND DreamscapesGraphicCommandTests)
    set_tests_properties(Dreamscapes.GraphicCommand PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesTypedMaskTests tests/tst_typedmask.cpp)
    set_target_properties(DreamscapesTypedMaskTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesTypedMaskTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_include_directories(DreamscapesTypedMaskTests PRIVATE src)
    target_compile_definitions(DreamscapesTypedMaskTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.TypedMaskInput COMMAND DreamscapesTypedMaskTests)
    set_tests_properties(Dreamscapes.TypedMaskInput PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesPhotoImportTests tests/tst_photoimport.cpp)
    set_target_properties(DreamscapesPhotoImportTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesPhotoImportTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_include_directories(DreamscapesPhotoImportTests PRIVATE src)
    target_compile_definitions(DreamscapesPhotoImportTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(DreamscapesPhotoImportTests PRIVATE DREAMSCAPES_TEST_FFMPEG="${DREAMSCAPES_FFMPEG_EXECUTABLE}")
    add_test(NAME Dreamscapes.PhotoImport COMMAND DreamscapesPhotoImportTests)
    set_tests_properties(Dreamscapes.PhotoImport PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesDynamicRasterTests tests/tst_dynamicraster.cpp)
    set_target_properties(DreamscapesDynamicRasterTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesDynamicRasterTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_include_directories(DreamscapesDynamicRasterTests PRIVATE src)
    target_compile_definitions(DreamscapesDynamicRasterTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.DynamicRaster COMMAND DreamscapesDynamicRasterTests)
    set_tests_properties(Dreamscapes.DynamicRaster PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesColorSamplerTests tests/tst_colorsampler.cpp)
    qt_add_executable(DreamscapesCanvasColorSamplingTests tests/tst_canvascolorsampling.cpp)
    foreach(color_test IN ITEMS DreamscapesColorSamplerTests DreamscapesCanvasColorSamplingTests)
        set_target_properties(${color_test} PROPERTIES MACOSX_BUNDLE FALSE)
        target_link_libraries(${color_test} PRIVATE DreamscapesGeneration Qt6::Test)
        target_include_directories(${color_test} PRIVATE src)
        target_compile_definitions(${color_test} PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    endforeach()
    add_test(NAME Dreamscapes.ColorSampler COMMAND DreamscapesColorSamplerTests)
    add_test(NAME Dreamscapes.CanvasColorSampling COMMAND DreamscapesCanvasColorSamplingTests)
    set_tests_properties(Dreamscapes.ColorSampler Dreamscapes.CanvasColorSampling PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesLayerHistogramTests tests/tst_layerhistogram.cpp)
    set_target_properties(DreamscapesLayerHistogramTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesLayerHistogramTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_include_directories(DreamscapesLayerHistogramTests PRIVATE src)
    target_compile_definitions(DreamscapesLayerHistogramTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.LayerHistogram COMMAND DreamscapesLayerHistogramTests)
    set_tests_properties(Dreamscapes.LayerHistogram PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesElementsSelectionTests tests/tst_elementsselection.cpp)
    set_target_properties(DreamscapesElementsSelectionTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesElementsSelectionTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesElementsSelectionTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ElementsSelection COMMAND DreamscapesElementsSelectionTests)
    set_tests_properties(Dreamscapes.ElementsSelection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesElementTransformTests tests/tst_elementtransform.cpp)
    add_executable(DreamscapesAlignmentGeometryTests tests/tst_alignmentgeometry.cpp src/App/Views/Editor/AlignmentGeometry.cpp)
    target_include_directories(DreamscapesAlignmentGeometryTests PRIVATE src)
    add_test(NAME Dreamscapes.AlignmentGeometry COMMAND DreamscapesAlignmentGeometryTests)
    qt_add_executable(DreamscapesViewOptionsTests tests/tst_viewoptions.cpp)
    set_target_properties(DreamscapesViewOptionsTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesViewOptionsTests PRIVATE DreamscapesGeneration Qt6::Test Qt6::Widgets Qt6::QuickControls2)
    target_compile_definitions(DreamscapesViewOptionsTests PRIVATE
        DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/App"
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    lvrs_configure_qml_app(DreamscapesViewOptionsTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.ViewOptions COMMAND DreamscapesViewOptionsTests)
    add_test(NAME Dreamscapes.ViewOptionsHost COMMAND DreamscapesGuiTests editorViewOptionsDriveCanvas editorViewDisplayModesRender)
    set_tests_properties(Dreamscapes.ViewOptionsHost PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 90)
    set_tests_properties(Dreamscapes.ViewOptions PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    qt_add_executable(DreamscapesCanvasFeedbackTests tests/tst_canvasfeedback.cpp)
    set_target_properties(DreamscapesCanvasFeedbackTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesCanvasFeedbackTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesCanvasFeedbackTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.CanvasFeedback COMMAND DreamscapesCanvasFeedbackTests)
    set_tests_properties(Dreamscapes.CanvasFeedback PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
    set_target_properties(DreamscapesElementTransformTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesElementTransformTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesElementTransformTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ElementTransform COMMAND DreamscapesElementTransformTests)
    set_tests_properties(Dreamscapes.ElementTransform PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesEditorGraphicAdjustTests tests/tst_editorgraphicadjust.cpp)
    qt_add_executable(DreamscapesColorManagerTests tests/tst_colormanager.cpp)
    set_target_properties(DreamscapesColorManagerTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesColorManagerTests PRIVATE DreamscapesGeneration Qt6::Test Qt6::Widgets Qt6::QuickControls2)
    target_compile_definitions(DreamscapesColorManagerTests PRIVATE
        DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/App"
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    lvrs_configure_qml_app(DreamscapesColorManagerTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.ColorManager COMMAND DreamscapesColorManagerTests)
    set_tests_properties(Dreamscapes.ColorManager PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    target_link_libraries(DreamscapesEditorGraphicAdjustTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesEditorGraphicAdjustTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.EditorGraphicAdjust COMMAND DreamscapesEditorGraphicAdjustTests)
    set_tests_properties(Dreamscapes.EditorGraphicAdjust PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
    target_link_libraries(DreamscapesEditorToolsTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesEditorToolsTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.EditorTools COMMAND DreamscapesEditorToolsTests)
    qt_add_executable(DreamscapesClippingOverlayTests tests/tst_clippingoverlay.cpp)
    target_link_libraries(DreamscapesClippingOverlayTests PRIVATE DreamscapesGeneration Qt6::Test)
    add_test(NAME Dreamscapes.ClippingOverlay COMMAND DreamscapesClippingOverlayTests)
    qt_add_executable(DreamscapesFrameExporterTests tests/tst_frameexporter.cpp)
    target_link_libraries(DreamscapesFrameExporterTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesFrameExporterTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.FrameExporter COMMAND DreamscapesFrameExporterTests)
    qt_add_executable(DreamscapesAudioPlaybackTests tests/tst_audioplayback.cpp)
    target_link_libraries(DreamscapesAudioPlaybackTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesAudioPlaybackTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.AudioPlayback COMMAND DreamscapesAudioPlaybackTests)
    set_tests_properties(Dreamscapes.AudioPlayback PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 90)
    add_test(NAME Dreamscapes.AudioPlaybackHost COMMAND DreamscapesGuiTests editorTimelineAudioPreparationAndCancellation)
    set_tests_properties(Dreamscapes.AudioPlaybackHost PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 60)
    add_test(NAME Dreamscapes.FrameExporterHost COMMAND DreamscapesGuiTests editorFrameExportDialogRetainsTheChosenFrame)
    set_tests_properties(Dreamscapes.FrameExporterHost PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 60)
    set_tests_properties(Dreamscapes.FrameExporter PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)
    set_tests_properties(Dreamscapes.ClippingOverlay PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)
    set_tests_properties(Dreamscapes.EditorTools PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    target_link_libraries(DreamscapesEditorCanvasTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesEditorCanvasTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.EditorCanvas COMMAND DreamscapesEditorCanvasTests)
    qt_add_executable(DreamscapesCanvasPresentationTests tests/tst_canvaspresentation.cpp)
    target_link_libraries(DreamscapesCanvasPresentationTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesCanvasPresentationTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.CanvasPresentation COMMAND DreamscapesCanvasPresentationTests)
    set_tests_properties(Dreamscapes.CanvasPresentation PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)
    qt_add_executable(DreamscapesEditorFileInfoTests tests/tst_editorfileinfo.cpp)
    set_target_properties(DreamscapesEditorFileInfoTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesEditorFileInfoTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesEditorFileInfoTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_SOURCE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME Dreamscapes.EditorFileInfo COMMAND DreamscapesEditorFileInfoTests)
    set_tests_properties(Dreamscapes.EditorFileInfo PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
    qt_add_executable(DreamscapesEditorProjectTests tests/tst_editorproject.cpp)
    target_link_libraries(DreamscapesEditorProjectTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesEditorProjectTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.EditorProject COMMAND DreamscapesEditorProjectTests)
    qt_add_executable(DreamscapesDocumentOpenTests tests/tst_documentopen.cpp)
    target_link_libraries(DreamscapesDocumentOpenTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesDocumentOpenTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.DocumentOpen COMMAND DreamscapesDocumentOpenTests)
    set_tests_properties(Dreamscapes.DocumentOpen PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180)
    qt_add_executable(DreamscapesImageImportTests tests/tst_imageimport.cpp)
    target_link_libraries(DreamscapesImageImportTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesImageImportTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ImageImport COMMAND DreamscapesImageImportTests)
    qt_add_executable(DreamscapesImageImportRequestTests tests/tst_imageimportrequest.cpp)
    target_link_libraries(DreamscapesImageImportRequestTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesImageImportRequestTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ImageImportRequest COMMAND DreamscapesImageImportRequestTests)
    set_tests_properties(Dreamscapes.ImageImportRequest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120)
    # The full native suite includes durable SQLite fixture creation/edits. Its
    # isolated measured run took 222 s under storage contention; interaction
    # dispatch/destruction assertions retain their independent 250 ms bounds.
    set_tests_properties(Dreamscapes.EditorProject PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 300)
    qt_add_executable(DreamscapesPagePreviewTests tests/tst_pagepreviews.cpp)
    target_link_libraries(DreamscapesPagePreviewTests PRIVATE DreamscapesGeneration Qt6::Test)
    add_test(NAME Dreamscapes.PagePreviews COMMAND DreamscapesPagePreviewTests)
    set_tests_properties(Dreamscapes.PagePreviews PROPERTIES TIMEOUT 60)
    set_tests_properties(Dreamscapes.EditorCanvas PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 240)
    add_test(NAME Dreamscapes.EditorCanvasGui COMMAND DreamscapesGuiTests editorCanvasPanAndZoomStayInsideViewport editorNativeCanvasEditsAndPersists editorToolbarOperations canvasRoutesPreserveSelectionAndDraft)
    add_test(NAME Dreamscapes.EditorRenderQuality COMMAND DreamscapesGuiTests editorDocumentRenderQuality)
    set_tests_properties(Dreamscapes.EditorRenderQuality PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    set_tests_properties(Dreamscapes.EditorCanvasGui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    add_test(NAME Dreamscapes.HomeEditorRoutes COMMAND DreamscapesGuiTests
        homeDefersEditorUntilNeeded homeEditorRoutes generatedImageSelectionRoutes advancedCanvasMatchesResolutionAndStreamsItsBatch advancedAttachmentsPersistNativeSources)
    add_test(NAME Dreamscapes.ImageOpenGui COMMAND DreamscapesGuiTests storageImageSelectionUsesOriginals)
    set_tests_properties(Dreamscapes.ImageOpenGui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    qt_add_executable(DreamscapesImageOpenTests tests/tst_imageopen.cpp)
    target_link_libraries(DreamscapesImageOpenTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesImageOpenTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ImageOpen COMMAND DreamscapesImageOpenTests)
    set_tests_properties(Dreamscapes.ImageOpen PROPERTIES TIMEOUT 60)
    # Several cold QML/storage lifetimes share this group. Per-action watchdogs
    # retain their short deadlines; allow their aggregate to finish under load.
    set_tests_properties(Dreamscapes.HomeEditorRoutes PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 300)
    add_test(NAME Dreamscapes.MultiCanvasGui COMMAND DreamscapesGuiTests multiCanvasProjectSelection)
    add_test(NAME Dreamscapes.ViewStateBoundary COMMAND DreamscapesGuiTests
        viewTransitionsDoNotRescanStorage referenceViewsDoNotPrepareStorage)
    set_tests_properties(Dreamscapes.ViewStateBoundary PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 90)
    add_test(NAME Dreamscapes.InteractionRoutes COMMAND DreamscapesGuiTests applicationCommandRoutesReportTheirOutcome)
    set_tests_properties(Dreamscapes.InteractionRoutes PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    set_tests_properties(Dreamscapes.MultiCanvasGui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 180)
    qt_add_executable(DreamscapesNewCanvasTests tests/tst_newcanvas.cpp)
    target_link_libraries(DreamscapesNewCanvasTests PRIVATE DreamscapesGeneration Qt6::Test)
    add_test(NAME Dreamscapes.NewCanvas COMMAND DreamscapesNewCanvasTests)
    set_tests_properties(Dreamscapes.NewCanvas PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)
    add_test(NAME Dreamscapes.NewCanvasGui COMMAND DreamscapesGuiTests
        newCanvasChoosesPrintAndCustomSizes newCanvasSearchCancelAndKeyboard newCanvasPaddingAndStaticDetails
        canvasRoutesPreserveSelectionAndDraft mobileEditorToolbarSlidesAndSelects)
    set_tests_properties(Dreamscapes.NewCanvasGui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 300)
    target_link_libraries(DreamscapesHomeCanvasTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesHomeCanvasTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.HomeCanvas COMMAND DreamscapesHomeCanvasTests)
    set_tests_properties(Dreamscapes.HomeCanvas PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60)

    qt_add_executable(DreamscapesGenerationTests src/App/Generation/tst_Generation.cpp)
    qt_add_executable(DreamscapesAdvancedParametersTests tests/tst_advancedparameters.cpp)
    target_link_libraries(DreamscapesAdvancedParametersTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesAdvancedParametersTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.AdvancedParameters COMMAND DreamscapesAdvancedParametersTests)
    target_link_libraries(DreamscapesGenerationTests PRIVATE DreamscapesGeneration Qt6::Test iiFileProvider::Native)
    target_compile_definitions(DreamscapesGenerationTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_FAKE_GENERATOR="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Generation/fixtures/generate.py")
    add_test(NAME Dreamscapes.Generation COMMAND DreamscapesGenerationTests)
    qt_add_executable(DreamscapesGenerationProcessTests tests/tst_generationprocess.cpp)
    target_link_libraries(DreamscapesGenerationProcessTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesGenerationProcessTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_PROCESS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/generation_process.py"
        DREAMSCAPES_TEST_PYTHON="${Python3_EXECUTABLE}")
    add_test(NAME Dreamscapes.GenerationProcess COMMAND DreamscapesGenerationProcessTests)
    set_tests_properties(Dreamscapes.GenerationProcess PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    # Per-job watchdog assertions keep their own short deadlines. Allow the
    # complete fixture suite to finish while a real generation uses external I/O.
    # CPU-progress fixtures use a 1.5-second watchdog: do not schedule competing
    # UI/ffmpeg fixtures in this CTest pool while measuring their liveness.
    set_tests_properties(Dreamscapes.Generation PROPERTIES
        ENVIRONMENT "SOCIETY_DISABLE_SESSION_RESTORE=1" TIMEOUT 300 RUN_SERIAL TRUE)
    qt_add_executable(DreamscapesImageExportTests src/App/Views/Result/tst_ImageFileExporter.cpp)
    target_link_libraries(DreamscapesImageExportTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesImageExportTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ImageExport COMMAND DreamscapesImageExportTests)
    qt_add_executable(DreamscapesPhotoLibraryTests src/App/Views/Result/tst_PhotoLibraryExporter.cpp)
    target_link_libraries(DreamscapesPhotoLibraryTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesPhotoLibraryTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.PhotoLibrary COMMAND DreamscapesPhotoLibraryTests)
    add_test(NAME Dreamscapes.PhotoLibraryHost COMMAND DreamscapesGuiTests
        resultImageSaveToPhotos resultImageContextMenu resultImageSaveDialogPreservesSelectionAndCancel)
    add_test(NAME Dreamscapes.NativeResultExport COMMAND DreamscapesGuiTests nativeResultSaveCommandsUseTheDocument)
    set_tests_properties(Dreamscapes.NativeResultExport PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 120)
    set_tests_properties(Dreamscapes.PhotoLibraryHost PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 90)
    add_test(NAME Dreamscapes.VideoComposition COMMAND DreamscapesGuiTests videoCompositionEditsAreAtomic)
    set_tests_properties(Dreamscapes.VideoComposition PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1" TIMEOUT 90)
endif()

# Running desktop apps publish authenticated same-user MCP endpoints.
if(UNIX AND NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    target_sources(Dreamscapes PRIVATE src/App/Mcp/DreamscapesMcp.cpp src/App/Mcp/DreamscapesMcp.h)
    target_link_libraries(Dreamscapes PRIVATE iiMultimodalAgent::iiMultimodalAgent)
    set_source_files_properties("${IIMULTIMODALAGENT_QML_DIR}/UserQuestionsSheet.qml" PROPERTIES QT_RESOURCE_ALIAS "UserQuestionsSheet.qml")
    qt_add_resources(Dreamscapes AgentQuestionUi PREFIX "/iiLocalLLM"
        FILES "${IIMULTIMODALAGENT_QML_DIR}/UserQuestionsSheet.qml")
    target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_WITH_LOCAL_MCP=1)
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        # Existing UI tests must not advertise fixtures to the user's live agent.
        get_property(_existing_app_tests DIRECTORY PROPERTY TESTS)
        set_property(TEST ${_existing_app_tests} APPEND PROPERTY ENVIRONMENT "IILOCALLLM_DISABLE_APP_MCP=1")
        qt_add_executable(DreamscapesMcpTests tests/tst_mcp.cpp)
        target_link_libraries(DreamscapesMcpTests PRIVATE Qt6::Test iiMultimodalAgent::iiMultimodalAgent iiSocietyContainer::iiSocietyContainer)
        target_compile_definitions(DreamscapesMcpTests PRIVATE
            MCP_APP_EXECUTABLE="$<TARGET_FILE:Dreamscapes>"
            MCP_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_definitions(DreamscapesMcpTests PRIVATE
            MCP_GENERATOR_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/src/App/Generation/fixtures/generate.py")
        target_link_libraries(DreamscapesMcpTests PRIVATE Qt6::Gui)
        add_dependencies(DreamscapesMcpTests Dreamscapes)
        add_test(NAME Dreamscapes.Mcp COMMAND DreamscapesMcpTests)
        set_tests_properties(Dreamscapes.Mcp PROPERTIES TIMEOUT 90)
    endif()
endif()

if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
add_executable(DreamscapesFailureAgent src/App/Generation/failure_agent_main.cpp)
target_link_libraries(DreamscapesFailureAgent PRIVATE iiMultimodalAgent::iiMultimodalAgent iiMultimodalAgent::FailureDiagnostics)
target_link_libraries(DreamscapesFailureAgent PRIVATE iiFileProvider::CoreIO iiFileProvider::Native)
target_include_directories(DreamscapesFailureAgent PRIVATE src/App/Generation)
set_target_properties(DreamscapesFailureAgent PROPERTIES MACOSX_BUNDLE FALSE)
if(APPLE AND dreamscapes_diffusion_prefix)
    set_property(TARGET DreamscapesFailureAgent APPEND PROPERTY BUILD_RPATH "${dreamscapes_diffusion_prefix}/lib")
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    qt_add_executable(DreamscapesFailureAgentTests tests/tst_failureagent.cpp)
    target_link_libraries(DreamscapesFailureAgentTests PRIVATE DreamscapesGeneration iiMultimodalAgent::FailureDiagnostics Qt6::Test)
    target_compile_definitions(DreamscapesFailureAgentTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_FAILURE_AGENT="$<TARGET_FILE:DreamscapesFailureAgent>")
    add_dependencies(DreamscapesFailureAgentTests DreamscapesFailureAgent)
    add_test(NAME Dreamscapes.FailureAgent COMMAND DreamscapesFailureAgentTests)
    set_tests_properties(Dreamscapes.FailureAgent PROPERTIES TIMEOUT 90 ENVIRONMENT "SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
endif()
add_dependencies(Dreamscapes DreamscapesFailureAgent)
if(APPLE AND NOT IOS)
    add_custom_command(TARGET Dreamscapes POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:DreamscapesFailureAgent>" "$<TARGET_BUNDLE_DIR:Dreamscapes>/Contents/MacOS/"
        VERBATIM)
elseif(NOT IOS AND NOT ANDROID)
    add_custom_command(TARGET Dreamscapes POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:DreamscapesFailureAgent>" "$<TARGET_FILE_DIR:Dreamscapes>/"
        VERBATIM)
endif()
endif()

# Apply after Qt/LVRS finalizers, which may enable bundles for test targets.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/single-app/SingleApp.cmake")
product_plain_executables("${CMAKE_SOURCE_DIR}" Dreamscapes)
cmake_language(DEFER CALL product_single_app Dreamscapes "bin")

# FindOpenSSL imports UNKNOWN_LIBRARY targets, which the shared-runtime inventory
# cannot classify. Offer the configured dylibs explicitly; only dependencies
# actually used by the app/helpers are copied, even after a clean bundle rebuild.
if(APPLE AND NOT IOS)
    foreach(_openssl_component IN ITEMS Crypto SSL)
        string(TOUPPER "${_openssl_component}" _openssl_upper)
        set(_openssl_runtime "${OPENSSL_${_openssl_upper}_LIBRARY}")
        if(TARGET OpenSSL::${_openssl_component} AND EXISTS "${_openssl_runtime}" AND _openssl_runtime MATCHES "\\.dylib$")
            add_library(DreamscapesOpenSSL${_openssl_component}Runtime SHARED IMPORTED)
            set_target_properties(DreamscapesOpenSSL${_openssl_component}Runtime PROPERTIES IMPORTED_LOCATION "${_openssl_runtime}")
        endif()
    endforeach()
endif()
product_deploy_app(Dreamscapes "src/App" COMPACT QUICK_STYLE Basic EMBEDDED_RESOURCES_MODULES LVRS)

if(WIN32 AND BUILD_TESTING)
    get_property(_dreamscapes_tests DIRECTORY PROPERTY TESTS)
    foreach(_dreamscapes_test IN LISTS _dreamscapes_tests)
        get_property(_test_environment TEST ${_dreamscapes_test} PROPERTY ENVIRONMENT)
        list(TRANSFORM _test_environment REPLACE "QT_QPA_PLATFORM=offscreen" "QT_QPA_PLATFORM=windows")
        set_tests_properties(${_dreamscapes_test} PROPERTIES ENVIRONMENT "${_test_environment}")
    endforeach()
endif()

if(BUILD_TESTING)
    qt_add_executable(DreamscapesMaskPanelTests tests/tst_maskpanel.cpp)
    set_target_properties(DreamscapesMaskPanelTests PROPERTIES MACOSX_BUNDLE FALSE)
    target_link_libraries(DreamscapesMaskPanelTests PRIVATE DreamscapesGeneration Qt6::Test Qt6::Widgets Qt6::QuickControls2)
    target_compile_definitions(DreamscapesMaskPanelTests PRIVATE DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/App" DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    lvrs_configure_qml_app(DreamscapesMaskPanelTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.MaskPanel COMMAND DreamscapesMaskPanelTests)
endif()

if(APPLE AND BUILD_TESTING)
    # Shell-wide SDK overrides must not replace the packages selected by CMake.
    # Native tests resolve their linked SDKs through their generated build RPATHs.
    get_property(_dreamscapes_native_tests DIRECTORY PROPERTY TESTS)
    foreach(_dreamscapes_native_test IN LISTS _dreamscapes_native_tests)
        set_property(TEST ${_dreamscapes_native_test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
            "DYLD_LIBRARY_PATH=unset:" "DYLD_FRAMEWORK_PATH=unset:"
            "DYLD_FALLBACK_LIBRARY_PATH=unset:" "DYLD_VERSIONED_LIBRARY_PATH=unset:")
        if(TARGET OpenSSL::Crypto)
            # SDK dylibs may retain the provider's install name. Resolve only
            # CMake's selected Crypto package after discarding ambient overrides.
            set_property(TEST ${_dreamscapes_native_test} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                "DYLD_LIBRARY_PATH=set:$<TARGET_FILE_DIR:OpenSSL::Crypto>")
        endif()
    endforeach()
endif()
