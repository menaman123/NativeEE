# cmake/ps2r_run_tool.cmake -- run a generator and keep its (large) output in a log file.
#
#   cmake -DLABEL=<name> -DLOG=<file> [-DWORKDIR=<dir>] "-DCMD=<arg;arg;...>" -P ps2r_run_tool.cmake
#
# The recompilers print one line per translated block (the EE tool: ~120k lines); a build
# log should not carry that. stdout+stderr go to LOG; on a non-zero exit the tail of the
# log is shown and the script fails. Portable (no shell redirection in the custom command).
foreach(v LABEL LOG CMD)
    if(NOT DEFINED ${v})
        message(FATAL_ERROR "ps2r_run_tool.cmake: -D${v} is required")
    endif()
endforeach()
get_filename_component(_log_dir "${LOG}" DIRECTORY)
file(MAKE_DIRECTORY "${_log_dir}")
if(DEFINED WORKDIR)
    file(MAKE_DIRECTORY "${WORKDIR}")
    set(_wd WORKING_DIRECTORY "${WORKDIR}")
else()
    set(_wd)
endif()
string(TIMESTAMP _t0 "%s")
execute_process(COMMAND ${CMD} ${_wd}
    RESULT_VARIABLE _rc
    OUTPUT_FILE "${LOG}"
    ERROR_FILE "${LOG}")
string(TIMESTAMP _t1 "%s")
math(EXPR _secs "${_t1} - ${_t0}")
if(NOT _rc EQUAL 0)
    file(STRINGS "${LOG}" _lines)
    list(LENGTH _lines _n)
    set(_tail "")
    if(_n GREATER 40)
        math(EXPR _from "${_n} - 40")
        list(SUBLIST _lines ${_from} 40 _lines)
    endif()
    list(JOIN _lines "\n  " _tail)
    list(JOIN CMD " " _cmd_text)
    message(FATAL_ERROR "${LABEL}: the tool exited with ${_rc} after ${_secs} s\n  command: ${_cmd_text}\n  log: ${LOG}\n  --- last lines ---\n  ${_tail}")
endif()
message(STATUS "${LABEL}: ok in ${_secs} s (log: ${LOG})")
