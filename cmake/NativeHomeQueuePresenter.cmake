if(TARGET DreamscapesNativePanelHost AND TARGET DreamscapesHomeQueueSession)
    add_library(DreamscapesHomeQueuePresenter STATIC
        src/App/Views/Home/NativeHomeQueuePresenter.h src/App/Views/Home/NativeHomeQueuePresenter.cpp)
    set_target_properties(DreamscapesHomeQueuePresenter PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeQueuePresenter PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeQueuePresenter PUBLIC DreamscapesNativePanelHost
        DreamscapesHomeQueueSession)
    if(APPLE AND NOT IOS AND TARGET DreamscapesNativeEmbeddedSurface AND TARGET Dreamscapes)
        set_target_properties(Dreamscapes PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
        target_sources(Dreamscapes PRIVATE src/App/Views/Home/NativeHomeQueuePresenterMac.h
            src/App/Views/Home/NativeHomeQueuePresenterMac.mm)
        set_source_files_properties(src/App/Views/Home/NativeHomeQueuePresenterMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        set_source_files_properties(resources/design/Home/queue-ready.svg PROPERTIES QT_RESOURCE_ALIAS "queue-ready.svg")
        qt_add_resources(Dreamscapes native_home_queue_indicator PREFIX "/native-home-queue"
            FILES resources/design/Home/queue-ready.svg)
        target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_HOME_QUEUE_PRESENTER=1)
        target_link_libraries(Dreamscapes PRIVATE DreamscapesHomeQueuePresenter DreamscapesNativeEmbeddedSurface "-framework AppKit")
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(DreamscapesNativeHomeQueuePresenterTests tests/tst_native_home_queue_presenter.cpp)
        set_target_properties(DreamscapesNativeHomeQueuePresenterTests PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
        target_compile_definitions(DreamscapesNativeHomeQueuePresenterTests PRIVATE
            DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeHomeQueuePresenterTests PRIVATE DreamscapesHomeQueuePresenter)
        add_test(NAME Dreamscapes.NativeHomeQueuePresenter COMMAND DreamscapesNativeHomeQueuePresenterTests)
        set_tests_properties(Dreamscapes.NativeHomeQueuePresenter PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
    endif()
endif()
