# QPMS embedding of libscuff — assessment, 2026-11-07

Question under discussion: wire the scuff-em BEM core as a shared library into
QPMS, with QPMS's own `scatspec` YAML format (extended with gmsh mesh-file
paths for scatterers) replacing the `.scuffgeo` front end. This note records
what the current build scripts and headers already support and where the gaps
are. QPMS lives at `~/repo/qpms` (C11/CMake, Cython bindings; scatspec in
`qpms/scatspec.{h,c}`, format doc `scatspec.md`).

## Verdict

The build/packaging side already does what QPMS needs with zero changes. The
header API provides ~90% of the needed surface; the gaps are small and
library-side. The main *semantic* caveats are ErrExit-on-bad-input and the
C/C++ boundary.

## Build scripts: ready as-is

- Single aggregate shared library `libscuff` (all modules OBJECT libraries,
  aggregated; `BUILD_SHARED_LIBS=ON` default), SONAME `libscuff.so.0`.
  QPMS links exactly one `-lscuff` (see `ai_notes/cmake-implementation-2026-10-26.md`).
- Relocatable `scuff-em.pc`: `Cflags: -I{includedir}/scuff-em`,
  `Libs: -lscuff`, `Libs.private: HDF5/GSL/BLAS/LAPACK/OpenMP/...`.
- CMake package `find_package(scuff-em CONFIG)` → target `scuff-em::scuff`
  with find_dependency() for HDF5/BLAS/LAPACK/GSL/OpenMP.
- Both external-client modes (pkg-config and CMake package, shared and
  static) were validated during the CMake migration with an external C++23
  client.
- `scuff_common` INTERFACE target propagates `cxx_std_23` (affects only C++
  TUs of the consumer), `HAVE_CONFIG_H`, `VERSIONSTRING`, include dirs,
  warnings; `-Werror` only with `SCUFF_WARNINGS_AS_ERRORS` (not default).

## Headers: namespace'd, installed flat, mostly sufficient

- Everything in `namespace scuff`; 21 public headers installed flat under
  `include/scuff-em` (list in `libs/CMakeLists.txt`). No
  `using namespace std` in any installed header (Phase 4, `e4e17bda`);
  vocabulary typedefs `dVec/iVec/sVec` are std::vector aliases.
- `RWGSurface` (`libscuff.h:194`): constructs directly from a gmsh mesh file
  (`RWGSurface(const char *MeshFile, int MeshTag=-1)`), from raw
  vertex/panel arrays, or parametrically. Gmsh MSH formats 1/2/4 ASCII are
  read (`ParseMeshFiles.cc`). Copy deleted; dtor frees properly (recent
  ownership sweeps). `Transform(GT)`/`UnTransform()` available.
- `RWGGeometry` (`libscuff.h:374`): filename constructor ONLY. The
  post-parse finalization (BFIndexOffset, Mate[], MMJ detection, FIBBI
  caches) is inline in the ctor; members are effectively public
  (`//private:` commented out), so hand-assembly is possible but
  unsupported.
- Materials: OBJECT interiors become regions via `AddRegion(label,
  MaterialName)` → `new MatProp(name)`; region 0 is `EXTERIOR`.
  `RWGGeometry::SetEpsMu(label|all, Eps, Mu)` (`RWGGeometry.cc:1106`)
  overrides a region's MatProp with constants;
  `UpdateCachedEpsMuValues(Omega)` re-evaluates at each assembly, so a
  caller-driven frequency loop can call `SetEpsMu` per omega for dispersive
  media. Constant materials also expressible as MatProp coded names
  (`CONST_EPS<x>_MU<y>`, `VACUUM`, `PEC`).
- `GetSphericalMoments` is NOT declared in `libscuff.h` (scuff-tmatrix.cc:58
  declares it locally) — the only header gap for the T-matrix pipeline.

## T-matrix pipeline is already library-level

