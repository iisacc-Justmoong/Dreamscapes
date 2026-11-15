if(NOT TARGET DreamscapesNativeOverlay)
    add_library(DreamscapesNativeOverlay STATIC
        src/App/Native/NativeOverlayCoordinator.h src/App/Native/NativeOverlayCoordinator.cpp
        src/App/Native/ExternalOverlaySurface.h src/App/Native/ExternalOverlaySurface.cpp)
    set_target_properties(DreamscapesNativeOverlay PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesNativeOverlay PUBLIC cxx_std_23)
    target_include_directories(DreamscapesNativeOverlay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(DreamscapesNativeOverlay PUBLIC iiGUI::iiGUI Qt6::Core Qt6::Gui)
endif()
target_link_libraries(Dreamscapes PRIVATE DreamscapesNativeOverlay)
