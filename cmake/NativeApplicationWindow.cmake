add_library(DreamscapesNativeApplicationWindow STATIC
    src/App/Native/NativeApplicationWindow.h src/App/Native/NativeApplicationWindow.cpp)
set_target_properties(DreamscapesNativeApplicationWindow PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesNativeApplicationWindow PUBLIC cxx_std_23)
target_include_directories(DreamscapesNativeApplicationWindow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DreamscapesNativeApplicationWindow PUBLIC iiGUI::iiGUI iiSDKCore::Runtime)

add_library(DreamscapesNativeApplicationRuntime STATIC
    src/App/ApplicationShutdownCoordinator.h src/App/ApplicationShutdownCoordinator.cpp
    src/App/Native/NativeApplicationLifecycle.h src/App/Native/NativeApplicationLifecycle.cpp
    src/App/Native/NativeApplicationRuntime.h src/App/Native/NativeApplicationRuntime.cpp)
target_compile_features(DreamscapesNativeApplicationRuntime PUBLIC cxx_std_23)
target_include_directories(DreamscapesNativeApplicationRuntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DreamscapesNativeApplicationRuntime PUBLIC iiSDKCore::Runtime)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Dreamscapes.NativeLifecycleBoundary
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_lifecycle_boundary.py")
    set_tests_properties(Dreamscapes.NativeLifecycleBoundary PROPERTIES TIMEOUT 120)
    add_executable(DreamscapesNativeShutdownCoordinatorTests tests/tst_native_shutdown_coordinator.cpp)
    target_link_libraries(DreamscapesNativeShutdownCoordinatorTests PRIVATE DreamscapesNativeApplicationRuntime)
    add_test(NAME Dreamscapes.NativeShutdownCoordinator COMMAND DreamscapesNativeShutdownCoordinatorTests)
    add_executable(DreamscapesNativeApplicationLifecycleTests tests/tst_native_application_lifecycle.cpp)
    target_link_libraries(DreamscapesNativeApplicationLifecycleTests PRIVATE DreamscapesNativeApplicationRuntime)
    add_test(NAME Dreamscapes.NativeApplicationLifecycle COMMAND DreamscapesNativeApplicationLifecycleTests)
    add_executable(DreamscapesNativeApplicationRuntimeTests tests/tst_native_application_runtime.cpp)
    target_link_libraries(DreamscapesNativeApplicationRuntimeTests PRIVATE DreamscapesNativeApplicationRuntime)
    add_test(NAME Dreamscapes.NativeApplicationRuntime COMMAND DreamscapesNativeApplicationRuntimeTests)
    set_tests_properties(Dreamscapes.NativeApplicationRuntime PROPERTIES TIMEOUT 30)
    add_executable(DreamscapesNativeApplicationWindowStateTests tests/tst_native_application_window_state.cpp)
    set_target_properties(DreamscapesNativeApplicationWindowStateTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeApplicationWindowStateTests PRIVATE DreamscapesNativeApplicationWindow)
    add_test(NAME Dreamscapes.NativeApplicationWindowState COMMAND DreamscapesNativeApplicationWindowStateTests)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND APPLE AND NOT IOS AND CMAKE_OBJCXX_COMPILER)
    add_executable(DreamscapesNativeApplicationCoreWindowTests tests/tst_native_application_core_window.cpp)
    target_link_libraries(DreamscapesNativeApplicationCoreWindowTests PRIVATE
        DreamscapesNativeApplicationWindow DreamscapesNativeApplicationRuntime)
    add_test(NAME Dreamscapes.NativeApplicationCoreWindow COMMAND DreamscapesNativeApplicationCoreWindowTests)
    set_tests_properties(Dreamscapes.NativeApplicationCoreWindow PROPERTIES TIMEOUT 30)
    add_executable(DreamscapesNativeApplicationWindowTests tests/tst_native_application_window_mac.mm)
    set_target_properties(DreamscapesNativeApplicationWindowTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
    target_compile_options(DreamscapesNativeApplicationWindowTests PRIVATE -fobjc-arc)
    target_link_libraries(DreamscapesNativeApplicationWindowTests PRIVATE
        DreamscapesNativeApplicationWindow DreamscapesHomeQueueNative "-framework AppKit")
    add_test(NAME Dreamscapes.NativeApplicationWindow COMMAND DreamscapesNativeApplicationWindowTests
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg")
    set_tests_properties(Dreamscapes.NativeApplicationWindow PROPERTIES TIMEOUT 90)
endif()
