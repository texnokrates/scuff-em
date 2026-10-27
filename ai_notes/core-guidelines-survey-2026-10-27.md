# C++ Core Guidelines survey and C / C++23 disposition — 2026-10-27

Status: **survey note; G0–G2 of the plan are now implemented** (results in
§8).  This note
maps violations of the ISO C++ Core Guidelines across the hand-written
sources and decides, per module, whether to refactor as clean C++23
following the guidelines (including pointer-ownership conventions) or to
convert effectively-C files to plain C.  It complements
`ai_notes/c++23-migration-plan-2026-10-26.md` (CLI11/C++23 phases) and
`ai_notes/c++23-status-2026-10-27.md` (workstream status).

Method: six parallel read-only module-cluster surveys (libhrutil; libscuff;
libscuffSolver+libStaticSolver+libIncField+libMatProp; libhmat+libSubstrate+
libMDInterp; libSpherical+libTriInt+libSGJC; the twelve built applications),
plus a `clang-tidy-22` baseline run with `cppcoreguidelines-*`,
`bugprone-*`, `modernize-*` over `libs/libhrutil/libhrutil.cc` as a
quantitative sample.  All line references were verified against HEAD
`0dc43f89`.

Scale: ~70k hand-written lines under `libs/`, ~20k under `applications/`
(excluding generated tables, vendored third-party code, and the dormant
`scuff-cas2D`/`scuff-microstrip` trees).

---

## 1. Adopted conventions (what "per Core Guidelines" means here)

1. **Ownership (R.1–R.14, R.21–R.23, R.30–R.37).**
   - Owning resources default to `std::unique_ptr` (`make_unique` for
     construction, `vector<T>`/`vector<unique_ptr<T>>` for sets).
   - Raw owning pointers that must remain raw (C interop, LAPACK
     workspaces, cache internals) are annotated `gsl::owner<T*>`.
   - Non-owning parameters are `T*`, `T&`, or `std::span` — never ambiguous
     "maybe-owning" pointers; factories return `unique_ptr` or
     `gsl::owner<T*>` with documented deletion responsibility (I.11, I.12,
     I.13); aliasing parameters keep `T*` plus a comment stating borrowed
     lifetime (R.30).
   - **Tooling decision:** define `gsl::owner` as a two-line alias in an
     installed header (e.g. `libs/libhrutil/Owner.h`) rather than vendoring
     the full GSL.  `clang-tidy` recognizes `gsl::owner` by name, so
     `cppcoreguidelines-owning-memory` becomes useful immediately; a full
     GSL dependency can come later if wanted.
2. **Bounds (ES.28, Bounds rules).**  New/changed code uses `std::array` /
   `std::vector` / `std::span`; `pro-bounds-*` findings in numerical
   kernels are not chased mechanically — they get fixed when a kernel is
   refactored, not in bulk.
3. **Error handling (E.2, E.25, E.30).**  Applications keep the
   exit-at-`main` convention (ErrExit is acceptable at the CLI boundary).
   Library code that will be reused by tests/other callers migrates from
   `exit()` to returned error state (`std::optional` / `std::expected` /
   out-param), matching migration-plan §6.3.  No exceptions inside
   numerical kernels.
4. **Classes (C.21, C.129).**  Anything with an owning raw member gets a
   deleted or correct copy/assignment (rule-of-five); polymorphic bases
   keep public virtual destructors (`IncField` already complies).
5. **Constants/macros (ES.30–ES.33, Enum.3–Enum.5).**  `#define` constants
   become `constexpr`; new enumerations are `enum class`; existing `#define`
   sets (PFT_*, PPIALG_*, …) become `enum class` when their module is
   refactored, not in bulk.
6. **Constraints inherited from the master plan:** numerical results stay
   byte-for-byte identical unless a specific defect is being fixed;
   generated/vendored code is excluded (see §2); the CLI11 Stage B/C
   sequence continues unchanged.

---

## 2. Executive disposition summary

