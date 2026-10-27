# Unit tests for libhrutil and the command-line migration.
#
# These tests link against the aggregate library so that they exercise the
# same code that the installed applications use.  They are intentionally
# dependency-free: a tiny assertion harness keeps the CI footprint small and
# avoids adding a test framework to the public build.

add_executable(scuff-unit-tests TestMain.cc)
target_link_libraries(scuff-unit-tests PRIVATE scuff scuff_common)

add_executable(scuff-cli-tests CliOptionsTest.cc)
target_link_libraries(scuff-cli-tests PRIVATE scuff scuff_common)

add_executable(scuff-typed-cli-tests ScuffCLITest.cc)
target_link_libraries(scuff-typed-cli-tests PRIVATE scuff scuff_common scuff_cli11)

add_executable(scuff-geometry-tests GeometryTest.cc)
target_link_libraries(scuff-geometry-tests PRIVATE scuff scuff_common)

# The geometry tests need real geometry and mesh files at run time; they
# are copied into the build tree so the test does not depend on the
# caller's working directory.
set(GEOMETRY_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/GeometryTestData")
file(MAKE_DIRECTORY "${GEOMETRY_TEST_DIR}")
foreach(_file "${PROJECT_SOURCE_DIR}/tests/Mie/LossySphere_327.scuffgeo"
              "${PROJECT_SOURCE_DIR}/tests/Mie/Sphere_327.msh"
              "${CMAKE_CURRENT_SOURCE_DIR}/TwoSpheres.scuffgeo")
  get_filename_component(_name "${_file}" NAME)
  configure_file("${_file}" "${GEOMETRY_TEST_DIR}/${_name}" COPYONLY)
endforeach()

if(BUILD_TESTING)
  add_test(NAME UnitTests COMMAND scuff-unit-tests)
  set_tests_properties(UnitTests PROPERTIES TIMEOUT 300)

  add_test(NAME CliOptions COMMAND scuff-cli-tests)
  set_tests_properties(CliOptions PROPERTIES TIMEOUT 120)

  add_test(NAME TypedCli COMMAND scuff-typed-cli-tests)
  set_tests_properties(TypedCli PROPERTIES TIMEOUT 120)

  add_test(NAME Geometry COMMAND scuff-geometry-tests)
  set_tests_properties(Geometry PROPERTIES
    WORKING_DIRECTORY "${GEOMETRY_TEST_DIR}"
    TIMEOUT 120)
endif()
