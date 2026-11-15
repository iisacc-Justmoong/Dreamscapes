if(TARGET DreamscapesReviewInspectorView AND TARGET DreamscapesReviewMediaNative AND TARGET DreamscapesNativePanelHost AND TARGET DreamscapesGeneration)
    add_library(DreamscapesReviewInspectorPresenter STATIC
        src/App/Views/Result/NativeReviewInspectorPresenter.h
        src/App/Views/Result/NativeReviewInspectorPresenter.cpp)
    set_target_properties(DreamscapesReviewInspectorPresenter PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesReviewInspectorPresenter PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesReviewInspectorPresenter PUBLIC
        DreamscapesReviewInspectorView DreamscapesReviewMediaNative DreamscapesNativePanelHost DreamscapesGeneration)
    if(APPLE AND NOT IOS AND CMAKE_OBJCXX_COMPILER AND TARGET Dreamscapes AND
       TARGET DreamscapesNativeEmbeddedSurface AND TARGET DreamscapesNativeFileImages AND TARGET DreamscapesReviewQtPlayback)
        target_sources(Dreamscapes PRIVATE src/App/Views/Result/NativeReviewInspectorPresenterMac.h
            src/App/Views/Result/NativeReviewInspectorPresenterMac.mm)
        set_source_files_properties(src/App/Views/Result/NativeReviewInspectorPresenterMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_REVIEW_INSPECTOR_PRESENTER=1)
        target_link_libraries(Dreamscapes PRIVATE DreamscapesReviewInspectorPresenter
            DreamscapesReviewQtPlayback DreamscapesNativeEmbeddedSurface DreamscapesNativeFileImages "-framework AppKit")
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(DreamscapesNativeReviewInspectorPresenterTests tests/tst_native_review_inspector_presenter.cpp)
        set_target_properties(DreamscapesNativeReviewInspectorPresenterTests PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeReviewInspectorPresenterTests PRIVATE DreamscapesReviewInspectorPresenter)
        add_test(NAME Dreamscapes.NativeReviewInspectorPresenter COMMAND DreamscapesNativeReviewInspectorPresenterTests)
        set_tests_properties(Dreamscapes.NativeReviewInspectorPresenter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
