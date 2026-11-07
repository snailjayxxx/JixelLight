# Append after qt_add_shaders, in the same CMake directory. Qt's shader flags
# and resources stay intact; only its fresh depfile is escaped before CMake reads it.
function(jixellight_escape_qsb_depfiles)
    # The parser matches this version's unescaped, one-path-per-line writer.
    # A newer Qt may already fix escaping or change its depfile/output layout.
    if(NOT Qt6_VERSION VERSION_EQUAL "6.8.3")
        return()
    endif()
    foreach(shader IN LISTS ARGN)
        get_filename_component(shader_name "${shader}" NAME)
        get_filename_component(shader_source "${shader}" ABSOLUTE)
        set(shader_output "${CMAKE_CURRENT_BINARY_DIR}/.qsb/${shader_name}.qsb")
        add_custom_command(OUTPUT "${shader_output}" APPEND
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/FixQsbDepfile.py"
                "${shader_output}.d" "${shader_output}" "${shader_source}"
            DEPENDS "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/FixQsbDepfile.py")
    endforeach()
endfunction()
