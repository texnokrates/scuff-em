# Shared target helpers for the SCUFF-EM CMake build.

# Common interface target providing include paths, feature definitions and
# warning/sanitizer options to every C/C++ target in the project.
add_library(scuff_common INTERFACE)
target_compile_features(scuff_common INTERFACE cxx_std_${SCUFF_CXX_STANDARD})
target_compile_definitions(scuff_common INTERFACE
  HAVE_CONFIG_H=1
  "VERSIONSTRING=\"${SCUFF_VERSION_STRING}\""
)
target_include_directories(scuff_common INTERFACE
  "${CMAKE_BINARY_DIR}/include"
  ${SCUFF_INTERNAL_INCLUDE_DIRS}
)

if(SCUFF_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(scuff_common INTERFACE
    -Wall
    -W
    -Wcast-qual
    -Wpointer-arith
    -Wcast-align
    -Wno-long-long
    -Wshadow
    -Wwrite-strings
    -Wredundant-decls
  )
  if(SCUFF_WARNINGS_AS_ERRORS)
    target_compile_options(scuff_common INTERFACE -Werror)
  endif()
endif()

if(SCUFF_ENABLE_SANITIZERS)
  target_compile_options(scuff_common INTERFACE
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
  )
  target_link_options(scuff_common INTERFACE
    -fsanitize=address,undefined
  )
endif()

# Create one of the internal OBJECT libraries.  All internal modules share the
# same include paths and feature macros; their source lists are always explicit.
function(scuff_add_module name)
  add_library(${name} OBJECT ${ARGN})
  target_link_libraries(${name} PRIVATE scuff_common)
endfunction()

# Create an installed command-line application.
function(scuff_add_application name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE scuff scuff_common)
  # Some applications include their own headers with angle brackets, which
  # worked with the old in-directory Autotools build.
  list(GET ARGN 0 _first_source)
  get_filename_component(_source_dir "${_first_source}" DIRECTORY)
  if(_source_dir)
    target_include_directories(${name} PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/${_source_dir}"
    )
  endif()
  install(TARGETS ${name}
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
endfunction()
