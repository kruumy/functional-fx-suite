# Script mode: turns a PiPL .r file into a Windows .rc with the AE SDK's PiPLtool.
# Inputs: COMPILER (cl or clang-cl), SDK (AE SDK Examples folder), INPUT (.r), OUTPUT (.rc)

get_filename_component(work "${OUTPUT}" DIRECTORY)
get_filename_component(stem "${OUTPUT}" NAME_WE)
set(rr "${work}/${stem}.rr")
set(rrc "${work}/${stem}.rrc")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "PiPL step failed (${result}): ${ARGN}\n${errors}")
    endif()
endfunction()

run(${COMPILER} /nologo /EP "/I${SDK}/Headers" "/I${SDK}/Resources" "/Tc${INPUT}" OUTPUT_FILE "${rr}")
run("${SDK}/Resources/PiPLtool.exe" "${rr}" "${rrc}")
run(${COMPILER} /nologo /D MSWindows /EP "/Tc${rrc}" OUTPUT_FILE "${OUTPUT}")
