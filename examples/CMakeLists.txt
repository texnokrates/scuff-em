# Install example data.  The list mirrors examples/Makefile.am.  Build-system
# files and accidental build artifacts are excluded.

set(SCUFF_EXAMPLE_DIRECTORIES
  DiffractionPatterns
  CorrugatedDisc
  CubeTorus
  GetTMatrix
  HalfSpaceLDOS
  ImplicitSubstrate
  MicrostripDevices
  NanorodResonantModes
  PaulTrap
  PlateWithHole
  PerforatedThinFilm
  PlatonicSolids
  SiO2Spheres
  SiliconBeams
  SolidSphere
  SphericalShell
  StriplineCapacitor
  ThinFilm
  TwoBodyCapacitors
  WireAntenna
  YagiUdaAntennas
)

foreach(_example IN LISTS SCUFF_EXAMPLE_DIRECTORIES)
  install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${_example}/"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/scuff-em/examples/${_example}"
    PATTERN "Makefile*" EXCLUDE
    PATTERN ".deps" EXCLUDE
    PATTERN ".libs" EXCLUDE
    PATTERN "*.o" EXCLUDE
    PATTERN "*.lo" EXCLUDE
    PATTERN "*.la" EXCLUDE
    PATTERN ".git*" EXCLUDE
  )
endforeach()
