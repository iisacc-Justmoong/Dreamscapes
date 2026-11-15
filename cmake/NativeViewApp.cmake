# The migration inventory is the single source of the compiled LVUI view list.
# The account SDK is built against OpenSSL 3. Avoid loading a second major
# version into the same process; toolchains may provide static or shared libs.
find_package(OpenSSL 3.0...<4.0 REQUIRED COMPONENTS Crypto SSL)
find_path(DREAMSCAPES_IILD_INCLUDE_DIR Generation/ImageParameters.hpp
    HINTS "${iiLocalDiffusion_DIR}/../../.." "$ENV{HOME}/.local/SDK/iiLocalDiffusion"
    PATH_SUFFIXES include REQUIRED)
find_package(iiSocialIntegration CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocialIntegration")
find_package(iiFileProviderObjects CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiFileProvider")
find_package(iiSocietyHelper CONFIG REQUIRED COMPONENTS Monitoring HINTS "$ENV{HOME}/.local/SDK/iiSocietyHelper")
find_package(iiLocalDiffusion 0.6.2 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLocalDiffusion")
find_package(iiVisualCodec CONFIG REQUIRED COMPONENTS Image HINTS "$ENV{HOME}/.local/SDK/iiVisualCodec")
find_package(iiColorManager 0.1.1 CONFIG REQUIRED COMPONENTS Values HINTS "$ENV{HOME}/.local/SDK/iiColorManager")
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
lvui_generate(NAME NativeGenerationWorkspace SOURCE src/App/Views/Generation/NativeGenerationWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Generation/NativeGenerationWorkspaceView.h OUTPUT_VARIABLE _native_generation_workspace)
lvui_generate(NAME NativeEditorWorkspace SOURCE src/App/Views/Editor/NativeEditorWorkspace.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Editor/NativeEditorWorkspaceView.h OUTPUT_VARIABLE _native_editor_workspace)
lvui_generate(NAME NativeCanvasPanel SOURCE src/App/Views/Editor/NativeCanvasPanel.lvui FRAMEWORK iiGUI
    INCLUDES App/Views/Editor/NativeCanvasPanelView.h OUTPUT_VARIABLE _native_canvas_panel)
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
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/native/canvas-presets.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App/Views/Home/Data/canvas-presets.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg" DREAMSCAPES_QUEUE_READY_SVG)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/native/preferences-categories.txt" DREAMSCAPES_PREFERENCES_CATEGORIES)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/native/canvas-presets.json" DREAMSCAPES_CANVAS_CATALOG)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/viewmodels/file.json" DREAMSCAPES_FILE_PANEL_CATALOG)
configure_file(src/App/Native/NativeAssets.h.in "${CMAKE_CURRENT_BINARY_DIR}/NativeAssets.h" @ONLY)
add_library(DreamscapesNativeViewApp STATIC
    src/App/Native/NativeMigratedView.h src/App/Native/NativeMigratedView.cpp
    src/App/Native/NativeLaunchLicense.h src/App/Native/NativeLaunchLicense.cpp
    src/App/Native/NativeSocietyLicense.h src/App/Native/NativeSocietyLicense.cpp
    src/App/Native/NativeLicenseBridge.h src/App/Native/NativeLicenseBridge.cpp
    src/App/Native/NativeLicenseGateView.h src/App/Native/NativeLicenseGateView.cpp
    src/App/Native/NativeDocumentSession.h src/App/Native/NativeDocumentSession.cpp
    src/App/Native/NativeDocumentMasks.cpp
    src/App/Native/NativeDocumentVector.cpp
    src/App/Native/NativeCanvasFiles.h src/App/Native/NativeCanvasFiles.cpp
    src/App/Native/NativeCanvasFileModel.h src/App/Native/NativeCanvasFileModel.cpp
    src/App/Native/NativeSocietyStorage.h src/App/Native/NativeSocietyStorage.cpp
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
    src/App/Views/Editor/NativeEditorWorkspace.h src/App/Views/Editor/NativeEditorWorkspace.cpp
    src/App/Views/Editor/NativeEditorFrame.h src/App/Views/Editor/NativeEditorFrame.cpp
    src/App/Views/Editor/NativeLayerPanel.h src/App/Views/Editor/NativeLayerPanel.cpp
    src/App/Views/Editor/NativeElementsPanel.h src/App/Views/Editor/NativeElementsPanel.cpp
    src/App/Views/Editor/NativeMaskPanel.h src/App/Views/Editor/NativeMaskPanel.cpp
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
    src/App/Views/Editor/NativeEditorWorkspaceView.h src/App/Views/Editor/NativeEditorWorkspaceView.cpp
    src/App/Views/Publishing/NativePublishWorkspace.h src/App/Views/Publishing/NativePublishWorkspace.cpp
    src/App/Views/Publishing/NativePublishWorkspaceView.h src/App/Views/Publishing/NativePublishWorkspaceView.cpp
    src/App/Generation/ModelPreferences.h src/App/Generation/ModelPreferences.cpp
    src/App/Views/Preferences/NativeGenerationPreferences.h src/App/Views/Preferences/NativeGenerationPreferences.cpp
    src/App/Views/Preferences/NativePreferencesWorkspace.h src/App/Views/Preferences/NativePreferencesWorkspace.cpp
    src/App/Views/Preferences/NativePreferencesWorkspaceView.h src/App/Views/Preferences/NativePreferencesWorkspaceView.cpp
    src/App/Native/NativeApplicationShell.h src/App/Native/NativeApplicationShell.cpp
    src/App/Native/NativeApplicationShellView.h src/App/Native/NativeApplicationShellView.cpp
    "${_native_registry}" "${_native_shell}" "${_native_generation_workspace}" "${_native_editor_workspace}" "${_native_canvas_panel}" "${_native_society_license_gate}" "${_native_publish_workspace}" "${_native_storage_workspace}" "${_native_preferences_workspace}" "${_native_canvas_workspace}" ${_native_view_headers})
set_target_properties(DreamscapesNativeViewApp PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesNativeViewApp PUBLIC cxx_std_23)
target_include_directories(DreamscapesNativeViewApp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${DREAMSCAPES_IILD_INCLUDE_DIR}")
target_link_libraries(DreamscapesNativeViewApp PUBLIC DreamscapesNativeApplicationWindow DreamscapesHomeWorkspaceNative DreamscapesReviewGalleryNative DreamscapesViewModelsNative iiSharedCanvas::Native iiSocialIntegration::iiSocialIntegration iiFileProvider::Objects
    LVUI::Native iiGUI::iiGUI iiSDKCore::Runtime iiSocietyHelper::Monitoring iiSocietyContainer::FileTree
    iiAccountManager::Native iiLicenseManager::Presentation OpenSSL::Crypto)
target_link_libraries(DreamscapesNativeViewApp PRIVATE iiLocalDiffusion::ModelPackaging iiVisualCodec::Image iiColorManager::Values)

add_executable(Dreamscapes MACOSX_BUNDLE src/App/Native/NativeMain.cpp)
set_target_properties(Dreamscapes PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.iisacc.dreamscapes" MACOSX_BUNDLE_BUNDLE_NAME "Dreamscapes")
target_compile_features(Dreamscapes PRIVATE cxx_std_23)
target_include_directories(Dreamscapes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Dreamscapes PRIVATE DreamscapesNativeViewApp)
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
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Dreamscapes.NativeViewConversion COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_view_conversion.py")
    add_test(NAME Dreamscapes.NativeViewAppBinary COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_app_binary.py" "$<TARGET_FILE:Dreamscapes>")
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
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
    add_executable(DreamscapesNativeSocietyModelsTests tests/tst_native_society_models.cpp)
    target_compile_definitions(DreamscapesNativeSocietyModelsTests PRIVATE DREAMSCAPES_TEST_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeSocietyModelsTests PRIVATE DreamscapesNativeViewApp)
    add_test(NAME Dreamscapes.NativeSocietyModels COMMAND DreamscapesNativeSocietyModelsTests)
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
