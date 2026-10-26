SCUFF-EM
========

A comprehensive and full-featured computational physics suite for
boundary-element analysis of electromagnetic scattering,
fluctuation-induced phenomena (Casimir forces and radiative
heat transfer), nanophotonics, RF device engineering,
electrostatics, and more. It includes a core C++ library
(`libscuff`) and many command-line applications.

SCUFF-EM stands for

 Surface-CUrrent-Field Formulation of ElectroMagnetism

For documentation and further information on SCUFF-EM visit the
[SCUFF-EM documentation homepage](http://homerreid.github.io/scuff-em-documentation).

Building
--------

SCUFF-EM uses CMake (3.20 or newer). A C++23 compiler, a Fortran
compiler, and BLAS/LAPACK are required. HDF5, GSL, readline, DCUTRI,
and libGDSII are detected automatically when available.

```bash
git clone https://github.com/HomerReid/scuff-em.git
cd scuff-em
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/scuff-em-installation
```

Useful configuration options:

| Option | Default | Description |
|---|---|---|
| `BUILD_SHARED_LIBS` | `ON` | Build `libscuff` as a shared library |
| `SCUFF_CXX_STANDARD` | `23` | C++ standard used for the build |
| `SCUFF_THREAD_BACKEND` | `OpenMP` | Threading backend: `OpenMP`, `Pthreads`, or `None` |
| `SCUFF_WITH_HDF5` | `ON` | Build HDF5 support |
| `SCUFF_WITH_GSL` | `AUTO` | Enable GSL (required by some spherical-wave code) |
| `SCUFF_INSTALL_EXAMPLES` | `ON` | Install example input files |
| `SCUFF_INSTALL_TESTS` | `ON` | Install regression-test data and scripts |
| `SCUFF_BUILD_DEV_PROGRAMS` | `OFF` | Build internal developer test programs |

See [doc/Installation.md](doc/Installation.md) for detailed
platform-specific instructions.

The Python/SWIG interface has been retired. The supported public
interface is the C++ library, `pkg-config` metadata, and the exported
CMake package (`find_package(scuff-em CONFIG)`).
