cmake_minimum_required(VERSION 3.31)
project(Dreamscapes VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 QuickDialogs2 Concurrent)

# Keep every product dependency required, including packages not installed yet.
# Future packages are expected to export <Package>::<Package> targets.
find_package(iiCSMIDI CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiCSMIDI")
find_package(iiLicenseManager CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLicenseManager")
find_package(iiLocalDiffusion 0.6.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiLocalDiffusion")
find_package(iiPaintEngine CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiPaintEngine")
find_package(iiSocietyContainer 0.10.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyContainer")
find_package(iiSocietyHelper 0.5.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietyHelper")
find_package(iiUpdateManager CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiUpdateManager")
find_package(LVRS CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/LVRS")
qt_standard_project_setup(REQUIRES 6.8)
if(IOS AND NOT iiLocalDiffusion_NATIVE_DIFFUSION)
    message(FATAL_ERROR "Dreamscapes iOS requires iiLocalDiffusion with IILD_ENABLE_NATIVE_DIFFUSION=ON")
endif()
if(iiLocalDiffusion_NATIVE_DIFFUSION)
    get_filename_component(dreamscapes_diffusion_prefix "${iiLocalDiffusion_DIR}/../../.." ABSOLUTE)
endif()

# Run the existing SDK's inference launcher; no second engine or weight cache.
if(NOT IOS AND NOT ANDROID)
    find_program(DREAMSCAPES_DIFFUSION_EXECUTABLE NAMES iild-generate
        HINTS "${iiLocalDiffusion_DIR}/../../../bin")
endif()
if(NOT DREAMSCAPES_DIFFUSION_EXECUTABLE)
    set(DREAMSCAPES_DIFFUSION_EXECUTABLE "")
endif()
set(DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE "" CACHE FILEPATH "Optional Python environment for the installed iiLocalDiffusion runtime")
if(DREAMSCAPES_DIFFUSION_EXECUTABLE AND NOT CMAKE_CROSSCOMPILING)
    execute_process(COMMAND "${DREAMSCAPES_DIFFUSION_EXECUTABLE}" --help
        OUTPUT_VARIABLE diffusion_help ERROR_VARIABLE diffusion_help_error
        RESULT_VARIABLE diffusion_help_result TIMEOUT 15)
    if(NOT diffusion_help_result EQUAL 0 OR NOT diffusion_help MATCHES "--model-path" OR NOT diffusion_help MATCHES "--worker" OR NOT diffusion_help MATCHES "foreground-residency")
        message(FATAL_ERROR "The installed iild-generate is outdated or unavailable. Reinstall the current iiLocalDiffusion package and set DREAMSCAPES_DIFFUSION_EXECUTABLE to its launcher. ${diffusion_help_error}")
    endif()
    execute_process(COMMAND "${DREAMSCAPES_DIFFUSION_EXECUTABLE}" --backend local --help
        OUTPUT_VARIABLE diffusion_local_help RESULT_VARIABLE diffusion_local_result TIMEOUT 15)
    if(NOT diffusion_local_result EQUAL 0 OR NOT diffusion_local_help MATCHES "--work-dir" OR NOT diffusion_local_help MATCHES "--cache-dir" OR NOT diffusion_local_help MATCHES "--preview-dir" OR NOT diffusion_local_help MATCHES "Diffusers/PyTorch")
        message(FATAL_ERROR "The installed iiLocalDiffusion must provide the standalone Diffusers/PyTorch checkpoint runtime with --work-dir, --cache-dir and --preview-dir. Reinstall the current SDK runtime.")
    endif()
endif()
add_library(DreamscapesGeneration STATIC)
qt_add_qml_module(DreamscapesGeneration
    URI Dreamscapes.Storage VERSION 1.0 NO_PLUGIN
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Dreamscapes/Storage"
    SOURCES App/Generation/GenerationController.h App/Generation/GenerationController.cpp
        App/Views/Result/ImageFileExporter.h App/Views/Result/ImageFileExporter.cpp
        App/Views/Result/PhotoLibraryExporter.h App/Views/Result/PhotoLibraryExporter.cpp)
if(APPLE)
    enable_language(OBJCXX)
    target_sources(DreamscapesGeneration PRIVATE App/Views/Result/PhotoLibraryApple.mm)
    set_source_files_properties(App/Views/Result/PhotoLibraryApple.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(DreamscapesGeneration PRIVATE "-framework Photos" "-framework Foundation")
elseif(ANDROID)
    target_sources(DreamscapesGeneration PRIVATE App/Views/Result/PhotoLibraryAndroid.cpp)
else()
    target_sources(DreamscapesGeneration PRIVATE App/Views/Result/PhotoLibraryUnsupported.cpp)
endif()
target_link_libraries(DreamscapesGeneration PUBLIC Qt6::Gui Qt6::Qml iiSocietyContainer::iiSocietyContainer iiSocietyHelper::iiSocietyHelper iiLocalDiffusion::iiLocalDiffusion Qt6::Concurrent)
target_include_directories(DreamscapesGeneration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(DreamscapesGeneration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App/Generation")
target_include_directories(DreamscapesGeneration PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/App/Views/Result")
if(IOS)
    target_sources(DreamscapesGeneration PRIVATE App/Generation/GenerationScreenActivity.mm
        App/Generation/GenerationBackgroundActivity.mm)
    set_source_files_properties(App/Generation/GenerationBackgroundActivity.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(DreamscapesGeneration PRIVATE "-framework UIKit" "-framework BackgroundTasks")
endif()
target_compile_definitions(DreamscapesGeneration PRIVATE
    DREAMSCAPES_DIFFUSION_EXECUTABLE="${DREAMSCAPES_DIFFUSION_EXECUTABLE}"
    DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE="${DREAMSCAPES_DIFFUSION_PYTHON_EXECUTABLE}")

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
        App/Views/Home/QuickGenerate.qml
        App/Views/Result/GenerationResult.qml
    NO_PLATFORM_RUNTIME_TARGETS
)
qt_add_resources(Dreamscapes result_assets
    PREFIX "/qt/qml/Dreamscapes"
    FILES App/Views/Result/Assets/right.svg)
option(DREAMSCAPES_LOCAL_RUNTIME_PROBE "Enable opt-in local device inference verification" OFF)
if(DREAMSCAPES_LOCAL_RUNTIME_PROBE)
    target_compile_definitions(DreamscapesGeneration PRIVATE DREAMSCAPES_LOCAL_RUNTIME_PROBE)
    target_sources(Dreamscapes PRIVATE tests/LocalRuntimeProbe.cpp)
    target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_LOCAL_RUNTIME_PROBE)
endif()
if(iiLocalDiffusion_NATIVE_DIFFUSION)
    file(GLOB dreamscapes_native_notices CONFIGURE_DEPENDS
        "${dreamscapes_diffusion_prefix}/share/iiLocalDiffusion/licenses/*")
    qt_add_resources(Dreamscapes native_inference_licenses PREFIX "/licenses/native-inference"
        BASE "${dreamscapes_diffusion_prefix}/share/iiLocalDiffusion/licenses"
        FILES ${dreamscapes_native_notices})
endif()

set(dreamscapes_product_dependencies
    iiCSMIDI::iiCSMIDI
    iiLicenseManager::iiLicenseManager
    iiLocalDiffusion::iiLocalDiffusion
    iiPaintEngine::iiPaintEngine
    iiSocietyContainer::iiSocietyContainer
    iiSocietyHelper::iiSocietyHelper
    iiUpdateManager::iiUpdateManager
    LVRS::LVRS
)
target_link_libraries(Dreamscapes PRIVATE DreamscapesGeneration Qt6::QuickDialogs2 ${dreamscapes_product_dependencies})
target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_APP_VERSION="${PROJECT_VERSION}")

if(ANDROID)
    set_property(TARGET Dreamscapes PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/platform/android")
    foreach(dependency IN ITEMS ${dreamscapes_product_dependencies} iiFileProvider::iiFileProvider)
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
if(IOS)
    qt_import_plugins(Dreamscapes INCLUDE Qt6::QSQLiteDriverPlugin)
    set_property(TARGET Dreamscapes APPEND PROPERTY BUILD_RPATH "@executable_path/Frameworks")
    # iOS cannot load the development machine's SDK libraries. Embed and sign
    # every dynamic product library, including iiCSMIDI's FileProvider dependency.
    set(dreamscapes_ios_runtime_dependencies ${dreamscapes_product_dependencies}
        iiFileProvider::iiFileProvider)
    foreach(dependency IN LISTS dreamscapes_ios_runtime_dependencies)
        get_target_property(dependency_type ${dependency} TYPE)
        if(dependency_type STREQUAL "SHARED_LIBRARY")
            # Xcode's embed property requires concrete paths for imported SDKs.
            # Copy the install-name filename, which can differ from VERSION.
            get_target_property(dependency_configs ${dependency} IMPORTED_CONFIGURATIONS)
            list(GET dependency_configs 0 dependency_config)
            get_target_property(dependency_file ${dependency} IMPORTED_LOCATION_${dependency_config})
            get_target_property(dependency_soname ${dependency} IMPORTED_SONAME_${dependency_config})
            get_filename_component(dependency_directory "${dependency_file}" DIRECTORY)
            get_filename_component(dependency_name "${dependency_soname}" NAME)
            set(dependency_runtime_file "${dependency_directory}/${dependency_name}")
            if(NOT EXISTS "${dependency_runtime_file}")
                message(FATAL_ERROR "Missing iOS runtime library: ${dependency_runtime_file}")
            endif()
            set_property(TARGET Dreamscapes APPEND PROPERTY XCODE_EMBED_FRAMEWORKS "${dependency_runtime_file}")
        endif()
    endforeach()
    set_target_properties(Dreamscapes PROPERTIES
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY YES
        XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY YES)
    set(SOCIETY_IOS_APP_GROUP "group.com.iisacc.society" CACHE STRING "Same App Group as the Society app")
    set(SOCIETY_IOS_TEAM "" CACHE STRING "Apple development team shared with Society")
    iiSocietyContainer_configure_ios_client(Dreamscapes APP_GROUP "${SOCIETY_IOS_APP_GROUP}"
        TEAM "${SOCIETY_IOS_TEAM}" DISPLAY_NAME Dreamscapes)
    # Large checkpoint mappings need virtual address space as well as RAM.
    # The runtime still sizes its working set from the actual process limit.
    configure_file(Platform/iOS/Dreamscapes.entitlements.in
        "${CMAKE_CURRENT_BINARY_DIR}/Dreamscapes.entitlements" @ONLY)
    set_property(TARGET Dreamscapes PROPERTY XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
        "${CMAKE_CURRENT_BINARY_DIR}/Dreamscapes.entitlements")
endif()

if(APPLE)
    # Extend the selected Qt/Society template without discarding iOS App Group settings.
    get_target_property(dreamscapes_plist Dreamscapes MACOSX_BUNDLE_INFO_PLIST)
    if(NOT dreamscapes_plist)
        set(dreamscapes_plist "${Qt6_DIR}/macos/Info.plist.app.in")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${dreamscapes_plist}")
    file(READ "${dreamscapes_plist}" dreamscapes_plist_content)
    if(IOS)
        string(REGEX REPLACE "</dict>[ \t\r\n]*</plist>"
            "<key>UIBackgroundModes</key><array><string>processing</string></array>\n<key>BGTaskSchedulerPermittedIdentifiers</key><array><string>com.iisacc.dreamscapes.generation.*</string></array>\n</dict></plist>"
            dreamscapes_plist_content "${dreamscapes_plist_content}")
    endif()
    string(REGEX REPLACE "</dict>[ \t\r\n]*</plist>"
        "<key>NSPhotoLibraryAddUsageDescription</key><string>Save generated images to Photos. iCloud Photos syncs them when enabled for your photo library.</string>\n</dict></plist>"
        dreamscapes_plist_content "${dreamscapes_plist_content}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/photo-library")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/photo-library/Dreamscapes.Info.plist.in" "${dreamscapes_plist_content}")
    set_property(TARGET Dreamscapes PROPERTY MACOSX_BUNDLE_INFO_PLIST
        "${CMAKE_CURRENT_BINARY_DIR}/photo-library/Dreamscapes.Info.plist.in")
endif()

include(cmake/AppIcons.cmake)
dreamscapes_add_app_icons(Dreamscapes)

include(CTest)
option(DREAMSCAPES_BUILD_ANDROID_PHOTO_TESTS "Build the native MediaStore integration test APK" OFF)
if(ANDROID AND DREAMSCAPES_BUILD_ANDROID_PHOTO_TESTS)
    set(QT_USE_TARGET_ANDROID_BUILD_DIR TRUE CACHE BOOL "Keep Android test and app packages separate" FORCE)
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    qt_add_executable(DreamscapesPhotoNativeTests App/Views/Result/tst_PhotoLibraryAndroid.cpp)
    target_link_libraries(DreamscapesPhotoNativeTests PRIVATE DreamscapesGeneration Qt6::Test)
    get_target_property(dreamscapes_android_package Dreamscapes QT_ANDROID_PACKAGE_SOURCE_DIR)
    set_target_properties(DreamscapesPhotoNativeTests PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${dreamscapes_android_package}"
        QT_ANDROID_PACKAGE_NAME "com.iisacc.dreamscapes.phototests"
        QT_ANDROID_APP_NAME "Dreamscapes Photo Tests")
    set_property(TARGET DreamscapesPhotoNativeTests APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
        "$<TARGET_FILE:iiSocietyContainer::iiSocietyContainer>")
endif()
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME Dreamscapes.AppIcons
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_app_icons.py")
    # Only the integration harness acts as Society peers; the app has no Sync transport.
    find_package(iiSocietySync 0.3.0 CONFIG REQUIRED HINTS "$ENV{HOME}/.local/SDK/iiSocietySync")
    find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Test)
    qt_add_executable(DreamscapesLocalSocietyTests App/Generation/tst_LocalSociety.cpp)
    target_link_libraries(DreamscapesLocalSocietyTests PRIVATE DreamscapesGeneration
        iiSocietyHelper::iiSocietyHelper iiSocietySync::iiSocietySync Qt6::Test)
    target_compile_definitions(DreamscapesLocalSocietyTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_FAKE_GENERATOR="${CMAKE_CURRENT_SOURCE_DIR}/App/Generation/fixtures/generate.py")
    add_test(NAME Dreamscapes.LocalSociety COMMAND DreamscapesLocalSocietyTests)
    set_tests_properties(Dreamscapes.LocalSociety PROPERTIES TIMEOUT 60)
    # Keep this GUI target limited to sources present in Dreamscapes.
    qt_add_executable(DreamscapesGuiTests App/tst_Gui.cpp)
    target_link_libraries(DreamscapesGuiTests PRIVATE Qt6::Test Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2)
    target_link_libraries(DreamscapesGuiTests PRIVATE DreamscapesGeneration)
    target_link_libraries(DreamscapesGuiTests PRIVATE iiSocietyHelper::iiSocietyHelper)
    target_compile_definitions(DreamscapesGuiTests PRIVATE
        DREAMSCAPES_QML_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/App"
        DREAMSCAPES_EXECUTABLE_PATH="$<TARGET_FILE:Dreamscapes>"
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_FAKE_GENERATOR="${CMAKE_CURRENT_SOURCE_DIR}/App/Generation/fixtures/generate.py"
    )
    add_dependencies(DreamscapesGuiTests Dreamscapes)
    lvrs_configure_qml_app(DreamscapesGuiTests NO_PLATFORM_RUNTIME_TARGETS)
    add_test(NAME Dreamscapes.Gui COMMAND DreamscapesGuiTests)
    set_tests_properties(Dreamscapes.Gui PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
        TIMEOUT 120
    )
    qt_add_executable(DreamscapesGenerationTests App/Generation/tst_Generation.cpp)
    target_link_libraries(DreamscapesGenerationTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesGenerationTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}"
        DREAMSCAPES_FAKE_GENERATOR="${CMAKE_CURRENT_SOURCE_DIR}/App/Generation/fixtures/generate.py")
    add_test(NAME Dreamscapes.Generation COMMAND DreamscapesGenerationTests)
    set_tests_properties(Dreamscapes.Generation PROPERTIES TIMEOUT 45)
    qt_add_executable(DreamscapesImageExportTests App/Views/Result/tst_ImageFileExporter.cpp)
    target_link_libraries(DreamscapesImageExportTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesImageExportTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.ImageExport COMMAND DreamscapesImageExportTests)
    qt_add_executable(DreamscapesPhotoLibraryTests App/Views/Result/tst_PhotoLibraryExporter.cpp)
    target_link_libraries(DreamscapesPhotoLibraryTests PRIVATE DreamscapesGeneration Qt6::Test)
    target_compile_definitions(DreamscapesPhotoLibraryTests PRIVATE
        DREAMSCAPES_TEST_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME Dreamscapes.PhotoLibrary COMMAND DreamscapesPhotoLibraryTests)
endif()
