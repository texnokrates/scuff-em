# CMake migration plan — phase 1 of the build/C++ modernization

Date: 2026-10-26
Status: **planning only — no CMake files added yet**

## Decisions from the user

- SWIG / Python bindings are obsolete. They must not appear in the CMake build.
- `libhdf5-dev` is now installed system-wide.
- The missing `<stdlib.h>` include was committed separately.
- Autotools is to be replaced by CMake.
- The C++ code will be rewritten in modern C++23 **after** a trustworthy CMake
  build exists. Build-system migration and C++ refactoring should not be mixed
  in the same change set.

## Goals of the CMake migration

1. Reproduce the current working targets with CMake:
   - one installed `libscuff` library;
   - all maintained command-line applications;
   - the `CheckSCUFFData` test helper;
   - Mie and Fresnel regression tests runnable through CTest;
   - optional HDF5, GSL, OpenMP/pthreads, readline, DCUTRI and GDSII support.
2. Generate the same install layout as Autotools (binaries, headers,
   pkg-config, tests, examples), with the pkg-config include path corrected.
3. Keep dependency checks and feature macros explicit and portable.
4. Provide a clean baseline for the subsequent C++23 rewrite:
   - compiler warnings and `compile_commands.json`;
   - no SWIG, no Python, no generated wrapper code;
   - support for sanitizers and clang-tidy;
   - source lists are explicit, not globbed.
5. Remove Autotools only after CMake parity is demonstrated.

Non-goals for the first CMake change:

- no numerical/code refactoring;
- no API redesign;
- no deletion of historical source trees yet;
- no C++ rewrite mixed into build-system commits.

## Environment and current baseline

- CMake 3.31.6 and Ninja 1.12.1 are available.
- Debian 13, GCC 14.2, gfortran 14.2.
- HDF5 1.14.5 is installed (`pkg-config hdf5` works; headers are in
  `/usr/include/hdf5/serial`, libraries in
  `/usr/lib/x86_64-linux-gnu/hdf5/serial`).
- The Autotools build currently passes both regression tests.
- Quick `g++ -std=c++23 -fsyntax-only` checks on representative active library
  and application sources passed. A full active-source audit remains to be
  done. Several stale, non-built files already fail under C++23 (e.g.
  `libs/libIncField/tPointSource.cc`, `libs/libSubstrate/FullWave_PlaneWave.cc`,
  `libs/libSubstrate/Interpolation.cc`, `libs/libStaticSolver/StaticSubstrate.cc`).
  This is why CMake must use **explicit source lists copied from the
  `Makefile.am` files**, never `file(GLOB ...)`.

## Current build inventory

### Libraries

| Current target | Kind | Notes / key dependencies |
|---|---|---|
| `libhrutil` | internal static | base utilities |
| `libhmat` | internal static | LAPACK/BLAS, optional HDF5 |
| `libSGJC` | internal static | C sources only |
| `libMDInterp` | internal static | `libhrutil`, `libhmat` |
| `libcmatheval` | internal static | C sources currently compiled as C++ |
| `libMatProp` | internal static | `libhrutil`, `libMDInterp`, `libhmat`, `libcmatheval` |
| `libTriInt` | internal static | `libhrutil`, `libSGJC`, `libhmat`, optional DCUTRI |
| `libSpherical` | internal static | `libhrutil`, `libhmat`, optional GSL |
| `libIncField` | internal static | `libhrutil`, `libTriInt`, `libSpherical`, `libSGJC`, `libhmat` |
| `libSubstrate` | internal static | `libhrutil`, `libhmat`, `libMatProp`, `libMDInterp`, `libSGJC`, `libSpherical`; calls `scuff::CalcGC` from `libscuff` |
| `libscuffSolver` | internal static | `libscuff`, `libhmat`, GDSII optional |
| `libStaticSolver` | internal static | `libscuff`, `libhmat`, `libTriInt`, etc. |
| `libscuff` | installed shared/static | all of the above are linked into it with whole-archive semantics |

