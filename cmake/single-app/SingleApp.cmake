# Workspace-wide contract: one application bundle per product, all helpers/tests
# are ordinary executables. Platform/configuration builds share this output slot.
function(product_plain_executables directory application)
    # Dynamic macOS QML deployment scans only the authored QML directory in the
    # deployer; Qt's default scan of the repository also crawls historical builds.
    if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND TARGET Qt6::Qml AND TARGET "${application}")
        get_target_property(qml_type Qt6::Qml TYPE)
        if(qml_type STREQUAL "SHARED_LIBRARY")
            set_property(TARGET "${application}" PROPERTY QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
        endif()
    endif()
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type "${target}" TYPE)
        if(type STREQUAL "EXECUTABLE" AND NOT target STREQUAL application)
            set_target_properties("${target}" PROPERTIES MACOSX_BUNDLE FALSE)
            if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
                set_property(TARGET "${target}" PROPERTY QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
            endif()
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        product_plain_executables("${child}" "${application}")
    endforeach()
endfunction()

function(product_single_app application output)
    if(NOT APPLE OR NOT TARGET "${application}")
        return()
    endif()
    set(marker "${CMAKE_SOURCE_DIR}/build/.single-app-platform")
    set(platform "${CMAKE_SYSTEM_NAME}|${CMAKE_OSX_SYSROOT}|${CMAKE_OSX_ARCHITECTURES}")
    if(EXISTS "${marker}")
        file(READ "${marker}" previous)
        if(NOT previous STREQUAL platform)
            get_target_property(name "${application}" OUTPUT_NAME)
            if(NOT name)
                set(name "${application}")
            endif()
            file(REMOVE_RECURSE "${CMAKE_SOURCE_DIR}/build/${output}/${name}.app")
        endif()
    endif()
    file(WRITE "${marker}" "${platform}")
    add_custom_target("${application}OutputPlatform"
        COMMAND "${CMAKE_COMMAND}" "-DMARKER=${marker}" "-DEXPECTED=${platform}"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CheckPlatform.cmake" VERBATIM)
    add_dependencies("${application}" "${application}OutputPlatform")
    product_plain_executables("${CMAKE_SOURCE_DIR}" "${application}")
    set_target_properties("${application}" PROPERTIES
        MACOSX_BUNDLE TRUE
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build/${output}$<0:>")
    foreach(config IN ITEMS Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" upper)
        set_property(TARGET "${application}" PROPERTY RUNTIME_OUTPUT_DIRECTORY_${upper}
            "${CMAKE_SOURCE_DIR}/build/${output}")
    endforeach()
    # Do not let Xcode add configuration/platform directories or archive copies.
    set_target_properties("${application}" PROPERTIES
        XCODE_ATTRIBUTE_CONFIGURATION_BUILD_DIR "${CMAKE_SOURCE_DIR}/build/${output}"
        XCODE_ATTRIBUTE_DEPLOYMENT_LOCATION NO
        XCODE_ATTRIBUTE_SKIP_INSTALL YES)
endfunction()

function(product_deploy_app application qml)
    if(NOT APPLE OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR NOT TARGET "${application}")
        return()
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    find_program(product_macdeployqt macdeployqt HINTS "${Qt6_DIR}/../../../bin" REQUIRED)
    set(deployer "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/deploy_macos.py")
    set(args)
    if(qml)
        list(APPEND args --qml-dir "${CMAKE_SOURCE_DIR}/${qml}")
    endif()
    foreach(path IN ITEMS "${LVRS_QML_IMPORT_PATH}" "${iiAcountManager_QML_IMPORT_PATH}" "${CMAKE_BINARY_DIR}")
        if(path)
            list(APPEND args --qml-import "${path}")
        endif()
    endforeach()
    get_property(imported DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY IMPORTED_TARGETS)
    foreach(dependency IN LISTS imported)
        get_target_property(type "${dependency}" TYPE)
        if(type STREQUAL "SHARED_LIBRARY")
            list(APPEND args --library "$<TARGET_FILE:${dependency}>")
        endif()
    endforeach()
    set_property(TARGET "${application}" APPEND PROPERTY LINK_DEPENDS "${deployer}")
    add_custom_command(TARGET "${application}" POST_BUILD
        COMMAND "${Python3_EXECUTABLE}" -B "${deployer}"
            --app "$<TARGET_BUNDLE_DIR:${application}>" --macdeployqt "${product_macdeployqt}"
            --log "${CMAKE_BINARY_DIR}/single-app-deployment.log" ${args}
        VERBATIM)
endfunction()
