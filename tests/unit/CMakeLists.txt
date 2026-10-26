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

if(BUILD_TESTING)
  add_test(NAME UnitTests COMMAND scuff-unit-tests)
  set_tests_properties(UnitTests PROPERTIES TIMEOUT 300)

  add_test(NAME CliOptions COMMAND scuff-cli-tests)
  set_tests_properties(CliOptions PROPERTIES TIMEOUT 120)

  add_test(NAME TypedCli COMMAND scuff-typed-cli-tests)
  set_tests_properties(TypedCli PROPERTIES TIMEOUT 120)
endif()
