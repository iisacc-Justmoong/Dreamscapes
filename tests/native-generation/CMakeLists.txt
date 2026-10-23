cmake_minimum_required(VERSION 3.24)
project(DreamscapesNativeGenerationBoundary LANGUAGES CXX)
include(CTest)
find_package(iiImageCodec 0.1 CONFIG REQUIRED)
find_package(iiFileProviderNative 0.8 CONFIG REQUIRED)
find_path(IILOCALDIFFUSION_INCLUDE_DIRECTORY Generation/NativeDiffusion.hpp REQUIRED)
add_executable(DreamscapesNativeGenerationBoundary ../tst_nativegeneration.cpp
    ../../src/App/Generation/NativeGenerationWork.cpp)
target_include_directories(DreamscapesNativeGenerationBoundary PRIVATE
    ../../src/App/Generation "${IILOCALDIFFUSION_INCLUDE_DIRECTORY}")
target_link_libraries(DreamscapesNativeGenerationBoundary PRIVATE iiImageCodec::iiImageCodec iiFileProvider::Native)
target_compile_features(DreamscapesNativeGenerationBoundary PRIVATE cxx_std_23)
add_test(NAME Dreamscapes.NativeGenerationBoundary COMMAND DreamscapesNativeGenerationBoundary "${CMAKE_CURRENT_BINARY_DIR}")
