# The app composes the module's declaration, bridge and iiGUI host. The SDK
# modules remain independently buildable and do not acquire peer dependencies.
if(NOT IIMULTIMODALAGENT_VIEWS_DIR OR NOT EXISTS "${IIMULTIMODALAGENT_VIEWS_DIR}/UserQuestionsSheet.lvui")
    message(FATAL_ERROR "The selected iiMultimodalAgent package has no UserQuestionsSheet.lvui view")
endif()
lvui_generate(NAME UserQuestionsSheet SOURCE "${IIMULTIMODALAGENT_VIEWS_DIR}/UserQuestionsSheet.lvui"
    FRAMEWORK iiGUI INCLUDES iiMultimodalAgent/agent/NativeQuestionViewModel.h
    OUTPUT_VARIABLE _dreamscapes_native_question_view)
include(cmake/NativeOverlay.cmake)
target_sources(Dreamscapes PRIVATE
    src/App/Mcp/NativeQuestionPresenter.h src/App/Mcp/NativeQuestionPresenter.cpp
    "${_dreamscapes_native_question_view}")
target_include_directories(Dreamscapes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Dreamscapes PRIVATE LVUI::Native iiGUI::iiGUI Qt6::Gui)
target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_QUESTION_PRESENTER=1)