Important: the current support libraries are not independent. `libSubstrate`
references `scuff::CalcGC`, while `libscuff` links `libSubstrate`;
`libscuffSolver` and `libStaticSolver` also depend on `libscuff` symbols.
CMake must not model these as a simple acyclic chain of shared libraries.

### Applications currently built

`scuff-analyze`, `scuff-caspol`, `scuff-cas3D`, `scuff-ldos`, `scuff-neq`
(plus `scuff-integrate`), `scuff-plotEpsMu`, `scuff-rf`, `scuff-scatter`,
`scuff-spectrum`, `scuff-static`, `scuff-tmatrix`, `scuff-transmission`.

Present in the tree but **not** in `applications/SUBDIRS` and therefore not
built by Autotools:

- `applications/scuff-cas2D` (also contains legacy Fortran sources and private
  `libTDRT` / `libAmosBessel` libraries);
- `applications/scuff-microstrip`.

Recommendation: keep both out of the default CMake build. Add a cache option
later if they are still wanted.

### Test and developer programs

- `tests/CheckSCUFFData` — installed test helper.
- `tests/Mie/TestMie.sh`, `tests/Fresnel/TestFresnel.sh` — regression scripts.
- Internal `noinst_PROGRAMS`: `tProcessOptions`, `tLUSolve`, `tMultiply`,
  `tReadFromFile`, `tTextIO`, `tlibhmat2`, `tQR`, `tGetEntries`, `tSMatrix`,
  `tlibSGJC`, `tlibSGJC2`, `tBeyn411`, `tcmatheval`.
- `unitTests/`, `interactiveTests/`, `libs/libSubstrate/tests/` and
  `libs/libscuffSolver/tests/` are not built by the default Autotools build.

Recommendation: build only `CheckSCUFFData` by default. Put internal test
programs behind `SCUFF_BUILD_DEV_PROGRAMS=OFF` and the unit/interactive suites
behind a separate option.

### Generated/committed build inputs

Committed, so CMake needs no flex/bison/perl step:

- `libs/libhmat/lapack_names.h`;
- `libs/libMatProp/cmatheval/parser.c` and `scanner.c`;
- `libs/libMatProp/cmatheval/matheval.c`.

Generated by the build system:

- `config.h` (feature macros);
- `scuff-em.pc` (from `scuff-em-pkgconfig.in`);
- application version string (`VERSIONSTRING`).

The current Makefiles use `git describe always --dirty` in most application
directories, which is invalid and returns an error. Only `scuff-tmatrix` uses
the correct `git describe --always --dirty`. CMake should compute the version
once and inject it centrally.

### Feature macros currently provided by `config.h`

These must be generated by CMake (or replaced by modern equivalents):

- `HAVE_HDF5`;
- `HAVE_DCUTRI`;
- `HAVE_LIBGDSII`;
- `HAVE_LIBGSL`, `HAVE_LIBGSLCBLAS`;
- `HAVE_LIBREADLINE` (and `HAVE_READLINE`, which one test file checks);
- `HAVE_EXECINFO_H`, `HAVE_BACKTRACE`;
- `HAVE_ISNAN`, `HAVE_ISINF`, `HAVE_ERF`, `HAVE_ERFC`, `HAVE_COPYSIGN`;
- `USE_OPENMP` / `USE_PTHREAD`;
- `HAVE_CXX11`, `HAVE_TR1`;
- `F77_FUNC`, `F77_FUNC_`;
- `PACKAGE_VERSION` / `VERSION`;
- `HAVE_CONFIG_H` (enable `#include "config.h"` paths).

For the C++23 era, define `HAVE_CXX11` but **do not** define `HAVE_TR1`.
Then code uses `<unordered_map>` instead of `<tr1/unordered_map>`.

### Install layout today

- Libraries: `libscuff.so.0.0.0`, symlinks `libscuff.so.0`, `libscuff.so`
  (libtool version info `0:0:0`).
- Public headers: `${includedir}/scuff-em/`.
- pkg-config: `${libdir}/pkgconfig/scuff-em.pc`.
  - Bug in the current file: `Cflags: -I${includedir}` while headers are
    installed in `${includedir}/scuff-em`.
