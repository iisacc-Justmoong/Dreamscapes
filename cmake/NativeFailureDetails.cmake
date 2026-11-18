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

# The application host and queue projection are compiled by NativeViewApp.
