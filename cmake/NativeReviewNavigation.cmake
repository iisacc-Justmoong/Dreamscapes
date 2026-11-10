# The application composes LVUI's collection contract with iiGUI navigation.
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native HINTS "$ENV{HOME}/.local/SDK/LVUI")
find_package(iiGUI 0.5.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiGUI")
add_library(DreamscapesReviewNavigationNative STATIC
    src/App/Views/Result/NativeReviewNavigation.h
    src/App/Views/Result/NativeReviewNavigation.cpp)
set_target_properties(DreamscapesReviewNavigationNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesReviewNavigationNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesReviewNavigationNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DreamscapesReviewNavigationNative PUBLIC DreamscapesViewModelsNative LVUI::Native iiGUI::iiGUI)

# This isolated LVUI result view remains separate from the production entry
# point until its data sources and all remaining product flows are connected.
lvui_generate(NAME NativeReviewGallery SOURCE src/App/Views/Result/NativeReviewGallery.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Result/NativeReviewGallery.h OUTPUT_VARIABLE _native_review_gallery)
add_library(DreamscapesReviewGalleryNative STATIC
    src/App/Views/Result/NativeReviewGallery.h
    src/App/Views/Result/NativeReviewGallery.cpp
    src/App/Views/Result/NativeReviewGalleryView.h
    src/App/Views/Result/NativeReviewGalleryView.cpp
    "${_native_review_gallery}")
set_target_properties(DreamscapesReviewGalleryNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesReviewGalleryNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesReviewGalleryNative PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesReviewGalleryNative PUBLIC DreamscapesReviewNavigationNative iiGUI::iiGUI)

option(DREAMSCAPES_NATIVE_REVIEW_IMAGES "Build the native result-preview provider" ON)
if(DREAMSCAPES_NATIVE_REVIEW_IMAGES)
    find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS ImageIO HINTS "$ENV{HOME}/.local/SDK/LVUI")
    find_package(iiSharedCanvasNative 0.35 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSharedCanvas")
    find_package(iiVisualCodec CONFIG REQUIRED COMPONENTS Image HINTS "$ENV{HOME}/.local/SDK/iiVisualCodec")
    find_package(iiNetwork CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiNetwork")
    find_package(iiFileProviderNative CONFIG REQUIRED HINTS
        "$ENV{HOME}/.local/SDK/iiFileProvider" "$ENV{HOME}/.local/SDK/iiFileProviderNative")
    include(CheckCXXSourceCompiles)
    include(CMakePushCheckState)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_LIBRARIES iiSDKCore::Runtime)
    unset(DREAMSCAPES_IISDKCORE_SIGNAL_API CACHE)
    check_cxx_source_compiles([=[
        #include <iiSDKCore/Runtime/EventLoop.h>
        template<class T> concept SignalSender = requires(const T& sender,
                ii::runtime::Task task, ii::runtime::TaskToken token, ii::runtime::Clock::time_point time) {
            sender.on_signal(task);
            sender.signal(token, time);
        };
        static_assert(SignalSender<ii::runtime::EventLoop::Sender>);
        int main() { return 0; }
    ]=] DREAMSCAPES_IISDKCORE_SIGNAL_API)
    cmake_pop_check_state()
    if(NOT DREAMSCAPES_IISDKCORE_SIGNAL_API)
        message(FATAL_ERROR "The LVUI image adapter requires an iiSDKCore Runtime with EventLoop::Sender on_signal and signal")
    endif()
    # ImageIO supplies only the typed resolver contract. Compose LVUI's
    # separately replaceable application adapter with the chosen SDKs.
    if(NOT TARGET ApplicationImageIOAdapter)
        get_filename_component(_lvui_package_prefix "${LVUI_DIR}/../../.." ABSOLUTE)
        set(_lvui_image_adapter "${_lvui_package_prefix}/share/LVUI/examples/Integrations")
        if(NOT EXISTS "${_lvui_image_adapter}/CMakeLists.txt")
            message(FATAL_ERROR "The selected LVUI package must include its application image adapter")
        endif()
        function(dreamscapes_add_image_adapter)
            set(APPLICATION_COLOR_ADAPTER OFF)
            set(APPLICATION_IMAGE_ADAPTER ON)
            add_subdirectory("${_lvui_image_adapter}" "${CMAKE_CURRENT_BINARY_DIR}/native-lvui-image-adapter" EXCLUDE_FROM_ALL)
        endfunction()
        dreamscapes_add_image_adapter()
        set_target_properties(ApplicationImageIOAdapter PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    endif()
    add_library(DreamscapesReviewImagesNative STATIC
        src/App/Views/Result/NativeReviewImages.h
        src/App/Views/Result/NativeReviewImages.cpp)
    set_target_properties(DreamscapesReviewImagesNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesReviewImagesNative PUBLIC cxx_std_23)
    target_include_directories(DreamscapesReviewImagesNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(DreamscapesReviewImagesNative PUBLIC LVUI::ImageIO iiGUI::iiGUI
        PRIVATE ApplicationImageIOAdapter iiSharedCanvas::Native iiVisualCodec::Image iiNetwork::iiNetwork iiFileProvider::Native)
endif()

# The existing generation controller still publishes Qt values. Keep its
# temporary transport adapter outside the Qt-free LVUI view and model.
if(TARGET DreamscapesViewModels)
    add_library(DreamscapesReviewGenerationBridge STATIC
        src/App/Views/Result/NativeReviewGenerationBridge.h
        src/App/Views/Result/NativeReviewGenerationBridge.cpp)
    set_target_properties(DreamscapesReviewGenerationBridge PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesReviewGenerationBridge PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesReviewGenerationBridge PUBLIC DreamscapesReviewGalleryNative DreamscapesViewModels)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeReviewNavigationTests tests/tst_native_review_navigation.cpp)
    set_target_properties(DreamscapesNativeReviewNavigationTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeReviewNavigationTests PRIVATE DreamscapesReviewNavigationNative)
    add_test(NAME Dreamscapes.NativeReviewNavigation COMMAND DreamscapesNativeReviewNavigationTests)
    add_executable(DreamscapesNativeReviewGalleryTests tests/tst_native_review_gallery.cpp)
    set_target_properties(DreamscapesNativeReviewGalleryTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeReviewGalleryTests PRIVATE DreamscapesReviewGalleryNative)
    add_test(NAME Dreamscapes.NativeReviewGallery COMMAND DreamscapesNativeReviewGalleryTests)
    add_executable(DreamscapesNativeReviewGalleryLinkTests tests/tst_native_review_gallery_link.cpp)
    set_target_properties(DreamscapesNativeReviewGalleryLinkTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(DreamscapesNativeReviewGalleryLinkTests PRIVATE DreamscapesReviewGalleryNative)
    add_test(NAME Dreamscapes.NativeReviewGalleryLink COMMAND DreamscapesNativeReviewGalleryLinkTests)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Dreamscapes.NativeReviewRendererBoundary
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
            "${_native_review_gallery}" "$<TARGET_FILE:DreamscapesNativeReviewGalleryLinkTests>")
    if(TARGET DreamscapesReviewGenerationBridge)
        add_executable(DreamscapesNativeReviewGenerationBridgeTests tests/tst_native_review_generation_bridge.cpp)
        set_target_properties(DreamscapesNativeReviewGenerationBridgeTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_link_libraries(DreamscapesNativeReviewGenerationBridgeTests PRIVATE DreamscapesReviewGenerationBridge)
        add_test(NAME Dreamscapes.NativeReviewGenerationBridge COMMAND DreamscapesNativeReviewGenerationBridgeTests)
    endif()
    if(TARGET DreamscapesFailureDetailsBridge)
        qt_add_executable(DreamscapesNativeFailureBridgeTests tests/tst_native_failure_bridge.cpp)
        target_compile_definitions(DreamscapesNativeFailureBridgeTests PRIVATE
            DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeFailureBridgeTests PRIVATE DreamscapesFailureDetailsBridge DreamscapesGeneration)
        add_test(NAME Dreamscapes.NativeFailureBridge COMMAND DreamscapesNativeFailureBridgeTests)
        set_tests_properties(Dreamscapes.NativeFailureBridge PROPERTIES
            ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
    endif()
    if(TARGET DreamscapesFailurePresenter AND CMAKE_OBJCXX_COMPILER)
        qt_add_executable(DreamscapesNativeFailurePresenterMacTests tests/tst_native_failure_presenter_mac.mm)
        set_target_properties(DreamscapesNativeFailurePresenterMacTests PROPERTIES MACOSX_BUNDLE OFF
            OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
        target_compile_options(DreamscapesNativeFailurePresenterMacTests PRIVATE -fobjc-arc)
        target_compile_definitions(DreamscapesNativeFailurePresenterMacTests PRIVATE
            DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
        target_link_libraries(DreamscapesNativeFailurePresenterMacTests PRIVATE DreamscapesFailurePresenter
            DreamscapesGeneration "-framework AppKit")
        add_test(NAME Dreamscapes.NativeFailurePresenterMac COMMAND DreamscapesNativeFailurePresenterMacTests)
        set_tests_properties(Dreamscapes.NativeFailurePresenterMac PROPERTIES TIMEOUT 45
            ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
    endif()
    # The private harness must match the selected iiGUI binary. It is opt-in
    # because installed iiGUI packages intentionally omit internal headers.
    set(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR "" CACHE PATH
        "Matching iiGUI build/include directory for the optional headless widget test")
    if(DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR AND EXISTS "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}/iiGUI/WidgetState.h")
        add_executable(DreamscapesNativeFailureDetailsTests tests/tst_native_failure_details.cpp)
        set_target_properties(DreamscapesNativeFailureDetailsTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeFailureDetailsTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeFailureDetailsTests PRIVATE DreamscapesFailureDetailsNative)
        add_test(NAME Dreamscapes.NativeFailureDetails COMMAND DreamscapesNativeFailureDetailsTests)
        add_test(NAME Dreamscapes.NativeFailureRendererBoundary
            COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_failure_renderer_boundary.py"
                "${_native_failure_details}" "$<TARGET_FILE:DreamscapesNativeFailureDetailsTests>")
        if(APPLE AND NOT IOS AND CMAKE_OBJCXX_COMPILER)
            add_executable(DreamscapesNativeFailureClipboardMacTests tests/tst_native_failure_clipboard_mac.mm)
            set_target_properties(DreamscapesNativeFailureClipboardMacTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
                OBJCXX_STANDARD 23 OBJCXX_STANDARD_REQUIRED YES)
            target_include_directories(DreamscapesNativeFailureClipboardMacTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_compile_options(DreamscapesNativeFailureClipboardMacTests PRIVATE -fobjc-arc)
            target_link_libraries(DreamscapesNativeFailureClipboardMacTests PRIVATE DreamscapesFailureDetailsNative "-framework AppKit")
            add_test(NAME Dreamscapes.NativeFailureClipboardMac COMMAND DreamscapesNativeFailureClipboardMacTests)
        endif()
        add_executable(DreamscapesNativeReviewGalleryWidgetTests tests/tst_native_review_gallery_widget.cpp)
        set_target_properties(DreamscapesNativeReviewGalleryWidgetTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
        target_include_directories(DreamscapesNativeReviewGalleryWidgetTests PRIVATE
            "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
        target_link_libraries(DreamscapesNativeReviewGalleryWidgetTests PRIVATE DreamscapesReviewGalleryNative)
        add_test(NAME Dreamscapes.NativeReviewGalleryWidget COMMAND DreamscapesNativeReviewGalleryWidgetTests)
        if(TARGET DreamscapesViewModels)
            qt_add_executable(DreamscapesNativeReviewSharedSessionTests tests/tst_native_review_shared_session.cpp
                src/App/Views/Result/GenerationReviewModel.h src/App/Views/Result/GenerationReviewModel.cpp)
            target_include_directories(DreamscapesNativeReviewSharedSessionTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_link_libraries(DreamscapesNativeReviewSharedSessionTests PRIVATE DreamscapesReviewGalleryNative Qt6::Core)
            add_test(NAME Dreamscapes.NativeReviewSharedSession COMMAND DreamscapesNativeReviewSharedSessionTests)
        endif()
        if(TARGET DreamscapesReviewImagesNative)
            find_package(SQLite3 REQUIRED)
            add_executable(DreamscapesNativeReviewImageIOTests tests/tst_native_review_gallery_imageio.cpp)
            set_target_properties(DreamscapesNativeReviewImageIOTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
            target_include_directories(DreamscapesNativeReviewImageIOTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_link_libraries(DreamscapesNativeReviewImageIOTests PRIVATE DreamscapesReviewGalleryNative DreamscapesReviewImagesNative)
            add_test(NAME Dreamscapes.NativeReviewImageIO COMMAND DreamscapesNativeReviewImageIOTests
                "${CMAKE_CURRENT_SOURCE_DIR}/resources/Appicon/generated/android/res/mipmap-mdpi/ic_launcher.png")
            add_test(NAME Dreamscapes.NativeReviewImageIORendererBoundary
                COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                    "${_native_review_gallery}" "$<TARGET_FILE:DreamscapesNativeReviewImageIOTests>")
            add_executable(DreamscapesNativeReviewIiscPreviewTests tests/tst_native_review_iisc_preview.cpp)
            set_target_properties(DreamscapesNativeReviewIiscPreviewTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
            target_include_directories(DreamscapesNativeReviewIiscPreviewTests PRIVATE "${DREAMSCAPES_IIGUI_TEST_INCLUDE_DIR}")
            target_link_libraries(DreamscapesNativeReviewIiscPreviewTests PRIVATE DreamscapesReviewGalleryNative
                DreamscapesReviewImagesNative iiSharedCanvas::Native iiNetwork::iiNetwork iiFileProvider::Native SQLite3::SQLite3)
            add_test(NAME Dreamscapes.NativeReviewIiscPreview COMMAND DreamscapesNativeReviewIiscPreviewTests
                "${CMAKE_CURRENT_SOURCE_DIR}/resources")
            set_tests_properties(Dreamscapes.NativeReviewIiscPreview PROPERTIES
                ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources")
            add_test(NAME Dreamscapes.NativeReviewIiscRendererBoundary
                COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_review_renderer_boundary.py"
                    "${_native_review_gallery}" "$<TARGET_FILE:DreamscapesNativeReviewIiscPreviewTests>")
        endif()
    endif()
endif()