`applications/scuff-tmatrix/scuff-tmatrix.cc` is 250 lines total, ~90 of
which are the math: `AllocateBEMMatrix` → per-omega
`AssembleBEMMatrix(Omega, M)` + `M->LUFactorize()` → per incident
`SphericalWave` (libSpherical, public): `AssembleRHSVector(Omega, &SW, KN)`
+ `LUSolve` → `GetSphericalMoments(G, Omega, LMax, KN, TColumn)`. All of
these are in libscuff. QPMS already consumes scuff `.TMatrix` text files
(`QPMS_SCATSPEC_KINDSOURCE_SCUFF_FILE`), so in-process computation can feed
the same data with the same (L,M,P) column indexing; per-kind lMax already
exists in scatspec.

## Two integration routes

1. **Zero scuff-em changes**: the scatspec loader (or the QPMS shim) emits a
   temporary `.scuffgeo` — `OBJECT <label> / MESHFILE <path> / MATERIAL
   <name> / [TRANSFORMATION] / ENDOBJECT` + `MEDIUM` — and constructs
   `RWGGeometry(file)`. This is exactly the proven precedent:
   `scuffSolver::AddObject` + `scuffSolver::InitGeometry()`
   (`libscuffSolver/scuffSolver.cc:317`) writes `.pyscuffgeo.XXXXXX` from
   accumulated calls. (Env `PYSCUFF_RETAIN_GEOFILE` keeps the file for
   debugging.)
2. **Small library additions (recommended)**:
   a. Declare `GetSphericalMoments` in `libscuff.h`, or better lift the
      T-matrix loop from scuff-tmatrix.cc into a libscuff function (e.g.
      `ComputeTMatrix(G, Omega, LMax) -> HMatrix`), ~60 lines moved.
   b. Optional: a programmatic RWGGeometry builder (ctor taking
      `std::vector<std::unique_ptr<RWGSurface>>` + region materials), by
      extracting the ctor tail into a `Finalize()`. Removes the temp-file
      dance; medium-size refactor of RWGGeometry.cc:353-800.

For QPMS's T-matrix use case only single-object compact geometries are
needed (one surface per particle kind, computed in the particle frame);
placements/rotations stay in QPMS's own machinery, and periodicity stays in
QPMS's Ewald layer. scuff lattice/PBC machinery is not needed.

## Caveats

- **ErrExit everywhere**: malformed mesh files, unknown materials, wrong
  matrix sizes terminate the process (ErrExit is used in ctors, e.g.
  AddRegion). Tolerable for an MVP shim; a real fix is a longer error-path
  refactor. The scuff-tmatrix material-validity pattern
  (UpdateCachedEpsMuValues + isnan check of EpsTF/MuTF) is worth copying.
- **Language boundary**: QPMS core is C; libscuff is C++-only API (no
  extern "C", C++ classes). Pattern: a small C++23 shim TU in QPMS exposing
  an `extern "C"` C API (mirror of what scuffSolver does for its Python
  wrapper), linked against `scuff-em::scuff`; the rest of QPMS links the
  shim. pkg-config users must get libstdc++ via the shim being C++.
- **Threading**: assembly is OpenMP-parallel internally; QPMS should pin
  OMP_NUM_THREADS and avoid nested parallelism surprises.
- **Cache**: `PreloadCache`/`StoreCache` optionally accelerate repeated
  assemblies across an omega sweep (single-object meshes with many
  placements of the same kind hit the same caches).
- scatspec material strings (QPMS specexpr, `material: Ag` or key=value
  Drude params) → easiest mapping: QPMS evaluates eps/mu at omega and calls
  `SetEpsMu` per region per omega; constant media can alternatively use
  MatProp coded names.

## scatspec extension sketch (QPMS side)

New kind source, e.g. `QPMS_SCATSPEC_KINDSOURCE_SCUFF_MESH`:

```yaml
particles:
  ring:
    shape: mesh          # new: BEM-computed T-matrix from a mesh
    meshfile: meshes/ring.msh   # gmsh .msh path (resolved relative to spec)
    material: Ag
    lMax: 4
```

Flow: scatspec parse → per mesh-based kind, shim builds RWGGeometry (route 1
or 2) once per kind, computes T-matrix per omega inside
`qpms_scatspec_build_scatsys()` (or lazily), exposing it as an existing
file-based T-matrix generator would be.
