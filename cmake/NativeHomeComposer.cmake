lvui_generate(NAME NativeHomeComposerPrompt SOURCE src/App/Views/Home/NativeHomeComposerPrompt.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeComposerPromptView.h OUTPUT_VARIABLE _native_home_composer_prompt)
lvui_generate(NAME NativeHomeComposerChoice SOURCE src/App/Views/Home/NativeHomeComposerChoice.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeComposerChoiceView.h OUTPUT_VARIABLE _native_home_composer_choice)
lvui_generate(NAME NativeHomeComposerSeed SOURCE src/App/Views/Home/NativeHomeComposerSeed.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeComposerSeedView.h OUTPUT_VARIABLE _native_home_composer_seed)
lvui_generate(NAME NativeHomePresetSave SOURCE src/App/Views/Home/NativeHomePresetSave.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomePresetSaveView.h OUTPUT_VARIABLE _native_home_preset_save)
lvui_generate(NAME NativeHomeAttachments SOURCE src/App/Views/Home/NativeHomeAttachments.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeAttachmentsView.h OUTPUT_VARIABLE _native_home_attachments)
lvui_generate(NAME NativeHomeComposerAction SOURCE src/App/Views/Home/NativeHomeComposerAction.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeComposerActionView.h OUTPUT_VARIABLE _native_home_composer_action)
lvui_generate(NAME NativeHomeNegativePrompt SOURCE src/App/Views/Home/NativeHomeNegativePrompt.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeNegativePromptView.h OUTPUT_VARIABLE _native_home_negative_prompt)
lvui_generate(NAME NativeHomeComposerPanel SOURCE src/App/Views/Home/NativeHomeComposerPanel.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeComposerPanelView.h OUTPUT_VARIABLE _native_home_composer_panel)
lvui_generate(NAME NativeHomePresetGroup SOURCE src/App/Views/Home/NativeHomePresetGroup.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomePresetGroupView.h OUTPUT_VARIABLE _native_home_preset_group)
add_library(DreamscapesHomeComposerNative STATIC
    src/App/Views/Home/NativeHomeComposer.h src/App/Views/Home/NativeHomeComposer.cpp
    src/App/Views/Home/NativeHomeComposerPrompt.h src/App/Views/Home/NativeHomeComposerPrompt.cpp
    src/App/Views/Home/NativeHomeComposerChoice.h src/App/Views/Home/NativeHomeComposerChoice.cpp
    src/App/Views/Home/NativeHomeComposerSeed.h src/App/Views/Home/NativeHomeComposerSeed.cpp
    src/App/Views/Home/NativeHomePresetSave.h src/App/Views/Home/NativeHomePresetSave.cpp
    src/App/Views/Home/NativeHomeAttachments.h src/App/Views/Home/NativeHomeAttachments.cpp
    src/App/Views/Home/NativeHomeComposerAction.h src/App/Views/Home/NativeHomeComposerAction.cpp
    src/App/Views/Home/NativeHomeComposerLayout.h src/App/Views/Home/NativeHomeComposerLayout.cpp
    src/App/Views/Home/NativeHomeComposerPanel.h src/App/Views/Home/NativeHomeComposerPanel.cpp
    src/App/Views/Home/NativeHomeComposerPanelView.h src/App/Views/Home/NativeHomeComposerPanelView.cpp "${_native_home_composer_panel}"
    src/App/Views/Home/NativeHomeComposerGrid.h src/App/Views/Home/NativeHomeComposerGrid.cpp
    src/App/Views/Home/NativeHomeComposerSources.h src/App/Views/Home/NativeHomeComposerSources.cpp
    src/App/Views/Home/NativeHomeComposerFactory.h src/App/Views/Home/NativeHomeComposerFactory.cpp
    src/App/Views/Home/NativeHomePresetGroup.h src/App/Views/Home/NativeHomePresetGroup.cpp
    src/App/Views/Home/NativeHomePresetGroupView.h src/App/Views/Home/NativeHomePresetGroupView.cpp "${_native_home_preset_group}"
    src/App/Views/Home/NativeHomeNegativePrompt.h src/App/Views/Home/NativeHomeNegativePrompt.cpp
    src/App/Views/Home/NativeHomeNegativePromptView.h src/App/Views/Home/NativeHomeNegativePromptView.cpp "${_native_home_negative_prompt}"
    src/App/Views/Home/NativeHomeComposerActionView.h src/App/Views/Home/NativeHomeComposerActionView.cpp "${_native_home_composer_action}"
    src/App/Views/Home/NativeHomeAttachmentsView.h src/App/Views/Home/NativeHomeAttachmentsView.cpp "${_native_home_attachments}"
    src/App/Views/Home/NativeHomePresetSaveView.h src/App/Views/Home/NativeHomePresetSaveView.cpp "${_native_home_preset_save}"
    src/App/Views/Home/NativeHomeComposerSeedView.h src/App/Views/Home/NativeHomeComposerSeedView.cpp "${_native_home_composer_seed}"
    src/App/Views/Home/NativeHomeComposerPromptView.h src/App/Views/Home/NativeHomeComposerPromptView.cpp "${_native_home_composer_prompt}"
    src/App/Views/Home/NativeHomeComposerChoiceView.h src/App/Views/Home/NativeHomeComposerChoiceView.cpp "${_native_home_composer_choice}")
set_target_properties(DreamscapesHomeComposerNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesHomeComposerNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesHomeComposerNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesHomeComposerNative PUBLIC DreamscapesViewModelsNative LVUI::Native iiGUI::iiGUI)
if(TARGET DreamscapesGeneration)
    add_library(DreamscapesHomeComposerSession STATIC
        src/App/Views/Home/NativeHomeComposerSession.h src/App/Views/Home/NativeHomeComposerSession.cpp
        src/App/Views/Home/NativeHomeReferenceBinding.h src/App/Views/Home/NativeHomeReferenceBinding.cpp
        src/App/Views/Home/NativeHomeReferencePicker.h src/App/Views/Home/NativeHomeReferencePicker.cpp)
    set_target_properties(DreamscapesHomeComposerSession PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeComposerSession PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeComposerSession PUBLIC DreamscapesHomeComposerNative DreamscapesGeneration)
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeHomeComposerTests tests/tst_native_home_composer.cpp)
    set_target_properties(DreamscapesNativeHomeComposerTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposer COMMAND DreamscapesNativeHomeComposerTests)
    add_executable(DreamscapesNativeHomeComposerPromptTests tests/tst_native_home_composer_prompt.cpp)
    set_target_properties(DreamscapesNativeHomeComposerPromptTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerPromptTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposerPrompt COMMAND DreamscapesNativeHomeComposerPromptTests)
    add_executable(DreamscapesNativeHomeComposerChoiceTests tests/tst_native_home_composer_choice.cpp)
    set_target_properties(DreamscapesNativeHomeComposerChoiceTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerChoiceTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposerChoice COMMAND DreamscapesNativeHomeComposerChoiceTests)
    add_executable(DreamscapesNativeHomeComposerSeedTests tests/tst_native_home_composer_seed.cpp)
    set_target_properties(DreamscapesNativeHomeComposerSeedTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerSeedTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposerSeed COMMAND DreamscapesNativeHomeComposerSeedTests)
    add_executable(DreamscapesNativeHomePresetSaveTests tests/tst_native_home_preset_save.cpp)
    set_target_properties(DreamscapesNativeHomePresetSaveTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomePresetSaveTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomePresetSave COMMAND DreamscapesNativeHomePresetSaveTests)
    add_executable(DreamscapesNativeHomeAttachmentsTests tests/tst_native_home_attachments.cpp)
    set_target_properties(DreamscapesNativeHomeAttachmentsTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeAttachmentsTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeAttachments COMMAND DreamscapesNativeHomeAttachmentsTests)
    add_executable(DreamscapesNativeHomeComposerActionTests tests/tst_native_home_composer_action.cpp)
    set_target_properties(DreamscapesNativeHomeComposerActionTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerActionTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposerAction COMMAND DreamscapesNativeHomeComposerActionTests)
    add_executable(DreamscapesNativeHomeNegativePromptTests tests/tst_native_home_negative_prompt.cpp)
    set_target_properties(DreamscapesNativeHomeNegativePromptTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeNegativePromptTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeNegativePrompt COMMAND DreamscapesNativeHomeNegativePromptTests)
    add_executable(DreamscapesNativeHomeComposerLayoutTests tests/tst_native_home_composer_layout.cpp)
    set_target_properties(DreamscapesNativeHomeComposerLayoutTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerLayoutTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposerLayout COMMAND DreamscapesNativeHomeComposerLayoutTests)
    add_executable(DreamscapesNativeHomeComposerPanelTests tests/tst_native_home_composer_panel.cpp)
    set_target_properties(DreamscapesNativeHomeComposerPanelTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerPanelTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposerPanel COMMAND DreamscapesNativeHomeComposerPanelTests)
    add_executable(DreamscapesNativeHomePresetGroupTests tests/tst_native_home_preset_group.cpp)
    set_target_properties(DreamscapesNativeHomePresetGroupTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomePresetGroupTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomePresetGroup COMMAND DreamscapesNativeHomePresetGroupTests)
    add_executable(DreamscapesNativeHomeComposerFactoryTests tests/tst_native_home_composer_factory.cpp)
    set_target_properties(DreamscapesNativeHomeComposerFactoryTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerFactoryTests PRIVATE DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposerFactory COMMAND DreamscapesNativeHomeComposerFactoryTests)
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeHomeComposerPromptWidgetTests tests/tst_native_home_composer_prompt_widget.cpp)
        set_target_properties(DreamscapesNativeHomeComposerPromptWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeComposerPromptWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeComposerPromptWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeComposerPromptWidget COMMAND DreamscapesNativeHomeComposerPromptWidgetTests)
        add_executable(DreamscapesNativeHomeComposerChoiceWidgetTests tests/tst_native_home_composer_choice_widget.cpp)
        set_target_properties(DreamscapesNativeHomeComposerChoiceWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeComposerChoiceWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeComposerChoiceWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeComposerChoiceWidget COMMAND DreamscapesNativeHomeComposerChoiceWidgetTests)
        add_executable(DreamscapesNativeHomeComposerSeedWidgetTests tests/tst_native_home_composer_seed_widget.cpp)
        set_target_properties(DreamscapesNativeHomeComposerSeedWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeComposerSeedWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeComposerSeedWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeComposerSeedWidget COMMAND DreamscapesNativeHomeComposerSeedWidgetTests)
        add_executable(DreamscapesNativeHomePresetSaveWidgetTests tests/tst_native_home_preset_save_widget.cpp)
        set_target_properties(DreamscapesNativeHomePresetSaveWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomePresetSaveWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomePresetSaveWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomePresetSaveWidget COMMAND DreamscapesNativeHomePresetSaveWidgetTests)
        add_executable(DreamscapesNativeHomeAttachmentsWidgetTests tests/tst_native_home_attachments_widget.cpp)
        set_target_properties(DreamscapesNativeHomeAttachmentsWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeAttachmentsWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeAttachmentsWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeAttachmentsWidget COMMAND DreamscapesNativeHomeAttachmentsWidgetTests)
        add_executable(DreamscapesNativeHomeComposerActionWidgetTests tests/tst_native_home_composer_action_widget.cpp)
        set_target_properties(DreamscapesNativeHomeComposerActionWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeComposerActionWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeComposerActionWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeComposerActionWidget COMMAND DreamscapesNativeHomeComposerActionWidgetTests)
        add_executable(DreamscapesNativeHomeNegativePromptWidgetTests tests/tst_native_home_negative_prompt_widget.cpp)
        set_target_properties(DreamscapesNativeHomeNegativePromptWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeNegativePromptWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeNegativePromptWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeNegativePromptWidget COMMAND DreamscapesNativeHomeNegativePromptWidgetTests)
        add_executable(DreamscapesNativeHomeComposerGridWidgetTests tests/tst_native_home_composer_grid_widget.cpp)
        set_target_properties(DreamscapesNativeHomeComposerGridWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeComposerGridWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeComposerGridWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeComposerGridWidget COMMAND DreamscapesNativeHomeComposerGridWidgetTests)
        add_executable(DreamscapesNativeHomeComposerPanelWidgetTests tests/tst_native_home_composer_panel_widget.cpp)
        set_target_properties(DreamscapesNativeHomeComposerPanelWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeComposerPanelWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeComposerPanelWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeComposerPanelWidget COMMAND DreamscapesNativeHomeComposerPanelWidgetTests)
        add_executable(DreamscapesNativeHomePresetGroupWidgetTests tests/tst_native_home_preset_group_widget.cpp)
        set_target_properties(DreamscapesNativeHomePresetGroupWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomePresetGroupWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomePresetGroupWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomePresetGroupWidget COMMAND DreamscapesNativeHomePresetGroupWidgetTests)
        add_executable(DreamscapesNativeHomeComposerSourcesWidgetTests tests/tst_native_home_composer_sources_widget.cpp)
        set_target_properties(DreamscapesNativeHomeComposerSourcesWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeComposerSourcesWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeComposerSourcesWidgetTests PRIVATE DreamscapesHomeComposerNative)
        add_test(NAME Dreamscapes.NativeHomeComposerSourcesWidget COMMAND DreamscapesNativeHomeComposerSourcesWidgetTests)
        add_test(NAME Dreamscapes.NativeHomePresetGroupRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_preset_group}" "$<TARGET_FILE:DreamscapesNativeHomePresetGroupWidgetTests>" composed)
        add_test(NAME Dreamscapes.NativeHomeComposerPanelRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_composer_panel}" "$<TARGET_FILE:DreamscapesNativeHomeComposerPanelWidgetTests>" composed)
        add_test(NAME Dreamscapes.NativeHomeNegativePromptRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_negative_prompt}" "$<TARGET_FILE:DreamscapesNativeHomeNegativePromptWidgetTests>" negative)
        add_test(NAME Dreamscapes.NativeHomeComposerActionRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_composer_action}" "$<TARGET_FILE:DreamscapesNativeHomeComposerActionWidgetTests>" button)
        add_test(NAME Dreamscapes.NativeHomeAttachmentsRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_attachments}" "$<TARGET_FILE:DreamscapesNativeHomeAttachmentsWidgetTests>")
        add_test(NAME Dreamscapes.NativeHomePresetSaveRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_preset_save}" "$<TARGET_FILE:DreamscapesNativeHomePresetSaveWidgetTests>" seed)
        add_test(NAME Dreamscapes.NativeHomeComposerSeedRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_composer_seed}" "$<TARGET_FILE:DreamscapesNativeHomeComposerSeedWidgetTests>" seed)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME Dreamscapes.NativeHomeComposerPromptRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_composer_prompt}" "$<TARGET_FILE:DreamscapesNativeHomeComposerPromptWidgetTests>" text-editor)
        add_test(NAME Dreamscapes.NativeHomeComposerChoiceRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_composer_choice}" "$<TARGET_FILE:DreamscapesNativeHomeComposerChoiceWidgetTests>" menu)
        if(TARGET DreamscapesHomeComposerSession)
            add_executable(DreamscapesNativeHomeComposerPromptIntegrationTests tests/tst_native_home_composer_prompt_integration.cpp)
            set_target_properties(DreamscapesNativeHomeComposerPromptIntegrationTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
            target_include_directories(DreamscapesNativeHomeComposerPromptIntegrationTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_link_libraries(DreamscapesNativeHomeComposerPromptIntegrationTests PRIVATE DreamscapesHomeComposerSession)
            add_test(NAME Dreamscapes.NativeHomeComposerPromptIntegration COMMAND DreamscapesNativeHomeComposerPromptIntegrationTests)
            add_executable(DreamscapesNativeHomeComposerChoiceIntegrationTests tests/tst_native_home_composer_choice_integration.cpp)
            set_target_properties(DreamscapesNativeHomeComposerChoiceIntegrationTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
            target_include_directories(DreamscapesNativeHomeComposerChoiceIntegrationTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_link_libraries(DreamscapesNativeHomeComposerChoiceIntegrationTests PRIVATE DreamscapesHomeComposerSession)
            add_test(NAME Dreamscapes.NativeHomeComposerChoiceIntegration COMMAND DreamscapesNativeHomeComposerChoiceIntegrationTests)
        endif()
    endif()
    if(TARGET DreamscapesHomeComposerSession)
        add_executable(DreamscapesNativeHomeComposerSessionTests tests/tst_native_home_composer_session.cpp)
        set_target_properties(DreamscapesNativeHomeComposerSessionTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeHomeComposerSessionTests PRIVATE DreamscapesHomeComposerSession)
        add_test(NAME Dreamscapes.NativeHomeComposerSession COMMAND DreamscapesNativeHomeComposerSessionTests)
    endif()
endif()
