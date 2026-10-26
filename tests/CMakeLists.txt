# Regression tests and the CheckSCUFFData helper.

add_executable(CheckSCUFFData CheckSCUFFData.cc)
target_link_libraries(CheckSCUFFData PRIVATE scuff scuff_common)

if(SCUFF_INSTALL_TESTS)
  install(TARGETS CheckSCUFFData
    RUNTIME DESTINATION "${CMAKE_INSTALL_DATADIR}/scuff-em/tests"
  )
endif()

set(MIE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Mie")
set(MIE_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/Mie")
set(FRESNEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Fresnel")
set(FRESNEL_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/Fresnel")

set(MIE_DATA
  OmegaFile
  EPFile
  LossySphere_327.scuffgeo
  Sphere_327.msh
  Checklist.DSIPFT
  Checklist.EMTPFT
  Checklist.Moments
  Checklist.EPFile.total
)
set(MIE_REFERENCE_DATA
  LossySphere_327.DSIPFT
  LossySphere_327.EMTPFT
  LossySphere_327.Moments
  LossySphere_327.EPFile.total
  LossySphere_327.Runtime
)

set(FRESNEL_DATA
  E10HalfSpace_40.scuffgeo
  Square_40.msh
  Fresnel.Checklist
)
set(FRESNEL_REFERENCE_DATA
  E10HalfSpace_40.Runtime
  E10HalfSpace_40.transmission
)

file(MAKE_DIRECTORY "${MIE_BUILD_DIR}/reference")
file(MAKE_DIRECTORY "${FRESNEL_BUILD_DIR}/reference")
foreach(_file IN LISTS MIE_DATA)
  configure_file("${MIE_SOURCE_DIR}/${_file}" "${MIE_BUILD_DIR}/${_file}" COPYONLY)
endforeach()
foreach(_file IN LISTS MIE_REFERENCE_DATA)
  configure_file("${MIE_SOURCE_DIR}/reference/${_file}"
                 "${MIE_BUILD_DIR}/reference/${_file}" COPYONLY)
endforeach()
foreach(_file IN LISTS FRESNEL_DATA)
  configure_file("${FRESNEL_SOURCE_DIR}/${_file}"
                 "${FRESNEL_BUILD_DIR}/${_file}" COPYONLY)
endforeach()
foreach(_file IN LISTS FRESNEL_REFERENCE_DATA)
  configure_file("${FRESNEL_SOURCE_DIR}/reference/${_file}"
                 "${FRESNEL_BUILD_DIR}/reference/${_file}" COPYONLY)
endforeach()

if(SCUFF_INSTALL_TESTS)
  set(_mie_install_files "")
  foreach(_file IN LISTS MIE_DATA)
    list(APPEND _mie_install_files "${MIE_SOURCE_DIR}/${_file}")
  endforeach()
  set(_mie_reference_install_files "")
  foreach(_file IN LISTS MIE_REFERENCE_DATA)
    list(APPEND _mie_reference_install_files "${MIE_SOURCE_DIR}/reference/${_file}")
  endforeach()
  set(_fresnel_install_files "")
  foreach(_file IN LISTS FRESNEL_DATA)
    list(APPEND _fresnel_install_files "${FRESNEL_SOURCE_DIR}/${_file}")
  endforeach()
  set(_fresnel_reference_install_files "")
  foreach(_file IN LISTS FRESNEL_REFERENCE_DATA)
    list(APPEND _fresnel_reference_install_files "${FRESNEL_SOURCE_DIR}/reference/${_file}")
  endforeach()

  install(FILES ${_mie_install_files}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/scuff-em/tests/Mie"
  )
  install(FILES ${_mie_reference_install_files}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/scuff-em/tests/Mie/reference"
  )
  install(FILES ${_fresnel_install_files}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/scuff-em/tests/Fresnel"
  )
  install(FILES ${_fresnel_reference_install_files}
    DESTINATION "${CMAKE_INSTALL_DATADIR}/scuff-em/tests/Fresnel/reference"
  )
  install(PROGRAMS
    "${MIE_SOURCE_DIR}/TestMie.sh"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/scuff-em/tests/Mie"
  )
  install(PROGRAMS
    "${FRESNEL_SOURCE_DIR}/TestFresnel.sh"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/scuff-em/tests/Fresnel"
  )
endif()

if(BUILD_TESTING)
  add_test(NAME Mie
    COMMAND "${CMAKE_COMMAND}" -E env
      "CHECKSCUFFDATA=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/CheckSCUFFData"
      "PATH=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}:$ENV{PATH}"
      "OMP_NUM_THREADS=2"
      bash "${MIE_SOURCE_DIR}/TestMie.sh"
  )
  set_tests_properties(Mie PROPERTIES
    WORKING_DIRECTORY "${MIE_BUILD_DIR}"
    TIMEOUT 1800
  )

  add_test(NAME Fresnel
    COMMAND "${CMAKE_COMMAND}" -E env
      "CHECKSCUFFDATA=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/CheckSCUFFData"
      "PATH=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}:$ENV{PATH}"
      "OMP_NUM_THREADS=2"
      bash "${FRESNEL_SOURCE_DIR}/TestFresnel.sh"
  )
  set_tests_properties(Fresnel PROPERTIES
    WORKING_DIRECTORY "${FRESNEL_BUILD_DIR}"
    TIMEOUT 1800
  )

  # Smoke test for the first typed CLI11 application migration.  It is a
  # small LMax=1 problem, so it runs quickly compared with the Mie suite.
  set(TMATRIX_CLI_DIR "${CMAKE_CURRENT_BINARY_DIR}/TMatrixCli")
  file(MAKE_DIRECTORY "${TMATRIX_CLI_DIR}")
  foreach(_file SphericalShell.scuffgeo Sphere_R1P0.msh Sphere_R0P5.msh)
    configure_file("${PROJECT_SOURCE_DIR}/examples/SphericalShell/${_file}"
                   "${TMATRIX_CLI_DIR}/${_file}" COPYONLY)
  endforeach()
  add_test(NAME TMatrixCli
    COMMAND scuff-tmatrix
      --geometry SphericalShell.scuffgeo
      --LMax 1
      --omega 0.001
      --FileBase "${TMATRIX_CLI_DIR}/TMatrixCli"
  )
  set_tests_properties(TMatrixCli PROPERTIES
    WORKING_DIRECTORY "${TMATRIX_CLI_DIR}"
    TIMEOUT 300
  )
endif()

add_subdirectory(unit)
