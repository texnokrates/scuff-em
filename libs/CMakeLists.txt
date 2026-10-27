# Internal SCUFF-EM modules.
#
# The internal modules are built as OBJECT libraries and aggregated into the
# single installed `scuff` library.  This mirrors the historical libtool
# behaviour and avoids artificial shared-library boundaries and target cycles.

scuff_add_module(scuff_hrutil
  libhrutil/libhrutil.cc
  libhrutil/ProcessArguments.cc
  libhrutil/ProcessOptions.cc
  libhrutil/ScuffCLI.cc
  libhrutil/Vector.cc
)
target_link_libraries(scuff_hrutil PRIVATE scuff_cli11)

scuff_add_module(scuff_hmat
  libhmat/LBWrappers.cc
  libhmat/C2ML.cc
  libhmat/HDF5IO.cc
  libhmat/GetEntries.cc
  libhmat/HMatrix.cc
  libhmat/HVector.cc
  libhmat/SMatrix.cc
  libhmat/Sort.cc
  libhmat/TextIO.cc
)

scuff_add_module(scuff_sgjc
  libSGJC/pcubature.c
  libSGJC/hcubature.c
  libSGJC/libSGJC.c
)

scuff_add_module(scuff_mdinterp
  libMDInterp/BinSearch.c
  libMDInterp/Interp1D.cc
  libMDInterp/Interp2D.cc
  libMDInterp/Interp3D.cc
  libMDInterp/Interp4D.cc
  libMDInterp/InterpND.cc
  libMDInterp/freadEC.c
)

# cmatheval was historically compiled as C++ (`CC = $(CXX)` in its
# Makefile.am).  Preserve that behaviour during the build-system migration.
scuff_add_module(scuff_cmatheval
  libMatProp/cmatheval/parser.c
  libMatProp/cmatheval/scanner.c
  libMatProp/cmatheval/matheval.c
  libMatProp/cmatheval/node.c
  libMatProp/cmatheval/symbol_table.c
  libMatProp/cmatheval/xmalloc.c
  libMatProp/cmatheval/xmath.c
)
set_source_files_properties(
  libMatProp/cmatheval/parser.c
  libMatProp/cmatheval/scanner.c
  libMatProp/cmatheval/matheval.c
  libMatProp/cmatheval/node.c
  libMatProp/cmatheval/symbol_table.c
  libMatProp/cmatheval/xmalloc.c
  libMatProp/cmatheval/xmath.c
  PROPERTIES LANGUAGE CXX
)
target_include_directories(scuff_cmatheval PRIVATE
  "${PROJECT_SOURCE_DIR}/libs/libMatProp/cmatheval"
)

scuff_add_module(scuff_matprop
  libMatProp/libMatProp.cc
  libMatProp/UserDefinedMaterials.cc
)

scuff_add_module(scuff_triint
  libTriInt/libTriInt.cc
  libTriInt/CCRules.cc
  libTriInt/LebedevRules.cc
  libTriInt/CCCubature.cc
  libTriInt/TCR.cc
  libTriInt/BZIntegration.cc
  libTriInt/LatticeSum.cc
  libTriInt/IntegrateCliffFunction.cc
)

scuff_add_module(scuff_spherical
  libSpherical/DifferentialOperators.cc
  libSpherical/libSpherical.cc
  libSpherical/AmosBessel.cc
  libSpherical/TranslationMatrices.cc
  libSpherical/VectorSphericalWaves.cc
  libSpherical/zbsubs.c
  libSpherical/machcon.c
  libSpherical/drc3jm.c
)

scuff_add_module(scuff_incfield
  libIncField/IncField.cc
  libIncField/IncFieldList.cc
  libIncField/GaussianBeam.cc
  libIncField/PlaneWave.cc
  libIncField/PointSource.cc
  libIncField/SphericalWave.cc
)

scuff_add_module(scuff_substrate
  libSubstrate/libSubstrate.cc
  libSubstrate/FullWave.cc
  libSubstrate/gFrak.cc
  libSubstrate/GTwiddle.cc
  libSubstrate/MOI.cc
  libSubstrate/SommerfeldIntegrator.cc
  libSubstrate/Static.cc
)

scuff_add_module(scuff_staticsolver
  libStaticSolver/StaticSolver.cc
  libStaticSolver/Excitations.cc
  libStaticSolver/GetPPI.cc
  libStaticSolver/GetPhiE.cc
  libStaticSolver/PostProcessing.cc
)

scuff_add_module(scuff_scuffsolver
  libscuffSolver/MOIIntegrals.cc
  libscuffSolver/OutputModules.cc
  libscuffSolver/scuffSolver.cc
  libscuffSolver/RWGPorts.cc
)

scuff_add_module(scuff_core
  libscuff/AssembleBEMMatrix2018.cc
  libscuff/AssembleBEMMatrix.cc
  libscuff/AssembleRHSVector.cc
  libscuff/AssessPanelPair.cc
  libscuff/CalcGC.cc
  libscuff/DSIPFT.cc
  libscuff/EdgeEdgeInteractions.cc
  libscuff/EMTPFT.cc
  libscuff/EPPFT.cc
  libscuff/EquivalentEdgePairs.cc
  libscuff/ExpandCurrentDistribution.cc
  libscuff/Faddeeva.cc
  libscuff/FIBBICache.cc
  libscuff/FIPPICache.cc
  libscuff/GBarAccelerator.cc
  libscuff/GBarVDEwald.cc
  libscuff/GCMatrixElements.cc
  libscuff/GetDipoleMoments.cc
  libscuff/GetDyadicGFs.cc
  libscuff/GetFields.cc
  libscuff/GetNearFields.cc
  libscuff/GetPFT.cc
  libscuff/GetSphericalMoments.cc
  libscuff/GTransformation.cc
  libscuff/InitEdgeList.cc
  libscuff/MomentPFT.cc
  libscuff/OPFT.cc
  libscuff/PanelCubature.cc
  libscuff/PanelPanelInteractions.cc
  libscuff/PBCSetup.cc
  libscuff/PointInObject.cc
  libscuff/QDFIPPI.cc
  libscuff/QIFIPPICubature.cc
  libscuff/QIFIPPITaylorDuffy.cc
  libscuff/QIFIPPITaylorDuffyV2P0.cc
  libscuff/ParseMeshFiles.cc
  libscuff/RWGGeometry.cc
  libscuff/RWGSurface.cc
  libscuff/rwlock.cc
  libscuff/SurfaceSurfaceInteractions.cc
  libscuff/TaylorDuffy.cc
  libscuff/Visualize.cc
)

