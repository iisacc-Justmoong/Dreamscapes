cmake_minimum_required(VERSION 3.31)
project(Dreamscapes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)

# Keep every product dependency required, including packages not installed yet.
# Future packages are expected to export <Package>::<Package> targets.
find_package(iiCSMIDI CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiCSMIDI")
find_package(iiLicenseManager CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLicenseManager")
find_package(iiLocalDiffusion CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLocalDiffusion")
find_package(iiPaintEngine CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiPaintEngine")
find_package(iiSocietyContainer CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")
find_package(iiSocietyHelper CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyHelper")
find_package(iiSocietySync CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietySync")
find_package(iiUpdateManager CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiUpdateManager")
find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/LVRS")
qt_standard_project_setup(REQUIRES 6.8)

# Keep build-tree launches independent of shell-only DYLD_LIBRARY_PATH settings.
if(APPLE AND NOT IOS)
    list(APPEND CMAKE_BUILD_RPATH "$<TARGET_FILE_DIR:LVRS::LVRS>")
endif()

lvrs_add_qml_app(
    TARGET Dreamscapes
    URI Dreamscapes
    VERSION 1.0
    SOURCES main.cpp
    QML_FILES
        App/Main.qml
        App/Views/Desktop.qml
        App/Views/Mobile.qml
        App/Views/Home/QuickGenerate.qml
    NO_PLATFORM_RUNTIME_TARGETS
)

set(dreamscapes_product_dependencies
    iiCSMIDI::iiCSMIDI
    iiLicenseManager::iiLicenseManager
    iiLocalDiffusion::iiLocalDiffusion
    iiPaintEngine::iiPaintEngine
    iiSocietyContainer::iiSocietyContainer
    iiSocietyHelper::iiSocietyHelper
    iiSocietySync::iiSocietySync
    iiUpdateManager::iiUpdateManager
    LVRS::LVRS
)
target_link_libraries(Dreamscapes PRIVATE ${dreamscapes_product_dependencies})

if(ANDROID)
    foreach(dependency IN LISTS dreamscapes_product_dependencies)
        set_property(TARGET Dreamscapes APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
            "$<TARGET_FILE:${dependency}>")
    endforeach()
endif()

# The installed iOS archive has QML registration/resources but no plugin archive.
# Retain its startup objects even when no C++ symbol directly references them.
get_target_property(dreamscapes_lvrs_type LVRS::LVRS TYPE)
if(IOS AND dreamscapes_lvrs_type STREQUAL "STATIC_LIBRARY")
    set_property(TARGET Dreamscapes PROPERTY LINK_LIBRARY_OVERRIDE "WHOLE_ARCHIVE,LVRS::LVRS")
endif()

set_target_properties(Dreamscapes PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.iisacc.dreamscapes"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    QT_ANDROID_PACKAGE_NAME "com.iisacc.dreamscapes"
)

include(CTest)
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    qt_add_executable(DreamscapesGuiTests App/tst_Gui.cpp)
    target_link_libraries(DreamscapesGuiTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2)
    target_compile_definitions(DreamscapesGuiTests PRIVATE
        DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/App"
        DREAMSCAPES_EXECUTABLE_PATH="$<TARGET_FILE:Dreamscapes>"
    )
    add_dependencies(DreamscapesGuiTests Dreamscapes)
    lvrs_configure_qml_app(DreamscapesGuiTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.Gui COMMAND DreamscapesGuiTests)
    set_tests_properties(Dreamscapes.Gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 30
    )
endif()
