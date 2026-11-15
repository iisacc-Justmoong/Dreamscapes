# Qt 6.8's imported desktop OpenGL target still lists AGL, which current
# macOS SDKs no longer ship. The retained frameworks provide the APIs used by
# Qt and Dreamscapes. Do not apply this compatibility fix to iOS or non-Apple.
if(APPLE AND NOT IOS)
    foreach(_dreamscapes_target WrapOpenGL::WrapOpenGL Qt6::Gui)
        if(TARGET ${_dreamscapes_target})
            foreach(_dreamscapes_property INTERFACE_LINK_LIBRARIES INTERFACE_LINK_OPTIONS)
                get_target_property(_dreamscapes_links ${_dreamscapes_target} ${_dreamscapes_property})
                if(_dreamscapes_links)
                    list(FILTER _dreamscapes_links EXCLUDE REGEX "(^|[ /])AGL($|[ /.])|AGL\\.framework")
                    set_property(TARGET ${_dreamscapes_target} PROPERTY ${_dreamscapes_property} "${_dreamscapes_links}")
                endif()
            endforeach()
        endif()
    endforeach()
endif()
