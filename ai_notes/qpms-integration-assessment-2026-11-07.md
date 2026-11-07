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

## Full integration / hybrid VSWF+BEM coupling (2026-11-07, follow-up)

Scope beyond T-matrix generation: (1) BEM-computed T-matrix → pure-MS solve
in QPMS; (2) VSWF coefficients back to the BEM representation (surface
currents/fields); ultimately a hybrid system matrix with VSWF–VSWF, BE–BE
and VSWF–BE blocks, assembled at QPMS level.

The right abstraction is a per-(kind, omega) "serviced BEM object" exposing
four operators, every one of which is a thin loop over *existing, public*
libscuff primitives (all verified in the headers today):

- **T** (NumMoments x NumMoments): scuff-tmatrix loop
  (`AssembleBEMMatrix` + `LUFactorize` once, then per incident
  `SphericalWave`: `AssembleRHSVector` + `LUSolve` + `GetSphericalMoments`).
- **X: VSWF→KN** (NumBFs x NumMoments): the same loop, storing the solved
  KN vectors instead of their moments. Legitimate because RHS assembly is
  linear in the incident field and `SphericalWave` is deliberately
  unit-amplitude (libIncField.h:197 — L/M/P only, NO amplitude member):
  an arbitrary incident VSWF coefficient vector p maps to KN = X·p.
  This operator is the new piece needed for step 3 (and the VSWF→BE
  coupling block for the hybrid); it costs one extra RHS solve per column
  beyond T, amortized over all downstream uses.
- **M: KN→VSWF** (NumMoments x NumBFs): `GetSphericalMoments` per RWG
  basis function (unit-KN columns). The BE→VSWF coupling block for the
  hybrid. **Gap: `GetSphericalMoments` is not declared in `libscuff.h`**
  (only a local prototype in scuff-tmatrix.cc:58) — must be added.
  Identity T = M·X gives an end-to-end convention/regression check.
- **B: BE–BE**: `AssembleBEMMatrix` (full) or per-pair
  `AssembleBEMMatrixBlock(nsa, nsb, ...)` — already declared public
  (libscuff.h:571). One RWGGeometry holding all meshed objects at their
  placements; `Mate[]` + FIBBI caches reuse identical-mesh work.

Step 3 (VSWF → surface fields) with these operators, per omega:

1. KN = X·p (p = QPMS incident VSWF coefficients in scuff convention).
2. Surface fields: `GetPanelSourceDensities(Omega, KN, PSD)` —
   TotalPanels x 13: cols 0-2 centroid, 3 area, 4 electric charge density,
   5-7 electric current K, 8 magnetic charge eta, 9-11 magnetic current N,
   12 Poynting flux (GetDipoleMoments.cc:150 comment);
   `PlotSurfaceCurrents` / `S->PlotScalarDensity` for mesh plots.
3. Field maps: `GetFields(IFList, KN, ...)` treats IF and KN as
   independently optional (GetFields.cc:380ff: `if (KN)` adds scattered,
   `if (IFList)` walks the incident chain) — so scattered-only maps work
   with IF=nullptr, and QPMS evaluates the incident VSWF superposition
   with its own evaluator. `IncField::Next` chaining exists
   (libIncField.h:60) and `AssembleRHSVector` walks the chain, but without
   a SphericalWave amplitude member a chain can only carry unit waves —
   irrelevant for the X route; adding an amplitude member would be a small
   optional convenience patch.

Hybrid system (assembled in QPMS): BEM object j has DOF KN_j; rows
B_jj·KN_j + Σ_k≠j B_jk·KN_k = X_j·p_j(incidence), with incidence from
translated outgoing fields of other objects (particles via T_i, BEM
objects via M_j·KN_j) handled by QPMS's translation/Ewald machinery. An
isolated BEM object among T-matrix particles is fully characterized by T
(X needed only for post-processing); explicit KN DOF are needed once ≥2
BEM objects couple directly.

Conventions: QPMS already converts scuff's SW/.TMatrix convention on the
`SCUFF_FILE` import path; the same conversion applies elementwise to p
vectors entering X and moment vectors leaving M. Reuse that code.

Work items on the scuff-em side (unchanged build story — one libscuff,
pkg-config / CMake package):
1. Declare `GetSphericalMoments` in `libscuff.h` (required).
2. Recommended small helpers as thin libscuff loops (else the shim loops):
   `GetSphericalMomentMatrix` (M) and `GetSWResponseMatrix` (X).
3. Optional: amplitude member for `SphericalWave` (convenience only).
4. ErrExit robustness now matters more: QPMS will hold geometry/solver
   objects long-term and call them repeatedly; malformed input terminates
   the process today.
5. Geometry construction route (temp-.scuffgeo vs builder ctor) as above;
   per-object placement via TRANSFORMATION lines or `RWGSurface::Transform`.

QPMS-side items: convention mapping for vectors (reuse import-path code);
per-(kind, omega) caching of T/X/M and the factorized BEM matrix; the
hybrid assembly in `qpms_scatsys_t`; the C shim exposing these operators
with an explicit handle-based lifetime model.
