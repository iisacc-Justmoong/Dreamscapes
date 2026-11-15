cmake_minimum_required(VERSION 3.24)
project(DreamscapesQuestionCoordinatorTests LANGUAGES CXX)
include(CTest)
add_executable(DreamscapesQuestionCoordinatorTests
    coordinator.cpp ../../src/App/Native/NativeOverlayCoordinator.cpp)
target_compile_features(DreamscapesQuestionCoordinatorTests PRIVATE cxx_std_23)
target_include_directories(DreamscapesQuestionCoordinatorTests PRIVATE ../../src)
add_test(NAME Dreamscapes.QuestionOverlayCoordinator COMMAND DreamscapesQuestionCoordinatorTests)
set_tests_properties(Dreamscapes.QuestionOverlayCoordinator PROPERTIES TIMEOUT 30)
