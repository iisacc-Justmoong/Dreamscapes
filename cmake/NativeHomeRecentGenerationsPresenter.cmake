if(TARGET DreamscapesNativePanelHost AND TARGET DreamscapesHomeRecentGenerationsSession)
    add_library(DreamscapesHomeRecentGenerationsPresenter STATIC
        src/App/Views/Home/NativeHomeRecentGenerationsPresenter.h src/App/Views/Home/NativeHomeRecentGenerationsPresenter.cpp)
    set_target_properties(DreamscapesHomeRecentGenerationsPresenter PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesHomeRecentGenerationsPresenter PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesHomeRecentGenerationsPresenter PUBLIC DreamscapesNativePanelHost DreamscapesHomeRecentGenerationsSession)
    if(APPLE AND NOT IOS AND TARGET DreamscapesNativeEmbeddedSurface AND TARGET DreamscapesNativeFileImages AND TARGET Dreamscapes)
        set_target_properties(Dreamscapes PROPERTIES OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
        target_sources(Dreamscapes PRIVATE src/App/Views/Home/NativeHomeRecentGenerationsPresenterMac.h
            src/App/Views/Home/NativeHomeRecentGenerationsPresenterMac.mm)
        set_source_files_properties(src/App/Views/Home/NativeHomeRecentGenerationsPresenterMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_HOME_RECENT_GENERATIONS_PRESENTER=1)
        target_link_libraries(Dreamscapes PRIVATE DreamscapesHomeRecentGenerationsPresenter DreamscapesNativeEmbeddedSurface
            DreamscapesNativeFileImages "-framework AppKit")
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_executable(DreamscapesNativeHomeRecentGenerationsPresenterTests tests/tst_native_home_recent_generations_presenter.cpp)
        set_target_properties(DreamscapesNativeHomeRecentGenerationsPresenterTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeHomeRecentGenerationsPresenterTests PRIVATE DreamscapesHomeRecentGenerationsPresenter)
        add_test(NAME Dreamscapes.NativeHomeRecentGenerationsPresenter COMMAND DreamscapesNativeHomeRecentGenerationsPresenterTests)
        set_tests_properties(Dreamscapes.NativeHomeRecentGenerationsPresenter PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources")
    endif()
endif()