| Module | Disposition | Headline |
|---|---|---|
| libhrutil | mixed (see §7) | String/path/S2CD/logging → C++23; POSIX plumbing and thin varargs core stay C-spirited; ScuffCLI already exemplifies the target style |
| libscuff | C++23 refactor | The API anchor; rule-of-five + unique_ptr, `void*` erasure removal; geometry core last |
| libscuffSolver | C++23 refactor | Highest defect density found (§5); TBlocks double-delete, CachedIF type confusion |
| libStaticSolver | C++23 refactor | Dead StaticSubstrate.cc to delete; ErrExit(NULL) bug |
| libIncField | C++23 refactor | Small virtual hierarchy; new/free UB |
| libMatProp | C++23 refactor | Implicit-copy double-free trap; uninitialized Owns* flags |
| libhmat | C++23 refactor | Pointer-steal "copy ctors", LAPACK workspace realloc dance; formats/layout frozen |
| libSubstrate | C++23 refactor (light) | InitScalarGFInterpolator aliasing trap; 4 dead files to delete |
| libMDInterp | C++23 refactor + 2 plain-C files | BinSearch.cc/freadEC.cc → plain C; Interp classes rule-of-five |
| libSpherical | C++23 refactor | cdouble makes plain C pointless; unique_ptr kills ~15 new[]/delete[] pairs |
| libTriInt | **mostly plain C** | libTriInt.cc, TCR.cc, CCCubature.cc, IntegrateCliffFunction.cc have zero C++ content; LatticeSum/BZIntegration stay C++ (HMatrix in signatures) |
| libSGJC | leave-as-is | libSGJC.c already plain C; hcubature.c **and pcubature.c** are vendored SGJ cubature |
| applications (12) | C++23 refactor | Plain C infeasible — every main constructs C++ library objects; RAII at main scope piggybacked on Stage B CLI11 migrations |

Excluded from everything: generated tables (`CCRules.cc`, `LebedevRules.cc`,
`clencurt.h`, `TValues.cc`, `G1G2Values.cc`, …), f2c output
(`zbsubs.c`, `machcon.c`, `drc3jm.c`), cmatheval parser/scanner, vendored
cubature (`hcubature.c`, `pcubature.c`, `vwrapper.h`, `converged.h`),
vendored LAPACK prototypes (`lapack.h`, `lapack_names.h`), `Faddeeva.cc`,
`libVec.h`, and the dormant `scuff-cas2D`/`scuff-microstrip` trees.

---

## 3. Violation catalog (aggregate, representative examples)

### R — resource management (systemic; ~250 sites in libscuff, ~140 in apps, ~65 in libhmat/libSubstrate/libMDInterp, ~45 in solver/field libs)

- Owning raw-pointer members without copy control: `RWGSurface`
  (libscuff.h:243–315), `RWGGeometry` (libscuff.h:543–617), `HMatrix`
  (`DM/ZM/ipiv/work`, HMatrix.cc:111–239), `FIPPICache`, `FIBBICache`,
  `EquivalentEdgePairTable` (`void *MasterTable`).
- `malloc/free` family: 261 textual occurrences in libscuff alone; mixed
  `new`/`free` and `mallocEC`/`delete` mismatches exist (see §5).
