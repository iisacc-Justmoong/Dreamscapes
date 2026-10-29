add_library(DreamscapesViewModelsNative STATIC
    src/App/ViewModels/Native/ViewValue.cpp
    src/App/ViewModels/Native/ViewCatalog.cpp
    src/App/ViewModels/Native/VideoTimeline.cpp
    src/App/ViewModels/Native/EditorToolState.cpp
    src/App/ViewModels/Native/GenerationPresentation.cpp
    src/App/ViewModels/Native/GenerationDraft.cpp
    src/App/ViewModels/Native/GraphicDraft.cpp
    src/App/ViewModels/Native/ViewRuntime.cpp
    src/App/ViewModels/Native/ViewProjection.cpp
    src/App/ViewModels/Native/CanvasDraft.cpp
    src/App/Views/Result/NativeReviewSession.h
    src/App/Views/Result/NativeReviewSession.cpp)
set_target_properties(DreamscapesViewModelsNative PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesViewModelsNative PUBLIC cxx_std_23)
target_include_directories(DreamscapesViewModelsNative PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DreamscapesViewModelsNative PUBLIC iiSDKCore::Runtime iiJson::iiJson)
