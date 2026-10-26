# CMake build implementation — 2026-10-26

Status: **implemented and validated** (Autotools still present; removal is the
next, separate commit).

This note records what was built, how it maps to the old Autotools build, and
what was verified.  The design rationale is in
`ai_notes/cmake-migration-plan-2026-10-26.md`.

## Decisions applied

- CMake is now the primary build system.
- SWIG / Python targets are not mentioned anywhere in the CMake files.
- `BUILD_SHARED_LIBS=ON` by default.
- `scuff-cas2D` and `scuff-microstrip` are not ported.
- Examples and test data are installed by default, with
  `SCUFF_INSTALL_EXAMPLES=OFF` / `SCUFF_INSTALL_TESTS=OFF` to opt out.
- C++23 is the default (`SCUFF_CXX_STANDARD=23`, `-std=gnu++23`).
- Autotools is to be removed immediately after this parity step, in its own
  commit.

## Added files

### Top level

- `CMakeLists.txt`
- `.gitignore` additions for CMake build directories and the tracked
  `cmake/config.h.in` exception

### `cmake/`

- `ScuffOptions.cmake` — user-facing cache options and defaults
- `ScuffVersion.cmake` — single `git describe --always --dirty` computation;
  numeric fallback `0.96.0` for CMake package metadata
- `ScuffDependencies.cmake` — required and optional dependency detection,
  feature macros, Fortran mangling header
- `ScuffTargets.cmake` — common interface target and target helper functions
- `config.h.in` — CMake-generated feature header
- `scuff-em.pc.in` — relocatable pkg-config template
- `scuff-emConfig.cmake.in` — CMake package config template

### Directory build files

