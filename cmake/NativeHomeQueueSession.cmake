if(TARGET DreamscapesHomeQueueBridge AND TARGET DreamscapesNativeGenerationProjectionBinding)
    add_library(DreamscapesHomeQueueSession STATIC
        src/App/Views/Home/NativeHomeQueueSession.h src/App/Views/Home/NativeHomeQueueSession.cpp)
    set_target_properties(DreamscapesHomeQueueSession PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeQueueSession PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeQueueSession PUBLIC
        DreamscapesHomeQueueBridge DreamscapesNativeGenerationProjectionBinding)
    if(TARGET DreamscapesNativeGenerationProjectionBindingTests)
        target_link_libraries(DreamscapesNativeGenerationProjectionBindingTests PRIVATE DreamscapesHomeQueueSession)
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND APPLE AND NOT IOS AND CMAKE_OBJCXX_COMPILER)
        add_executable(DreamscapesNativeHomeQueueWindowTests tests/tst_native_home_queue_window_mac.mm)
        set_target_properties(DreamscapesNativeHomeQueueWindowTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
            OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(DreamscapesNativeHomeQueueWindowTests PRIVATE -fobjc-arc)
        target_compile_definitions(DreamscapesNativeHomeQueueWindowTests PRIVATE
            DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeHomeQueueWindowTests PRIVATE
            DreamscapesHomeQueueSession DreamscapesNativeApplicationWindow "-framework AppKit")
        add_test(NAME Dreamscapes.NativeHomeQueueWindow COMMAND DreamscapesNativeHomeQueueWindowTests
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/design/Home/queue-ready.svg")
        set_tests_properties(Dreamscapes.NativeHomeQueueWindow PROPERTIES TIMEOUT 90
            ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
    endif()
endif()
