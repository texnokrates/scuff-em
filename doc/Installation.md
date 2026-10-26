# Installing SCUFF-EM

## Build requirements

SCUFF-EM is built with [CMake](https://cmake.org) 3.20 or newer.
The required toolchain and numerical libraries are:

+ A C++23-capable compiler (GCC 13 or newer, Clang 17 or newer).
+ A Fortran compiler (needed for the Fortran name-mangling header and
  the LAPACK/BLAS interface).
+ [LAPACK and BLAS](http://www.netlib.org/lapack). An optimized
  implementation such as OpenBLAS is recommended.

The following external packages are optional. CMake detects them
automatically and reports the resulting configuration at the end of
the configure step.

+ [HDF5](http://www.hdfgroup.org/HDF5) — enabled by default. It is used
  for binary output files. Configure with `-DSCUFF_WITH_HDF5=OFF` to
  disable it.
+ [GSL](https://www.gnu.org/software/gsl/) — used by some spherical-wave
  routines. Detected automatically when present.
+ `readline` — used by a few developer test programs.
+ `DCUTRI` — optional adaptive cubature backend.
+ [libGDSII](http://github.com/HomerReid/libGDSII) — optional GDSII input
  support.

[Gmsh](http://geuz.org/gmsh) is not required to build or run SCUFF-EM,
but it is the meshing tool used throughout the documentation.

On Debian/Ubuntu Linux systems, a complete build environment can be
installed with:

```bash
sudo apt-get install cmake ninja-build g++ gfortran \
  libopenblas-dev liblapack-dev libhdf5-dev \
  libgsl-dev libreadline-dev gmsh
```

## Clone and build

```bash
git clone https://github.com/HomerReid/scuff-em.git
cd scuff-em
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/scuff-em-installation
```

The applications (`scuff-scatter`, `scuff-rf`, etc.) are installed in
`PREFIX/bin`, the library in `PREFIX/lib`, headers in
`PREFIX/include/scuff-em`, and the examples in
`PREFIX/share/scuff-em/examples`. Tests and test data are installed
under `PREFIX/share/scuff-em/tests`.

## Configuration options

Pass options to CMake with `-DOPTION=VALUE`. The most useful options are:

| Option | Default | Description |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo` | `Release`, `Debug`, `RelWithDebInfo`, or `MinSizeRel` |
| `CMAKE_INSTALL_PREFIX` | `/usr/local` | Installation prefix |
| `BUILD_SHARED_LIBS` | `ON` | Build `libscuff` as a shared library; set `OFF` for a static library |
| `SCUFF_CXX_STANDARD` | `23` | C++ standard (`17`, `20`, or `23`) |
| `SCUFF_THREAD_BACKEND` | `OpenMP` | `OpenMP`, `Pthreads`, or `None` |
| `SCUFF_WITH_HDF5` | `ON` | Enable HDF5 output |
| `SCUFF_WITH_GSL` | `AUTO` | GSL support: `AUTO`, `ON`, or `OFF` |
| `SCUFF_WITH_READLINE` | `AUTO` | readline support: `AUTO`, `ON`, or `OFF` |
| `SCUFF_WITH_DCUTRI` | `AUTO` | DCUTRI support: `AUTO`, `ON`, or `OFF` |
| `SCUFF_WITH_GDSII` | `AUTO` | libGDSII support: `AUTO`, `ON`, or `OFF` |
| `SCUFF_INSTALL_EXAMPLES` | `ON` | Install example input files |
| `SCUFF_INSTALL_TESTS` | `ON` | Install test data and scripts |
| `SCUFF_BUILD_DEV_PROGRAMS` | `OFF` | Build internal developer test programs |
| `SCUFF_WARNINGS` | `ON` | Enable the project warning set |
| `SCUFF_WARNINGS_AS_ERRORS` | `OFF` | Treat warnings as errors |
| `SCUFF_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer |

If an optional package is installed in a nonstandard location, point
CMake at it with `CMAKE_PREFIX_PATH`, or its package-specific hint:

```bash
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/opt/my-libs \
  -DHDF5_ROOT=/opt/hdf5 \
  -DGSL_ROOT=/opt/gsl
```

<a name="Threading"></a>
## Multithreading: OpenMP vs. pthreads

The SCUFF-EM core library uses multithreading for the main stages of the
boundary-element method. OpenMP is the default backend. To use the
legacy pthreads backend instead:

```bash
cmake -S . -B build -DSCUFF_THREAD_BACKEND=Pthreads
```

To build without multithreading (useful for debugging or constrained
environments):

```bash
cmake -S . -B build -DSCUFF_THREAD_BACKEND=None
```

With OpenMP, setting `OMP_NUM_THREADS` controls the number of threads:

```bash
OMP_NUM_THREADS=8 scuff-scatter < scuff-scatter.args
```

On some Linux systems, setting `GOMP_CPU_AFFINITY` may improve
performance. For an 8-core machine:

```bash
export GOMP_CPU_AFFINITY="0-7"
```

## Tests

The Mie and Fresnel regression tests are registered with CTest:

```bash
ctest --test-dir build --output-on-failure
```

Tests run with `OMP_NUM_THREADS=2` by default. To run one test:

```bash
ctest --test-dir build -R Mie --output-on-failure
```

## Static builds

```bash
cmake -S . -B build-static -DBUILD_SHARED_LIBS=OFF
cmake --build build-static -j
cmake --install build-static --prefix /path/to/installation
```

Both shared and static builds export a CMake package and a `pkg-config`
file for downstream C++ projects.

## Using SCUFF-EM from another project

With `pkg-config`:

```bash
c++ -std=c++23 mycode.cc -o mycode $(pkg-config --cflags --libs scuff-em)
```

With CMake:

```cmake
find_package(scuff-em CONFIG REQUIRED)
target_link_libraries(mycode PRIVATE scuff-em::scuff)
```

## Troubleshooting

+ If CMake cannot find HDF5, install the development package (`libhdf5-dev`
  on Debian/Ubuntu) or set `HDF5_ROOT`.
+ If OpenMP is unavailable, configure with
  `-DSCUFF_THREAD_BACKEND=Pthreads`.
+ If you change compiler or dependency locations, remove the build
  directory and configure again to avoid stale cache entries.

If you have trouble installing SCUFF-EM, please file an issue on the
[SCUFF-EM GitHub page](https://github.com/HomerReid/scuff-em/issues).