- `libs/CMakeLists.txt`
- `applications/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `examples/CMakeLists.txt`

## Target graph

All internal modules are `OBJECT` libraries:

```text
scuff_hrutil      scuff_hmat        scuff_sgjc       scuff_mdinterp
scuff_cmatheval   scuff_matprop     scuff_triint     scuff_spherical
scuff_incfield    scuff_substrate   scuff_staticsolver
scuff_scuffsolver scuff_core
```

They are aggregated into one `scuff` library via `$<TARGET_OBJECTS:...>`.
This mirrors the old libtool whole-archive result and avoids the real
dependency cycles (`libSubstrate -> scuff::CalcGC`, and the solver/core
cross-references).  No per-module shared libraries are created.

The aggregate target:

- output name `scuff`;
- libtool-compatible version `0.0.0` / SONAME `libscuff.so.0`;
- links LAPACK, BLAS, Threads, and optionally HDF5 (C + HL), GSL,
  OpenMP, DCUTRI, libGDSII;
- installs flat headers under `${includedir}/scuff-em`, matching the old
  layout.

The `scuff_common` interface target centralises:

- build-tree include directories (generated include directory first);
- `HAVE_CONFIG_H=1`;
- `VERSIONSTRING` from the single git computation;
- warning flags and optional sanitizers;
- the OpenMP compile/link usage requirement.

## Applications ported

`scuff-analyze`, `scuff-cas3D`, `scuff-caspol`, `scuff-ldos`, `scuff-neq`,
`scuff-integrate`, `scuff-plotEpsMu`, `scuff-rf`, `scuff-scatter`,
`scuff-spectrum`, `scuff-static`, `scuff-tmatrix`, `scuff-transmission`.

`scuff-cas3D` links LAPACK/BLAS directly because it calls `zgetrf_` and
friends itself.

Optional developer programs (`SCUFF_BUILD_DEV_PROGRAMS=ON`) build:

- `tProcessOptions`
- `tLUSolve`, `tMultiply`, `tReadFromFile`, `tTextIO`, `tlibhmat2`, `tQR`,
  `tGetEntries`, `tSMatrix`
- `tlibSGJC`, `tlibSGJC2`
- `tcmatheval`
- `tBeyn411`

## Feature detection notes

- `find_package(HDF5 COMPONENTS C HL REQUIRED)` works with Debian's
  `/usr/include/hdf5/serial` layout and provides `hdf5::hdf5` +
  `hdf5::hdf5_hl`.  Both are needed because `HDF5IO.cc` uses `H5LT*`.
- `FortranCInterface_HEADER(... MACRO_NAMESPACE "F77_")` produces
  `F77_GLOBAL` / `F77_GLOBAL_`, **not** the historical `F77_FUNC` /
  `F77_FUNC_`.  `config.h` maps the old names onto the generated ones.
- Math function checks (`erf`, `erfc`, …) need libm in
  `CMAKE_REQUIRED_LIBRARIES`; without it these functions appear missing.
- `HAVE_TR1` is deliberately not defined.  The active code uses the C++11
  `<unordered_map>` branch, not `<tr1/unordered_map>`.
- `HAVE_CXX11` is defined for compatibility with the existing conditionals.
- `cmatheval` C files remain compiled as C++ (`LANGUAGE CXX`), preserving the
  old `CC = $(CXX)` behaviour.
- `SCUFF_THREAD_BACKEND` chooses `OpenMP` (default), `Pthreads` or `None`.
  pthreads itself is always linked because `rwlock.cc` uses pthread rwlocks.

## pkg-config and CMake package

The `.pc` file is generated with `${pcfiledir}`-relative paths, so
`cmake --install --prefix <stage>` produces working metadata:

```text
Cflags: -I${includedir}/scuff-em
Libs:   -L${libdir} -lscuff
Libs.private: <HDF5/GSL/LAPACK/BLAS/OpenMP/...>
```

This also fixes the historical bug where `Cflags` pointed at
`${includedir}` instead of `${includedir}/scuff-em`.

A CMake package is exported as `scuff-em::scuff`, with
`find_dependency()` calls for Threads, HDF5 (when enabled), BLAS/LAPACK,
GSL (when enabled) and OpenMP (when enabled).

## Install layout

- `${bindir}` — all 13 applications.
- `${libdir}/libscuff.so.0.0.0` plus `libscuff.so.0` / `libscuff.so`
  symlinks (or `libscuff.a` for static builds).
- `${includedir}/scuff-em/` — public headers.
- `${libdir}/pkgconfig/scuff-em.pc`.
- `${libdir}/cmake/scuff-em/` — CMake package files.
- `${datadir}/scuff-em/tests/` — `CheckSCUFFData`, Mie and Fresnel data,
  reference files and scripts.
- `${datadir}/scuff-em/examples/<name>/` — the 21 example directories listed
  in `examples/Makefile.am` (build-system files and artifacts excluded).

## Validation performed

Environment: Debian 13, GCC 14.2, CMake 3.31.6, Ninja 1.12.1, HDF5 1.14.5,
OpenBLAS, GSL 2.8.

### Build/test matrix

| Configuration | Build | Mie | Fresnel |
|---|---|---|---|
| Shared, HDF5 ON, OpenMP, GSL ON (default) | pass | pass | pass |
| Shared, HDF5 OFF, Pthreads, GSL OFF | pass | pass | pass |
| Static, HDF5 ON, OpenMP, GSL ON | pass | pass | pass |
| Developer programs ON | pass | — | — |

All tests were run through CTest with `OMP_NUM_THREADS=2` and passed.

### Install/package validation

- `cmake --install build-cmake --prefix /tmp/scuff-cmake-stage` succeeds.
- Installed library SONAME is `libscuff.so.0`.
- An external C++23 client builds and runs using
  `pkg-config --cflags --libs scuff-em`.
- An external CMake client builds and runs with
  `find_package(scuff-em CONFIG REQUIRED)` and `scuff-em::scuff`.
- The same CMake-package client works against a static install.
- Installed tests/examples contain the expected files and no `Makefile*`
  leftovers in examples.

### C++23 status

The active source lists compile under `-std=gnu++23` without source changes.
The default warning set produces 442 warnings, mostly legacy
format-truncation, unused-parameter, and `memset`-on-`std::complex` warnings.
No warnings are treated as errors.  The C++ rewrite remains a separate
workstream, as planned.

## Follow-up / not done here

- Remove Autotools in the next commit and update `README` / installation docs
  / CI to CMake.
- Unit tests (`unitTests/`) and interactive tests (`interactiveTests/`) are
  not part of the old default build and were not ported.
- Dormant `scuff-cas2D` / `scuff-microstrip` sources remain in the tree but
  are not built.
- The later C++23 refactor should remove `HAVE_CXX11`/TR1 conditionals,
  `using namespace std` in headers, raw allocations, and the large warning
  backlog.
