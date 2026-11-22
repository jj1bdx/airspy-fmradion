# CheckSubmodules.cmake: check that every git submodule is initialized,
# checked out at its pinned commit, and free of local changes.
#
# Run in script mode:
#
# cmake -DGIT_EXECUTABLE=<git> -DSOURCE_DIR=<top-level source dir>
# [-DSEVERITY=WARNING] -P CheckSubmodules.cmake
#
# SEVERITY defaults to FATAL_ERROR, which makes cmake exit with a
# non-zero status when a problem is found.

if(NOT DEFINED SEVERITY)
  set(SEVERITY FATAL_ERROR)
endif()
if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckSubmodules: GIT_EXECUTABLE and SOURCE_DIR "
                      "must be defined")
endif()

execute_process(
  COMMAND ${GIT_EXECUTABLE} submodule status --recursive
  WORKING_DIRECTORY ${SOURCE_DIR}
  OUTPUT_VARIABLE status_output
  ERROR_VARIABLE status_error
  RESULT_VARIABLE status_result
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT status_result EQUAL 0)
  message(${SEVERITY} "git submodule status failed: ${status_error}")
  return()
endif()

set(problems "")
string(REPLACE "\n" ";" status_lines "${status_output}")
foreach(line IN LISTS status_lines)
  # Each line is <flag><commit> <path>[ (<describe>)]
  if(NOT line MATCHES "^(.)([0-9a-f]+) ([^ ]+)")
    string(APPEND problems "\n  cannot parse: ${line}")
    continue()
  endif()
  set(flag "${CMAKE_MATCH_1}")
  set(commit "${CMAKE_MATCH_2}")
  set(path "${CMAKE_MATCH_3}")
  if(flag STREQUAL "-")
    string(APPEND problems "\n  ${path}: not initialized")
  elseif(flag STREQUAL "+")
    # The pinned commit is the one recorded in the index; the output of
    # ls-files --stage is <mode> <commit> <stage>\t<path>
    execute_process(
      COMMAND ${GIT_EXECUTABLE} ls-files --stage -- ${path}
      WORKING_DIRECTORY ${SOURCE_DIR}
      OUTPUT_VARIABLE stage_output
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(pinned "unknown")
    if(stage_output MATCHES "^[0-7]+ ([0-9a-f]+) ")
      set(pinned "${CMAKE_MATCH_1}")
    endif()
    string(APPEND problems "\n  ${path}: checked out ${commit}, "
           "pinned ${pinned}")
  elseif(flag STREQUAL "U")
    string(APPEND problems "\n  ${path}: merge conflict")
  else()
    execute_process(
      COMMAND ${GIT_EXECUTABLE} -C ${path} status --porcelain
              --untracked-files=no
      WORKING_DIRECTORY ${SOURCE_DIR}
      OUTPUT_VARIABLE changes
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT changes STREQUAL "")
      string(REPLACE "\n" "\n    " changes "${changes}")
      string(APPEND problems
             "\n  ${path}: local changes\n    ${changes}")
    endif()
  endif()
endforeach()

if(problems STREQUAL "")
  message(STATUS "Submodules match their pinned commits")
else()
  message(
    ${SEVERITY}
    "Submodules do not match their pinned commits:${problems}\n"
    "To check out the pinned commits, run: "
    "git submodule update --init --recursive\n"
    "Local changes in a submodule must be committed or discarded "
    "with git -C <path>.")
endif()
