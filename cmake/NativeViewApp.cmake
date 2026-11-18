# The migration inventory is the single source of the compiled LVUI view list.
option(DREAMSCAPES_ENABLE_LICENSE_VERIFICATION "Verify the Society license at application launch" OFF)
# The account SDK is built against OpenSSL 3. Avoid loading a second major
# version into the same process; toolchains may provide static or shared libs.
find_package(OpenSSL 3.0...<4.0 REQUIRED COMPONENTS Crypto SSL)
find_package(ZLIB REQUIRED)
find_path(DREAMSCAPES_IILD_INCLUDE_DIR Generation/ImageParameters.hpp
    HINTS "${iiLocalDiffusion_DIR}/../../.." "$ENV{HOME}/.local/SDK/iiLocalDiffusion"
    PATH_SUFFIXES include REQUIRED)
find_package(iiSocialIntegration CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocialIntegration")
find_package(iiDeviceMultimedia 0.2 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiDeviceMultimedia")
find_package(iiFileProviderObjects CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiFileProvider")
find_package(iiSocietyHelper CONFIG REQUIRED COMPONENTS Monitoring HINTS "$ENV{HOME}/.local/SDK/iiSocietyHelper")
find_package(iiLocalDiffusion 0.6.2 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLocalDiffusion")
find_package(iiVisualCodec CONFIG REQUIRED COMPONENTS Image HINTS "$ENV{HOME}/.local/SDK/iiVisualCodec")
find_package(iiColorManager 0.1.1 CONFIG REQUIRED COMPONENTS Values iiColorManager HINTS "$ENV{HOME}/.local/SDK/iiColorManager")
find_package(iiGraphicAdjust 0.6.2 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiGraphicAdjust")
find_package(iiMultimodalAgent 0.1 CONFIG REQUIRED COMPONENTS FailureDiagnostics HINTS "$ENV{HOME}/.local/SDK/iiMultimodalAgent")
add_library(DreamscapesNativeGenerationQueue STATIC
    src/App/Generation/NativeGenerationQueue.h src/App/Generation/NativeGenerationQueue.cpp
    src/App/Generation/NativeWorkerProtocol.h src/App/Generation/NativeWorkerProtocol.cpp
    src/App/Generation/NativeFailureDiagnostics.h src/App/Generation/NativeFailureDiagnostics.cpp)
target_compile_features(DreamscapesNativeGenerationQueue PUBLIC cxx_std_23)
target_include_directories(DreamscapesNativeGenerationQueue PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DreamscapesNativeGenerationQueue PUBLIC DreamscapesViewModelsNative iiSDKCore::Runtime
    PRIVATE iiMultimodalAgent::FailureDiagnostics iiJson::iiJson)
add_library(DreamscapesNativeGenerationReviewBridge STATIC
    src/App/Views/Result/NativeGenerationReviewBridge.h src/App/Views/Result/NativeGenerationReviewBridge.cpp)
target_compile_features(DreamscapesNativeGenerationReviewBridge PUBLIC cxx_std_23)
target_link_libraries(DreamscapesNativeGenerationReviewBridge PUBLIC DreamscapesNativeGenerationQueue DreamscapesHomeQueueNative)
if(NOT CMAKE_CROSSCOMPILING)
    set(_dreamscapes_native_account_hint "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiAccountManager/build/dreamscapes-aligned-stage")
    set(_dreamscapes_license_view_hint "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiLicenseManager/build/presentation-static-stage")
endif()
find_package(iiAccountManagerNative 0.2.10 CONFIG REQUIRED HINTS
    "$ENV{HOME}/.local/SDK/iiAccountManager" "${_dreamscapes_native_account_hint}")
if(NOT iiAccountManagerNative_HAS_SOCIETY_SESSION)
    message(FATAL_ERROR "Dreamscapes requires iiAccountManager Native SocietySession")
endif()
find_package(iiLicenseManagerPresentation 0.3 CONFIG REQUIRED HINTS
    "$ENV{HOME}/.local/SDK/iiLicenseManager" "${_dreamscapes_license_view_hint}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/migration/view-manifest.json" _view_manifest)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/migration/view-manifest.json")
string(JSON _view_count LENGTH "${_view_manifest}" views)
if(_view_count LESS 1)
    message(FATAL_ERROR "The native view inventory is empty")
endif()
math(EXPR _view_last "${_view_count} - 1")
set(_native_view_headers "")
set(_registry_includes "")
set(_registry_cases "")
set(_registry_descriptors "")
foreach(_index RANGE 0 ${_view_last})
    string(JSON _view_name GET "${_view_manifest}" views ${_index} name)
    string(JSON _view_path GET "${_view_manifest}" views ${_index} replacement)
    string(JSON _source_path GET "${_view_manifest}" views ${_index} source)
    if(_view_name STREQUAL "NativeLayerPanel")
        lvui_generate(NAME "${_view_name}" SOURCE "${_view_path}" FRAMEWORK iiGUI
            INCLUDES App/Views/Editor/NativeLayerPanelView.h OUTPUT_VARIABLE _view_header)
    elseif(_view_name STREQUAL "NativeFilePanel")
        lvui_generate(NAME "${_view_name}" SOURCE "${_view_path}" FRAMEWORK iiGUI
            INCLUDES App/Views/Editor/NativeFilePanelView.h OUTPUT_VARIABLE _view_header)
    elseif(_view_name STREQUAL "NativeElementsPanel")
        lvui_generate(NAME "${_view_name}" SOURCE "${_view_path}" FRAMEWORK iiGUI
            INCLUDES App/Views/Editor/NativeElementsPanelView.h OUTPUT_VARIABLE _view_header)
    elseif(_view_name STREQUAL "NativeTextPanel")
        lvui_generate(NAME "${_view_name}" SOURCE "${_view_path}" FRAMEWORK iiGUI
            INCLUDES App/Views/Editor/NativeTextPanelView.h OUTPUT_VARIABLE _view_header)
    elseif(_view_name STREQUAL "NativeMaskPanel")
        lvui_generate(NAME "${_view_name}" SOURCE "${_view_path}" FRAMEWORK iiGUI
            INCLUDES App/Views/Editor/NativeMaskPanelView.h OUTPUT_VARIABLE _view_header)
    else()
        lvui_generate(NAME "${_view_name}" SOURCE "${_view_path}" FRAMEWORK iiGUI
            INCLUDES App/Native/NativeMigratedView.h OUTPUT_VARIABLE _view_header)
    endif()
    list(APPEND _native_view_headers "${_view_header}")
    string(APPEND _registry_includes "#include \"${_view_name}.generated.h\"\n")
    if(_view_name STREQUAL "NativeLayerPanel")
        string(APPEND _registry_cases "    if (key == \"${_source_path}\") return dreamscapes::editor::buildLayerPanel(builder,parent,dreamscapes::editor::LayerPanelModel::create(dreamscapes::native::DocumentSession::create()));\n")
    elseif(_view_name STREQUAL "NativeFilePanel")
        string(APPEND _registry_cases "    if (key == \"${_source_path}\") { auto panel=dreamscapes::editor::FilePanelModel::create(dreamscapes::native::DocumentSession::create(),filePanelCatalog); panel->setVisible(true); return dreamscapes::editor::buildFilePanel(builder,parent,panel); }\n")
    elseif(_view_name STREQUAL "NativeElementsPanel")
        string(APPEND _registry_cases "    if (key == \"${_source_path}\") { auto panel=dreamscapes::editor::ElementsPanelModel::create(dreamscapes::native::DocumentSession::create()); panel->setVisible(true); return dreamscapes::editor::buildElementsPanel(builder,parent,panel); }\n")
    elseif(_view_name STREQUAL "NativeTextPanel")
        string(APPEND _registry_cases "    if (key == \"${_source_path}\") { auto defaults=dreamscapes::editor::parseTextDefaults(toolCatalog); if (!defaults) return std::unexpected(defaults.error()); auto panel=dreamscapes::editor::TextToolModel::create(dreamscapes::native::DocumentSession::create(),*defaults); panel->setVisible(true); return dreamscapes::editor::buildTextPanel(builder,parent,panel); }\n")
    elseif(_view_name STREQUAL "NativeMaskPanel")
        string(APPEND _registry_cases "    if (key == \"${_source_path}\") { auto panel=dreamscapes::editor::MaskPanelModel::create(dreamscapes::native::DocumentSession::create()); panel->setVisible(true); return dreamscapes::editor::buildMaskPanel(builder,parent,panel); }\n")
    else()
        string(APPEND _registry_cases "    if (key == \"${_source_path}\") return lvui_generated::${_view_name}::build(builder,parent,[](auto &) {},nullptr,factories);\n")
    endif()
    string(APPEND _registry_descriptors "        {\"${_source_path}\",\"${_view_path}\"},\n")
endforeach()
set(_native_registry "${CMAKE_CURRENT_BINARY_DIR}/NativeMigratedViewRegistry.cpp")
file(GENERATE OUTPUT "${_native_registry}" CONTENT "#include \"App/Native/NativeMigratedView.h\"\n#include \"NativeAssets.h\"\n${_registry_includes}\nnamespace dreamscapes::native {\nii::core::Result<void> buildMigratedView(ii::gui::WidgetBuilder &builder,ii::gui::Widget parent,std::string_view key,std::shared_ptr<MigratedViewModel> model) {\n    if (!model) return ii::core::fail(ii::core::ErrorCode::invalid_argument,\"Missing native view model\");\n    auto factories=lvui::native::BackendFactories{}.with<NativeMigratedViewBackend>([model]{return NativeMigratedViewBackend(model);});\n${_registry_cases}    return ii::core::fail(ii::core::ErrorCode::invalid_argument,\"Unknown native view\");\n}\nconst std::vector<MigratedViewDescriptor> &migratedViews() {\n    static const std::vector<MigratedViewDescriptor> views{\n${_registry_descriptors}    };\n    return views;\n}\n}\n")

lvui_generate(NAME NativeApplicationShell SOURCE src/App/Native/NativeApplicationShell.lvui FRAMEWORK iiGUI
    INCLUDES App/Native/NativeApplicationShellView.h OUTPUT_VARIABLE _native_shell)
lvui_generate(NAME NativeTitleBar SOURCE src/App/Native/NativeTitleBar.lvui FRAMEWORK iiGUI
    INCLUDES App/Native/NativeTitleBarView.h OUTPUT_VARIABLE _native_title_bar)
lvui_generate(NAME NativeGenerationWorkspace SOURCE src/App/Views/Generation/NativeGenerationWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Generation/NativeGenerationWorkspaceView.h OUTPUT_VARIABLE _native_generation_workspace)
lvui_generate(NAME NativeGenerationReviewWorkspace SOURCE src/App/Views/Result/NativeGenerationReviewWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Result/NativeGenerationReviewWorkspace.h OUTPUT_VARIABLE _native_generation_review_workspace)
lvui_generate(NAME NativeEditorWorkspace SOURCE src/App/Views/Editor/NativeEditorWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Editor/NativeEditorWorkspaceView.h OUTPUT_VARIABLE _native_editor_workspace)
lvui_generate(NAME NativeCanvasPanel SOURCE src/App/Views/Editor/NativeCanvasPanel.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Editor/NativeCanvasPanelView.h OUTPUT_VARIABLE _native_canvas_panel)
lvui_generate(NAME NativeBackgroundPanel SOURCE src/App/Views/Editor/NativeBackgroundPanel.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Editor/NativeBackgroundPanelView.h OUTPUT_VARIABLE _native_background_panel)
lvui_generate(NAME NativeAudioPanel SOURCE src/App/Views/Editor/NativeAudioPanel.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Editor/NativeAudioPanelView.h OUTPUT_VARIABLE _native_audio_panel)
lvui_generate(NAME NativeLightPanel SOURCE src/App/Views/Editor/NativeLightPanel.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Editor/NativeLightPanelView.h OUTPUT_VARIABLE _native_light_panel)
lvui_generate(NAME NativeBrushPanel SOURCE src/App/Views/Editor/NativeBrushPanel.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Editor/NativeBrushPanelView.h OUTPUT_VARIABLE _native_brush_panel)
lvui_generate(NAME NativeSocietyLicenseGate SOURCE "${iiLicenseManagerPresentation_VIEWS_DIR}/SocietyLicenseGate.lvui" FRAMEWORK iiGUI
    INCLUDES iiLicenseManager/SocietyLicenseGateModel.h OUTPUT_VARIABLE _native_society_license_gate)
lvui_generate(NAME NativePublishWorkspace SOURCE src/App/Views/Publishing/NativePublishWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Publishing/NativePublishWorkspaceView.h OUTPUT_VARIABLE _native_publish_workspace)
lvui_generate(NAME NativeStorageWorkspace SOURCE src/App/Views/Home/NativeStorageWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Home/NativeStorageWorkspaceView.h OUTPUT_VARIABLE _native_storage_workspace)
lvui_generate(NAME NativePreferencesWorkspace SOURCE src/App/Views/Preferences/NativePreferencesWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Preferences/NativePreferencesWorkspaceView.h OUTPUT_VARIABLE _native_preferences_workspace)
lvui_generate(NAME NativeCanvasWorkspace SOURCE src/App/Views/Home/NativeCanvasWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Home/NativeCanvasWorkspaceView.h OUTPUT_VARIABLE _native_canvas_workspace)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/native/preferences-categories.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/viewmodels/file.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/viewmodels/tools.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/viewmodels/background-defaults.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/native/canvas-presets.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Home/Data/canvas-presets.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg" DREAMSCAPES_QUEUE_READY_SVG)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/native/preferences-categories.txt" DREAMSCAPES_PREFERENCES_CATEGORIES)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/native/canvas-presets.json" DREAMSCAPES_CANVAS_CATALOG)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/viewmodels/file.json" DREAMSCAPES_FILE_PANEL_CATALOG)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/viewmodels/tools.json" DREAMSCAPES_TOOL_CATALOG)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/viewmodels/background-defaults.json" DREAMSCAPES_BACKGROUND_CATALOG)
configure_file(src/App/Native/NativeAssets.h.in "${CMAKE_CURRENT_BINARY_DIR}/NativeAssets.h" @ONLY)
add_library(DreamscapesNativeViewApp STATIC
    src/App/Native/NativeMigratedView.h src/App/Native/NativeMigratedView.cpp
    src/App/Generation/NativeFailureDetailsBridge.h src/App/Generation/NativeFailureDetailsBridge.cpp
    src/App/Generation/NativeFailurePresenter.h src/App/Generation/NativeFailurePresenter.cpp
    src/App/Native/NativeLaunchLicense.h src/App/Native/NativeLaunchLicense.cpp
    src/App/Native/NativeSocietyLicense.h src/App/Native/NativeSocietyLicense.cpp
    src/App/Native/NativeLicenseBridge.h src/App/Native/NativeLicenseBridge.cpp
    src/App/Native/NativeLicenseGateView.h src/App/Native/NativeLicenseGateView.cpp
    src/App/Native/NativeDocumentSession.h src/App/Native/NativeDocumentSession.cpp
    src/App/Native/NativeDocumentMasks.cpp
    src/App/Native/NativeDocumentVector.cpp
    src/App/Native/NativeDocumentBackground.cpp
    src/App/Native/NativeDocumentLight.cpp
    src/App/Native/NativeDocumentBrush.cpp
    src/App/Native/NativeDocumentAudio.cpp
    src/App/Native/NativeCanvasFiles.h src/App/Native/NativeCanvasFiles.cpp
    src/App/Native/NativeCanvasFileModel.h src/App/Native/NativeCanvasFileModel.cpp
    src/App/Native/NativeReviewFileSave.h src/App/Native/NativeReviewFileSave.cpp
    src/App/Native/NativeSocietyStorage.h src/App/Native/NativeSocietyStorage.cpp
    src/App/Native/NativeSocietyGeneration.h src/App/Native/NativeSocietyGeneration.cpp
    src/App/Native/NativeSocietyVideoGeneration.h src/App/Native/NativeSocietyVideoGeneration.cpp
    src/App/Native/NativeGenerationStaging.h src/App/Native/NativeGenerationStaging.cpp
    src/App/Native/NativeGenerationMetadata.h src/App/Native/NativeGenerationMetadata.cpp
    src/App/Native/NativeGenerationRuntime.h src/App/Native/NativeGenerationRuntime.cpp
    src/App/Native/NativeSocietyStorageModel.h src/App/Native/NativeSocietyStorageModel.cpp
    src/App/Native/NativeSocietyHistory.h src/App/Native/NativeSocietyHistory.cpp
    src/App/Native/NativeSocietyModels.h src/App/Native/NativeSocietyModels.cpp
    src/App/Native/NativeApplicationCommand.h src/App/Native/NativeApplicationCommand.cpp
    src/App/Views/Home/NativeHomeLocalDraft.h src/App/Views/Home/NativeHomeLocalDraft.cpp
    src/App/Views/Home/NativeStorageWorkspace.h src/App/Views/Home/NativeStorageWorkspace.cpp
    src/App/Views/Home/NativeStorageWorkspaceView.h src/App/Views/Home/NativeStorageWorkspaceView.cpp
    src/App/Views/Home/NativeCanvasWorkspace.h src/App/Views/Home/NativeCanvasWorkspace.cpp
    src/App/Views/Home/NativeCanvasWorkspaceView.h src/App/Views/Home/NativeCanvasWorkspaceView.cpp
    src/App/Views/Generation/NativeGenerationWorkspace.h src/App/Views/Generation/NativeGenerationWorkspace.cpp
    src/App/Views/Generation/NativeGenerationWorkspaceView.h src/App/Views/Generation/NativeGenerationWorkspaceView.cpp
    src/App/Views/Result/NativeGenerationReviewWorkspace.h src/App/Views/Result/NativeGenerationReviewWorkspace.cpp
    src/App/Views/Editor/NativeEditorWorkspace.h src/App/Views/Editor/NativeEditorWorkspace.cpp
    src/App/Views/Editor/NativeEditorFrame.h src/App/Views/Editor/NativeEditorFrame.cpp
    src/App/Views/Editor/NativeHistogram.h src/App/Views/Editor/NativeHistogram.cpp
    src/App/Views/Editor/NativeShapeTool.h src/App/Views/Editor/NativeShapeTool.cpp
    src/App/Views/Editor/NativeTextTool.h src/App/Views/Editor/NativeTextTool.cpp
    src/App/Views/Editor/NativeTextOutline.cpp
    src/App/Views/Editor/NativeTextPanelView.h src/App/Views/Editor/NativeTextPanelView.cpp
    src/App/Views/Editor/NativeVectorGeometry.h src/App/Views/Editor/NativeVectorGeometry.cpp
    src/App/Views/Editor/NativeSelectionTool.h src/App/Views/Editor/NativeSelectionTool.cpp
    src/App/Views/Editor/NativeSelectionSurface.h src/App/Views/Editor/NativeSelectionSurface.cpp
    src/App/Views/Editor/NativeMaskLasso.h src/App/Views/Editor/NativeMaskLasso.cpp
    src/App/Views/Editor/NativeMaskLassoSurface.h src/App/Views/Editor/NativeMaskLassoSurface.cpp
    src/App/Views/Editor/NativeShapeSurface.h src/App/Views/Editor/NativeShapeSurface.cpp
    src/App/Views/Editor/NativeLayerPanel.h src/App/Views/Editor/NativeLayerPanel.cpp
    src/App/Views/Editor/NativeElementsPanel.h src/App/Views/Editor/NativeElementsPanel.cpp
    src/App/Views/Editor/NativeMaskPanel.h src/App/Views/Editor/NativeMaskPanel.cpp
    src/App/Views/Editor/NativeBackgroundColor.h src/App/Views/Editor/NativeBackgroundColor.cpp
    src/App/Views/Editor/NativeLightAdjustment.h src/App/Views/Editor/NativeLightAdjustment.cpp
    src/App/Views/Editor/NativeBrushStroke.h src/App/Views/Editor/NativeBrushStroke.cpp
    src/App/Views/Editor/NativeBrushQueue.h src/App/Views/Editor/NativeBrushQueue.cpp
    src/App/Views/Editor/NativeBrushTool.h src/App/Views/Editor/NativeBrushTool.cpp
    src/App/Views/Editor/NativeBrushPanelView.h src/App/Views/Editor/NativeBrushPanelView.cpp
    src/App/Views/Editor/NativeBrushSurface.h src/App/Views/Editor/NativeBrushSurface.cpp
    src/App/Views/Editor/NativeLightPanel.h src/App/Views/Editor/NativeLightPanel.cpp
    src/App/Views/Editor/NativeLightPanelView.h src/App/Views/Editor/NativeLightPanelView.cpp
    src/App/Views/Editor/NativeAudioTrack.h src/App/Views/Editor/NativeAudioTrack.cpp
    src/App/Views/Editor/NativeAudioPanel.h src/App/Views/Editor/NativeAudioPanel.cpp
    src/App/Views/Editor/NativeAudioPanelView.h src/App/Views/Editor/NativeAudioPanelView.cpp
    src/App/Views/Editor/NativeBackgroundPanel.h src/App/Views/Editor/NativeBackgroundPanel.cpp
    src/App/Views/Editor/NativeBackgroundPanelView.h src/App/Views/Editor/NativeBackgroundPanelView.cpp
    src/App/Views/Editor/NativeMaskPanelView.h src/App/Views/Editor/NativeMaskPanelView.cpp
    src/App/Views/Editor/NativeElementsPanelView.h src/App/Views/Editor/NativeElementsPanelView.cpp
    src/App/Views/Editor/NativeFilePanel.h src/App/Views/Editor/NativeFilePanel.cpp
    src/App/Views/Editor/NativeFilePanelView.h src/App/Views/Editor/NativeFilePanelView.cpp
    src/App/Views/Editor/NativeCanvasPanel.h src/App/Views/Editor/NativeCanvasPanel.cpp
    src/App/Views/Editor/NativeCanvasPanelView.h src/App/Views/Editor/NativeCanvasPanelView.cpp
    src/App/Views/Editor/NativeLayerPanelView.h src/App/Views/Editor/NativeLayerPanelView.cpp
    src/App/Views/Editor/NativeTimelinePanel.h src/App/Views/Editor/NativeTimelinePanel.cpp
    src/App/Views/Editor/NativeTimelinePanelView.h src/App/Views/Editor/NativeTimelinePanelView.cpp
    src/App/Views/Editor/NativeTimelinePlayback.h src/App/Views/Editor/NativeTimelinePlayback.cpp
    src/App/Views/Editor/NativeTimelineAudio.h src/App/Views/Editor/NativeTimelineAudio.cpp
    src/App/Views/Editor/NativeEditorWorkspaceView.h src/App/Views/Editor/NativeEditorWorkspaceView.cpp
    src/App/Views/Publishing/NativePublishWorkspace.h src/App/Views/Publishing/NativePublishWorkspace.cpp
    src/App/Views/Publishing/NativePublicationService.h src/App/Views/Publishing/NativePublicationService.cpp
    src/App/Views/Publishing/NativePublishMedia.h src/App/Views/Publishing/NativePublishMedia.cpp
    src/App/Views/Publishing/NativePublishWorkspaceView.h src/App/Views/Publishing/NativePublishWorkspaceView.cpp
    src/App/Generation/ModelPreferences.h src/App/Generation/ModelPreferences.cpp
    src/App/Views/Preferences/NativeGenerationPreferences.h src/App/Views/Preferences/NativeGenerationPreferences.cpp
    src/App/Views/Preferences/NativePreferencesWorkspace.h src/App/Views/Preferences/NativePreferencesWorkspace.cpp
    src/App/Views/Preferences/NativePreferencesWorkspaceView.h src/App/Views/Preferences/NativePreferencesWorkspaceView.cpp
    src/App/Native/NativeApplicationShell.h src/App/Native/NativeApplicationShell.cpp
    src/App/Native/NativeApplicationShellView.h src/App/Native/NativeApplicationShellView.cpp
    src/App/Native/NativeTitleBar.h src/App/Native/NativeTitleBar.cpp
    src/App/Native/NativeTitleBarView.h src/App/Native/NativeTitleBarView.cpp
    "${_native_registry}" "${_native_shell}" "${_native_title_bar}" "${_native_generation_workspace}" "${_native_generation_review_workspace}" "${_native_editor_workspace}" "${_native_canvas_panel}" "${_native_background_panel}" "${_native_audio_panel}" "${_native_light_panel}" "${_native_brush_panel}" "${_native_society_license_gate}" "${_native_publish_workspace}" "${_native_storage_workspace}" "${_native_preferences_workspace}" "${_native_canvas_workspace}" ${_native_view_headers})
set_target_properties(DreamscapesNativeViewApp PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
set_property(SOURCE src/App/Native/NativeLaunchLicense.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    DREAMSCAPES_ENABLE_LICENSE_VERIFICATION=$<BOOL:${DREAMSCAPES_ENABLE_LICENSE_VERIFICATION}>)
target_compile_features(DreamscapesNativeViewApp PUBLIC cxx_std_23)
target_include_directories(DreamscapesNativeViewApp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${DREAMSCAPES_IILD_INCLUDE_DIR}")
target_link_libraries(DreamscapesNativeViewApp PUBLIC DreamscapesNativeApplicationWindow DreamscapesHomeWorkspaceNative DreamscapesReviewGalleryNative DreamscapesReviewQueueNative DreamscapesReviewInspectorView DreamscapesFailureDetailsNative DreamscapesNativeGenerationReviewBridge DreamscapesViewModelsNative iiSharedCanvas::Native iiSocialIntegration::iiSocialIntegration iiFileProvider::Objects
    LVUI::LVUI LVUI::Native iiGUI::iiGUI iiSDKCore::Runtime iiSocietyHelper::Monitoring iiSocietyContainer::FileTree
    iiAccountManager::Native iiLicenseManager::Presentation iiColorManager::iiColorManager iiGraphicAdjust::iiGraphicAdjust iiDeviceMultimedia::iiDeviceMultimedia OpenSSL::Crypto)
if(APPLE)
    target_link_libraries(DreamscapesNativeViewApp PRIVATE "-framework CoreText" "-framework CoreGraphics" "-framework CoreFoundation")
endif()
if(WIN32)
    target_link_libraries(DreamscapesNativeViewApp PRIVATE ole32)
endif()
target_link_libraries(DreamscapesNativeViewApp PRIVATE iiLocalDiffusion::ModelPackaging iiVisualCodec::Image ZLIB::ZLIB)

add_executable(Dreamscapes MACOSX_BUNDLE src/App/Native/NativeMain.cpp)
set_target_properties(Dreamscapes PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.iisacc.dreamscapes" MACOSX_BUNDLE_BUNDLE_NAME "Dreamscapes"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}" MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
if(APPLE AND NOT IOS)
    set(_native_app_icon "${CMAKE_CURRENT_SOURCE_DIR}/resources/Appicon/generated/macos/Dreamscapes.icns")
    target_sources(Dreamscapes PRIVATE "${_native_app_icon}")
    set_source_files_properties("${_native_app_icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(Dreamscapes PROPERTIES MACOSX_BUNDLE_ICON_FILE Dreamscapes.icns
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NativeInfo.plist.in")
    if(iiGUI_NATIVE_RESOURCES)
        target_sources(Dreamscapes PRIVATE ${iiGUI_NATIVE_RESOURCES})
        set_source_files_properties(${iiGUI_NATIVE_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
endif()
target_compile_features(Dreamscapes PRIVATE cxx_std_23)
target_include_directories(Dreamscapes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Dreamscapes PRIVATE DreamscapesNativeViewApp DreamscapesNativeApplicationRuntime)
set(DREAMSCAPES_DIFFUSION_EXECUTABLE "" CACHE FILEPATH "iiLocalDiffusion launcher for the native app")
set(_dreamscapes_worker "${DREAMSCAPES_DIFFUSION_EXECUTABLE}")
if(NOT _dreamscapes_worker)
    set(_dreamscapes_worker_names iild-generate)
    if(WIN32)
        list(PREPEND _dreamscapes_worker_names iild-generate.exe)
    endif()
    find_program(_dreamscapes_package_worker NAMES ${_dreamscapes_worker_names}
        PATHS "${iiLocalDiffusion_DIR}/../../../bin" NO_DEFAULT_PATH NO_CACHE)
    set(_dreamscapes_worker "${_dreamscapes_package_worker}")
    if(_dreamscapes_worker)
        target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_DIFFUSION_PACKAGE_WORKER=1)
    endif()
endif()
if(_dreamscapes_worker)
    cmake_path(SET _dreamscapes_worker NORMALIZE "${_dreamscapes_worker}")
    target_compile_definitions(Dreamscapes PRIVATE "DREAMSCAPES_DIFFUSION_EXECUTABLE=\"${_dreamscapes_worker}\"")
endif()
if(NOT IOS AND NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(iiSocietyHelperNative 0.12 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyHelper")
    add_library(DreamscapesNativeResidentCommand STATIC
        src/App/Native/NativeResidentCommand.h src/App/Native/NativeResidentCommand.cpp)
    target_compile_features(DreamscapesNativeResidentCommand PUBLIC cxx_std_23)
    target_include_directories(DreamscapesNativeResidentCommand PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(DreamscapesNativeResidentCommand PRIVATE iiSocietyHelperNative::ResidentClient iiJson::iiJson)
    target_link_libraries(Dreamscapes PRIVATE DreamscapesNativeResidentCommand)
    target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_RESIDENT_CLIENT=1)
    if(APPLE)
        target_link_options(Dreamscapes PRIVATE "LINKER:-rpath,@executable_path/../Frameworks")
        add_custom_command(TARGET Dreamscapes POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_DIR:Dreamscapes>/Contents/Frameworks"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:iiSocietyHelperNative::ResidentClient>"
                "$<TARGET_BUNDLE_DIR:Dreamscapes>/Contents/Frameworks/$<TARGET_SONAME_FILE_NAME:iiSocietyHelperNative::ResidentClient>"
            VERBATIM)
    elseif(WIN32)
        add_custom_command(TARGET Dreamscapes POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:iiSocietyHelperNative::ResidentClient>" "$<TARGET_FILE_DIR:Dreamscapes>"
            VERBATIM)
    elseif(UNIX)
        set_property(TARGET Dreamscapes APPEND PROPERTY BUILD_RPATH "$ORIGIN")
        set_property(TARGET Dreamscapes APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
        add_custom_command(TARGET Dreamscapes POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:iiSocietyHelperNative::ResidentClient>"
                "$<TARGET_FILE_DIR:Dreamscapes>/$<TARGET_SONAME_FILE_NAME:iiSocietyHelperNative::ResidentClient>"
            VERBATIM)
    endif()
endif()
if(TARGET DreamscapesNativeFileImages)
    target_link_libraries(Dreamscapes PRIVATE DreamscapesNativeFileImages)
    target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_REVIEW_IMAGES=1)
endif()
if(APPLE AND NOT IOS)
    find_program(DREAMSCAPES_CODESIGN_EXECUTABLE codesign REQUIRED)
    # The shared build directory can contain a prior bundle signature. Replace
    # it after linking so it matches the new native executable and resources.
    add_custom_command(TARGET Dreamscapes POST_BUILD
        COMMAND "${DREAMSCAPES_CODESIGN_EXECUTABLE}" --force --sign - "$<TARGET_BUNDLE_DIR:Dreamscapes>"
        VERBATIM)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    if(UNIX AND TARGET DreamscapesNativeResidentCommand)
        find_package(Threads REQUIRED)
        add_executable(DreamscapesNativeResidentCommandTests tests/tst_native_resident_command.cpp)
        target_link_libraries(DreamscapesNativeResidentCommandTests PRIVATE DreamscapesNativeResidentCommand iiJson::iiJson Threads::Threads)
        target_compile_definitions(DreamscapesNativeResidentCommandTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        add_test(NAME Dreamscapes.NativeResidentCommand COMMAND DreamscapesNativeResidentCommandTests)
    endif()
    add_executable(DreamscapesNativeWorkerProtocolTests tests/tst_native_worker_protocol.cpp)
    target_link_libraries(DreamscapesNativeWorkerProtocolTests PRIVATE DreamscapesNativeGenerationQueue iiJson::iiJson)
    add_test(NAME Dreamscapes.NativeWorkerProtocol COMMAND DreamscapesNativeWorkerProtocolTests)
    add_executable(DreamscapesNativeFailureDiagnosticsTests tests/tst_native_failure_diagnostics.cpp)
    target_link_libraries(DreamscapesNativeFailureDiagnosticsTests PRIVATE DreamscapesNativeGenerationQueue)
    add_test(NAME Dreamscapes.NativeFailureDiagnostics COMMAND DreamscapesNativeFailureDiagnosticsTests)
    add_executable(DreamscapesNativeFailureBridgeTests tests/tst_native_failure_bridge.cpp)
    target_link_libraries(DreamscapesNativeFailureBridgeTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeFailureBridge COMMAND DreamscapesNativeFailureBridgeTests)
    add_executable(DreamscapesNativeGenerationQueueTests tests/tst_native_generation_queue.cpp)
    target_link_libraries(DreamscapesNativeGenerationQueueTests PRIVATE DreamscapesNativeGenerationQueue)
    add_test(NAME Dreamscapes.NativeGenerationQueue COMMAND DreamscapesNativeGenerationQueueTests)
    add_executable(DreamscapesNativeGenerationReviewBridgeTests tests/tst_native_generation_review_bridge.cpp)
    target_link_libraries(DreamscapesNativeGenerationReviewBridgeTests PRIVATE DreamscapesNativeGenerationReviewBridge)
    add_test(NAME Dreamscapes.NativeGenerationReviewBridge COMMAND DreamscapesNativeGenerationReviewBridgeTests)
    add_executable(DreamscapesNativeGenerationShellTests tests/tst_native_generation_shell.cpp)
    target_include_directories(DreamscapesNativeGenerationShellTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(DreamscapesNativeGenerationShellTests PRIVATE DreamscapesNativeViewApp DreamscapesNativeGenerationReviewBridge)
    add_test(NAME Dreamscapes.NativeGenerationShell COMMAND DreamscapesNativeGenerationShellTests)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Dreamscapes.NativeViewConversion COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_view_conversion.py")
    add_test(NAME Dreamscapes.NativeViewAppBinary COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_app_binary.py" "$<TARGET_FILE:Dreamscapes>")
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeBrushWidgetTests tests/tst_native_brush_widget.cpp)
        target_include_directories(DreamscapesNativeBrushWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(DreamscapesNativeBrushWidgetTests PRIVATE DreamscapesNativeViewApp)
        add_test(NAME Dreamscapes.NativeBrushWidget COMMAND DreamscapesNativeBrushWidgetTests)
        add_executable(DreamscapesNativeGenerationReviewWidgetTests tests/tst_native_generation_review_widget.cpp)
        target_include_directories(DreamscapesNativeGenerationReviewWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeGenerationReviewWidgetTests PRIVATE DreamscapesNativeViewApp)
        add_test(NAME Dreamscapes.NativeGenerationReviewWidget COMMAND DreamscapesNativeGenerationReviewWidgetTests)
        add_executable(DreamscapesNativeViewAppTests tests/tst_native_view_app.cpp)
        target_include_directories(DreamscapesNativeViewAppTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
        target_compile_definitions(DreamscapesNativeViewAppTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeViewAppTests PRIVATE DreamscapesNativeViewApp iiVisualCodec::Image)
        add_test(NAME Dreamscapes.NativeViewApp COMMAND DreamscapesNativeViewAppTests)
    endif()
    add_executable(DreamscapesNativeHomeLocalDraftTests tests/tst_native_home_local_draft.cpp)
    target_link_libraries(DreamscapesNativeHomeLocalDraftTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeHomeLocalDraft COMMAND DreamscapesNativeHomeLocalDraftTests)
    add_executable(DreamscapesNativeLaunchLicenseTests tests/tst_native_launch_license.cpp)
    target_link_libraries(DreamscapesNativeLaunchLicenseTests PRIVATE DreamscapesNativeViewApp)
    target_compile_definitions(DreamscapesNativeLaunchLicenseTests PRIVATE
        DREAMSCAPES_TEST_EXPECT_LICENSE_VERIFICATION=$<BOOL:${DREAMSCAPES_ENABLE_LICENSE_VERIFICATION}>)
    add_test(NAME Dreamscapes.NativeLaunchLicense COMMAND DreamscapesNativeLaunchLicenseTests)
    add_executable(DreamscapesNativeGenerationWorkspaceTests tests/tst_native_generation_workspace.cpp)
    target_link_libraries(DreamscapesNativeGenerationWorkspaceTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeGenerationWorkspace COMMAND DreamscapesNativeGenerationWorkspaceTests)
    add_executable(DreamscapesNativeDocumentSessionTests tests/tst_native_document_session.cpp)
    target_link_libraries(DreamscapesNativeDocumentSessionTests PRIVATE DreamscapesNativeViewApp iiSharedCanvas::Native)
    add_test(NAME Dreamscapes.NativeDocumentSession COMMAND DreamscapesNativeDocumentSessionTests)
    add_executable(DreamscapesNativeCanvasFilesTests tests/tst_native_canvas_files.cpp)
    target_compile_definitions(DreamscapesNativeCanvasFilesTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeCanvasFilesTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeCanvasFiles COMMAND DreamscapesNativeCanvasFilesTests)
    add_executable(DreamscapesNativeCanvasFileModelTests tests/tst_native_canvas_file_model.cpp)
    target_include_directories(DreamscapesNativeCanvasFileModelTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(DreamscapesNativeCanvasFileModelTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeCanvasFileModelTests PRIVATE DreamscapesNativeViewApp iiVisualCodec::Image)
    add_test(NAME Dreamscapes.NativeCanvasFileModel COMMAND DreamscapesNativeCanvasFileModelTests)
    add_executable(DreamscapesNativeReviewFileSaveTests tests/tst_native_review_file_save.cpp)
    target_compile_definitions(DreamscapesNativeReviewFileSaveTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeReviewFileSaveTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeReviewFileSave COMMAND DreamscapesNativeReviewFileSaveTests)
    add_executable(DreamscapesNativeEditorFrameTests tests/tst_native_editor_frame.cpp)
    target_link_libraries(DreamscapesNativeEditorFrameTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeEditorFrame COMMAND DreamscapesNativeEditorFrameTests)
    add_executable(DreamscapesNativeHistogramTests tests/tst_native_histogram.cpp)
    target_link_libraries(DreamscapesNativeHistogramTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeHistogram COMMAND DreamscapesNativeHistogramTests)
    add_executable(DreamscapesNativeShapeToolTests tests/tst_native_shape_tool.cpp)
    target_include_directories(DreamscapesNativeShapeToolTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(DreamscapesNativeShapeToolTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeShapeToolTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeShapeTool COMMAND DreamscapesNativeShapeToolTests)
    add_executable(DreamscapesNativeTextToolTests tests/tst_native_text_tool.cpp)
    target_include_directories(DreamscapesNativeTextToolTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(DreamscapesNativeTextToolTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeTextToolTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeTextTool COMMAND DreamscapesNativeTextToolTests)
    add_executable(DreamscapesNativeSelectionToolTests tests/tst_native_selection_tool.cpp)
    target_link_libraries(DreamscapesNativeSelectionToolTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeSelectionTool COMMAND DreamscapesNativeSelectionToolTests)
    add_executable(DreamscapesNativeMaskLassoTests tests/tst_native_mask_lasso.cpp)
    target_compile_definitions(DreamscapesNativeMaskLassoTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeMaskLassoTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeMaskLasso COMMAND DreamscapesNativeMaskLassoTests)
    add_executable(DreamscapesNativeLayerPanelTests tests/tst_native_layer_panel.cpp)
    target_link_libraries(DreamscapesNativeLayerPanelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeLayerPanel COMMAND DreamscapesNativeLayerPanelTests)
    add_executable(DreamscapesNativeElementsPanelTests tests/tst_native_elements_panel.cpp)
    target_compile_definitions(DreamscapesNativeElementsPanelTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeElementsPanelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeElementsPanel COMMAND DreamscapesNativeElementsPanelTests)
    add_executable(DreamscapesNativeMaskPanelTests tests/tst_native_mask_panel.cpp)
    target_compile_definitions(DreamscapesNativeMaskPanelTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeMaskPanelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeMaskPanel COMMAND DreamscapesNativeMaskPanelTests)
    add_executable(DreamscapesNativeBackgroundColorTests tests/tst_native_background_color.cpp)
    target_link_libraries(DreamscapesNativeBackgroundColorTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeBackgroundColor COMMAND DreamscapesNativeBackgroundColorTests)
    add_executable(DreamscapesNativeLightAdjustmentTests tests/tst_native_light_adjustment.cpp)
    target_link_libraries(DreamscapesNativeLightAdjustmentTests PRIVATE DreamscapesNativeViewApp iiGraphicAdjust::iiGraphicAdjust)
    add_test(NAME Dreamscapes.NativeLightAdjustment COMMAND DreamscapesNativeLightAdjustmentTests)
    add_executable(DreamscapesNativeBrushStrokeTests tests/tst_native_brush_stroke.cpp)
    target_link_libraries(DreamscapesNativeBrushStrokeTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeBrushStroke COMMAND DreamscapesNativeBrushStrokeTests)
    add_executable(DreamscapesNativeBrushQueueTests tests/tst_native_brush_queue.cpp)
    target_link_libraries(DreamscapesNativeBrushQueueTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeBrushQueue COMMAND DreamscapesNativeBrushQueueTests)
    add_executable(DreamscapesNativeBrushToolTests tests/tst_native_brush_tool.cpp)
    target_link_libraries(DreamscapesNativeBrushToolTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeBrushTool COMMAND DreamscapesNativeBrushToolTests)
    add_executable(DreamscapesNativeLightPanelTests tests/tst_native_light_panel.cpp)
    target_link_libraries(DreamscapesNativeLightPanelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeLightPanel COMMAND DreamscapesNativeLightPanelTests)
    add_executable(DreamscapesNativeBackgroundPanelTests tests/tst_native_background_panel.cpp)
    target_compile_definitions(DreamscapesNativeBackgroundPanelTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeBackgroundPanelTests PRIVATE DreamscapesNativeViewApp iiVisualCodec::Image)
    add_test(NAME Dreamscapes.NativeBackgroundPanel COMMAND DreamscapesNativeBackgroundPanelTests)
    add_executable(DreamscapesNativeAudioTrackTests tests/tst_native_audio_track.cpp)
    target_link_libraries(DreamscapesNativeAudioTrackTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeAudioTrack COMMAND DreamscapesNativeAudioTrackTests)
    add_executable(DreamscapesNativeAudioPanelTests tests/tst_native_audio_panel.cpp)
    target_compile_definitions(DreamscapesNativeAudioPanelTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeAudioPanelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeAudioPanel COMMAND DreamscapesNativeAudioPanelTests)
    add_executable(DreamscapesNativeAudioShellTests tests/tst_native_audio_shell.cpp)
    target_include_directories(DreamscapesNativeAudioShellTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(DreamscapesNativeAudioShellTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeAudioShellTests PRIVATE DreamscapesNativeViewApp iiVisualCodec::Image)
    add_test(NAME Dreamscapes.NativeAudioShell COMMAND DreamscapesNativeAudioShellTests)
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeLightPanelWidgetTests tests/tst_native_light_panel_widget.cpp)
        target_include_directories(DreamscapesNativeLightPanelWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(DreamscapesNativeLightPanelWidgetTests PRIVATE DreamscapesNativeViewApp)
        add_test(NAME Dreamscapes.NativeLightPanelWidget COMMAND DreamscapesNativeLightPanelWidgetTests)
        add_executable(DreamscapesNativeBackgroundPanelWidgetTests tests/tst_native_background_panel_widget.cpp)
        target_include_directories(DreamscapesNativeBackgroundPanelWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeBackgroundPanelWidgetTests PRIVATE DreamscapesNativeViewApp)
        add_test(NAME Dreamscapes.NativeBackgroundPanelWidget COMMAND DreamscapesNativeBackgroundPanelWidgetTests)
        add_executable(DreamscapesNativeAudioPanelWidgetTests tests/tst_native_audio_panel_widget.cpp)
        target_include_directories(DreamscapesNativeAudioPanelWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeAudioPanelWidgetTests PRIVATE DreamscapesNativeViewApp)
        add_test(NAME Dreamscapes.NativeAudioPanelWidget COMMAND DreamscapesNativeAudioPanelWidgetTests)
    endif()
    add_executable(DreamscapesNativeCanvasPanelTests tests/tst_native_canvas_panel.cpp)
    target_link_libraries(DreamscapesNativeCanvasPanelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeCanvasPanel COMMAND DreamscapesNativeCanvasPanelTests)
    add_executable(DreamscapesNativeFilePanelTests tests/tst_native_file_panel.cpp)
    target_compile_definitions(DreamscapesNativeFilePanelTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeFilePanelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeFilePanel COMMAND DreamscapesNativeFilePanelTests)
    add_executable(DreamscapesNativeTimelinePlaybackTests tests/tst_native_timeline_playback.cpp)
    target_link_libraries(DreamscapesNativeTimelinePlaybackTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeTimelinePlayback COMMAND DreamscapesNativeTimelinePlaybackTests)
    add_executable(DreamscapesNativeTimelinePanelTests tests/tst_native_timeline_panel.cpp)
    target_link_libraries(DreamscapesNativeTimelinePanelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeTimelinePanel COMMAND DreamscapesNativeTimelinePanelTests)
    add_executable(DreamscapesNativePublishWorkspaceTests tests/tst_native_publish_workspace.cpp)
    target_link_libraries(DreamscapesNativePublishWorkspaceTests PRIVATE DreamscapesNativeViewApp iiSocialIntegration::iiSocialIntegration)
    add_test(NAME Dreamscapes.NativePublishWorkspace COMMAND DreamscapesNativePublishWorkspaceTests)
    add_executable(DreamscapesNativePublicationServiceTests tests/tst_native_publication_service.cpp)
    target_link_libraries(DreamscapesNativePublicationServiceTests PRIVATE DreamscapesNativeViewApp iiSocialIntegration::iiSocialIntegration)
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        target_include_directories(DreamscapesNativePublicationServiceTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_compile_definitions(DreamscapesNativePublicationServiceTests PRIVATE DREAMSCAPES_PUBLICATION_WIDGET_TEST=1)
    endif()
    add_test(NAME Dreamscapes.NativePublicationService COMMAND DreamscapesNativePublicationServiceTests)
    add_executable(DreamscapesNativeTimelineAudioTests tests/tst_native_timeline_audio.cpp)
    target_link_libraries(DreamscapesNativeTimelineAudioTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeTimelineAudio COMMAND DreamscapesNativeTimelineAudioTests)
    add_executable(DreamscapesNativePublishMediaTests tests/tst_native_publish_media.cpp)
    target_compile_definitions(DreamscapesNativePublishMediaTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativePublishMediaTests PRIVATE DreamscapesNativeViewApp iiVisualCodec::Image)
    add_test(NAME Dreamscapes.NativePublishMedia COMMAND DreamscapesNativePublishMediaTests)
    add_executable(DreamscapesNativePublishMediaModelTests tests/tst_native_publish_media_model.cpp)
    target_compile_definitions(DreamscapesNativePublishMediaModelTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativePublishMediaModelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativePublishMediaModel COMMAND DreamscapesNativePublishMediaModelTests)
    add_executable(DreamscapesNativeStorageWorkspaceTests tests/tst_native_storage_workspace.cpp)
    target_link_libraries(DreamscapesNativeStorageWorkspaceTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeStorageWorkspace COMMAND DreamscapesNativeStorageWorkspaceTests)
    add_executable(DreamscapesNativeSocietyStorageTests tests/tst_native_society_storage.cpp)
    target_compile_definitions(DreamscapesNativeSocietyStorageTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeSocietyStorageTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeSocietyStorage COMMAND DreamscapesNativeSocietyStorageTests)
    add_executable(DreamscapesNativeSocietyHistoryTests tests/tst_native_society_history.cpp)
    target_compile_definitions(DreamscapesNativeSocietyHistoryTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeSocietyHistoryTests PRIVATE DreamscapesNativeViewApp)
    if(TARGET DreamscapesNativeFileImages)
        target_link_libraries(DreamscapesNativeSocietyHistoryTests PRIVATE DreamscapesNativeFileImages)
        target_compile_definitions(DreamscapesNativeSocietyHistoryTests PRIVATE DREAMSCAPES_HISTORY_IMAGE_TEST=1)
    endif()
    add_test(NAME Dreamscapes.NativeSocietyHistory COMMAND DreamscapesNativeSocietyHistoryTests)
    # Keep catalogue tests independent of the LVUI window and all unrelated
    # view libraries; the app and this test compile the same storage sources.
    add_executable(DreamscapesNativeSocietyModelsTests tests/tst_native_society_models.cpp
        src/App/Native/NativeSocietyModels.cpp src/App/Native/NativeSocietyStorage.cpp)
    target_compile_definitions(DreamscapesNativeSocietyModelsTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_include_directories(DreamscapesNativeSocietyModelsTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src" "${DREAMSCAPES_IILD_INCLUDE_DIR}")
    target_link_libraries(DreamscapesNativeSocietyModelsTests PRIVATE iiLocalDiffusion::ModelPackaging
        iiFileProvider::Objects iiSocietyContainer::FileTree iiSocietyHelper::Monitoring iiSDKCore::Runtime iiJson::iiJson)
    add_test(NAME Dreamscapes.NativeSocietyModels COMMAND DreamscapesNativeSocietyModelsTests)
    add_executable(DreamscapesNativeSocietyGenerationTests tests/tst_native_society_generation.cpp)
    target_compile_definitions(DreamscapesNativeSocietyGenerationTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeSocietyGenerationTests PRIVATE DreamscapesNativeViewApp iiVisualCodec::Image OpenSSL::Crypto)
    add_test(NAME Dreamscapes.NativeSocietyGeneration COMMAND DreamscapesNativeSocietyGenerationTests)
    add_executable(DreamscapesNativeSocietyVideoGenerationTests tests/tst_native_society_video_generation.cpp)
    target_compile_definitions(DreamscapesNativeSocietyVideoGenerationTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeSocietyVideoGenerationTests PRIVATE DreamscapesNativeViewApp OpenSSL::Crypto)
    add_test(NAME Dreamscapes.NativeSocietyVideoGeneration COMMAND DreamscapesNativeSocietyVideoGenerationTests)
    add_executable(DreamscapesNativeGenerationRuntimeTests tests/tst_native_generation_runtime.cpp)
    target_compile_definitions(DreamscapesNativeGenerationRuntimeTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeGenerationRuntimeTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeGenerationRuntime COMMAND DreamscapesNativeGenerationRuntimeTests)
    add_executable(DreamscapesNativeSocietyStorageModelTests tests/tst_native_society_storage_model.cpp)
    target_include_directories(DreamscapesNativeSocietyStorageModelTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(DreamscapesNativeSocietyStorageModelTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeSocietyStorageModelTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeSocietyStorageModel COMMAND DreamscapesNativeSocietyStorageModelTests)
    add_executable(DreamscapesNativePreferencesWorkspaceTests tests/tst_native_preferences_workspace.cpp)
    target_link_libraries(DreamscapesNativePreferencesWorkspaceTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativePreferencesWorkspace COMMAND DreamscapesNativePreferencesWorkspaceTests)
    add_executable(DreamscapesNativeGenerationPreferencesTests tests/tst_native_generation_preferences.cpp)
    target_include_directories(DreamscapesNativeGenerationPreferencesTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(DreamscapesNativeGenerationPreferencesTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeGenerationPreferencesTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeGenerationPreferences COMMAND DreamscapesNativeGenerationPreferencesTests)
    add_executable(DreamscapesNativeCanvasWorkspaceTests tests/tst_native_canvas_workspace.cpp)
    target_include_directories(DreamscapesNativeCanvasWorkspaceTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(DreamscapesNativeCanvasWorkspaceTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeCanvasWorkspace COMMAND DreamscapesNativeCanvasWorkspaceTests)
endif()
