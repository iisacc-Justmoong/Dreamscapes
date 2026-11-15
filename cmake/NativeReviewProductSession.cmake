if(TARGET DreamscapesReviewGenerationBridge AND TARGET DreamscapesNativeGenerationProjectionBinding)
    add_library(DreamscapesReviewProductSession STATIC
        src/App/Views/Result/NativeReviewQueueSession.h
        src/App/Views/Result/NativeReviewQueueSession.cpp)
    set_target_properties(DreamscapesReviewProductSession PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesReviewProductSession PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesReviewProductSession PUBLIC
        DreamscapesNativeGenerationProjectionBinding DreamscapesReviewGenerationBridge)
    add_library(DreamscapesReviewQueuePresenter STATIC
        src/App/Views/Result/NativeReviewQueuePresenter.h
        src/App/Views/Result/NativeReviewQueuePresenter.cpp)
    set_target_properties(DreamscapesReviewQueuePresenter PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesReviewQueuePresenter PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesReviewQueuePresenter PUBLIC DreamscapesReviewProductSession DreamscapesNativePanelHost)
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(DreamscapesNativeReviewQueueRegionTests tests/tst_native_review_queue_region.cpp)
        set_target_properties(DreamscapesNativeReviewQueueRegionTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeReviewQueueRegionTests PRIVATE DreamscapesNativePanelHost)
        add_test(NAME Dreamscapes.NativeReviewQueueRegion COMMAND DreamscapesNativeReviewQueueRegionTests)
        set_tests_properties(Dreamscapes.NativeReviewQueueRegion PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(DreamscapesNativeReviewQueueOverlaysTests tests/tst_native_review_queue_overlays.cpp)
        set_target_properties(DreamscapesNativeReviewQueueOverlaysTests PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeReviewQueueOverlaysTests PRIVATE DreamscapesNativePanelHost)
        add_test(NAME Dreamscapes.NativeReviewQueueOverlays COMMAND DreamscapesNativeReviewQueueOverlaysTests)
        set_tests_properties(Dreamscapes.NativeReviewQueueOverlays PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_executable(DreamscapesNativeReviewQueuePresenterTests tests/tst_native_review_queue_presenter.cpp)
        set_target_properties(DreamscapesNativeReviewQueuePresenterTests PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
        target_compile_definitions(DreamscapesNativeReviewQueuePresenterTests PRIVATE
            DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeReviewQueuePresenterTests PRIVATE DreamscapesReviewQueuePresenter)
        add_test(NAME Dreamscapes.NativeReviewQueuePresenter COMMAND DreamscapesNativeReviewQueuePresenterTests)
        set_tests_properties(Dreamscapes.NativeReviewQueuePresenter PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND
       EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeReviewQueueProductSessionTests tests/tst_native_review_queue_product_session.cpp)
        set_target_properties(DreamscapesNativeReviewQueueProductSessionTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeReviewQueueProductSessionTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_compile_definitions(DreamscapesNativeReviewQueueProductSessionTests PRIVATE
            DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeReviewQueueProductSessionTests PRIVATE
            DreamscapesReviewProductSession DreamscapesReviewQueueNative)
        add_test(NAME Dreamscapes.NativeReviewQueueProductSession COMMAND DreamscapesNativeReviewQueueProductSessionTests)
        set_tests_properties(Dreamscapes.NativeReviewQueueProductSession PROPERTIES
            ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
    endif()
    if(APPLE AND NOT IOS AND CMAKE_OBJCXX_COMPILER AND TARGET DreamscapesReviewImagesNative)
        add_library(DreamscapesReviewQueueSurface STATIC
            src/App/Views/Result/NativeReviewQueueSurface.h
            src/App/Views/Result/NativeReviewQueueSurface.mm)
        set_target_properties(DreamscapesReviewQueueSurface PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
            OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(DreamscapesReviewQueueSurface PRIVATE -fobjc-arc)
        target_compile_features(DreamscapesReviewQueueSurface PUBLIC cxx_std_23)
        target_link_libraries(DreamscapesReviewQueueSurface PUBLIC DreamscapesReviewQueueNative
            PRIVATE DreamscapesNativeEmbeddedSurface DreamscapesReviewImagesNative Qt6::Core "-framework AppKit")
        if(TARGET Dreamscapes)
            set_target_properties(Dreamscapes PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
            target_sources(Dreamscapes PRIVATE src/App/Views/Result/NativeReviewQueuePresenterMac.h
                src/App/Views/Result/NativeReviewQueuePresenterMac.mm)
            set_source_files_properties(src/App/Views/Result/NativeReviewQueuePresenterMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
            target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_REVIEW_QUEUE_PRESENTER=1)
            target_link_libraries(Dreamscapes PRIVATE DreamscapesReviewQueuePresenter DreamscapesReviewQueueSurface "-framework AppKit")
        endif()
        if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
            add_executable(DreamscapesNativeReviewQueueSurfaceMacTests tests/tst_native_review_queue_surface_mac.mm)
            set_target_properties(DreamscapesNativeReviewQueueSurfaceMacTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
                OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
            target_compile_options(DreamscapesNativeReviewQueueSurfaceMacTests PRIVATE -fobjc-arc)
            target_link_libraries(DreamscapesNativeReviewQueueSurfaceMacTests PRIVATE DreamscapesReviewQueueSurface Qt6::Core "-framework AppKit")
            add_test(NAME Dreamscapes.NativeReviewQueueSurfaceMac COMMAND DreamscapesNativeReviewQueueSurfaceMacTests
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/Appicon/generated/android/res/mipmap-mdpi/ic_launcher.png")
            set_tests_properties(Dreamscapes.NativeReviewQueueSurfaceMac PROPERTIES TIMEOUT 90)
            add_executable(DreamscapesNativeReviewQueueSurfaceLifetimeMacTests tests/tst_native_review_queue_surface_lifetime_mac.mm)
            set_target_properties(DreamscapesNativeReviewQueueSurfaceLifetimeMacTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
                OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
            target_compile_options(DreamscapesNativeReviewQueueSurfaceLifetimeMacTests PRIVATE -fobjc-arc)
            target_link_libraries(DreamscapesNativeReviewQueueSurfaceLifetimeMacTests PRIVATE DreamscapesReviewQueueSurface Qt6::Core "-framework AppKit")
            add_test(NAME Dreamscapes.NativeReviewQueueSurfaceLifetimeMac COMMAND DreamscapesNativeReviewQueueSurfaceLifetimeMacTests
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/Appicon/generated/android/res/mipmap-mdpi/ic_launcher.png")
            set_tests_properties(Dreamscapes.NativeReviewQueueSurfaceLifetimeMac PROPERTIES TIMEOUT 90)
        endif()
    endif()
endif()
