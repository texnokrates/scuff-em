# Fixes for the qpms bug report (2026-11-08)

Source: `/home/mmn/repo/qpms/ai_notes/scuffem_bugreport.md` (issues
observed while wiring scuff-tmatrix into qpms; two genuine bugs, one
doc error with real consequences, plus robustness/doc notes). All
fixes on branch `oc`, one commit per report item, CTest 7/7 after
each code commit.

## Item → commit mapping

| Item | Commit | Fix |
|------|--------|-----|
| 1 (major) unit-dependent silent vertex welding | `863ee23d` | `[libscuff] Mesh parser: mesh-scale-relative vertex-weld tolerance + weld reporting` |
| 2 (doc) --Omega unit 3e14 vs 2.99792458e14 | `b4609834` | `[doc] --Omega unit: c/(1 um) = 2.99792458e14 rad/sec, not 3e14` |
| 3 (robustness) library ErrExit aborts host | `05c4b9af` | `[libhrutil] Make ErrExit overridable via SetErrExitHandler()` |
| 4 (minor) .TMatrix frequency column 6 digits | `57c1849a` | `[scuff-tmatrix] Write .TMatrix frequency column at full double precision` |
| 5 (minor/doc) single-row matprop abort + CONST_* undocumented | `2d173a06` | `[libMatProp] Accept single-row material tables as constants; document CONST_* builtins` |
| 6 (qpms-side claim correction) | — | no scuff-em change needed (already corrected in qpms) |

## Item 1 details (ParseGMSHFile welding)

- Weld tolerance is now `WELD_RELTOL`(1e-6) × bounding-box diagonal,
  i.e. relative to the mesh scale as written in the file. The old
  hard-coded absolute 1e-6 (mesh units) welded essentially all
  vertices of any mesh not written in µm (e.g. gmsh meshes in SI
  metres), which then failed downstream as a misleading
  "invalid mesh topology" error with the weld itself silent.
- Weld clusters bookkept by union-find (representative = lowest
  index, matching the old remap-j-to-lowest-i semantics); pairwise
  comparisons pruned by sorting nodes on x (only pairs with x-gap <
  tolerance are compared). Replaces the O(N²) scan + O(N) remap per
  weld. (A fully hash-based dedup remains possible but the sort
  pruning + union-find removes the pathological cost.)
- Reporting: any welds → `Log("Welded %i redundant vertices ...")`;
  >1% of vertices welded → loud `Warn("... coordinates are probably
  not in scuff-em's default length unit (microns)")`.
- Verified: gmsh torus mesh in metres now parses with correct
  area/volume (previously aborted); same mesh in µm unaffected;
  hand-made duplicate-node mesh welds with warning and parses.
  Unit-neutrality cross-checked µm vs m (areas differ by exactly
  1e12). Note the gmsh-format parser has a pre-existing quirk with
  `nTags==0` element lines (stale `nRead` in `bufPos` accumulation)
  — real gmsh output always carries tags, left alone, not in report.

## Item 2 details (docs)

22 files corrected: GeneralReference.md, FAQ.md, Materials.md,
scuff-tmatrix.md, scuff-scatter{,Options,Examples}.md, scuff-neq.md,
scuff-ldos/m, 6 example/test pages, plus LaTeX memos scuff-ldos,
scuff-neq, scuffSpherical (all three rebuilt OK; the
scuffSpherical dimensional-analysis result is unchanged at its
stated 2-digit precision). Worked examples rewritten in terms of
ω₀ or with exact values. Materials.md data-file example now says
``2.99792458e14``.

NOT touched (deliberate): `doc/docs/examples/SiO2Spheres/KruegerFormulas.jl`,
`Plotter.gp` and the NEQSpheres copies — their 3e14-based constants
follow the referenced Krüger-paper convention and their internal
constant consistency is loose anyway (mixed CODATA vintages);
`UnexplodedOrdnance.md`'s `\mho` typo (pre-existing, unrelated).

## Item 3 details (ErrExit handler)

- `SetErrExitHandler(ErrExitHandler, void *UserData)` in libhrutil;
  handler receives the formatted message, replaces the default
  print+exit. A handler that RETURNS is still followed by exit(1)
  (call sites assume no return — recovery is by unwinding out of
  the handler: longjmp or exception). nullptr restores default.
- Not thread-safe; install once at host startup (same discipline
  as the rest of libhrutil's static logging state).
- Verified with throwaway host program: default path unchanged
  (exit 1); exception-throwing handler recovers from both sample
  messages in the report; removal restores termination.
- The report's fuller suggestion (threading return codes through
  the material/mesh layers) remains open; the handler covers the
  "host must not die" requirement. `MatProp::GetEpsMu` on off-axis
  complex ω with FILE_ materials still errors (now catchable).

## Item 4 details (scuff-tmatrix)

Frequency column of text .TMatrix files now `%.15g` (z2s-style
complex handling) via local `OmegaStr()`; log messages, comment
lines and HDF5 filenames keep z2s. Verified in a real run: requested
0.3141592653589793 now written as `0.314159265358979` (was
`0.314159`). TMatrixCli CTest green. Note output files are opened
in append mode: pre-fix rows in existing files keep 6-digit
frequencies.

## Item 5 details (libMatProp)

- `ReadInterpolationTable()`: single-row data file → `MP_CONSTANT`
  material with the row's Eps/Mu (Mu defaults 1 with 2-column files).
  The type/values propagate through the matprop DB on repeat
  lookups (the FILE_ branch now copies Type/Eps/Mu from the DB
  entry — previously it copied only the interpolator pointers,
  which would have re-created a broken MP_INTERP-with-no-interpolators
  state for constants).
- Verified: constant ε/μ returned at real, imaginary AND complex ω
  (this also un-blocks arbitrary complex-ω evaluation for one-row
  FILE_ materials, previously only possible for parsed materials);
  repeat lookup constant; multi-row interpolation unchanged.
- Materials.md: new paragraph in section 2 documenting the full
  `CONST_*` family (EPS/MU/SIGMA combinations, real/complex values,
  the leading-underscore trap) and the single-row-as-constant rule
  in section 3. All documented spellings verified by execution
  (including `CONST_EPS_2.0_SIGMA_1e3` → `2+1000i` at ω=ω₀, and the
  `_CONST_...` error path, caught via the new ErrExit handler).

## Remaining / deferred

- Error-callback threading through material/mesh layers (item 3's
  fuller suggestion) — open, low priority now that ErrExit is
  overridable.
- `Interp1D`'s generic "grid must have 2 or more data points"
  abort still exists for non-matprop users of libMDInterp; the
  matprop-level case is fixed.
- Full hash-based vertex dedup (the sort pruning above already
  removes the pathological cases).
- The pre-existing `nTags==0` gmsh-element parsing quirk noted above.
