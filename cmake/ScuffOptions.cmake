include(GNUInstallDirs)
include(CMakeDependentOption)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

set(SCUFF_CXX_STANDARD "23" CACHE STRING "C++ standard used to build SCUFF-EM")
set_property(CACHE SCUFF_CXX_STANDARD PROPERTY STRINGS 17 20 23)

option(SCUFF_WITH_HDF5 "Enable HDF5 support" ON)

set(SCUFF_THREAD_BACKEND "OpenMP" CACHE STRING "Threading backend: OpenMP, Pthreads or None")
set_property(CACHE SCUFF_THREAD_BACKEND PROPERTY STRINGS OpenMP Pthreads None)

set(SCUFF_WITH_GSL "AUTO" CACHE STRING "GSL support: AUTO, ON or OFF")
set_property(CACHE SCUFF_WITH_GSL PROPERTY STRINGS AUTO ON OFF)

set(SCUFF_WITH_READLINE "AUTO" CACHE STRING "readline support: AUTO, ON or OFF")
set_property(CACHE SCUFF_WITH_READLINE PROPERTY STRINGS AUTO ON OFF)

set(SCUFF_WITH_DCUTRI "AUTO" CACHE STRING "DCUTRI support: AUTO, ON or OFF")
set_property(CACHE SCUFF_WITH_DCUTRI PROPERTY STRINGS AUTO ON OFF)

set(SCUFF_WITH_GDSII "AUTO" CACHE STRING "libGDSII support: AUTO, ON or OFF")
set_property(CACHE SCUFF_WITH_GDSII PROPERTY STRINGS AUTO ON OFF)

option(SCUFF_BUILD_DEV_PROGRAMS "Build internal developer test programs" OFF)

option(SCUFF_USE_SYSTEM_CLI11
  "Use a system CLI11 instead of the vendored single header" OFF)

option(SCUFF_INSTALL_EXAMPLES "Install example files" ON)
option(SCUFF_INSTALL_TESTS "Install test data and scripts" ON)

option(SCUFF_WARNINGS "Enable compiler warnings" ON)
option(SCUFF_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SCUFF_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
