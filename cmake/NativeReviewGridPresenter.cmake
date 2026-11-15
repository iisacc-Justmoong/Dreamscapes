if(TARGET DreamscapesReviewGalleryNative AND TARGET DreamscapesNativePanelHost AND TARGET DreamscapesGeneration)
    add_library(DreamscapesReviewGridPresenter STATIC
        src/App/Views/Result/NativeReviewGridPresenter.h src/App/Views/Result/NativeReviewGridPresenter.cpp)
    set_target_properties(DreamscapesReviewGridPresenter PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesReviewGridPresenter PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesReviewGridPresenter PUBLIC
        DreamscapesReviewGalleryNative DreamscapesNativePanelHost DreamscapesGeneration)
    if(APPLE AND NOT IOS AND CMAKE_OBJCXX_COMPILER AND TARGET Dreamscapes AND
       TARGET DreamscapesNativeEmbeddedSurface AND TARGET DreamscapesNativeFileImages)
        target_sources(Dreamscapes PRIVATE src/App/Views/Result/NativeReviewGridPresenterMac.h
            src/App/Views/Result/NativeReviewGridPresenterMac.mm)
        set_source_files_properties(src/App/Views/Result/NativeReviewGridPresenterMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_REVIEW_GRID_PRESENTER=1)
        target_link_libraries(Dreamscapes PRIVATE DreamscapesReviewGridPresenter
            DreamscapesNativeEmbeddedSurface DreamscapesNativeFileImages "-framework AppKit")
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(DreamscapesNativeReviewGridPresenterTests tests/tst_native_review_grid_presenter.cpp)
        set_target_properties(DreamscapesNativeReviewGridPresenterTests PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeReviewGridPresenterTests PRIVATE DreamscapesReviewGridPresenter)
        add_test(NAME Dreamscapes.NativeReviewGridPresenter COMMAND DreamscapesNativeReviewGridPresenterTests)
        set_tests_properties(Dreamscapes.NativeReviewGridPresenter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