- Applications: `${bindir}`.
- Tests: `${datadir}/scuff-em/tests/`, with data/scripts under `Mie/` and
  `Fresnel/`.
- Examples: `${datadir}/scuff-em/examples/<example>/`.
- No documentation installation step.

## Proposed CMake structure

```text
CMakeLists.txt
cmake/
  ScuffOptions.cmake
  ScuffDependencies.cmake
  ScuffFeatureChecks.cmake
  ScuffVersion.cmake
  config.h.in
  scuff-em.pc.in
  scuff-emConfig.cmake.in
  FindDCUTRI.cmake
  FindGDSII.cmake
libs/CMakeLists.txt
applications/CMakeLists.txt
tests/CMakeLists.txt
examples/CMakeLists.txt
```

Use per-directory `CMakeLists.txt` files to mirror the repository, but keep
source lists explicit and close to today's `Makefile.am` lists.

Suggested minimum: CMake 3.20. This is enough for object libraries and modern
target usage requirements. Avoid relying on very new features unless needed.

## Target design

### Object libraries for internal modules

Use CMake `OBJECT` libraries for the internal modules:

```text
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
scuff_scuffsolver
scuff_staticsolver
scuff_core
```

Reasons:

- avoids artificial shared-library boundaries;
- avoids CMake target cycles caused by `libSubstrate <-> libscuff`;
- reproduces the current whole-archive aggregation;
- is simpler than libtool `.la` semantics;
- allows the future C++ rewrite to split real modules deliberately.

The installed library is one target:

```cmake
add_library(scuff SHARED
  $<TARGET_OBJECTS:scuff_core>
  $<TARGET_OBJECTS:scuff_substrate>
  ...
)
set_target_properties(scuff PROPERTIES
  VERSION   0.0.0
  SOVERSION 0
  OUTPUT_NAME scuff
)
```

If static builds are required, the same object libraries can feed
`add_library(scuff STATIC ...)` without changing the module graph.

Alternative to consider later: ordinary static libraries linked into `scuff`
with `$<LINK_LIBRARY:WHOLE_ARCHIVE,...>`. That requires CMake >= 3.24 and still
needs care around the dependency cycle.

### Dependency ownership

Use target-level usage requirements rather than global include/LDFLAGS:

- `scuff_hmat`: `LAPACK::LAPACK`, `BLAS::BLAS`, optional `HDF5::HDF5`.
- `scuff_spherical`: `GSL::gsl`, `GSL::gslcblas` when enabled.
- `scuff_triint`: optional DCUTRI.
- `scuff_scuffsolver`: optional GDSII.
- `scuff` and applications: `OpenMP::OpenMP_CXX` or `Threads::Threads`.
- readline: only the developer/test programs that need it.

This also avoids the current pkg-config over-linking (`SCUFFLIBS` pulls
readline/HDF5/etc. into every consumer).

### Applications

Each application is an executable target with the source list from its
`Makefile.am`, linking `scuff`:

```cmake
add_executable(scuff-scatter scuff-scatter.cc OutputModules.cc)
target_link_libraries(scuff-scatter PRIVATE scuff)
```

Use `VERSIONSTRING` from a generated version header (or a central compile
definition) instead of running `git describe` in every subdirectory.

### Tests

Use CTest:

```cmake
include(CTest)
if(BUILD_TESTING)
  add_executable(CheckSCUFFData CheckSCUFFData.cc)
  target_link_libraries(CheckSCUFFData PRIVATE scuff)

  add_test(NAME Mie
    COMMAND ${CMAKE_COMMAND} -E env
      CHECKSCUFFDATA=$<TARGET_FILE:CheckSCUFFData>
      PATH=$<TARGET_FILE_DIR:scuff-scatter>:$<TARGET_FILE_DIR:scuff-transmission>:$ENV{PATH}
      ${CMAKE_CURRENT_SOURCE_DIR}/Mie/TestMie.sh)
endif()
```

Stage test data into the build tree or run each test in a dedicated build
directory. The scripts write output files, so tests must not write into the
source tree. Preserve the reference-file comparison through `CheckSCUFFData`.

## Dependency and feature detection

### Required

