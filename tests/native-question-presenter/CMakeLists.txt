cmake_minimum_required(VERSION 3.31)
project(DreamscapesNativeQuestionPresenterTests LANGUAGES CXX)
include(CTest)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui)
find_package(iiSDKCore 0.18.1 CONFIG REQUIRED COMPONENTS Runtime)
include(../../cmake/QtApple.cmake)
find_package(iiMultimodalAgent 0.1 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiGUI 0.5 CONFIG REQUIRED)
lvui_generate(NAME UserQuestionsSheet SOURCE "${IIMULTIMODALAGENT_VIEWS_DIR}/UserQuestionsSheet.lvui"
    FRAMEWORK iiGUI INCLUDES iiMultimodalAgent/agent/NativeQuestionViewModel.h OUTPUT_VARIABLE generated)
add_executable(DreamscapesNativeQuestionPresenterTests presenter.cpp
    ../../src/App/ApplicationShutdownCoordinator.h ../../src/App/ApplicationShutdownCoordinator.cpp
    ../../src/App/Mcp/NativeQuestionPresenter.cpp ../../src/App/Mcp/NativeQuestionPresenter.h
    ../../src/App/Native/NativeOverlayCoordinator.cpp ../../src/App/Native/NativeOverlayCoordinator.h
    ../../src/App/Native/ExternalOverlaySurface.cpp ../../src/App/Native/ExternalOverlaySurface.h "${generated}")
target_compile_features(DreamscapesNativeQuestionPresenterTests PRIVATE cxx_std_23)
target_include_directories(DreamscapesNativeQuestionPresenterTests PRIVATE ../../src "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesNativeQuestionPresenterTests PRIVATE Qt6::Core Qt6::Gui
    iiMultimodalAgent::iiMultimodalAgent LVUI::Native iiGUI::iiGUI iiSDKCore::Runtime)
add_test(NAME Dreamscapes.NativeQuestionPresenterAdmission COMMAND DreamscapesNativeQuestionPresenterTests)
set_tests_properties(Dreamscapes.NativeQuestionPresenterAdmission PROPERTIES TIMEOUT 30)
if(APPLE)
    set_tests_properties(Dreamscapes.NativeQuestionPresenterAdmission PROPERTIES ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:;QT_QPA_PLATFORM=set:offscreen")
endif()
