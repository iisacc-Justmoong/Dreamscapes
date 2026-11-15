lvui_generate(NAME NativeHomeSelectedFile SOURCE src/App/Views/Home/NativeHomeSelectedFile.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeSelectedFile.h OUTPUT_VARIABLE _native_home_selected_file)
add_library(DreamscapesHomeSelectedFileNative STATIC
    src/App/Views/Home/NativeHomeSelectedFile.h src/App/Views/Home/NativeHomeSelectedFile.cpp
    src/App/Views/Home/NativeHomeSelectedFileView.h src/App/Views/Home/NativeHomeSelectedFileView.cpp
    "${_native_home_selected_file}")
set_target_properties(DreamscapesHomeSelectedFileNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesHomeSelectedFileNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesHomeSelectedFileNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesHomeSelectedFileNative PUBLIC DreamscapesViewModelsNative LVUI::Native iiGUI::iiGUI)

if(TARGET DreamscapesGeneration)
    add_library(DreamscapesHomeSelectedFileSession STATIC
        src/App/Views/Home/NativeHomeSelectedFileSession.h src/App/Views/Home/NativeHomeSelectedFileSession.cpp)
    set_target_properties(DreamscapesHomeSelectedFileSession PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeSelectedFileSession PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeSelectedFileSession PUBLIC DreamscapesHomeSelectedFileNative DreamscapesGeneration)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeHomeSelectedFileTests tests/tst_native_home_selected_file.cpp)
    set_target_properties(DreamscapesNativeHomeSelectedFileTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeSelectedFileTests PRIVATE DreamscapesHomeSelectedFileNative)
    add_test(NAME Dreamscapes.NativeHomeSelectedFile COMMAND DreamscapesNativeHomeSelectedFileTests)
    if(TARGET DreamscapesGeneration)
        add_executable(DreamscapesNativeHomeSelectedFileSessionTests tests/tst_native_home_selected_file_session.cpp)
        set_target_properties(DreamscapesNativeHomeSelectedFileSessionTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_compile_definitions(DreamscapesNativeHomeSelectedFileSessionTests PRIVATE
            DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeHomeSelectedFileSessionTests PRIVATE
            DreamscapesHomeSelectedFileSession)
        add_test(NAME Dreamscapes.NativeHomeSelectedFileSession COMMAND DreamscapesNativeHomeSelectedFileSessionTests)
        set_tests_properties(Dreamscapes.NativeHomeSelectedFileSession PROPERTIES TIMEOUT 60
            ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources")
    endif()
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeHomeSelectedFileWidgetTests tests/tst_native_home_selected_file_widget.cpp)
        set_target_properties(DreamscapesNativeHomeSelectedFileWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeSelectedFileWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeSelectedFileWidgetTests PRIVATE DreamscapesHomeSelectedFileNative)
        add_test(NAME Dreamscapes.NativeHomeSelectedFileWidget COMMAND DreamscapesNativeHomeSelectedFileWidgetTests)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME Dreamscapes.NativeHomeSelectedFileRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_selected_file}" "$<TARGET_FILE:DreamscapesNativeHomeSelectedFileWidgetTests>")
        if(TARGET DreamscapesNativeFileImages)
            add_executable(DreamscapesNativeHomeSelectedFileIiscTests tests/tst_native_home_selected_file_iisc.cpp)
            set_target_properties(DreamscapesNativeHomeSelectedFileIiscTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
            target_include_directories(DreamscapesNativeHomeSelectedFileIiscTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_link_libraries(DreamscapesNativeHomeSelectedFileIiscTests PRIVATE DreamscapesHomeSelectedFileNative
                DreamscapesNativeFileImages iiSharedCanvas::Native iiNetwork::iiNetwork iiFileProvider::Native)
            add_test(NAME Dreamscapes.NativeHomeSelectedFileIisc COMMAND DreamscapesNativeHomeSelectedFileIiscTests
                "${CMAKE_CURRENT_SOURCE_DIR}/resources")
        endif()
    endif()
endif()
