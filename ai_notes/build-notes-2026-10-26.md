# SCUFF-EM build notes — 2026-10-26

## Environment

- Debian GNU/Linux 13.7 (trixie), x86_64, 6 cores
- GCC/G++/gfortran 14.2.0
- autoconf 2.72, automake 1.17, libtool 2.5.4, GNU Make 4.4.1
- Python 3.13.5
- Repository at commit `b6e6c869` (branch `oc`, version string
  `v0.96-mmn-23-gb6e6c869`)
- No sudo access in the build environment, so system packages were not installed.

## Missing Debian packages

Already present (base build worked): `build-essential`, `gfortran`, `autoconf`,
`automake`, `libtool`, `pkg-config`, `libgsl-dev`, `libblas-dev`,
`liblapack-dev`, `libreadline-dev`, `python3-dev`, `python3-numpy`.

Missing for a *default* configure:

- `libhdf5-dev` — HDF5 is required by default. Without it configure stops with
  `couldn't find HDF5; configure --without-hdf5`.

Missing for the Python interface (and therefore also for the default build on this
machine, because shared libs end up enabled — see issue 2):

- `swig` — needed to generate `scuff-python.cpp` / `scuff.py`.

Recommended install command on Debian:

```sh
sudo apt install libhdf5-dev swig
# if building from a bare system:
sudo apt install build-essential gfortran autoconf automake libtool pkg-config \
  libgsl-dev libblas-dev liblapack-dev libreadline-dev libhdf5-dev swig \
  python3-dev python3-numpy
```

`libdcutri` is optional and is not available as a Debian package. Configure
silently omits it; no action needed unless DCUTRI integration is wanted.

## Verified build commands and results

Minimal build (no HDF5, no Python):

```sh
./autogen.sh --without-hdf5 --without-python
make -j6
```

Result: success; all command-line applications and `CheckSCUFFData` built.

Full build (HDF5 + Python) was verified by extracting `libhdf5-dev` and `swig`
into a temporary prefix (because sudo was unavailable), then configuring with:

```sh
./configure --enable-maintainer-mode --enable-shared --with-python \
  --with-hdf5-includedir=/usr/include/hdf5/serial \
  --with-hdf5-libdir=/usr/lib/x86_64-linux-gnu/hdf5/serial
make -j6
```

Result: success, including `_scuff.so`; `import scuff` from
`libs/python` works when both `libs/python` and `libs/python/.libs` are on
`PYTHONPATH` (the latter is needed only for the uninstalled build):

```sh
LD_LIBRARY_PATH="$PWD/libs/libscuff/.libs:$PWD/libs/python/.libs" \
PYTHONPATH="$PWD/libs/python:$PWD/libs/python/.libs" \
python3 -c "import scuff"
```

Test suite:

```sh
PATH="$PWD/applications/scuff-scatter:$PWD/applications/scuff-transmission:$PWD/tests:$PATH" \
OMP_NUM_THREADS=2 make check
```

Result: `TestMie.sh` PASS, `TestFresnel.sh` PASS (both in minimal and full
configurations).

## Issues found and suggested fixes

### 1. GCC 14: `machcon.c` fails on implicit declaration of `exit` (fixed)

`libs/libSpherical/machcon.c` includes `<stdio.h>` but calls `exit()`. GCC 14
promotes implicit function declarations to an error, so the build stopped with:

```text
machcon.c:737:5: error: implicit declaration of function 'exit'
```

Applied fix: add `#include <stdlib.h>`.

```diff
 #include <stdio.h>
+#include <stdlib.h>

 /* Table of constant values */
```

Temporary workaround without editing the source:

```sh
make CFLAGS="-g -O2 -Wno-error=implicit-function-declaration"
```

The include is the correct long-term fix.

### 2. `AC_DISABLE_SHARED` is called after `LT_INIT`, so it has no effect

`configure.ac` calls `LT_INIT` (line 10) before `AC_DISABLE_SHARED` (line 15).
With libtool, `AC_DISABLE_SHARED` must come before `LT_INIT`. As a result
configure always reports `checking whether to build shared libraries... yes`,
even though the comment says shared libraries should be disabled by default.

Consequence: because shared libraries are enabled, the Python interface is
enabled whenever Python and Numpy headers are present. On this machine that made
the default build require SWIG even though the user did not ask for Python.

Suggested fix: move `AC_DISABLE_SHARED` before `LT_INIT`, and/or explicitly
document `--disable-shared` / `--without-python`.

