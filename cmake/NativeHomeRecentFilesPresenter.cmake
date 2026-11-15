if(TARGET DreamscapesNativePanelHost AND TARGET DreamscapesHomeRecentFilesSession)
    add_library(DreamscapesHomeRecentFilesPresenter STATIC
        src/App/Views/Home/NativeHomeRecentFilesPresenter.h src/App/Views/Home/NativeHomeRecentFilesPresenter.cpp)
    set_target_properties(DreamscapesHomeRecentFilesPresenter PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeRecentFilesPresenter PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeRecentFilesPresenter PUBLIC DreamscapesNativePanelHost DreamscapesHomeRecentFilesSession)
    if(APPLE AND NOT IOS AND TARGET DreamscapesNativeEmbeddedSurface AND TARGET DreamscapesNativeFileImages AND TARGET Dreamscapes)
        set_target_properties(Dreamscapes PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
        target_sources(Dreamscapes PRIVATE src/App/Views/Home/NativeHomeRecentFilesPresenterMac.h
            src/App/Views/Home/NativeHomeRecentFilesPresenterMac.mm)
        set_source_files_properties(src/App/Views/Home/NativeHomeRecentFilesPresenterMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_HOME_RECENT_FILES_PRESENTER=1)
        target_link_libraries(Dreamscapes PRIVATE DreamscapesHomeRecentFilesPresenter DreamscapesNativeEmbeddedSurface
            DreamscapesNativeFileImages "-framework AppKit")
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(DreamscapesNativeHomeRecentFilesPresenterTests tests/tst_native_home_recent_files_presenter.cpp)
        set_target_properties(DreamscapesNativeHomeRecentFilesPresenterTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeHomeRecentFilesPresenterTests PRIVATE DreamscapesHomeRecentFilesPresenter)
        add_test(NAME Dreamscapes.NativeHomeRecentFilesPresenter COMMAND DreamscapesNativeHomeRecentFilesPresenterTests)
        set_tests_properties(Dreamscapes.NativeHomeRecentFilesPresenter PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources")
    endif()
endif()
