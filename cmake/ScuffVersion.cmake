# Version information for SCUFF-EM.
#
# The human-readable version string is derived from git (matching the
# historical `configure.ac` behaviour).  The numeric CMake project version is
# a fallback and is used for the CMake package version file.

find_package(Git QUIET)

set(SCUFF_VERSION_STRING "")
if(GIT_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --always --dirty
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE SCUFF_GIT_DESCRIBE_RESULT
    OUTPUT_VARIABLE SCUFF_GIT_DESCRIBE_OUTPUT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(SCUFF_GIT_DESCRIBE_RESULT EQUAL 0 AND NOT SCUFF_GIT_DESCRIBE_OUTPUT STREQUAL "")
    set(SCUFF_VERSION_STRING "${SCUFF_GIT_DESCRIBE_OUTPUT}")
  endif()
endif()

if(SCUFF_VERSION_STRING STREQUAL "")
  set(SCUFF_VERSION_STRING "v${PROJECT_VERSION}")
  message(STATUS "SCUFF-EM: git describe unavailable; using ${SCUFF_VERSION_STRING}")
else()
  message(STATUS "SCUFF-EM: version string ${SCUFF_VERSION_STRING}")
endif()

# Shared-library version (libtool CURRENT:REVISION:AGE = 0:0:0).
set(SCUFF_SHARED_VERSION "0.0.0")
set(SCUFF_SHARED_SOVERSION "0")
