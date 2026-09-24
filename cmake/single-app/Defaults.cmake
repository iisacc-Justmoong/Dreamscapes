# Load before project(): even CMake's compiler and IPO probes must remain plain
# executables. iOS otherwise defaults every add_executable() to a .app bundle.
set(CMAKE_MACOSX_BUNDLE OFF)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CMAKE_MACOSX_BUNDLE)
list(REMOVE_DUPLICATES CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
