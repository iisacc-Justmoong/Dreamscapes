file(READ "${MARKER}" actual)
if(NOT actual STREQUAL EXPECTED)
    message(FATAL_ERROR "Another Apple platform owns the single app output. Reconfigure this build tree before building it.")
endif()
