<h1>Installing <span class=SC>scuff-em</span></h1>

The <span class=SC>scuff-em</span> source distribution is built with
[CMake](https://cmake.org). CMake configures the compiler, finds the
required numerical libraries, and generates a build that can be compiled
and installed with a few commands. You do not need Autotools, SWIG, or a
[[python]] installation.

If you *aren't* familiar with CMake, you should still be able to install
<span class=SC>scuff-em</span> without much trouble by following the
OS-specific instructions below.

**In either case, if you have any trouble installing <span class=SC>scuff-em</span>,
please [file an issue on the <span class=SC>scuff-em</span> GitHub page][Issues].**

[TOC]

<a name="#GeneralComments"></a>
## 1. General comments about installing <span class=SC>scuff-em</span>

### External packages

[[scuff-em]] relies on a small number of well-established free open-source
software packages. Some are required, while others implement optional
functionality. CMake reports which optional packages were found at the
end of the configure step.

Required:

+ A C++23-capable compiler (GCC 13 or newer, or Clang 17 or newer).
+ A Fortran compiler, used for the Fortran name-mangling header and the
  LAPACK/BLAS interface.
+ [LAPACK/BLAS](http://www.netlib.org/lapack) or an equivalent replacement
  such as the Intel MKL. The excellent open-source
  [<span class=SC>openblas</span>](http://www.openblas.net/) package is
  recommended; it includes both LAPACK and BLAS and tunes itself for the
  local hardware.

Optional:

+ [HDF5](http://www.hdfgroup.org/HDF5) for writing output files in the
  standard HDF5 binary format. HDF5 is enabled by default; configure with
  `-DSCUFF_WITH_HDF5=OFF` to disable it.
+ [GSL](https://www.gnu.org/software/gsl/) for some spherical-wave routines.
+ `readline`, used by a few developer test programs.
+ `DCUTRI`, an optional adaptive cubature backend.
+ My [<span class="SC">libgdsii</span>](http://github.com/HomerReid/libGDSII)
  package, which provides GDSII file support.
+ Although not required to install, compile, or use [[scuff-em]],
  [<span class="SC">gmsh</span>](http://geuz.org/gmsh) is an extremely
  valuable open-source meshing and visualization tool used throughout the
  [[scuff-em]] documentation.

The Python/SWIG interface that appeared in older releases has been
retired. The supported interfaces are the C++ library, the installed
headers, `pkg-config` metadata, and the exported CMake package.

### Status of the GitHub repository

The current build status of the [[scuff-em]] master branch is:

![CMake build](https://github.com/HomerReid/scuff-em/actions/workflows/cmake.yml/badge.svg)

<a name="UsefulPrompt"></a>
### Setting up an informative command-line prompt

Regardless of the specific OS you're running (Linux, MacOS,
or Windows), the installation process will involve typing
commands at a prompt in a console window. Unfortunately,
in most cases the default prompt is not particularly
informative---in particular, it doesn't tell you the
current working directory (which can be easy to forget
when switching among folders in a complex build process)
or the current host name (which can be easy to forget
when you're frequently logging in and out of remote
machines.) Thus, for command-line work I find it useful
to set up a prompt that includes this information, which
you can do as follows:

```bash
% export PS1='$HISTCMD '`hostname`' $PWD <> '
34 mac-56-129-14.mit.edu /Users/homereid <> 
```

Note that the prompt now tells me my current working
directory (`/Users/homereid`) and the name of the
machine (`mac-56-129-14.mit.edu`) so I don't lose track
of where I am. (The `34` refers to the number of commands
I've executed in this console session thus far.)

If you find the above command useful, you may copy
it into your `${HOME}/.bashrc` file, in which case
the informative prompt will be set up automatically
whenever you open a console window.

### Build options

After fetching the source tree, configure the build with

```bash
 % cmake -S . -B build [OPTIONS]
```

then compile and install with

```bash
 % cmake --build build -j
 % cmake --install build
```

CMake caches its configuration in the build directory. To change
compiler or dependency locations after a failed configure, it is usually
easiest to remove the build directory and configure again. Here are the
more salient options you may wish to specify.

<a name="InstallationPrefix"></a>
#####Specifying the installation prefix

If the build process succeeds, all files needed to
run <span class=SC>scuff-em</span> calculations---including
executable programs, library files, and example calculations---will
be installed in subdirectories of a top-level directory known as the
*prefix.* The default prefix is `/usr/local`, but you may wish to install
<span class=SC>scuff-em</span> in a different location---in particular,
you will *need* to do this if you don't have administrator privileges on
the machine you are using. Set the prefix at configure time:

```bash
% cmake -S . -B build -DCMAKE_INSTALL_PREFIX=${HOME}/scuff-em-installation
% cmake --build build -j
% cmake --install build
```

Alternatively, keep the configured prefix and override it at install time:

```bash
% cmake --install build --prefix ${HOME}/scuff-em-installation
```

In this case, after the <span class=SC>scuff-em</span> installation
has completed, the various installed files will be in the following
locations:

+ Binary executable files (like `scuff-scatter` and `scuff-rf`) will be in
  `${HOME}/scuff-em-installation/bin`.

+ Library files (like `libscuff.so`) will be in
  `${HOME}/scuff-em-installation/lib`.

+ The <span class=SC>scuff-em</span> example files (like `YagiUdaAntennas`)
  will be in `${HOME}/scuff-em-installation/share/scuff-em/examples`.

If you go this route, you will probably want to add the location
of the <span class=SC>scuff-em</span> binary executables to your execution
path by saying

```bash
 % export PATH=${PATH}:${HOME}/scuff-em-installation/bin
```

Now you will be able to run command-line codes like `scuff-rf`
from any directory and the OS will know where to find the
program file.

<a name="HDF5Locations"></a>
#####Specifying locations of external libraries

In some cases, external packages used in the SCUFF-EM compilation
process may be installed in non-standard locations on your system.
Point CMake at them with `CMAKE_PREFIX_PATH`, or with the
package-specific `HDF5_ROOT` / `GSL_ROOT` variables:

```bash
 % cmake -S . -B build \
     -DCMAKE_PREFIX_PATH=/opt/my-libs \
     -DHDF5_ROOT=/opt/hdf5 \
     -DGSL_ROOT=/opt/gsl
```

HDF5's own CMake package handles Debian-style multi-architecture layouts
such as `/usr/include/hdf5/serial` automatically when `libhdf5-dev` is
installed normally. If you use a parallel HDF5 build, also use the
MPI-aware compilers as described below.

<a name="openMPI"></a>
#####Selecting the threading backend

<span class=SC>scuff-em</span> makes heavy use of
[<span class=SC>openmp</span>-based shared-memory multithreading](http://www.openmp.org/)
to accelerate tasks such as BEM matrix assembly. OpenMP is the default.
To use the legacy pthreads backend instead:

```bash
% cmake -S . -B build -DSCUFF_THREAD_BACKEND=Pthreads
```

To disable multithreading entirely (for example, when debugging):

```bash
% cmake -S . -B build -DSCUFF_THREAD_BACKEND=None
```

In some cases, particularly if you are using a parallel version of the
HDF5 libraries, you may need to use MPI-aware compilers when building
<span class=SC>scuff-em</span>. Set the `CC`, `CXX`, and `FC` environment
variables before configuring:

```bash
% export CC=mpicc
% export CXX=mpic++
% export FC=mpifort
% cmake -S . -B build [OPTIONS]
```

<a name="Disabling Python"></a>
##### Python and SWIG

The Python interface is no longer part of SCUFF-EM. There is no
`--with-python` option, no SWIG step, and no Python extension module to
build or install. Existing C++ codes should use the installed library and
headers directly.

##### Parallel build

After the configure step finishes, compile with

```bash
% cmake --build build -j 8
```

(replace `8` by the actual number of CPU cores on your machine).

<!------------------------------------------------------>
<!------------------------------------------------------>
<!------------------------------------------------------>
<a name="Linux"></a>
## 2. Installing on Debian/Ubuntu-based Linux systems

####A. Use <span class=SC>apt-get</span> to install external packages

On Debian/Ubuntu Linux systems, you can fetch all of the required and
recommended packages as follows:

```bash
% sudo apt-get install cmake ninja-build g++ gfortran \
    libopenblas-dev liblapack-dev libhdf5-dev \
    libgsl-dev libreadline-dev gmsh
```

####B. Clone the GitHub repository and build the code

The procedure should now be as simple as

```bash
% git clone https://github.com/HomerReid/scuff-em.git
% cd scuff-em
% cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=${HOME}/scuff-em-installation
% cmake --build build -j 8
% ctest --test-dir build --output-on-failure
% cmake --install build
```

(In the build line, replace `8` with the number of CPU cores
on your machine.)

As discussed above, if this succeeds, the executable files
for the <span class=SC>scuff-em</span> application modules
(such as `scuff-scatter`, `scuff-rf`, etc.) will be
installed in the directory `${HOME}/scuff-em-installation/bin`,
and the demonstration examples for the various application programs
will be in `${HOME}/scuff-em-installation/share/scuff-em/examples`.
At this point you may proceed to [running a computational example
and verifying CPU utilization](PerformanceCheck).

<!------------------------------------------------------>
<!------------------------------------------------------>
<!------------------------------------------------------>
<a name="MacOS"></a>
##3. Installing on MacOS

The following installation procedure assumes that
[XCode](https://developer.apple.com/xcode) and
[HomeBrew](https://brew.sh/) are pre-installed.
All commands are to be entered at the console prompt of a terminal
window (and, if you wish, after
[setting up an informative prompt](#UsefulPrompt) to help you keep track
of where you are in the filesystem).

####A. Use <span class=SC>xcode</span> and <span class=SC>homebrew</span> to install external packages

<h4>A1. Install command-line tools for Apple developer suite</h4>

```bash
 % xcode-select --install
```

<h4>A2. Use homebrew to install required open-source packages</h4>

```bash
 % brew update
 % brew install cmake ninja gcc openblas hdf5 gsl readline
```

Apple's Clang does not ship with an OpenMP runtime. Either install GCC
with Homebrew and select it explicitly, or configure SCUFF-EM with the
pthreads backend:

```bash
 % cmake -S . -B build \
     -DCMAKE_C_COMPILER=gcc-14 \
     -DCMAKE_CXX_COMPILER=g++-14 \
     -DCMAKE_Fortran_COMPILER=gfortran-14 \
     -DCMAKE_PREFIX_PATH="$(brew --prefix)"
```

or

```bash
 % cmake -S . -B build -DSCUFF_THREAD_BACKEND=Pthreads
```

Homebrew installs OpenBLAS, HDF5, and GSL under its own prefix. Passing
`-DCMAKE_PREFIX_PATH="$(brew --prefix)"` is usually sufficient; if not,
set `HDF5_ROOT="$(brew --prefix hdf5)"` and
`GSL_ROOT="$(brew --prefix gsl)"`.

You will most likely also want to install
[<span class=SC>gmsh</span>](http://gmsh.info/). (Although
[[gmsh]] is available within HomeBrew, the HomeBrew-installed
version seemed to be lacking graphics support when I tried it,
so instead I installed it from the
[<span class=SC>gmsh</span> website](http://gmsh.info/), but YMMV.)

####B. Clone the GitHub repository and build the code

```bash
#!/bin/bash

git clone https://github.com/HomerReid/scuff-em.git
cd scuff-em

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=${HOME}/scuff-em-installation \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"

cmake --build build -j 4
cmake --install build
```

As discussed above, for a system with e.g. 8 CPU cores
you would say `-j 8` instead of `-j 4` in the build line.

If this succeeds, you may proceed to [running a computational example
and verifying CPU utilization](PerformanceCheck).

<!------------------------------------------------------>
<!------------------------------------------------------>
<!------------------------------------------------------>
<a name="Windows"></a>
## 4. Installing on Windows

Native Windows builds are possible with CMake and an MSYS2/MinGW-w64
toolchain. Install the toolchain, CMake, Ninja, OpenBLAS/LAPACK, and
Fortran from MSYS2, then configure from a MinGW shell:

```bash
% cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
% cmake --build build
% cmake --install build --prefix C:/scuff-em-installation
```

Alternatively, build under WSL2 by following the Debian/Ubuntu
instructions above. Windows support is less tested than Linux and macOS;
please report build problems on the [issue tracker][Issues].

<!------------------------------------------------------>
<!------------------------------------------------------>
<!------------------------------------------------------>
<a name="PerformanceCheck"></a>
##5. Run a <span class=SC>scuff-em</span> computational example and check CPU utilization

If the build and installation succeed, you're in business
to run a <span class=SC>scuff-em</span> calculation
and verify that your machine's resources are being
fully utilized. For example, to compute
[input impedance vs. frequency for a Yagi-Uda antenna](../examples/YagiUdaAntennas), try:

```bash
export PATH=${PATH}:${HOME}/scuff-em-installation/bin
cd ${HOME}/scuff-em-installation/share/scuff-em/examples/YagiUdaAntennas
. RunScript.SZParms
```

This calculation should take anywhere between 10 seconds and a minute
depending on your CPU configuration. While the calculation is
running in one terminal window, open a second terminal window and
run `top` to monitor system performance---in particular,
to verify that `scuff-rf` is properly using all of your CPU
cores. On the system I used, which has 4 CPU cores, this looks
like this:

![Screen shot of `top` output](TopOnMacOS.png)

The red-framed portion of the display here is the part you
care about: you want the CPU utilization reported
for `scuff-rf` to be close to $N\times 100\%$, where $N$ is the
number of CPU cores on your machine. If you see something like this,
then <span class=SC>scuff-em</span> was built properly to
take advantage of your machine's resources. On the other hand,
if the CPU utilization reported for `scuff-rf` bounces around
near 100 %, then (assuming your machine has more than one CPU
core!) your <span class=SC>scuff-em</span> build is suboptimal---calculations
will work and will yield correct answers, but at a fraction
the speed that should be available on your machine.

**Note:** On some (but not all) Linux systems I have
found it necessary to set the environment variable `GOMP_CPU_AFFINITY`
to achieve maximum multithreaded performance in
<span class=SC>scuff-em</span> calculations. If you have an 8-core
machine, say something like

```bash
% export GOMP_CPU_AFFINITY="0-7"
```

(for a machine with `N` cores, replace the `7` with `N-1`).

<!------------------------------------------------------>
<!------------------------------------------------------>
<!------------------------------------------------------>
<a name="Debugging"></a>
## 6. Building for debugging

If you would like to run [[scuff-em]] API codes in a debugger
like [<span class="SC">gdb</span>](https://www.gnu.org/software/gdb),
configure a separate build directory with debugging symbols, no
optimization, and (optionally) the single-threaded backend, which does
not play well with [[GDB]]:

```bash
% cmake -S . -B build-debug \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSCUFF_THREAD_BACKEND=None
% cmake --build build-debug -j
```

Then run the code from the build tree:

```bash
 % gdb build-debug/bin/scuff-ldos

 (gdb) set args < scuff-ldos.args

 (gdb) break GetHalfSpaceDGFs
Breakpoint 1 at 0x409626: file AnalyticalDGFs.cc, line 217.

 (gdb) run
```

After you are finished debugging, use the release build directory for
normal runs. Keeping separate build directories (`build` and
`build-debug`) avoids repeated full rebuilds and configuration churn.

**Note**: If you have a better debugging solution that
does not require disabling multithreading, please
let me know about it. Often the problem spots lie after expensive
sections like BEM matrix assembly, and then it takes forever for the
code to run in the debugger to get there.

[GitHub]:                      https://github.com/HomerReid/scuff-em/
[Issues]:                      https://github.com/HomerReid/scuff-em/issues
[LogFiles]:                    ../applications/GeneralReference.md#LogFiles
