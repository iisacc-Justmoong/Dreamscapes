# A native Home queue view and its Qt-free model. The application continues to
# use its current Home until the native window and navigation are connected.
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native HINTS "$ENV{HOME}/.local/SDK/LVUI")
find_package(iiGUI 0.5.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiGUI")
lvui_generate(NAME NativeHomeQueue SOURCE src/App/Views/Home/NativeHomeQueue.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Home/NativeHomeQueue.h OUTPUT_VARIABLE _native_home_queue)

add_library(DreamscapesHomeQueueNative STATIC
    src/App/Views/Home/NativeHomeQueue.h
    src/App/Views/Home/NativeHomeQueue.cpp
    src/App/Views/Home/NativeHomeQueueView.h
    src/App/Views/Home/NativeHomeQueueView.cpp
    "${_native_home_queue}")
set_target_properties(DreamscapesHomeQueueNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesHomeQueueNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesHomeQueueNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesHomeQueueNative PUBLIC DreamscapesViewModelsNative LVUI::Native iiGUI::iiGUI)

# Keep the current controller's transport isolated from the native view model.
if(TARGET DreamscapesViewModels)
    add_library(DreamscapesHomeQueueBridge STATIC
        src/App/Views/Home/NativeHomeQueueBridge.h
        src/App/Views/Home/NativeHomeQueueBridge.cpp)
    set_target_properties(DreamscapesHomeQueueBridge PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeQueueBridge PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeQueueBridge PUBLIC DreamscapesHomeQueueNative DreamscapesViewModels)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeHomeQueueTests tests/tst_native_home_queue.cpp)
    set_target_properties(DreamscapesNativeHomeQueueTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeHomeQueueTests PRIVATE DreamscapesHomeQueueNative)
    add_test(NAME Dreamscapes.NativeHomeQueue COMMAND DreamscapesNativeHomeQueueTests)

    if(TARGET DreamscapesHomeQueueBridge)
        add_executable(DreamscapesNativeHomeQueueBridgeTests tests/tst_native_home_queue_bridge.cpp)
        set_target_properties(DreamscapesNativeHomeQueueBridgeTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeHomeQueueBridgeTests PRIVATE DreamscapesHomeQueueBridge)
        add_test(NAME Dreamscapes.NativeHomeQueueBridge COMMAND DreamscapesNativeHomeQueueBridgeTests)
    endif()

    set(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR "" CACHE PATH
        "Matching iiGUI build/include directory for the optional headless widget test")
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeHomeQueueWidgetTests tests/tst_native_home_queue_widget.cpp)
        set_target_properties(DreamscapesNativeHomeQueueWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeHomeQueueWidgetTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeHomeQueueWidgetTests PRIVATE DreamscapesHomeQueueNative)
        add_test(NAME Dreamscapes.NativeHomeQueueWidget COMMAND DreamscapesNativeHomeQueueWidgetTests
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME Dreamscapes.NativeHomeQueueRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                "${_native_home_queue}" "$<TARGET_FILE:DreamscapesNativeHomeQueueWidgetTests>")
        if(TARGET DreamscapesHomeQueueBridge)
            add_executable(DreamscapesNativeHomeQueueIntegrationTests tests/tst_native_home_queue_integration.cpp)
            set_target_properties(DreamscapesNativeHomeQueueIntegrationTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
            target_include_directories(DreamscapesNativeHomeQueueIntegrationTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_link_libraries(DreamscapesNativeHomeQueueIntegrationTests PRIVATE DreamscapesHomeQueueBridge)
            add_test(NAME Dreamscapes.NativeHomeQueueIntegration COMMAND DreamscapesNativeHomeQueueIntegrationTests
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg")
        endif()
    endif()
endif()
