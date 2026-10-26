# Autotools removal — 2026-10-26

Status: **complete**.  This is the second step of the build-system
migration described in `ai_notes/cmake-migration-plan-2026-10-26.md`.
The CMake implementation and its validation matrix are recorded in
`ai_notes/cmake-implementation-2026-10-26.md`.

## Removed from version control

- `autogen.sh`, `configure.ac`, and the top-level `Makefile.am`
- every per-directory `Makefile.am` under `applications/`, `examples/`,
  `libs/`, `tests/`, `unitTests/`, and `interactiveTests/`
- the `m4/` Autotools macro directory (`acx_blas.m4`, `acx_lapack.m4`,
  `ax_cxx_compile_stdcxx.m4`, `ax_openmp.m4`, `ax_pthread.m4`, and
  `Makefile.am`)
- the obsolete SWIG/Python build inputs in `libs/python/`
  (`Makefile.am`, `numpy.i`, `scuff.i`, `scuff-python.i`)
- the old `scuff-em-pkgconfig.in` template
- `.travis.yml` (Autotools + SWIG + Python 2 build)

The dormant `scuff-cas2D` and `scuff-microstrip` application sources are
still present, but they have no build files and are not built by CMake.
The old Python examples under `examples/pythonCapacitance` and
`examples/MicrostripDevices/pythonFiles` are also left untouched; they
are not installed or built.

Generated Autotools artifacts (`configure`, `Makefile.in`, `config.h`,
libtool helper symlinks, object files, stale test logs, and the generated
SWIG wrapper) were removed from the working tree.  They were never
tracked.

## `.gitignore`

The Autotools, SWIG-generated-file, and obsolete test-binary sections
were removed.  The remaining entries cover:

- in-source application binaries (now including `scuff-integrate`,
  `scuff-ldos`, and `scuff-spectrum`);
- generic compiled objects, libraries, and shared objects;
- CMake build directories and generated files;
- archives, logs, and editor/OS files.

## Documentation

- `README.md` now describes the CMake build, the supported options, and
  the C++ / pkg-config / CMake-package interfaces.  The Travis badge and
  Python API claims are gone.
- `README` (plain text) has the same CMake quick-start and no longer
  lists the dormant `scuff-cas2D` application.
- `doc/Installation.md` was rewritten around CMake, including required
  and optional dependencies, installation layout, options, threading,
  tests, static builds, downstream use, and troubleshooting.
- `doc/docs/reference/Installing.md` was rewritten for the website,
  preserving the existing section anchors and performance/debugging
  material while replacing Autotools, SWIG, and Python instructions.

## CI

`.travis.yml` was replaced with `.github/workflows/cmake.yml`, which:

- installs CMake, Ninja, GCC, gfortran, OpenBLAS/LAPACK, HDF5, GSL, and
  readline on `ubuntu-24.04`;
- configures and builds both shared and static libraries;
- runs the Mie and Fresnel CTest suites;
- installs into a temporary prefix to validate the install rules.

## Validation after removal

A clean build was performed from the post-removal tree:

```bash
rm -rf build-cmake
cmake -S . -B build-cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-cmake -j6
ctest --test-dir build-cmake --output-on-failure
cmake --install build-cmake --prefix /tmp/scuff-postremoval-stage
```

Results:

- configure and build succeeded (166 build steps, 442 legacy warnings,
  no errors);
- Mie passed (116.7 s), Fresnel passed (28.8 s), 2/2 CTest tests passed;
- install produced 13 applications, `libscuff.so`, headers, pkg-config
  metadata, the CMake package, test data, and 21 example directories;
- no `Makefile*` files were installed into the examples tree;
- the installed `scuff-em.pc` remains relocatable through
  `${pcfiledir}`.

The previous validation matrix (shared/static, HDF5 on/off, OpenMP and
pthreads, GSL on/off, optional developer programs, external pkg-config
and CMake clients) remains applicable because no source or CMake target
definitions changed in this commit.

## Next workstream

The C++23 modernization is intentionally separate from this build-system
migration.  It should address the legacy warning backlog, TR1-era
conditionals, header namespace pollution, raw allocations, and other
source-level issues without changing build behaviour.
