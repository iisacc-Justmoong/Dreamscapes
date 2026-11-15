lvui_generate(NAME NativeHomeWorkspace SOURCE src/App/Views/Home/NativeHomeWorkspace.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeWorkspaceView.h OUTPUT_VARIABLE _native_home_workspace)
add_library(DreamscapesHomeWorkspaceNative STATIC
    src/App/Views/Home/NativeHomeWorkspace.h src/App/Views/Home/NativeHomeWorkspace.cpp
    src/App/Views/Home/NativeHomeWorkspaceView.h src/App/Views/Home/NativeHomeWorkspaceView.cpp
    src/App/Views/Home/NativeHomeWorkspacePair.h src/App/Views/Home/NativeHomeWorkspacePair.cpp
    src/App/Views/Home/NativeHomeComposerWorkspaceBinding.h src/App/Views/Home/NativeHomeComposerWorkspaceBinding.cpp
    src/App/Views/Home/NativeHomeWorkspaceSources.h src/App/Views/Home/NativeHomeWorkspaceSources.cpp "${_native_home_workspace}")
set_target_properties(DreamscapesHomeWorkspaceNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesHomeWorkspaceNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesHomeWorkspaceNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesHomeWorkspaceNative PUBLIC DreamscapesHomeSidebarNative DreamscapesHomeQueueNative
    DreamscapesHomeRecentFilesNative DreamscapesHomeSelectedFileNative DreamscapesHomeRecentGenerationsNative DreamscapesHomeComposerNative LVUI::Native iiGUI::iiGUI)
if(TARGET DreamscapesHomeRecentFilesSession AND TARGET DreamscapesHomeSelectedFileSession AND TARGET DreamscapesHomeRecentGenerationsSession)
    add_library(DreamscapesHomeContentSession STATIC
        src/App/Views/Home/NativeHomeContentSession.h src/App/Views/Home/NativeHomeContentSession.cpp)
    set_target_properties(DreamscapesHomeContentSession PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeContentSession PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeContentSession PUBLIC DreamscapesHomeRecentFilesSession
        DreamscapesHomeSelectedFileSession DreamscapesHomeRecentGenerationsSession)
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(DreamscapesNativeHomeContentSessionTests tests/tst_native_home_content_session.cpp)
        set_target_properties(DreamscapesNativeHomeContentSessionTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_compile_definitions(DreamscapesNativeHomeContentSessionTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeHomeContentSessionTests PRIVATE DreamscapesHomeContentSession)
        add_test(NAME Dreamscapes.NativeHomeContentSession COMMAND DreamscapesNativeHomeContentSessionTests)
        set_tests_properties(Dreamscapes.NativeHomeContentSession PROPERTIES TIMEOUT 60
            ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
        if(TARGET DreamscapesHomeComposerSession AND DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
            add_executable(DreamscapesNativeHomeContentWorkspaceWidgetTests tests/tst_native_home_content_workspace_widget.cpp)
            set_target_properties(DreamscapesNativeHomeContentWorkspaceWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
            target_compile_definitions(DreamscapesNativeHomeContentWorkspaceWidgetTests PRIVATE DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
            target_include_directories(DreamscapesNativeHomeContentWorkspaceWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_link_libraries(DreamscapesNativeHomeContentWorkspaceWidgetTests PRIVATE DreamscapesHomeContentSession
                DreamscapesHomeWorkspaceNative DreamscapesHomeComposerSession)
            add_test(NAME Dreamscapes.NativeHomeContentWorkspaceWidget COMMAND DreamscapesNativeHomeContentWorkspaceWidgetTests
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg")
            set_tests_properties(Dreamscapes.NativeHomeContentWorkspaceWidget PROPERTIES TIMEOUT 60
                ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
        endif()
    endif()
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeHomeWorkspaceTests tests/tst_native_home_workspace.cpp)
    set_target_properties(DreamscapesNativeHomeWorkspaceTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeWorkspaceTests PRIVATE DreamscapesHomeWorkspaceNative)
    add_test(NAME Dreamscapes.NativeHomeWorkspace COMMAND DreamscapesNativeHomeWorkspaceTests)
    add_executable(DreamscapesNativeHomeComposerWorkspaceBindingTests tests/tst_native_home_composer_workspace_binding.cpp)
    set_target_properties(DreamscapesNativeHomeComposerWorkspaceBindingTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeComposerWorkspaceBindingTests PRIVATE DreamscapesHomeWorkspaceNative DreamscapesHomeComposerNative)
    add_test(NAME Dreamscapes.NativeHomeComposerWorkspaceBinding COMMAND DreamscapesNativeHomeComposerWorkspaceBindingTests)
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeHomeWorkspaceWidgetTests tests/tst_native_home_workspace_widget.cpp)
        set_target_properties(DreamscapesNativeHomeWorkspaceWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeWorkspaceWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeWorkspaceWidgetTests PRIVATE DreamscapesHomeWorkspaceNative)
        add_test(NAME Dreamscapes.NativeHomeWorkspaceWidget COMMAND DreamscapesNativeHomeWorkspaceWidgetTests
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg")
        add_executable(DreamscapesNativeHomeWorkspaceComposerWidgetTests tests/tst_native_home_workspace_composer_widget.cpp)
        set_target_properties(DreamscapesNativeHomeWorkspaceComposerWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeWorkspaceComposerWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeWorkspaceComposerWidgetTests PRIVATE DreamscapesHomeWorkspaceNative)
        add_test(NAME Dreamscapes.NativeHomeWorkspaceComposerWidget COMMAND DreamscapesNativeHomeWorkspaceComposerWidgetTests
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME Dreamscapes.NativeHomeWorkspaceRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_workspace}" "$<TARGET_FILE:DreamscapesNativeHomeWorkspaceWidgetTests>" native-view)
    endif()
endif()
