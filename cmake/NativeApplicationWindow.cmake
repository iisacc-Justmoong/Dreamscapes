add_library(DreamscapesNativeApplicationWindow STATIC
    src/App/Native/NativeApplicationWindow.h src/App/Native/NativeApplicationWindow.cpp)
set_target_properties(DreamscapesNativeApplicationWindow PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesNativeApplicationWindow PUBLIC cxx_std_23)
target_include_directories(DreamscapesNativeApplicationWindow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DreamscapesNativeApplicationWindow PUBLIC iiGUI::iiGUI iiSDKCore::Runtime)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeApplicationWindowStateTests tests/tst_native_application_window_state.cpp)
    set_target_properties(DreamscapesNativeApplicationWindowStateTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeApplicationWindowStateTests PRIVATE DreamscapesNativeApplicationWindow)
    add_test(NAME Dreamscapes.NativeApplicationWindowState COMMAND DreamscapesNativeApplicationWindowStateTests)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND APPLE AND NOT IOS AND CMAKE_OBJCXX_COMPILER)
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