- Owning factory returns, undocumented: `AllocateBEMMatrix`,
  `AssembleBEMMatrix`, `AssembleRHSVector`, `CreateGBarAccelerator`,
  `InitPFTOptions`, `ReadIncFieldList`, `ReadExcitationFile`,
  `ParsePortFile`, `LinSpace/LogSpace/GetOmegaList` (libhmat.h:154–160),
  `QR/Eig/SVD` silently allocating `Q/R/U/VT` (LBWrappers.cc:441–772),
  `GetWaveMatrix` (consume-semantics: deletes a wrong-size caller buffer,
  VectorSphericalWaves.cc:226–232), `GetSphericalMoments` (hidden
  reallocation of the caller's HVector, libscuff/GetSphericalMoments.cc:113,237).
- Allocate-if-NULL out-params: `GetEntries` (GetEntries.cc:198–284),
  `StaticSolver::Assemble...`/`PostProcessing` `OwnsM`-style flags,
  `Z2S/S2Z` (OutputModules.cc:545–584).
- `void*` type erasure: `CreateFIBBICache`/`DestroyFIBBICache`,
  `CreateABMBAccelerator`, `opTable`/`opFC` (libscuffInternals.h:79–347),
  `OpenHDF5Context(void*)`, integrand callback `void *fdata` protocols.

### C — classes / copy control (~12 classes affected)

- Rule-of-three violations (owning members, no copy control): `RWGSurface`,
  `RWGGeometry`, `FIPPICache`, `FIBBICache`, `EquivalentEdgePairTable`,
  `MatProp` (implicit copy shallow-copies `InterpReal`/`EpsExpression` while
  `OwnsInterpolators=true` — double-free trap, libMatProp.cc:101–150),
  `Interp2D/3D/4D` (owning `CTable`, no copy control at all), `RWGPortList`
  (owns port arrays, has no destructor at all).
- Non-standard ownership-stealing "copy ctors": `HMatrix(HMatrix *,
  bool takedatandownership=false)` (libhmat.h:177), `HVector(HVector *)`
  (libhmat.h:100), `Interp1D(Interp1D *)` (libMDInterp.h:216).
- `GTransformation` is the in-tree positive example (correct copy control).

### I — interfaces (~90 APIs)

- Out-param conventions vs return values (F.20/F.21): the `Vec*` family
  writing through a third pointer, `Toc(unsigned long *)`, `GetMemoryUsage`,
  `QR(HMatrix **Q, HMatrix **)`, 11-parameter `GetHalfSpaceDGFs_BZ`.
- Nullable-in/out pointers without `optional`: `GetSubstrateDGF(...,
  HMatrix *GMatrix=0)` allocating when null (FullWave.cc:299), documented
  only by implication.
- In-params that get mutated or deleted: `CheckHMatrix` may delete its
  candidate (documented, libhmat.h:406–410); `Tokenize(char *)` mutates its
  input and the `vector<char*>` overload returns views into the mutated
  string (44 call sites — the most dangerous API in libhrutil).
- Magic-number enum coding: `EEI_FORCE_PP 12345` (libscuffInternals.h:117),
  `PFTMethod` ints; deliberately-half-initialized arg structs
  (`GetPPIArgStruct` et al., libscuffInternals.h:52–56).

### ES / expressions, statements (~120 sites)

- Fixed `char[N]` buffers with implicit truncation contracts (apps: ~25;
  libMatProp `Expr[100]`, `ConstantNames[25][1000]` = 25 KB stack;
  scuffSolver `Line[100]`).
- `memset` on non-trivial `cdouble`/struct arrays (~30 sites; the class of
  `-Wclass-memaccess` warnings from the master plan baseline).
- Uninitialized members: `MatProp()` never sets `OwnsExpressions`/
  `OwnsInterpolators` (libMatProp.cc:82–86); `RWGSurface` early-return paths
  leave members indeterminate; apps rely invisibly on `ProcessOptions`
  zeroing their counters (scuff-scatter.cc:69–119).
- `#define` constants and option-value macros (`MAXSTR`, `MAXFREQ`,
  `DEG2RAD`, `PA_*`, `COMPLETE_VARARGS` injecting `char buffer[1000]` into
  callers).

### E — error handling (~350 `ErrExit`/`exit` sites)

- Library-layer `exit()` blocks the RAII conversion of applications: no
  cleanup runs on any error path today (acceptable only because everything
  is leaked at exit anyway).
- Two competing library error channels: `char *ErrMsg` out-members
  (`RWGSurface`, `MatProp`, `HMatrix` — strdup'd, mostly never freed) vs
  immediate `ErrExit`; plus the `AbortOnIOError` static toggle in libhmat
  (libhmat.h:385–392).
- Known broken consumers: `StaticSolver.cc:67–70` prints a NULL format
  instead of the message (§5).

### Enum / CP / SF

- Zero `enum class` in the tree; every enumeration is a `#define` set.
- Global mutable state: material cache `MPDB` (libMatProp.cc:60–68,
  process-lifetime, never freed), `GlobalFIPPICache` (rwlock-protected),
  libscuff static flags set from environment (~15 sites), function-static
  growable workspaces (`DSIPFT.cc:127`, `EMTPFT.cc:483–571`,
  `GetDyadicGFs.cc:74`, `BZIntegration.cc:265–274` — races and exit leaks),
  libhrutil `LogFileName`/`NumThreads`/`DefaultCD2SFormat` statics.
- `using namespace std;` in the installed `libhrutil.h:36` (SF.7), injected
  into ~229 translation units — already on the master plan (Phase 4, step
  31); the survey confirms it should ship together with the
  `libhrutiltypes.h` split and `constexpr` constants.

### clang-tidy-22 baseline (`libhrutil.cc`, single file)

~520 findings: 130 `modernize-use-trailing-return-type` (style; ignore),
97 `pro-bounds-array-to-pointer-decay`, 55 `pro-type-vararg` + 15
`avoid-variadic-functions` (the printf-wrapper design; keep), 48
`pro-bounds-pointer-arithmetic`, 42 `use-nullptr`, 20 `avoid-magic-numbers`,
15 `owning-memory`, 14 `macro-usage`, 13 `avoid-c-style-cast`, 12
`init-variables`, 9 `avoid-non-const-global-variables`, 4 `no-malloc`, plus
`bugprone-suspicious-realloc-usage`, `bugprone-unchecked-string-to-number-
conversion`, `bugprone-assignment-in-if-condition`.  Recommended ratchet
check set (CI, non-blocking first): `cppcoreguidelines-init-variables`,
`-pro-type-member-init`, `-no-malloc`, `-owning-memory`,
`bugprone-*` minus pedantic, `modernize-use-nullptr/-use-using/
-avoid-c-style-cast`.  Full `pro-bounds-*` waits for the span conversion.

---

## 4. Ownership traps worth knowing before refactoring

1. `PFTOptions` — most ambiguous ownership in libscuff (`IF`, `RHSVector`,
   `DRMatrix`, `kBloch`, `TInterior/TExterior`: allocation site
   `InitPFTOptions`, deletion site the caller, documented nowhere).
2. `LayeredSubstrate::InitScalarGFInterpolator` returns the same pointer
   stored in the member; callers must not delete; only
   `DestroyScalarGFInterpolator` may (MOI.cc:731–737) — undocumented.
3. Mate-surface aliasing: `FIBBICaches[ns] = FIBBICaches[Mate[ns]]`
   (RWGGeometry.cc:801–806), `TBlocks[ns]=TBlocks[Mate]` in scuff-scatter,
   scuff-neq, scuff-ldos, scuffSolver — naive loop-delete double-frees;
   mate entries must be observers, not owners.
4. `HMatrix(HMatrix*, bool takedatandownership)` steals; the implicit copy
   ctor double-frees; `ownsM`/`ownsV` flags scatter ownership decisions
   through call sites.
5. Process-lifetime caches that no application can free: `MPDB`,
   `GlobalFIPPICache`, FIBBI tables, GBarAccelerators, EEP tables — part of
   the 74 MB exit leak is library-side by design; applications should only
   RAII-ify what they own.
6. `IncField` chain: intrusive `Next` list with ownership split between
   traversal and deletion (`DeleteIncFieldChain`).

---

## 5. New defects found by the survey (candidates for the correctness phase)

These are *in addition to* the master-plan §5 list (mostly already fixed).
Severity: **A** = live memory-unsafety/wrong behavior on plausible paths;
**B** = leak/latent; **C** = dormant (dead or uncalled code).  "N" marks
numerics-affecting fixes that need a reference-validation gate before
landing.

| ID | Location | Finding | Sev |
|---|---|---|---|
| G-01 | libs/libscuff/RWGGeometry.cc:832→837 | `~RWGGeometry` reads `Mate` after `free(Mate)` — use-after-free in the destructor itself | A |
| G-02 | libs/libIncField/IncField.cc:67 vs :122 | `free(LBasis)` on a pointer that was `new`'d — new/free mismatch (UB) | A |
| G-03 | libs/libscuffSolver/scuffSolver.cc:162→165 | `free(G->Substrate)` immediately before assigning `new LayeredSubstrate` — free/new mismatch | A |
| G-04 | libs/libscuffSolver/scuffSolver.cc:688–692 | Unconditional `IncField*`→`PlaneWave*`/`PointSource*` casts to build `CachedIF`; overwrites the old cache without deleting it — type confusion/slicing + leak | A |
| G-05 | libs/libscuffSolver/scuffSolver.cc:559 + dtor :97–101 | `TBlocks[ns]=TBlocks[Mate]` aliasing deleted per-slot — double delete on mate geometries | A |
| G-06 | libs/libStaticSolver/Excitations.cc:495 | `mallocEC(sizeof(**SEList))` allocates struct-size instead of pointer-size | A |
| G-07 | applications/scuff-rf/GetPortVoltages.cc:321–327 | Inverted free guard `if (PanelCharges==0) free(PanelCharges)` — leaks the old buffer on every resize | A |
| G-08 | libs/libMatProp/libMatProp.cc:82–86, 313–347 | Uninitialized `OwnsExpressions/OwnsInterpolators` flags read on the cache-copy path; implicit copy ctor double-free trap | A |
| G-09 | libs/libscuff/AssembleRHSVector.cc:388,414; GetFieldGradients.cc:457,481 | `memcpy(ThreadData)` shallow-copies structs containing pointers — aliasing/UAF hazard under OpenMP | A |
| G-10 | libs/libscuff/FIPPICache.cc:111–118, 177–183 | Cache key pointers leaked on every miss; entries never freed in the destructor (by-design-ish, but the per-miss key leak is a real defect) | B |
| G-11 | libs/libhrutil/libhrutil.cc:960–972 | Signal handler does unconditional `fclose(f)` where `f` may be `stderr`; handler also does fprintf/fopen (async-signal-unsafe) | A |
| G-12 | libs/libhrutil/ProcessOptions.cc:447 (via :478–489) | Repeated `PA_STRING` option overwrites the previous `strdupEC` value — leak per repeat | B |
| G-13 | libs/libhrutil/libhrutil.cc:1062–1070 | `CheckEnv(char**)` returns an owning `strdup` with no documentation — leak-prone at all call sites | B |
| G-14 | libs/libStaticSolver/StaticSolver.cc:67–70 | `ErrExit(ErrMsg)` prints a NULL format instead of `Substrate->ErrMsg` | A |
| G-15 | libs/libIncField/IncFieldList.cc:251 | `IFList->NumIFs` dereferenced when the file yielded zero entries — NULL deref | A |
| G-16 | applications/scuff-rf/scuff-rf.cc:240–252 | `/*!!!*/` debug block: unconditional `/tmp/Port%i.Old` writes and null-deref of `FreqList` when no frequency option given | A |
| G-17 | applications/scuff-cas3D/scuff-cas3D.cc:214–224 | Missing `else`: `--XiQuadrature CLIFF` falls through to `ErrExit("unknown value")` — wrong behavior | A |
| G-18 | applications/scuff-neq/scuff-neq.cc:237 | `!OmegaKBPoints==0` precedence inversion — errors when the option *is* given | A |
| G-19 | applications/scuff-analyze/scuff-analyze.cc:594, :618–623 | Second `G=new RWGGeometry` overwrites the first (leak + double-owner); dangling `if (MeshFile)` whose body is the final printf | A/B |
| G-20 | libs/libSpherical/VectorSphericalWaves.cc:151 | `memset(RFArray,0,3*(LMax+1))` missing `*sizeof(cdouble)` — r==0 path consumes stack garbage | **N**, A |
| G-21 | libs/libSpherical/VectorSphericalWaves.cc:118 | Loop increments `AAMatrix` instead of `nc` (UB) — overload appears uncalled | C |
| G-22 | libs/libSpherical/libSpherical.cc:282 | `(m%1)` is always 0 — likely intended `m%2` in the `GetYlm` negative-m branch; changing it would change results | **N**, verify |
| G-23 | libs/libTriInt/BZIntegration.cc:676, 265–274 | `InitBZIArgs` has no free function anywhere (used by scuff-cas3D/caspol/ldos); static TC workspace freed only on size change | B |
| G-24 | libs/libhmat/* (LBWrappers.cc:278–947) | ~18 unchecked `realloc` sites in LAPACK workspace growth | B |
| G-25 | libs/libMDInterp/InterpND.cc:663–679 | `vec2str` 10-slot static ring buffer: thread-unsafe, pointers invalidated after 10 calls | B |
| G-26 | libs/libscuffSolver/RWGPorts.cc:73–87, :399 | Unchecked `malloc`/`realloc` (not `mallocEC`) for port structures; no destructor anywhere → port lists leak | B |
| G-27 | applications/scuff-transmission/scuff-transmission.cc:1 | Corrupted license header comment ("Homer ReidElectricOnly=true;") | C |

Fix sequencing follows master-plan Phase 1 discipline: one commit per
cluster, each with a sanitizer repro or unit test where feasible; G-20 and
G-22 need numerical comparison against the Mie/Fresnel suite (and an
upstream/reference check) before any change; G-21/G-27 land as-is because
they are behavior-neutral.

---

## 6. Plain-C conversion mechanics

For the recommended conversions (libTriInt quartet, libMDInterp pair):

1. Rename `.cc` → `.c`; replace `bool` with `stdbool.h`, `new[]/delete[]`
   with `mallocEC/free` (IntegrateCliffFunction.cc only), drop any
   libhrutil C++-only dependencies (CCCubature's error path needs a local
   handler or `ErrExit` stays — ErrExit is C-compatible).
2. Declare the converted functions `extern "C"` in the existing headers
   (libSGJC.h already demonstrates the pattern).
3. Verified caller compatibility: no caller of the libTriInt quartet
   consumes any C++ feature (call sites pass pointers/ints/doubles and
   bool arrays); ABI is unchanged, numerics untouched.
4. GCC 14 defaults to gnu17: `_Thread_local`, `stdbool.h`, `snprintf` all
   available; no blocker found.

Not converted (deliberately): libSpherical and libMDInterp Interp classes —
`cdouble` (`std::complex`) arithmetic and class APIs are load-bearing;
converting them would be a rewrite with real numerical risk and no gain.
libhrutil's POSIX plumbing stays C-spirited C++ (see §7) because `ErrExit`,
`thread_local`, and the varargs helpers are shared with everything else.

---

## 7. Plan (coordinated with the CLI11 commit sequence)

Ordering principle: conventions and correctness first; mechanical sweeps
before ownership redesign; geometry core last.  Each step is one or more
small commits, build+CTest green, notes updated.

**G0 — conventions (small, standalone)**
1. `[libhrutil] Add gsl::owner alias and ownership documentation header`
   (Owner.h; document R.* conventions; wire into libhrutil.h).
2. `[ai_notes] Record Core Guidelines survey and dispositions` (this note).

**G1 — defect fixes from §5** (before any ownership refactor, mirroring
master-plan Phase 1 style): G-01..G-19, G-23..G-26 in per-module commits
(`[libscuff] Fix ...`, `[scuff-rf] Fix ...`, etc.); G-20/G-22 gated on a
numerics verification task (compare Mie/Fresnel against pre-change
baseline, inspect upstream); G-21/G-27 trivial commits.  Also: delete the
signal-handler `fclose(stderr)` hazard (G-11) as part of libhrutil
hardening.

**G2 — dead code removal + plain-C conversions** (each independently
buildable; recommend confirming deletions before doing them):
- Delete dead/unbuilt files: libStaticSolver/StaticSubstrate.{h,cc},
  libSubstrate/{Interpolation,FullWave_PlaneWave,qIntegrate,
  MosigMichalski}.cc, libMDInterp/WriteMatrix*.cc + doom.cc,
  libIncField/tPointSource.cc (stale), scuff-rf/PointInsideObject.cc +
  GetMinDistanceToPanel.cc (unbuilt duplicates), libhrutil/PASnippet.cc.
- `[libTriInt] Convert cubature files to plain C` (quartet + extern "C").
- `[libMDInterp] Convert BinSearch/freadEC to plain C`.

**G3 — ownership modernization, leaf-first**
1. Applications piggyback RAII on their Stage B CLI11 migration (one app =
   one migration commit): `scuff-scatter` next per the existing plan —
   typed CLI11 + main-scope `unique_ptr` for G/M/RHS/KN/IFList +
   `vector<unique_ptr<HMatrix>>` with mate observers.  tmatrix gets the
   same finishing touch as the canonical example.
2. Mechanical sweeps in libscuff: Fields/moments + PFT function-static
   buffers → `unique_ptr`/`vector`; factory returns annotated
   `owner<T*>`/converted to `unique_ptr` per subsystem.
3. libhmat: rule-of-five for HMatrix/HVector (keep pointer-steal ctor as a
   named static factory `HMatrix::Adopt(...)` during transition), LAPACK
   workspaces → `vector<T>`, formats and column-major layout frozen.
4. libMatProp/libSubstrate/libMDInterp: `MatProp` copy-control fix,
   MPDB → typed map, Interp rule-of-five, scalar-GF interpolator lifetime
   documented/unique_ptr'd.
5. libscuff caches: kill `void*` erasure, unify on `std::shared_mutex`,
   fix FIPPICache key leak; concurrency review (TSan job from the master
   plan lands here).
6. libscuff geometry core last: RWGSurface/RWGGeometry rule-of-five,
   containerization, destructor-leak cleanup (most of the 74 MB backlog),
   after `owner`/`unique_ptr` conventions exist everywhere else.

**G4 — ratchet:** clang-tidy subset (§3) in CI non-blocking; flip to
blocking-for-changed-files once quiet; warning-budget enforcement from the
master plan continues in parallel.

### Open decisions (defaults in parentheses)

1. Delete the listed dead files outright, or quarantine under OldCodeFiles/
   (recommend: delete; git history preserves them).
2. G-22 (`m%2`): investigate now with a reference implementation, or defer
   (recommend: investigate with upstream Ylm sources; if confirmed, fix
   behind a numerics comparison).
3. Minimal `gsl::owner` alias vs vendoring full GSL (recommend: alias).
4. Convert applications' cleanup with `unique_ptr` in the same commit as
   their CLI11 typed migration (recommend: yes — single reviewable
   behavioral unit per app).

---

## 8. Implementation results — G0, G1, G2 (2026-10-27)

All of G0–G2 from §7 landed on 2026-10-27, one commit per module, with the
regular and ASan+UBSan test suites green at each step.  The open decisions
above were resolved per the recommendations (dead files deleted; G-22
investigated with a reference test; `gsl::owner` alias; RAII piggybacked on
app migrations in later steps).

### G0 — conventions

| Commit | Scope |
| --- | --- |
| `d5193415` | `libs/libhrutil/Owner.h`: `gsl::owner` alias + ownership-convention documentation, included and installed with `libhrutil.h`. |

### G1 — defect fixes (§5)

| Commit | Findings |
| --- | --- |
| `56ba38fd` | **libscuff:** G-01 (destructor read `Mate` after `free`), G-09 (ThreadData `memcpy` → copy assignment), G-10 (FIPPI cache: per-miss key leak, entries/preload buffer never freed — the class now owns `PreloadedRecords` and frees values by range test), plus two ASan-exposed pre-existing leaks (~RWGSurface `MeshFileDir`, geometry-constructor scratch string).  New `Geometry` unit test constructs/destroys single-surface and mate-pair geometries; added to the sanitizer CI job. |
| `7d76b4cc` | **scuffSolver:** G-03 (`free` on `new`-allocated substrate), G-04 (incident-field cache type confusion → `dynamic_cast` + warning), G-05 (mate block-matrix double delete), G-26 (port-list leaks → `~RWGPortList`), plus `Solve(port,current)` leak + uninitialized ports. |
| `e5ac9b3c` | **libIncField:** G-02 (`free` on `new`-allocated LBasis/RLBasis), G-15 (null deref on empty incident-field file). |
| `ff9e62b1` | **libStaticSolver:** G-06 (struct-size allocation), G-14 (`ErrExit(NULL)`). |
| `24f6d958` | **libMatProp:** G-08 (uninitialized ownership flags in short constructors). |
| `8ed76ecd` | **libhrutil:** G-11 (signal handler closed stderr; now `dprintf` + closes only opened files), G-12 (PA_STRING repeat leak, freed exactly when a same-parse occurrence wrote the slot), G-13 (owning `CheckEnv(char**)` contract documented). |
| `5ce3a31f` | **apps:** G-07 (inverted free guard in scuff-rf panel-charge cache), G-16 (debug block removed from scuff-rf), G-17 (`--XiQuadrature CLIFF` missing else), G-18 (`!OmegaKBPoints==0` precedence), G-19 (scuff-analyze double geometry, `EEPs[0]` tested twice, dangling `if`). |
| `0773398f` | **mixed:** G-24 (12 unchecked LAPACK workspace `realloc` → `reallocEC`), G-25 (`vec2str` ring → `thread_local`), G-23 (`DestroyBZIArgs` added, called by scuff-cas3D/caspol/ldos), G-21 (wrong-variable loop increment), G-27 (corrupted license header). |
| `7a395f86` | **numerics-gated:** G-20 (r==0 `memset` missing `sizeof(cdouble)`), G-22 (`m%1` → `m%2` in scalar `GetYlm`).  Validated by a new `Spherical` unit test comparing `GetYlm` against an independently coded textbook reference and against `GetYlmDerivArray`; full Mie/Fresnel golden suite unaffected.  (During validation the first version of the test's own reference was wrong by a factor of `2m+1` — `(2m+1)!!` instead of `(2m-1)!!` in `P_m^m` — which briefly made the *correct* code look broken; the committed test is the corrected one.) |

### G2 — dead code and plain-C conversions

| Commit | Scope |
| --- | --- |
| `8ea5ee4e` | Deleted 16 dead/unbuilt files: `StaticSubstrate.*`, `libSubstrate/{Interpolation,FullWave_PlaneWave,qIntegrate,MosigMichalski}.cc`, `libMDInterp/{WriteMatrix*,doom}.cc`, `libIncField/tPointSource.cc`, `scuff-rf/{PointInsideObject,GetMinDistanceToPanel}.cc`, `libhrutil/PASnippet.cc`; removed the two vestigial `StaticSubstrate.h` includes.  Each deletion verified against the CMake source lists and a symbol-reference sweep first. |
| `db719209` | `libMDInterp`: `BinSearch.c`, `freadEC.c` converted to plain C (local `EqualFloat`/error stand-ins; exports `extern "C"` in `libMDInterp.h`). |
| `9b0c80a4` | `libTriInt`: `TCR.c`, `libTriInt.c`, `CCCubature.c`, `IntegrateCliffFunction.c` converted to plain C.  New C-compatible `libTriIntC.h` declares all C-linkage exports (including `GetCCRule`, whose accessor in the generated `CCRules.cc` now inherits C linkage via that header); `libTriInt.h` includes it and keeps only the C++-specific surface (default args, `GetLatticeSum`, `GetLebedevRule`, DCUTRI stubs).  BZIntegration/LatticeSum remain C++ by design (HMatrix in signatures). |

### Test status

Regular build (RelWithDebInfo, C++23): full CTest 8/8 — `Mie` (~124 s),
`Fresnel` (~33 s), `TMatrixCli`, `UnitTests`, `CliOptions`, `TypedCli`,
`Geometry` (new), `Spherical` (new).  ASan+UBSan build: all five fast
suites pass with leak detection enabled.

### Remaining from §7

G3 (ownership modernization: app RAII piggybacked on the Stage B CLI11
migrations starting with scuff-scatter, then the leaf-first libscuff/libhmat
sweeps) and G4 (clang-tidy ratchet) are open.
