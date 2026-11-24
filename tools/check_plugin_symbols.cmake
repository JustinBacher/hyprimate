# Audit trusted ELF binaries only: ldd may execute code from its input.
foreach(argument PLUGIN HOST NM)
    if(NOT DEFINED ${argument} OR "${${argument}}" STREQUAL "")
        message(FATAL_ERROR "plugin symbol check failed: ${argument} is required")
    endif()
endforeach()

execute_process(
    COMMAND sh "${CMAKE_CURRENT_LIST_DIR}/check_plugin_symbols.sh" "${PLUGIN}" "${HOST}" "${NM}"
    RESULT_VARIABLE result
)
if(NOT result STREQUAL "0")
    message(FATAL_ERROR "plugin symbol check failed (checker exit: ${result})")
endif()