### 3. Python wrapper only has a build rule in maintainer mode

In `libs/python/Makefile.am` the SWIG rules are inside `if MAINTAINER_MODE`.
The generated files `scuff-python.cpp` and `scuff.py` are not committed. A plain

```sh
./configure --with-python
make
```

fails with `No rule to make target 'scuff-python.cpp'`. `./autogen.sh` happens to
work because it always passes `--enable-maintainer-mode`.

Suggested fixes, in order of preference:

1. Commit/distribute the SWIG-generated files.
2. Move the SWIG rule outside `if MAINTAINER_MODE` (guarded by `WITH_PYTHON`).
3. At minimum, make `--with-python` imply/require `--enable-maintainer-mode` and
   print a clear configure error if SWIG is missing. Currently a missing SWIG is
   only detected at build time as `swig: command not found` (Error 127).

### 4. Debian HDF5 layout is not auto-detected

Debian's `libhdf5-dev` installs:

- headers in `/usr/include/hdf5/serial/` (there is no `/usr/include/hdf5.h`)
- `libhdf5.so` and `libhdf5_hl.so` under
  `/usr/lib/x86_64-linux-gnu/hdf5/serial/` (not directly in
  `/usr/lib/x86_64-linux-gnu`)

`configure` only checks `hdf5.h` in the default include path, so a default
configure fails even with `libhdf5-dev` installed. The working flags are:

```sh
--with-hdf5-includedir=/usr/include/hdf5/serial
--with-hdf5-libdir=/usr/lib/x86_64-linux-gnu/hdf5/serial
```

(Use `dpkg-architecture -qDEB_HOST_MULTIARCH` instead of hard-coding
`x86_64-linux-gnu` on other architectures.)

Suggested improvement: probe `hdf5/serial`, `h5cc -show`, or `pkg-config hdf5`
in `configure.ac`, or at least print these Debian paths in the error message.
Minor cleanup: `configure.ac` line 118 appends the HDF5 library `-L` path to
`CPPFLAGS` as well as `LDFLAGS`; it should only go into `LDFLAGS` (and perhaps
`LIBS`).

### 5. Python `distutils` usage is fragile on Python 3.12+

`configure.ac` obtains the Python include directory with
`import distutils.sysconfig`. `distutils` was removed from CPython 3.12 and only
works here because Debian's setuptools provides a compatibility shim. Use
`sysconfig.get_path("include")` or `python3-config --includes` instead.

### 6. `make check` needs built executables on `PATH`

The test scripts locate `scuff-scatter`, `scuff-transmission`, and
`CheckSCUFFData` via `which`, but Automake does not add the build directories to
`PATH`. A plain `make check` in the build tree can therefore fail with
`could not find ... executable (check PATH?)`. The command that works is shown
above. A `TESTS_ENVIRONMENT`/`AM_TESTS_ENVIRONMENT` setting in
`tests/Mie/Makefile.am` and `tests/Fresnel/Makefile.am` would make plain
`make check` work.

### 7. Out-of-tree builds are blocked after an in-tree configure

Attempting an out-of-tree configure after configuring this checkout in-tree
fails with `source directory already configured; run "make distclean" there
first`. This is standard Autotools behaviour, but worth knowing: use a separate
clone/worktree if both configurations are needed.

### 8. `.gitignore` does not cover all generated files

After a build/test run, `git status` shows untracked artifacts such as
`applications/scuff-spectrum/scuff-spectrum`, `applications/scuff-neq/scuff-integrate`,
`applications/scuff-spectrum/tBeyn411`, `tests/CheckSCUFFData`,
`scuff-em-pkgconfig`, `scuff-em.pc`, and the generated test outputs under
`tests/Mie` and `tests/Fresnel`. Adding these patterns (and `*.trs`, `*.pc`,
`__pycache__/`) to `.gitignore` would keep the working tree clean.

## Current state left in the source tree

- `libs/libSpherical/machcon.c` contains the one-line `#include <stdlib.h>` fix.
- The in-tree build was reconfigured with `--without-hdf5 --without-python` after
  the full-feature verification, so `make` works without the temporary
  `/tmp/scuff-deps-*` packages. Install `libhdf5-dev` and `swig`, then rerun
  `./autogen.sh --enable-shared --with-python --with-hdf5-includedir=... --with-hdf5-libdir=...`
  to rebuild with optional features.
- Temporary extracted packages used for the full-feature test were under
  `/tmp/scuff-deps-okurka/` and can be deleted.
