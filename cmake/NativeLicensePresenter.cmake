find_package(iiLicenseManagerPresentation 0.3 CONFIG REQUIRED HINTS
    "$ENV{HOME}/.local/SDK/iiLicenseManager" "$ENV{USERPROFILE}/.local/SDK/iiLicenseManager")
if(NOT iiLicenseManagerPresentation_VIEWS_DIR OR NOT EXISTS "${iiLicenseManagerPresentation_VIEWS_DIR}/SocietyLicenseGate.lvui")
    message(FATAL_ERROR "The selected iiLicenseManagerPresentation package has no SocietyLicenseGate.lvui view")
endif()
include(cmake/NativeOverlay.cmake)
lvui_generate(NAME SocietyLicenseGate SOURCE "${iiLicenseManagerPresentation_VIEWS_DIR}/SocietyLicenseGate.lvui"
    FRAMEWORK iiGUI INCLUDES iiLicenseManager/SocietyLicenseGateModel.h
    OUTPUT_VARIABLE _dreamscapes_native_license_view)
target_sources(Dreamscapes PRIVATE
    src/App/Native/NativeLicensePresenter.h src/App/Native/NativeLicensePresenter.cpp
    src/App/Native/NativeLicenseBridge.h src/App/Native/NativeLicenseBridge.cpp
    "${_dreamscapes_native_license_view}")
target_include_directories(Dreamscapes PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Dreamscapes PRIVATE iiLicenseManager::Presentation LVUI::Native Qt6::Gui)
target_compile_definitions(Dreamscapes PRIVATE DREAMSCAPES_NATIVE_LICENSE_PRESENTER=1)