# ---------------------------------------------------------------------------
# Module-specific external dependencies
# ---------------------------------------------------------------------------

if(HAVE_HDF5)
  target_link_libraries(scuff_hmat PRIVATE ${SCUFF_HDF5_TARGETS})
endif()

if(HAVE_LIBGSL)
  target_link_libraries(scuff_spherical PRIVATE ${SCUFF_GSL_TARGETS})
endif()

if(HAVE_DCUTRI)
  target_link_libraries(scuff_triint PRIVATE "${DCUTRI_LIBRARY}")
endif()

if(HAVE_LIBGDSII)
  if(GDSII_INCLUDE_DIR)
    target_include_directories(scuff_scuffsolver PRIVATE "${GDSII_INCLUDE_DIR}")
  endif()
  target_link_libraries(scuff_scuffsolver PRIVATE "${GDSII_LIBRARY}")
endif()

if(USE_OPENMP)
  target_link_libraries(scuff_common INTERFACE OpenMP::OpenMP_CXX)
endif()

# ---------------------------------------------------------------------------
# Aggregate installed library
# ---------------------------------------------------------------------------

set(SCUFF_OBJECT_LIBRARIES
  scuff_hrutil
  scuff_hmat
  scuff_sgjc
  scuff_mdinterp
  scuff_cmatheval
  scuff_matprop
  scuff_triint
  scuff_spherical
  scuff_incfield
  scuff_substrate
  scuff_staticsolver
  scuff_scuffsolver
  scuff_core
)

set(SCUFF_OBJECTS "")
foreach(_module IN LISTS SCUFF_OBJECT_LIBRARIES)
  list(APPEND SCUFF_OBJECTS "$<TARGET_OBJECTS:${_module}>")
endforeach()

if(BUILD_SHARED_LIBS)
  add_library(scuff SHARED ${SCUFF_OBJECTS})
else()
  add_library(scuff STATIC ${SCUFF_OBJECTS})
endif()
set_target_properties(scuff PROPERTIES
  LINKER_LANGUAGE CXX
  OUTPUT_NAME scuff
  VERSION "${SCUFF_SHARED_VERSION}"
  SOVERSION "${SCUFF_SHARED_SOVERSION}"
)

target_include_directories(scuff PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libscuff>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libhrutil>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libhmat>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libSGJC>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libMDInterp>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libMatProp>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libMatProp/cmatheval>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libTriInt>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libSpherical>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libIncField>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libSubstrate>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libStaticSolver>
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/libs/libscuffSolver>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/scuff-em>
)

set(SCUFF_LINK_LIBRARIES
  LAPACK::LAPACK
  BLAS::BLAS
  Threads::Threads
)
if(HAVE_HDF5)
  list(APPEND SCUFF_LINK_LIBRARIES ${SCUFF_HDF5_TARGETS})
endif()
if(HAVE_LIBGSL)
  list(APPEND SCUFF_LINK_LIBRARIES ${SCUFF_GSL_TARGETS})
endif()
if(USE_OPENMP)
  list(APPEND SCUFF_LINK_LIBRARIES OpenMP::OpenMP_CXX)
endif()
if(HAVE_DCUTRI)
  list(APPEND SCUFF_LINK_LIBRARIES "${DCUTRI_LIBRARY}")
endif()
if(HAVE_LIBGDSII)
  list(APPEND SCUFF_LINK_LIBRARIES "${GDSII_LIBRARY}")
endif()

target_link_libraries(scuff
  PRIVATE
    ${SCUFF_LINK_LIBRARIES}
)

if(SCUFF_ENABLE_SANITIZERS)
  target_link_options(scuff PRIVATE -fsanitize=address,undefined)
endif()

install(TARGETS scuff
  EXPORT scuff-emTargets
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/scuff-em"
)

# ---------------------------------------------------------------------------
# Public headers (installed flat under include/scuff-em, as before)
# ---------------------------------------------------------------------------

set(SCUFF_PUBLIC_HEADERS
  libhrutil/libhrutil.h
  libhrutil/Owner.h
  libhmat/libhmat.h
  libSGJC/libSGJC.h
  libMDInterp/libMDInterp.h
  libMatProp/libMatProp.h
  libMatProp/cmatheval/cmatheval.h
  libTriInt/libTriInt.h
  libTriInt/BZIntegration.h
  libSpherical/libSpherical.h
  libIncField/libIncField.h
  libIncField/libVec.h
  libSubstrate/libSubstrate.h
  libStaticSolver/StaticSolver.h
  libscuffSolver/scuffSolver.h
  libscuff/libscuff.h
  libscuff/EquivalentEdgePairs.h
  libscuff/GTransformation.h
  libscuff/GBarAccelerator.h
  libscuff/PFTOptions.h
  libscuff/PanelCubature.h
)

install(FILES ${SCUFF_PUBLIC_HEADERS}
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/scuff-em"
)
