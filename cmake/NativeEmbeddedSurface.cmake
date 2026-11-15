if(APPLE AND NOT IOS AND CMAKE_OBJCXX_COMPILER)
    add_library(DreamscapesNativeEmbeddedSurface STATIC
        src/App/Native/NativeEmbeddedSurface.h src/App/Native/NativeEmbeddedSurface.mm)
    set_target_properties(DreamscapesNativeEmbeddedSurface PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
    target_compile_options(DreamscapesNativeEmbeddedSurface PRIVATE -fobjc-arc)
    target_include_directories(DreamscapesNativeEmbeddedSurface PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(DreamscapesNativeEmbeddedSurface PUBLIC iiGUI::iiGUI iiSDKCore::Runtime PRIVATE "-framework AppKit")
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND TARGET DreamscapesHomeQueueNative)
        add_executable(DreamscapesNativeEmbeddedSurfaceMacTests tests/tst_native_embedded_surface_mac.mm)
        set_target_properties(DreamscapesNativeEmbeddedSurfaceMacTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
            OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(DreamscapesNativeEmbeddedSurfaceMacTests PRIVATE -fobjc-arc)
        target_link_libraries(DreamscapesNativeEmbeddedSurfaceMacTests PRIVATE
            DreamscapesNativeEmbeddedSurface DreamscapesHomeQueueNative Qt6::Core "-framework AppKit")
        add_test(NAME Dreamscapes.NativeEmbeddedSurfaceMac COMMAND DreamscapesNativeEmbeddedSurfaceMacTests
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg")
        set_tests_properties(Dreamscapes.NativeEmbeddedSurfaceMac PROPERTIES TIMEOUT 90)
    endif()
endif()
