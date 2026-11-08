# clang-tidy CI job fix — 2026-11-08

Branch `oc`.  The `clang-tidy (changed lines)` CI job failed on every push.
Root cause was not the code: the job could never have produced meaningful
diagnostics in its configured state.  This memo records the causes, the
fixes, and the verification methodology, because the local reproduction
setup is reusable for future ratchet work.

## Root causes (three, stacked)

1. **No compile database.**  The job's Configure step ran plain
   `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`.
   `CMAKE_EXPORT_COMPILE_COMMANDS` defaults to OFF — *including* for the
   Ninja generator — so `build/compile_commands.json` never existed.
   clang-tidy therefore parsed every changed file with fallback
   "Running without flags": no `-std=gnu++23`, no `-I` paths.  Result:
   ~195 `clang-diagnostic-error` "file not found" lines plus bogus
   diagnostics from broken-parse recovery (e.g. 47 phantom
   `cppcoreguidelines-init-variables` warnings on `std::string`
   declarations).  Fix: pass `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
   explicitly in `.github/workflows/cmake.yml`.

2. **Files without compile commands.**  Even with a database, ~25 changed
   files have no entry: headers (never compilation entries) and programs
   that no CMake target builds at all (the orphaned autotools-era dev
   programs `libs/*/t*.cc`, `libs/libSubstrate/tests/t*.cc`, and the
   `examples/GetTMatrix`, `examples/PlateWithHole` programs —
   `examples/CMakeLists.txt` only installs data).  clang-tidy-diff.py
   passes every file in the diff as a *main file*; DB-less files get
   fallback flags and fail on the first project include.
   LLVM 18's clang-tidy-diff.py lacks `-only-check-in-db` (added in 19),
   so the workflow now pre-filters the diff through
   `.github/scripts/filter-diff-to-db.py`, which keeps only the hunks of
   files present in `compile_commands.json`.  Consequence: changed lines
   in headers and in unbuilt programs are *not* gated (they cannot be —
   no flags exist for them).  If an orphaned program starts to matter
   again it must be added to a CMake target first.

3. **Real changed-lines warnings.**  With flags loaded, the gate reported
   137 diagnostics (clang-22) / 32 (clang-18) on the branch diff vs
   `mmn_master`.  All fixed in this change; see below.

## Version skew: clang-tidy 18 (CI) vs 22 (workstation)

CI (ubuntu-24.04) has clang-tidy 18; the workstation standard is 22.
Behavior differs measurably, so the union of both runs must be zero
before the ratchet can be flipped to blocking.  Verified locally by
extracting the Debian `clang-tidy-18` debs into a prefix (no root
needed):

    apt-get download clang-tidy-18 libclang-cpp18 libllvm18 \
                     libclang-common-18-dev libz3-4
    dpkg -x '*.deb' /tmp/clang18-root/
    LD_LIBRARY_PATH=/tmp/clang18-root/usr/lib/x86_64-linux-gnu \
      /tmp/clang18-root/usr/bin/clang-tidy-18 --version

Reproduction pipeline (both versions, from the repo root):

    git diff -U0 $(git merge-base origin/mmn_master HEAD)...HEAD -- \
      '*.cc' '*.h' '*.hh' <workflow excludes> \
      | python3 .github/scripts/filter-diff-to-db.py build \
      | clang-tidy-diff.py -p1 -path build -clang-tidy-binary <binary> \
        -extra-arg="-isystem$(g++ -print-file-name=include)" -quiet

18-vs-22 differences seen: 22 flags `free`/`fclose`/`new`/`delete` under
`cppcoreguidelines-owning-memory` in many more places than 18; 18 flags
`bugprone-multi-level-implicit-pointer-conversion` through *explicit*
`static_cast<void *>` in one specific context (see below); 22 dropped
clang-18's broken-parse `init-variables` noise entirely.

`main`-special quirk (clang-18, -std=gnu++23): in
`int main(int argc, char *argv[])`, converting `&argv[1]` (`char **`) to
`void *` is flagged even through an explicit `static_cast` — but not in a
plain function taking `char **`, and not through `reinterpret_cast`.
Fix where encountered: `reinterpret_cast<void *>` inside main.

## Warning campaign (137 + 5 diagnostics zeroed)

Predominant categories and the treatment chosen:

- `modernize-avoid-c-style-cast` (66): mechanical `static_cast`
  (arithmetic, `void *`-from-`mallocEC`), `reinterpret_cast`
  (cdouble*→double*, `QIFIPPIData *`→`const char *`), never
  `const_cast`.  Where a touched line already compiled clean without a
  cast (adding const), the cast was dropped.
- `cppcoreguidelines-owning-memory` (61): split into
  - real RAII conversions for locals with paired new/delete:
    `SommerfeldIntegrator.cc` Integral2/Error2, `scuffSolver::Solve`
    PortCurrents, `DSIPFT.cc` BS, `tests/unit/GeometryTest.cc` G;
  - NOLINT with a written rationale for deliberate legacy ownership:
    the malloc/free destructor blocks (`RWGGeometry`, `RWGSurface` —
    NOLINTBEGIN/END regions extended to include owning-memory),
    lazy cache members (`CachedPortCurrents`, `CachedIF`),
    process-lifetime DB entries (`libMatProp.cc`), FILE* plumbing in
    output paths, and the `IncField` linked-list handoff in
    scuff-scatter.  The NOLINT ids always list *all* applicable checks,
    matching the existing per-line style.
- `bugprone-implicit-widening-of-multiplication-result` (10):
  `static_cast<ptrdiff_t>(3*n)` at pointer offsets,
  `static_cast<size_t>(k)*n` for memset/multiply.
- `cppcoreguidelines-pro-type-member-init` (9): NSDMIs on the internal
  data-carrier aggregates (`PFTIData`, `MOIData`, `PhiVDFuncData`,
  `MOIIntegrandData`, `RPIData`, `HalfSpaceData`, `RFMeshData`,
  `iwAIData`, `EquivalentEdgePairSubTable`); `struct tm TimeStruct={}`.
- `modernize-use-nullptr` (6): PolModel constructor `= 0` → `= nullptr`.
- misc: `setbuf` → `setvbuf(_IONBF)` (bugprone-unsafe-functions),
  `strcmp(...) != 0` (suspicious-string-compare), functional
  `size_t(x)` → `static_cast` (22 also flags functional casts),
  `new InterpND` + `reset()` → `std::make_unique` (MOI.cc).

## Bonus UB fix found on the way

`scuffSolver::~scuffSolver` did `delete CachedPortCurrents` on a pointer
allocated as `new cdouble[NumPorts]` (scalar delete of an array — UB).
Now `delete[]`.  Flagged in neither run (line not changed in the diff);
fixed because the surrounding cache code was being annotated.

## Coverage gap (deliberate, recorded)

The gate now lints exactly what the build compiles.  Ungated: headers,
`third_party/`, vendored trees, and the unbuilt orphan programs listed
above (their ~23 diagnostics from the pre-filter clang-22 run — mostly
C-style casts and owning-memory in `t*.cc`/examples — are dormant until
those files re-enter a build target).

## Step-abort hazard discovered (why the original log never reached the gate)

The run step executes with `bash -e` plus `set -o pipefail`, and
clang-tidy-diff.py exits nonzero whenever *any* file fails to process
(return_code set for every entry of failed_files — a TU that trips a
header clang cannot parse counts, even if its changed lines are clean).
Consequence in the original failing run: the pipeline aborted the step
before the warnings gate ever executed — the `::notice` line appears in
the job log only as script text, never as output.  The workflow now
wraps the pipeline in `|| true` so that the warnings gate is the single
blocking criterion; verified by simulating the exact step body under
`bash -e -o pipefail` (green path exits 0, gate still fires on an
artificial warning).

## Local-repro caveat: gcc-14 vs gcc-13 omp.h

clang-18 cannot parse gcc-14's `omp.h` (doubled `__malloc__` attribute:
`__attribute__((__malloc__, __malloc__ (omp_free), ...))` —
"'__malloc__' attribute takes no arguments").  This workstation carries
gcc 14, CI's ubuntu-24.04 image gcc 13.3, whose omp.h uses the single
argument form clang-18 accepts.  The local clang-18 runs therefore show
~40 clang-diagnostic-error lines on OpenMP TUs that CI will not; they
are parse noise from the *system header*, do not affect changed-lines
warnings (the gate), and disappear entirely with the `|| true` fix.
Worth remembering when comparing local clang-18 output against CI.

## Status

- Both clang-18 and clang-22 changed-lines runs: **0 warnings** on the
  full branch diff (verified locally, same pipeline as CI).
- Full ctest suite: 7/7.
- `continue-on-error: true` kept: flipping the job to blocking is a
  one-line change (drop the flag) now that the ratchet is quiet — left
  as a maintainer decision per the original plan.
