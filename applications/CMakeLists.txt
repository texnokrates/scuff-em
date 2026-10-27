# Command-line applications that are part of the maintained build.
# Dormant scuff-cas2D and scuff-microstrip are intentionally not ported.

scuff_add_application(scuff-analyze
  scuff-analyze/scuff-analyze.cc
)

scuff_add_application(scuff-cas3D
  scuff-cas3D/CasimirIntegrand.cc
  scuff-cas3D/CreateSC3Data.cc
  scuff-cas3D/SumsIntegrals.cc
  scuff-cas3D/scuff-cas3D.cc
)
# This application calls LAPACK directly.
target_link_libraries(scuff-cas3D PRIVATE LAPACK::LAPACK BLAS::BLAS)

scuff_add_application(scuff-caspol
  scuff-caspol/scuff-caspol.cc
  scuff-caspol/CreateSCPData.cc
  scuff-caspol/GetCPIntegrand.cc
  scuff-caspol/PolModel.cc
)

scuff_add_application(scuff-ldos
  scuff-ldos/scuff-ldos.cc
  scuff-ldos/CreateSLDData.cc
  scuff-ldos/GetLDOS.cc
  scuff-ldos/AnalyticalDGFs.cc
)

scuff_add_application(scuff-neq
  scuff-neq/scuff-neq.cc
  scuff-neq/WriteFlux.cc
  scuff-neq/CreateSNEQData.cc
)

scuff_add_application(scuff-integrate
  scuff-neq/scuff-integrate.cc
)

scuff_add_application(scuff-plotEpsMu
  scuff-plotEpsMu/scuff-plotEpsMu.cc
)

scuff_add_application(scuff-rf
  scuff-rf/scuff-rf.cc
  scuff-rf/EdgePanelInteractions.cc
  scuff-rf/GetPanelPotentials.cc
  scuff-rf/GetPortVoltages.cc
  scuff-rf/OutputModules.cc
  scuff-rf/RWGPorts.cc
  scuff-rf/ZSConvert.cc
)

scuff_add_application(scuff-scatter
  scuff-scatter/scuff-scatter.cc
  scuff-scatter/OutputModules.cc
)
# Second typed CLI11 migration.
target_link_libraries(scuff-scatter PRIVATE scuff_cli11)

scuff_add_application(scuff-spectrum
  scuff-spectrum/scuff-spectrum.cc
  scuff-spectrum/OutputModules.cc
  scuff-spectrum/libBeyn.cc
)

scuff_add_application(scuff-static
  scuff-static/scuff-static.cc
  scuff-static/BMAccelerator.cc
  scuff-static/OutputModules.cc
)

scuff_add_application(scuff-tmatrix
  scuff-tmatrix/scuff-tmatrix.cc
)
# First typed CLI11 migration.
target_link_libraries(scuff-tmatrix PRIVATE scuff_cli11)

scuff_add_application(scuff-transmission
  scuff-transmission/scuff-transmission.cc
  scuff-transmission/GetFlux.cc
  scuff-transmission/GetAmplitudes.cc
)

# ---------------------------------------------------------------------------
# Optional developer programs
# ---------------------------------------------------------------------------

if(SCUFF_BUILD_DEV_PROGRAMS)
  function(scuff_add_dev_program name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE scuff scuff_common)
  endfunction()

  scuff_add_dev_program(tProcessOptions
    ../libs/libhrutil/tProcessOptions.cc
  )

  foreach(_prog
      tLUSolve tMultiply tReadFromFile tTextIO tlibhmat2
      tQR tGetEntries tSMatrix)
    scuff_add_dev_program(${_prog}
      ../libs/libhmat/${_prog}.cc
    )
  endforeach()
  if(SCUFF_READLINE_LIBRARY)
    target_link_libraries(tGetEntries PRIVATE "${SCUFF_READLINE_LIBRARY}")
  endif()

  scuff_add_dev_program(tlibSGJC
    ../libs/libSGJC/tlibSGJC.cc
  )
  scuff_add_dev_program(tlibSGJC2
    ../libs/libSGJC/tlibSGJC2.cc
  )

  set_source_files_properties(
    ../libs/libMatProp/cmatheval/tcmatheval.c
    PROPERTIES LANGUAGE CXX
  )
  scuff_add_dev_program(tcmatheval
    ../libs/libMatProp/cmatheval/tcmatheval.c
  )

  scuff_add_dev_program(tBeyn411
    scuff-spectrum/tBeyn411.cc
    scuff-spectrum/libBeyn.cc
  )
  target_include_directories(tBeyn411 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/scuff-spectrum"
  )
endif()
