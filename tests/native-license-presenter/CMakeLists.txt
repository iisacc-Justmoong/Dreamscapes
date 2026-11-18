cmake_minimum_required(VERSION 3.31)
project(DreamscapesNativeLicensePresenterTests LANGUAGES CXX)
include(CTest)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8.3 EXACT REQUIRED COMPONENTS Core Gui)
find_package(iiSDKCore 0.18.1 CONFIG REQUIRED COMPONENTS Runtime)
include(../../cmake/QtApple.cmake)
find_package(iiLicenseManagerPresentation 0.3 CONFIG REQUIRED)
find_package(LVUI 0.1 CONFIG REQUIRED COMPONENTS Native)
find_package(iiGUI 0.5 CONFIG REQUIRED)
lvui_generate(NAME SocietyLicenseGate SOURCE "${iiLicenseManagerPresentation_VIEWS_DIR}/SocietyLicenseGate.lvui"
    FRAMEWORK iiGUI INCLUDES iiLicenseManager/SocietyLicenseGateModel.h OUTPUT_VARIABLE generated)
add_executable(DreamscapesNativeLicensePresenterTests presenter.cpp
    ../../src/App/ApplicationShutdownCoordinator.h ../../src/App/ApplicationShutdownCoordinator.cpp
    ../../src/App/Native/NativeLicensePresenter.cpp ../../src/App/Native/NativeLicensePresenter.h
    ../../src/App/Native/NativeLicenseBridge.cpp ../../src/App/Native/NativeLicenseBridge.h
    ../../src/App/Native/NativeOverlayCoordinator.cpp ../../src/App/Native/NativeOverlayCoordinator.h
    ../../src/App/Native/ExternalOverlaySurface.cpp ../../src/App/Native/ExternalOverlaySurface.h "${generated}")
target_compile_features(DreamscapesNativeLicensePresenterTests PRIVATE cxx_std_23)
target_include_directories(DreamscapesNativeLicensePresenterTests PRIVATE ../../src "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesNativeLicensePresenterTests PRIVATE Qt6::Core Qt6::Gui
    iiLicenseManager::Presentation LVUI::Native iiGUI::iiGUI iiSDKCore::Runtime)
add_test(NAME Dreamscapes.NativeLicensePresenterAdmission COMMAND DreamscapesNativeLicensePresenterTests)
set_tests_properties(Dreamscapes.NativeLicensePresenterAdmission PROPERTIES TIMEOUT 30)
add_executable(DreamscapesNativeLicenseViewTests view.cpp ../../src/App/Native/NativeLicenseBridge.cpp
    ../../src/App/Native/NativeLicenseBridge.h "${generated}")
target_compile_features(DreamscapesNativeLicenseViewTests PRIVATE cxx_std_23)
target_include_directories(DreamscapesNativeLicenseViewTests PRIVATE ../../src "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(DreamscapesNativeLicenseViewTests PRIVATE iiLicenseManager::Presentation LVUI::Native iiGUI::iiGUI)
add_test(NAME Dreamscapes.NativeLicenseView COMMAND DreamscapesNativeLicenseViewTests)
set_tests_properties(Dreamscapes.NativeLicenseView PROPERTIES TIMEOUT 30)
if(APPLE)
    set_tests_properties(Dreamscapes.NativeLicensePresenterAdmission PROPERTIES ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:;QT_QPA_PLATFORM=set:offscreen")
    set_tests_properties(Dreamscapes.NativeLicenseView PROPERTIES ENVIRONMENT_MODIFICATION
        "DYLD_LIBRARY_PATH=unset:;DYLD_FALLBACK_LIBRARY_PATH=unset:;DYLD_FRAMEWORK_PATH=unset:;DYLD_FALLBACK_FRAMEWORK_PATH=unset:")
endif()
