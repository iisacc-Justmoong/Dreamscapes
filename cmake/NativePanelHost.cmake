if(TARGET Qt6::Quick)
    add_library(DreamscapesNativePanelHost STATIC
        src/App/Native/NativePanelRegion.h src/App/Native/NativePanelRegion.cpp
        src/App/Native/NativePanelOverlays.h src/App/Native/NativePanelOverlays.cpp)
    set_target_properties(DreamscapesNativePanelHost PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)
    target_compile_features(DreamscapesNativePanelHost PUBLIC cxx_std_23)
    target_include_directories(DreamscapesNativePanelHost PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(DreamscapesNativePanelHost PUBLIC Qt6::Quick)
endif()
