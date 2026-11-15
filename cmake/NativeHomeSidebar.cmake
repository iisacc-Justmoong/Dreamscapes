lvui_generate(NAME NativeHomeSidebar SOURCE src/App/Views/Home/NativeHomeSidebar.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeSidebarView.h OUTPUT_VARIABLE _native_home_sidebar)
add_library(DreamscapesHomeSidebarNative STATIC
    src/App/Views/Home/NativeHomeSidebar.h src/App/Views/Home/NativeHomeSidebar.cpp
    src/App/Views/Home/NativeHomeSidebarView.h src/App/Views/Home/NativeHomeSidebarView.cpp "${_native_home_sidebar}")
set_target_properties(DreamscapesHomeSidebarNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesHomeSidebarNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesHomeSidebarNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesHomeSidebarNative PUBLIC DreamscapesViewModelsNative LVUI::Native iiGUI::iiGUI)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeHomeSidebarTests tests/tst_native_home_sidebar.cpp)
    set_target_properties(DreamscapesNativeHomeSidebarTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeSidebarTests PRIVATE DreamscapesHomeSidebarNative)
    add_test(NAME Dreamscapes.NativeHomeSidebar COMMAND DreamscapesNativeHomeSidebarTests)
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeHomeSidebarWidgetTests tests/tst_native_home_sidebar_widget.cpp)
        set_target_properties(DreamscapesNativeHomeSidebarWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeSidebarWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeSidebarWidgetTests PRIVATE DreamscapesHomeSidebarNative)
        add_test(NAME Dreamscapes.NativeHomeSidebarWidget COMMAND DreamscapesNativeHomeSidebarWidgetTests)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME Dreamscapes.NativeHomeSidebarRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_sidebar}" "$<TARGET_FILE:DreamscapesNativeHomeSidebarWidgetTests>" menu)
    endif()
endif()
