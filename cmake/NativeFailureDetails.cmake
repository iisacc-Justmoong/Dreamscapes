# The native view can be built independently of the test suite or Qt backend.
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native HINTS "$ENV{HOME}/.local/SDK/LVUI")
find_package(iiGUI 0.5.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiGUI")
lvui_generate(NAME NativeFailureDetails SOURCE src/App/Views/Failure/NativeFailureDetails.lvui
    FRAMEWORK iiGUI INCLUDES App/Views/Failure/NativeFailureDetails.h OUTPUT_VARIABLE _native_failure_details)
add_library(DreamscapesFailureDetailsNative STATIC
    src/App/Views/Failure/NativeFailureDetails.h src/App/Views/Failure/NativeFailureDetails.cpp
    src/App/Views/Failure/NativeFailureDetailsView.h src/App/Views/Failure/NativeFailureDetailsView.cpp
    "${_native_failure_details}")
set_target_properties(DreamscapesFailureDetailsNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesFailureDetailsNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesFailureDetailsNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesFailureDetailsNative PUBLIC DreamscapesViewModelsNative LVUI::Native iiGUI::iiGUI)

# The Qt transport remains a backend-only transition layer and is not linked
# into the independent view. A future native app host owns the bridge.
if(TARGET DreamscapesGeneration)
    add_library(DreamscapesFailureDetailsBridge STATIC
        src/App/Generation/NativeFailureDetailsBridge.h src/App/Generation/NativeFailureDetailsBridge.cpp)
    set_target_properties(DreamscapesFailureDetailsBridge PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesFailureDetailsBridge PUBLIC cxx_std_23)
    target_link_libraries(DreamscapesFailureDetailsBridge PUBLIC DreamscapesFailureDetailsNative
        PRIVATE DreamscapesGeneration)
    if(APPLE AND NOT IOS AND TARGET Dreamscapes)
        add_library(DreamscapesFailurePresenter STATIC
            src/App/Generation/NativeFailurePresenter.h src/App/Generation/NativeFailurePresenter.cpp)
        set_target_properties(DreamscapesFailurePresenter PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
        target_compile_features(DreamscapesFailurePresenter PUBLIC cxx_std_23)
        target_link_libraries(DreamscapesFailurePresenter PUBLIC DreamscapesFailureDetailsBridge Qt6::Core
            PRIVATE DreamscapesGeneration Qt6::Gui)
        target_link_libraries(Dreamscapes PRIVATE DreamscapesFailurePresenter)
        target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_FAILURE_PRESENTER=1)
    endif()
endif()
