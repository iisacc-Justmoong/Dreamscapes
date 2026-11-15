if(TARGET DreamscapesNativePanelHost AND TARGET DreamscapesHomeSelectedFileSession)
    add_library(DreamscapesHomeSelectedFilePresenter STATIC
        src/App/Views/Home/NativeHomeSelectedFilePresenter.h src/App/Views/Home/NativeHomeSelectedFilePresenter.cpp)
    set_target_properties(DreamscapesHomeSelectedFilePresenter PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeSelectedFilePresenter PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeSelectedFilePresenter PUBLIC DreamscapesNativePanelHost
        DreamscapesHomeSelectedFileSession)
    if(APPLE AND NOT IOS AND TARGET DreamscapesNativeEmbeddedSurface AND TARGET DreamscapesNativeFileImages AND TARGET Dreamscapes)
        set_target_properties(Dreamscapes PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
        target_sources(Dreamscapes PRIVATE src/App/Views/Home/NativeHomeSelectedFilePresenterMac.h
            src/App/Views/Home/NativeHomeSelectedFilePresenterMac.mm)
        set_source_files_properties(src/App/Views/Home/NativeHomeSelectedFilePresenterMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_HOME_SELECTED_FILE_PRESENTER=1)
        target_link_libraries(Dreamscapes PRIVATE DreamscapesHomeSelectedFilePresenter DreamscapesNativeEmbeddedSurface
            DreamscapesNativeFileImages "-framework AppKit")
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(DreamscapesNativeHomeSelectedFilePresenterTests tests/tst_native_home_selected_file_presenter.cpp)
        set_target_properties(DreamscapesNativeHomeSelectedFilePresenterTests PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
        target_compile_definitions(DreamscapesNativeHomeSelectedFilePresenterTests PRIVATE
            DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeHomeSelectedFilePresenterTests PRIVATE DreamscapesHomeSelectedFilePresenter)
        add_test(NAME Dreamscapes.NativeHomeSelectedFilePresenter COMMAND DreamscapesNativeHomeSelectedFilePresenterTests)
        set_tests_properties(Dreamscapes.NativeHomeSelectedFilePresenter PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources")
    endif()
endif()
