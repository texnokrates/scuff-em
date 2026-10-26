# Unit tests for libhrutil and the command-line migration.
#
# These tests link against the aggregate library so that they exercise the
# same code that the installed applications use.  They are intentionally
# dependency-free: a tiny assertion harness keeps the CI footprint small and
# avoids adding a test framework to the public build.

add_executable(scuff-unit-tests TestMain.cc)
target_link_libraries(scuff-unit-tests PRIVATE scuff scuff_common)

if(BUILD_TESTING)
  add_test(NAME UnitTests COMMAND scuff-unit-tests)
  set_tests_properties(UnitTests PROPERTIES TIMEOUT 300)
endif()
