# ---------------------------------------------------------------------------
# Documentation targets for SCUFF-EM.
#
#   memos  -- rebuild the LaTeX technical memos in doc/docs/tex via
#             latexmk (in place; the rendered PDFs are tracked in git)
#   docs   -- run Doxygen to produce the full HTML documentation
#             (API reference extracted from the library headers plus
#             the markdown documentation tree) in
#             ${CMAKE_BINARY_DIR}/doc/html, with the memo PDFs copied
#             alongside as tex/*.pdf
#
# Options:
#   SCUFF_BUILD_DOCS  -- provide the `docs` target (default: on if
#                        Doxygen was found)
#   SCUFF_BUILD_MEMOS -- provide the `memos` target (default: on if
#                        latexmk and pdflatex were found)
# ---------------------------------------------------------------------------

find_package(Doxygen 1.9 QUIET)
find_program(LATEXMK_EXECUTABLE latexmk)
find_program(PDFLATEX_EXECUTABLE pdflatex)

if(DOXYGEN_FOUND)
  set(SCUFF_BUILD_DOCS_DEFAULT ON)
else()
  set(SCUFF_BUILD_DOCS_DEFAULT OFF)
  message(STATUS "Doxygen not found; HTML documentation disabled")
endif()

if(LATEXMK_EXECUTABLE AND PDFLATEX_EXECUTABLE)
  set(SCUFF_BUILD_MEMOS_DEFAULT ON)
else()
  set(SCUFF_BUILD_MEMOS_DEFAULT OFF)
  message(STATUS "latexmk/pdflatex not found; memo rebuilding disabled")
endif()

option(SCUFF_BUILD_DOCS  "Provide the `docs` target (Doxygen HTML documentation)"
  ${SCUFF_BUILD_DOCS_DEFAULT})
option(SCUFF_BUILD_MEMOS "Provide the `memos` target (LaTeX technical memos)"
  ${SCUFF_BUILD_MEMOS_DEFAULT})

# ---------------------------------------------------------------------------
# memos: rebuild the LaTeX technical memos
# ---------------------------------------------------------------------------

set(SCUFF_MEMO_SOURCES
  Ewald
  Formulations
  Fresnel
  FullWaveSubstrate
  Junction
  lsInnards
  NearFields
  PFT
  PFTandNEQFormulas
  ScatteringAmplitudes
  scuff-ldos
  scuff-neq2
  scuff-neq
  scuff-rf
  scuff-scatter-periodic
  scuff-scatter
  scuffSpherical
  scuff-static
  scuff-transmission
  StaticDielectricSubstrate
  SurfaceImpedance
)

if(SCUFF_BUILD_MEMOS)
  add_custom_target(memos
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/doc/docs/tex
            ${LATEXMK_EXECUTABLE} -pdf
            ${SCUFF_MEMO_SOURCES}
    COMMENT "Building LaTeX technical memos in doc/docs/tex"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------
# docs: Doxygen HTML documentation
# ---------------------------------------------------------------------------

if(SCUFF_BUILD_DOCS)
  find_program(SCUFF_DOT_EXECUTABLE dot)
  if(SCUFF_DOT_EXECUTABLE)
    set(SCUFF_HAVE_DOT YES)
  else()
    set(SCUFF_HAVE_DOT NO)
  endif()

  set(SCUFF_DOXYFILE ${CMAKE_BINARY_DIR}/doc/Doxyfile)
  configure_file(
    ${CMAKE_SOURCE_DIR}/doc/Doxyfile.in
    ${SCUFF_DOXYFILE}
    @ONLY
  )

  # copy_only_memos.cmake materializes the rendered memo PDFs inside the
  # HTML output tree (where the documentation links expect them) without
  # dragging along LaTeX build artifacts.
  set(SCUFF_COPY_MEMOS_SCRIPT ${CMAKE_BINARY_DIR}/doc/CopyMemoPDFs.cmake)
  file(WRITE ${SCUFF_COPY_MEMOS_SCRIPT}
    "file(GLOB _MEMO_PDFS \"\${SRC_DIR}/*.pdf\")\n"
    "foreach(_PDF \${_MEMO_PDFS})\n"
    "  file(COPY \"\${_PDF}\" DESTINATION \"\${DST_DIR}\")\n"
    "endforeach()\n")

  if(SCUFF_BUILD_MEMOS)
    set(SCUFF_DOCS_DEPENDS memos)
  else()
    set(SCUFF_DOCS_DEPENDS "")
  endif()

  add_custom_target(docs
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/doc/html/tex
    COMMAND ${DOXYGEN_EXECUTABLE} ${SCUFF_DOXYFILE}
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_SOURCE_DIR}/doc/docs/tex
            -DDST_DIR=${CMAKE_BINARY_DIR}/doc/html/tex
            -P ${SCUFF_COPY_MEMOS_SCRIPT}
    DEPENDS ${SCUFF_DOCS_DEPENDS}
    COMMENT "Generating HTML documentation with Doxygen"
    VERBATIM
  )
endif()