- `find_package(BLAS REQUIRED)` and `find_package(LAPACK REQUIRED)`.
- Fortran ABI support: enable language `Fortran` and use
  `FortranCInterface_HEADER(... MACRO_NAMESPACE "F77_")` to generate
  `F77_FUNC` / `F77_FUNC_`. This mirrors `AC_F77_WRAPPERS`; gfortran is already
  a documented build dependency.
- `find_package(Threads REQUIRED)`.

### Default-on optional features

- `SCUFF_WITH_HDF5=ON` — `find_package(HDF5 COMPONENTS C HL REQUIRED)`;
  `HDF5_PREFER_PARALLEL FALSE`.
- `SCUFF_THREAD_BACKEND=OpenMP` — `find_package(OpenMP REQUIRED)`.
  Alternatives: `Pthreads`, `None`. Define `USE_OPENMP` or `USE_PTHREAD`.
- `SCUFF_WITH_GSL=AUTO` — GSL is currently checked but not mandatory; define
  `HAVE_LIBGSL`.

### Auto-detected optional features

- readline (tests/dev tools);
- DCUTRI;
- GDSII;
- `execinfo.h` / `backtrace` and math functions.

Use custom Find modules or `pkg-config` where appropriate. HDF5 already works
through Debian's pkg-config file, so `FindHDF5` should be tested early; if its
HL component handling is inadequate, use the `hdf5` pkg-config module or
explicit `find_path`/`find_library` fallback.

### Configuration header

Generate `config.h` with `configure_file`. Define `HAVE_CONFIG_H=1` for all
targets. Keep `config.h` in the build tree and add its directory before the
source directory so a stale in-source `config.h` from Autotools cannot shadow
it. Remove the in-source generated file during cleanup.

### Versioning

- Compute once:
  - human version from `git describe --always --dirty` (the current master
    branch style, e.g. `v0.96-mmn-25-ge5a3baa9`);
  - numeric project version for CMake from a fallback value.
- Inject `VERSIONSTRING` centrally.
- Do not reproduce the broken `git describe always --dirty` commands from the
  old Makefiles.

## CMake options

Recommended initial cache options:

```text
BUILD_SHARED_LIBS=ON            # final decision still open; see below
SCUFF_CXX_STANDARD=23
SCUFF_WITH_HDF5=ON
SCUFF_THREAD_BACKEND=OpenMP     # OpenMP | Pthreads | None
SCUFF_WITH_GSL=AUTO             # AUTO | ON | OFF
SCUFF_WITH_READLINE=AUTO
SCUFF_WITH_DCUTRI=AUTO
SCUFF_WITH_GDSII=AUTO
SCUFF_BUILD_DEV_PROGRAMS=OFF
SCUFF_BUILD_UNIT_TESTS=OFF
SCUFF_BUILD_INTERACTIVE_TESTS=OFF
SCUFF_BUILD_DORMANT_APPS=OFF    # scuff-cas2D, scuff-microstrip
SCUFF_INSTALL_EXAMPLES=ON
SCUFF_INSTALL_TESTS=ON
SCUFF_ENABLE_SANITIZERS=OFF
SCUFF_WARNINGS=ON
SCUFF_WARNINGS_AS_ERRORS=OFF
```

Use the standard `BUILD_TESTING` option for CTest rather than inventing a
parallel one.

## Install rules and packaging

- `install(TARGETS scuff ...)` with `RUNTIME`, `LIBRARY`, `ARCHIVE`,
  `INCLUDES DESTINATION`.
- Install public headers to `${CMAKE_INSTALL_INCLUDEDIR}/scuff-em`.
- Fix `scuff-em.pc`:
  - `Cflags: -I${includedir}/scuff-em`;
  - keep `Libs: -L${libdir} -lscuff`;
  - move private dependencies (HDF5, GSL, readline, BLAS/LAPACK where
    appropriate) to `Libs.private`;
  - use the CMake-computed version.
- Generate and install a CMake package config:
  - `scuff-emConfig.cmake`;
  - `scuff-emConfigVersion.cmake`;
  - exported target `scuff-em::scuff`;
  - `find_dependency()` for public dependencies.
