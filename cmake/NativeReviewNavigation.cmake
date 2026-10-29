# Native view bindings consume LVUI's public C++ collection interface.
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native HINTS "$ENV{HOME}/.local/SDK/LVUI")
add_library(DreamscapesReviewNavigationNative STATIC
    src/App/Views/Result/NativeReviewNavigation.h
    src/App/Views/Result/NativeReviewNavigation.cpp)
set_target_properties(DreamscapesReviewNavigationNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesReviewNavigationNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesReviewNavigationNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DreamscapesReviewNavigationNative PUBLIC DreamscapesViewModelsNative LVUI::Native)

# This builds an isolated, reusable LVUI result view; it does not replace the
# application entry point until the remaining product flows are connected.
find_package(LVRS 1 CONFIG REQUIRED COMPONENTS Native HINTS "$ENV{HOME}/.local/SDK/LVRS")
lvui_generate(NAME NativeReviewGallery SOURCE src/App/Views/Result/NativeReviewGallery.lvui
    FRAMEWORK LVRS INCLUDES App/Views/Result/NativeReviewGallery.h OUTPUT_VARIABLE _native_review_gallery)
add_library(DreamscapesReviewGalleryNative STATIC
    src/App/Views/Result/NativeReviewGallery.h
    src/App/Views/Result/NativeReviewGallery.cpp
    src/App/Views/Result/NativeReviewGalleryView.h
    src/App/Views/Result/NativeReviewGalleryView.cpp
    "${_native_review_gallery}")
set_target_properties(DreamscapesReviewGalleryNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesReviewGalleryNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesReviewGalleryNative PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesReviewGalleryNative PUBLIC DreamscapesReviewNavigationNative LVRS::LVRS)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeReviewNavigationTests tests/tst_native_review_navigation.cpp)
    target_link_libraries(DreamscapesNativeReviewNavigationTests PRIVATE DreamscapesReviewNavigationNative)
    add_test(NAME Dreamscapes.NativeReviewNavigation COMMAND DreamscapesNativeReviewNavigationTests)
    add_executable(DreamscapesNativeReviewGalleryTests tests/tst_native_review_gallery.cpp)
    target_link_libraries(DreamscapesNativeReviewGalleryTests PRIVATE DreamscapesReviewGalleryNative)
    add_test(NAME Dreamscapes.NativeReviewGallery COMMAND DreamscapesNativeReviewGalleryTests)
    add_executable(DreamscapesNativeReviewGalleryLinkTests tests/tst_native_review_gallery_link.cpp)
    target_link_libraries(DreamscapesNativeReviewGalleryLinkTests PRIVATE DreamscapesReviewGalleryNative)
    add_test(NAME Dreamscapes.NativeReviewGalleryLink COMMAND DreamscapesNativeReviewGalleryLinkTests)
    # In a source workspace, iiGUI's headless test harness verifies real widget
    # interaction without creating an operating-system window.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiGUI/src/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeReviewGalleryWidgetTests tests/tst_native_review_gallery_widget.cpp)
        target_include_directories(DreamscapesNativeReviewGalleryWidgetTests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../SDK/iiGUI/src")
        target_link_libraries(DreamscapesNativeReviewGalleryWidgetTests PRIVATE DreamscapesReviewGalleryNative)
        add_test(NAME Dreamscapes.NativeReviewGalleryWidget COMMAND DreamscapesNativeReviewGalleryWidgetTests)
    endif()
endif()
