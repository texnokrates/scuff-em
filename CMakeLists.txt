cmake_minimum_required(VERSION 3.20)

# Numeric fallback version for CMake package metadata.  The human-readable
# build string (shown by the command-line applications) comes from git.
set(SCUFF_VERSION 0.96.0)

project(scuff-em
  VERSION ${SCUFF_VERSION}
  LANGUAGES C CXX Fortran
)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CTest)
include(ScuffOptions)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
    "Build type (Release, Debug, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD ${SCUFF_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(ScuffVersion)
include(ScuffDependencies)
include(ScuffCLI11)

# All internal library directories are on the include path, mirroring the
# historical AM_CPPFLAGS.  The generated include directory comes first so that
# the build-tree config.h cannot be shadowed by a stale source-tree copy.
set(SCUFF_INTERNAL_INCLUDE_DIRS
  "${PROJECT_SOURCE_DIR}/libs/libhrutil"
  "${PROJECT_SOURCE_DIR}/libs/libhmat"
  "${PROJECT_SOURCE_DIR}/libs/libSGJC"
  "${PROJECT_SOURCE_DIR}/libs/libMDInterp"
  "${PROJECT_SOURCE_DIR}/libs/libMatProp"
  "${PROJECT_SOURCE_DIR}/libs/libMatProp/cmatheval"
  "${PROJECT_SOURCE_DIR}/libs/libTriInt"
  "${PROJECT_SOURCE_DIR}/libs/libSpherical"
  "${PROJECT_SOURCE_DIR}/libs/libIncField"
  "${PROJECT_SOURCE_DIR}/libs/libSubstrate"
  "${PROJECT_SOURCE_DIR}/libs/libStaticSolver"
  "${PROJECT_SOURCE_DIR}/libs/libscuffSolver"
  "${PROJECT_SOURCE_DIR}/libs/libscuff"
)

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/config.h.in"
  "${CMAKE_BINARY_DIR}/include/config.h"
  @ONLY
)

include(ScuffTargets)

# ---------------------------------------------------------------------------
# Feature summary
# ---------------------------------------------------------------------------

message(STATUS "")
message(STATUS "SCUFF-EM configuration")
message(STATUS "  C++ standard:      ${SCUFF_CXX_STANDARD}")
message(STATUS "  Shared libraries:  ${BUILD_SHARED_LIBS}")
message(STATUS "  Thread backend:    ${SCUFF_THREAD_BACKEND}")
message(STATUS "  HDF5:              ${HAVE_HDF5}")
message(STATUS "  GSL:               ${HAVE_LIBGSL}")
message(STATUS "  readline:          ${HAVE_LIBREADLINE}")
message(STATUS "  DCUTRI:            ${HAVE_DCUTRI}")
message(STATUS "  libGDSII:          ${HAVE_LIBGDSII}")
message(STATUS "  Install examples:  ${SCUFF_INSTALL_EXAMPLES}")
message(STATUS "  Install tests:     ${SCUFF_INSTALL_TESTS}")
message(STATUS "")

# ---------------------------------------------------------------------------
# Build and install
# ---------------------------------------------------------------------------

add_subdirectory(libs)
add_subdirectory(applications)
add_subdirectory(tests)

if(SCUFF_INSTALL_EXAMPLES)
  add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# pkg-config file
# ---------------------------------------------------------------------------

set(SCUFF_PC_PRIVATE_LIBS "")
if(HAVE_HDF5)
  list(APPEND SCUFF_PC_PRIVATE_LIBS ${HDF5_HL_LIBRARIES} ${HDF5_LIBRARIES})
endif()
if(HAVE_LIBGSL)
  list(APPEND SCUFF_PC_PRIVATE_LIBS ${GSL_LIBRARIES})
endif()
list(APPEND SCUFF_PC_PRIVATE_LIBS ${LAPACK_LIBRARIES} ${BLAS_LIBRARIES})
if(USE_OPENMP)
  list(APPEND SCUFF_PC_PRIVATE_LIBS ${OpenMP_CXX_FLAGS})
endif()
if(CMAKE_THREAD_LIBS_INIT)
  list(APPEND SCUFF_PC_PRIVATE_LIBS ${CMAKE_THREAD_LIBS_INIT})
endif()
if(HAVE_DCUTRI)
  list(APPEND SCUFF_PC_PRIVATE_LIBS "${DCUTRI_LIBRARY}")
endif()
if(HAVE_LIBGDSII)
  list(APPEND SCUFF_PC_PRIVATE_LIBS "${GDSII_LIBRARY}")
endif()
string(JOIN " " SCUFF_PC_PRIVATE_LIBS ${SCUFF_PC_PRIVATE_LIBS})
string(STRIP "${SCUFF_PC_PRIVATE_LIBS}" SCUFF_PC_PRIVATE_LIBS)

# Make the pkg-config file relocatable so that `cmake --install --prefix`
# and DESTDIR staging both produce a usable file.
set(_scuff_pc_dir "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig")
file(RELATIVE_PATH SCUFF_PC_PREFIX_REL "${_scuff_pc_dir}" "${CMAKE_INSTALL_PREFIX}")
file(RELATIVE_PATH SCUFF_PC_LIBDIR_REL "${_scuff_pc_dir}" "${CMAKE_INSTALL_FULL_LIBDIR}")
file(RELATIVE_PATH SCUFF_PC_INCLUDEDIR_REL "${_scuff_pc_dir}" "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
file(RELATIVE_PATH SCUFF_PC_DATADIR_REL "${_scuff_pc_dir}" "${CMAKE_INSTALL_FULL_DATADIR}")

configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/scuff-em.pc.in"
  "${CMAKE_BINARY_DIR}/scuff-em.pc"
  @ONLY
)
install(FILES "${CMAKE_BINARY_DIR}/scuff-em.pc"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
)

# ---------------------------------------------------------------------------
# CMake package config
# ---------------------------------------------------------------------------

include(CMakePackageConfigHelpers)

set(SCUFF_CMAKE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/scuff-em")

write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/scuff-emConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/scuff-emConfig.cmake.in"
  "${CMAKE_BINARY_DIR}/scuff-emConfig.cmake"
  INSTALL_DESTINATION "${SCUFF_CMAKE_INSTALL_DIR}"
)

install(FILES
  "${CMAKE_BINARY_DIR}/scuff-emConfig.cmake"
  "${CMAKE_BINARY_DIR}/scuff-emConfigVersion.cmake"
  DESTINATION "${SCUFF_CMAKE_INSTALL_DIR}"
)

install(EXPORT scuff-emTargets
  FILE scuff-emTargets.cmake
  NAMESPACE scuff-em::
  DESTINATION "${SCUFF_CMAKE_INSTALL_DIR}"
)