- Install binaries to `${CMAKE_INSTALL_BINDIR}`.
- Install tests/scripts/data to `${CMAKE_INSTALL_DATADIR}/scuff-em/tests`.
- Install examples to `${CMAKE_INSTALL_DATADIR}/scuff-em/examples/<name>`.
  Prefer preserving the explicit install lists, or use `install(DIRECTORY ...)`
  with exclusions for `Makefile*`, build artifacts and caches.
- Support `DESTDIR`/`cmake --install --prefix` staging for validation.

## Phased implementation

### Phase 0 — freeze the baseline (before changing anything)

- Record a clean Autotools build/test result (already done for this tree).
- Save the list of shared-library symbols and installed file list for
  comparison.
- Decide default shared vs static and whether examples/tests are installed.

Deliverable: baseline notes plus a file manifest.

### Phase 1 — CMake skeleton and dependency detection

- Add root `CMakeLists.txt`, `cmake/` modules, options, `config.h` template and
  version detection.
- Detect BLAS/LAPACK, Fortran ABI, Threads, OpenMP, HDF5, GSL, readline,
  DCUTRI and GDSII.
- Configure with Ninja and verify the generated `config.h` contains the
  expected macros.
- No full target build yet.

Validation:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

Expected: configure completes, feature summary is correct, empty build succeeds.

### Phase 2 — internal object libraries and `libscuff`

- Port the 12 internal module source lists into object libraries.
- Port the `libscuff` source list into `scuff_core`.
- Build the aggregate `scuff` shared library.
- Link against OpenMP/Threads, HDF5, GSL, LAPACK/BLAS as decided.
- Validate with `nm`, `ldd`, and a small C++ client that includes
  `<libscuff.h>`.

Validation:

```sh
cmake --build build --target scuff
ldd build/libs/libscuff.so
```

- Test both `-DSCUFF_WITH_HDF5=ON/OFF`.
- Test OpenMP and pthreads backends.
- Test shared and static builds.

### Phase 3 — applications

- Port all currently built applications and `scuff-integrate`.
- Use one shared version definition.
- Keep dormant `scuff-cas2D` / `scuff-microstrip` out unless explicitly enabled.
- Run each binary's `--help`/usage smoke test and compare with the Autotools
  build.

Validation: all 13 executable targets build; no accidental dependencies on
`libs/python`.

### Phase 4 — tests, install and packaging

- Add `CheckSCUFFData`, stage Mie/Fresnel data, and register both CTest tests.
- Add install rules for the library, headers, applications, pkg-config, tests
  and examples.
- Add CMake package export.
- Fix the pkg-config include path.
- Install into a staging prefix and compile a small external client with
  `pkg-config --cflags --libs scuff-em`.
- Run installed tests if possible.

Validation:

```sh
ctest --test-dir build --output-on-failure
cmake --install build --prefix /tmp/scuff-stage
PKG_CONFIG_PATH=/tmp/scuff-stage/lib/pkgconfig pkg-config --cflags --libs scuff-em
```

Both `TestMie.sh` and `TestFresnel.sh` must pass.

### Phase 5 — switch over and remove Autotools

- Update README, `doc/Installation.md`, CI and developer instructions to use
  CMake.
- Add CMake build directories to `.gitignore` (e.g. `build*/`,
  `CMakeUserPresets.json`, `compile_commands.json`).
- Remove Autotools files only after at least one full CMake release cycle or
  after explicit approval:
  - `autogen.sh`, `configure.ac`, top-level and per-directory `Makefile.am`;
  - `m4/` macros;
  - generated `Makefile.in`, `aclocal.m4`, `config.h.in`, `install-sh`,
    `ltmain.sh`, etc.;
  - SWIG/Python build files (`libs/python/Makefile.am`, SWIG rules) after
    confirming nothing else references them.
- Keep `scuff-em-pkgconfig.in` only if it is replaced by the CMake template; do
  not keep two sources of truth.

### Phase 6 — C++23 rewrite (separate workstream, after CMake parity)

CMake should already default to C++23, but the code rewrite starts only when
the CMake build is stable:

