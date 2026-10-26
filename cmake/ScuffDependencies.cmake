# Dependency and optional-feature detection for SCUFF-EM.
#
# This module defines the feature macros used by config.h.in as well as the
# imported targets/link libraries needed by the scuff target.

include(CheckIncludeFile)
include(CheckSymbolExists)

# ---------------------------------------------------------------------------
# Required numerical libraries
# ---------------------------------------------------------------------------

find_package(BLAS REQUIRED)
find_package(LAPACK REQUIRED)
set(HAVE_BLAS TRUE)
set(HAVE_LAPACK TRUE)

# pthread_rwlock is used by libscuff regardless of the parallel backend.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
set(HAVE_PTHREAD TRUE)

# ---------------------------------------------------------------------------
# Threading backend
# ---------------------------------------------------------------------------

if(SCUFF_THREAD_BACKEND STREQUAL "OpenMP")
  find_package(OpenMP REQUIRED COMPONENTS CXX)
  set(USE_OPENMP TRUE)
  set(HAVE_OPENMP TRUE)
  set(USE_PTHREAD FALSE)
elseif(SCUFF_THREAD_BACKEND STREQUAL "Pthreads")
  set(USE_OPENMP FALSE)
  set(HAVE_OPENMP FALSE)
  set(USE_PTHREAD TRUE)
elseif(SCUFF_THREAD_BACKEND STREQUAL "None")
  set(USE_OPENMP FALSE)
  set(HAVE_OPENMP FALSE)
  set(USE_PTHREAD FALSE)
else()
  message(FATAL_ERROR
    "Unsupported SCUFF_THREAD_BACKEND='${SCUFF_THREAD_BACKEND}'. "
    "Choose OpenMP, Pthreads or None.")
endif()

# ---------------------------------------------------------------------------
# HDF5 (default ON)
# ---------------------------------------------------------------------------

set(HAVE_HDF5 FALSE)
if(SCUFF_WITH_HDF5)
  set(HDF5_PREFER_PARALLEL FALSE)
  find_package(HDF5 COMPONENTS C HL REQUIRED)
  if(TARGET hdf5::hdf5 AND TARGET hdf5::hdf5_hl)
    set(SCUFF_HDF5_TARGETS hdf5::hdf5 hdf5::hdf5_hl)
  elseif(TARGET HDF5::HDF5)
    set(SCUFF_HDF5_TARGETS HDF5::HDF5)
  else()
    # Fall back to the traditional variables on older CMake releases.
    set(SCUFF_HDF5_TARGETS ${HDF5_LIBRARIES} ${HDF5_HL_LIBRARIES})
  endif()
  set(HAVE_HDF5 TRUE)
  message(STATUS "SCUFF-EM: HDF5 ${HDF5_VERSION} enabled")
else()
  message(STATUS "SCUFF-EM: HDF5 disabled")
endif()

# ---------------------------------------------------------------------------
# GSL, readline, DCUTRI, libGDSII
# ---------------------------------------------------------------------------

function(_scuff_tri_state option_name description)
  set(_value "${${option_name}}")
  if(NOT _value MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "${option_name} must be AUTO, ON or OFF (got '${_value}')")
  endif()
endfunction()

_scuff_tri_state(SCUFF_WITH_GSL "GSL")

set(HAVE_LIBGSL FALSE)
set(HAVE_LIBGSLCBLAS FALSE)
set(SCUFF_GSL_TARGETS "")
if(SCUFF_WITH_GSL STREQUAL "ON")
  find_package(GSL REQUIRED)
elseif(SCUFF_WITH_GSL STREQUAL "AUTO")
  find_package(GSL QUIET)
else()
  set(GSL_FOUND FALSE)
endif()
if(GSL_FOUND)
  set(HAVE_LIBGSL TRUE)
  set(HAVE_LIBGSLCBLAS TRUE)
  set(SCUFF_GSL_TARGETS GSL::gsl GSL::gslcblas)
  message(STATUS "SCUFF-EM: GSL enabled")
else()
  set(SCUFF_GSL_TARGETS "")
  if(SCUFF_WITH_GSL STREQUAL "ON")
    message(FATAL_ERROR "SCUFF_WITH_GSL=ON but GSL was not found")
  endif()
  message(STATUS "SCUFF-EM: GSL disabled")
endif()

# readline is only used by developer/test programs, but the feature macro is
# generated globally to match the historical config.h.
set(HAVE_LIBREADLINE FALSE)
set(HAVE_READLINE FALSE)
set(SCUFF_READLINE_LIBRARY "")
set(SCUFF_WITH_READLINE_FOUND FALSE)
if(NOT SCUFF_WITH_READLINE MATCHES "^(AUTO|ON|OFF)$")
  message(FATAL_ERROR "SCUFF_WITH_READLINE must be AUTO, ON or OFF")
