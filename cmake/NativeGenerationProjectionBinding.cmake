add_library(DreamscapesNativeGenerationProjectionBinding STATIC
    src/App/ViewModels/GenerationProjectionBinding.h
    src/App/ViewModels/GenerationProjectionBinding.cpp)
set_target_properties(DreamscapesNativeGenerationProjectionBinding PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_features(DreamscapesNativeGenerationProjectionBinding PUBLIC cxx_std_23)
target_link_libraries(DreamscapesNativeGenerationProjectionBinding PUBLIC DreamscapesGeneration)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(DreamscapesNativeGenerationProjectionBindingTests tests/tst_native_generation_projection_binding.cpp)
    set_target_properties(DreamscapesNativeGenerationProjectionBindingTests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_definitions(DreamscapesNativeGenerationProjectionBindingTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/resources")
    target_link_libraries(DreamscapesNativeGenerationProjectionBindingTests PRIVATE
        DreamscapesNativeGenerationProjectionBinding DreamscapesHomeQueueBridge DreamscapesReviewGenerationBridge)
    add_test(NAME Dreamscapes.NativeGenerationProjectionBinding COMMAND DreamscapesNativeGenerationProjectionBindingTests)
    set_tests_properties(Dreamscapes.NativeGenerationProjectionBinding PROPERTIES
        ENVIRONMENT "TMPDIR=${CMAKE_CURRENT_SOURCE_DIR}/resources;SOCIETY_DISABLE_SESSION_RESTORE=1;IILOCALLLM_DISABLE_APP_MCP=1")
endif()