- Remove `HAVE_TR1`/`HAVE_CXX11` branching; require standard C++ headers.
- Remove per-target `-std=c++11` overrides.
- Replace `using namespace std` in public headers.
- Replace manual `malloc`/`free`, raw owning pointers and C-style arrays with
  RAII containers (`std::vector`, `std::string`, `std::unique_ptr`).
- Modernize APIs with `std::span`, `std::optional`, `std::filesystem`,
  `constexpr`, `[[nodiscard]]`, `std::numbers`, ranges where useful.
- Break the monolithic `libscuff` aggregate into coherent modules once the
  internal dependency cycle is understood.
- Add clang-format/clang-tidy and sanitizer CI jobs.
- Exclude or delete stale unbuilt source files (the ones that do not appear in
  any `Makefile.am` and already fail C++23 syntax checks).
- Keep numerical regression tests as the safety net; refactor one module per
  change set.

## Risks and mitigations

| Risk | Mitigation |
|---|---|
| Circular dependencies between `libSubstrate`, `libStaticSolver`, `libscuffSolver` and `libscuff` | Use object libraries feeding one aggregate `scuff` target; do not create shared libraries for each module. |
| Debian HDF5 layout / HL component | Test `FindHDF5` early; fall back to `pkg-config hdf5` or explicit paths. Use `HDF5_PREFER_PARALLEL FALSE`. |
| Fortran symbol mangling / runtime | Require Fortran at configure time and use `FortranCInterface`; preserve gfortran as a documented dependency. |
| `cmatheval` C files were historically compiled as C++ | Keep them as C++ sources initially (`LANGUAGE CXX` or compile the object target as CXX). Do not "fix" this in the build migration. |
| Stale unbuilt sources fail under C++23 | Copy explicit source lists from `Makefile.am`; never glob. |
| Tests rely on `PATH` and write files | Use CTest environment generator expressions and dedicated build-directory working dirs. |
| Version string currently broken in most app Makefiles | Compute version once with correct `git describe --always --dirty`. |
| Global `-O3`/`-g -O2` flags conflict | Use standard CMake build types; no global `-O3`; add per-target warning options only. |
| Headers are installed under `include/scuff-em` but pkg-config points at `include` | Fix the `.pc` file and export the correct `INSTALL_INTERFACE`. |
| Removing Autotools too early breaks CI/docs | Keep both until Phase 5, then remove in one clearly reviewed change. |

## Open decisions

1. **Default library type:** current Autotools intended static (`AC_DISABLE_SHARED`
   was meant to be the default) but shared libraries were actually enabled due
   to ordering. Recommendation: default `BUILD_SHARED_LIBS=ON` for the installed
   library, while keeping static builds tested.
2. **Dormant applications:** keep `scuff-cas2D` and `scuff-microstrip` out of the
   initial CMake build, or port them behind an option?
3. **Example/test data installation:** default on or opt-in?
4. **Autotools removal timing:** immediately after CMake parity, or keep for one
   transition period?
5. **C++23 timing:** set the CMake default now (recommended) but defer source
   refactoring until the build is stable.

## Recommended first implementation slice

For the first CMake commit after this plan:

1. Root `CMakeLists.txt` + `cmake/` helper modules.
2. Options, version detection, `config.h`, dependency/feature checks.
3. A single `scuff` target containing only the core library plus all internal
   modules (explicit source lists).
4. No applications, no tests, no install/export rules yet.
5. Validate HDF5 on/off, OpenMP/pthreads, shared/static.

This gives an early, low-risk proof that CMake can compile the hard part
(`libscuff`) before touching application targets and packaging.

## Baseline verification commands

Autotools baseline already established:

```sh
./configure --without-hdf5 --without-python
make -j6
PATH="$PWD/applications/scuff-scatter:$PWD/applications/scuff-transmission:$PWD/tests:$PATH" \
  OMP_NUM_THREADS=2 make check
```

Both `TestMie.sh` and `TestFresnel.sh` pass. The CMake build must eventually
match or exceed this result, including a full HDF5-enabled configuration now
that `libhdf5-dev` is installed.
