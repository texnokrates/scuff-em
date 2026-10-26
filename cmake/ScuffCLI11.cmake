# CLI11 dependency for SCUFF-EM.
#
# The default build uses the vendored single header.  Distributions that
# package CLI11 separately can set SCUFF_USE_SYSTEM_CLI11=ON.

add_library(scuff_cli11 INTERFACE)

if(SCUFF_USE_SYSTEM_CLI11)
  find_package(CLI11 2.4 CONFIG REQUIRED)
  target_link_libraries(scuff_cli11 INTERFACE CLI11::CLI11)
  message(STATUS "SCUFF-EM: using system CLI11")
else()
  target_include_directories(scuff_cli11 INTERFACE
    "${PROJECT_SOURCE_DIR}/third_party/CLI11"
  )
  message(STATUS "SCUFF-EM: using vendored CLI11 2.7.2")
endif()

install(FILES "${PROJECT_SOURCE_DIR}/third_party/CLI11/LICENSE"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/scuff-em"
  RENAME CLI11-LICENSE
)