endif()
find_path(READLINE_INCLUDE_DIR readline/readline.h)
find_library(READLINE_LIBRARY NAMES readline)
if(READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
  set(SCUFF_WITH_READLINE_FOUND TRUE)
endif()
if(SCUFF_WITH_READLINE STREQUAL "ON" AND NOT SCUFF_WITH_READLINE_FOUND)
  message(FATAL_ERROR "SCUFF_WITH_READLINE=ON but readline was not found")
endif()
if(SCUFF_WITH_READLINE STREQUAL "OFF")
  set(SCUFF_WITH_READLINE_FOUND FALSE)
endif()
if(SCUFF_WITH_READLINE_FOUND)
  set(HAVE_LIBREADLINE TRUE)
  set(HAVE_READLINE TRUE)
  set(SCUFF_READLINE_LIBRARY "${READLINE_LIBRARY}")
  message(STATUS "SCUFF-EM: readline enabled (${READLINE_LIBRARY})")
else()
  message(STATUS "SCUFF-EM: readline disabled")
endif()

function(_scuff_find_optional_library description option_name prefix header library)
  set(_value "${${option_name}}")
  if(_value STREQUAL "OFF")
    set(${prefix}_FOUND FALSE PARENT_SCOPE)
    set(${prefix}_LIBRARY "" PARENT_SCOPE)
    set(${prefix}_INCLUDE_DIR "" PARENT_SCOPE)
    message(STATUS "SCUFF-EM: ${description} disabled")
    return()
  endif()

  set(_header_var "${prefix}_INCLUDE_DIR")
  set(_library_var "${prefix}_LIBRARY")
  if(header)
    find_path(${_header_var} NAMES ${header})
  else()
    set(${_header_var} "" )
  endif()
  find_library(${_library_var} NAMES ${library})

  set(_found TRUE)
  if(NOT ${_library_var})
    set(_found FALSE)
  endif()
  if(header AND NOT ${_header_var})
    set(_found FALSE)
  endif()

  if(_found)
    set(${prefix}_FOUND TRUE PARENT_SCOPE)
    set(${prefix}_LIBRARY "${${_library_var}}" PARENT_SCOPE)
    set(${prefix}_INCLUDE_DIR "${${_header_var}}" PARENT_SCOPE)
    message(STATUS "SCUFF-EM: ${description} enabled (${${_library_var}})")
  elseif(_value STREQUAL "ON")
    message(FATAL_ERROR "${option_name}=ON but ${description} was not found")
  else()
    set(${prefix}_FOUND FALSE PARENT_SCOPE)
    set(${prefix}_LIBRARY "" PARENT_SCOPE)
    set(${prefix}_INCLUDE_DIR "" PARENT_SCOPE)
    message(STATUS "SCUFF-EM: ${description} disabled")
  endif()
endfunction()

_scuff_find_optional_library("DCUTRI" SCUFF_WITH_DCUTRI DCUTRI "" DCUTRI)
set(HAVE_DCUTRI ${DCUTRI_FOUND})

_scuff_find_optional_library("libGDSII" SCUFF_WITH_GDSII GDSII libGDSII.h GDSII)
set(HAVE_LIBGDSII ${GDSII_FOUND})

# ---------------------------------------------------------------------------
# Platform facilities used for diagnostics
# ---------------------------------------------------------------------------

check_include_file(execinfo.h HAVE_EXECINFO_H)
if(HAVE_EXECINFO_H)
  check_symbol_exists(backtrace "execinfo.h" HAVE_BACKTRACE)
else()
  set(HAVE_BACKTRACE FALSE)
endif()

# The C library math functions live in libm on most Unix systems.  Include it
# in the link checks so that functions such as erf() are detected correctly.
find_library(SCUFF_M_LIBRARY NAMES m)
if(SCUFF_M_LIBRARY)
  set(CMAKE_REQUIRED_LIBRARIES "${SCUFF_M_LIBRARY}")
endif()

check_symbol_exists(isnan "math.h" HAVE_ISNAN)
check_symbol_exists(isinf "math.h" HAVE_ISINF)
include(CheckCXXSymbolExists)
check_cxx_symbol_exists(erf "cmath" HAVE_ERF)
check_cxx_symbol_exists(erfc "cmath" HAVE_ERFC)
check_symbol_exists(copysign "math.h" HAVE_COPYSIGN)

unset(CMAKE_REQUIRED_LIBRARIES)

# ---------------------------------------------------------------------------
# Fortran name mangling
# ---------------------------------------------------------------------------

include(FortranCInterface)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/include")
FortranCInterface_HEADER(
  "${CMAKE_BINARY_DIR}/include/F77.h"
  MACRO_NAMESPACE "F77_"
)
