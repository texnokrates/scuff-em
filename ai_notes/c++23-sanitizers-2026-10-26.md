# C++23 sanitizer and warning cleanup — 2026-10-26

Status: **implemented and validated**.  This note records the first
ASan/UBSan pass over the codebase and the removal of the remaining
`-Wformat-truncation` diagnostics listed in the Phase 1 note.  It follows
`ai_notes/c++23-cli11-adapter-2026-10-26.md`.

Commits:

| Commit | Scope |
| --- | --- |
| `e670858f` | Fixed UBSan-detected misaligned access and signed overflow in libscuff. |
| `2ccff48f` | Removed the remaining format-truncation warnings. |
| `c73715f8` | Added the ASan/UBSan CI job. |

## Sanitizer findings

The sanitizer build is enabled with `-DSCUFF_ENABLE_SANITIZERS=ON`
(AddressSanitizer + UndefinedBehaviorSanitizer).  Building and running the
unit tests surfaced no defects.  Running the `examples/SphericalShell`
`scuff-scatter` smoke test surfaced two real undefined-behavior bugs:

### Misaligned element swap in the kd-tree

`libs/libscuff/PointInObject.cc` partitioned `boxtri` arrays with a `swap()`
helper that treated arbitrary byte ranges as arrays of `long`.  A `boxtri`
is 52 bytes, so successive elements alternate between 4- and 8-byte
alignment; the `long` loads were therefore misaligned.  UBSan reported:

```text
PointInObject.cc:131: load of misaligned address ... for type 'long int',
which requires 8 byte alignment
```

The helper now copies through local `uint64_t` values with `memcpy`, which
is alignment-safe, and handles the trailing bytes separately.  The swap is
still a single pass over the element.

### Signed overflow in the Jenkins hash

`libs/libscuff/FIBBICache.cc` mixed the Jenkins hash in a signed `long`.
The mixing steps intentionally overflow the word, so the arithmetic was
undefined.  UBSan reported the overflow while building the FIBBI cache.
The accumulator is now `unsigned long`, and the returned `long` is the same
bit pattern for the hash-table users.  A duplicate `return` in
`EquivalentEdgePairs.cc` was removed at the same time.

## Truncation warnings

The remaining `-Wformat-truncation` warnings from Phase 1 are gone:

- `libs/libhmat/C2ML.cc`: suffix buffers now leave room for `.m`, `.hdf5`,
  `real_`, and `imag_` after a maximum-length base name.
- `libs/libMatProp/UserDefinedMaterials.cc`: the
  `SCUFF_MATPROP_PATH` lookup builds its candidate with `std::string`.
- `libs/libStaticSolver/PostProcessing.cc`: field-visualization and charge
  density filenames are composed with `std::string`.  This also fixes the
  single-transformation path, which previously truncated names to 100 bytes
  while the multi-transformation path used the full buffer.
- `applications/scuff-analyze/scuff-analyze.cc`: the displaced-mesh tag
  buffer now has room for two full integers.
- `applications/scuff-spectrum/scuff-spectrum.cc`: Bloch/contour labels and
  mode output names have realistic bounds.
- `libs/libscuff/FIBBICache.cc`: `strncpy` into a `MAXSTR` buffer was
  replaced with a bounded, always-terminated `snprintf`.

## CI job

`.github/workflows/cmake.yml` now has a `sanitizers` job that:

1. configures a `RelWithDebInfo` build with `SCUFF_ENABLE_SANITIZERS=ON`;
2. builds `scuff-unit-tests`, `scuff-cli-tests`, and `scuff-scatter`;
3. runs `UnitTests` and `CliOptions` with leak detection enabled;
4. runs a one-frequency SphericalShell smoke test with a point source,
   negative multi-value coordinates, and an evaluation-point file.

Leak detection is disabled only for the application smoke test.  The
application currently leaks about 74 MB at exit through long-lived geometry,
material, and cache objects (72,056 allocations in the local run).  That is a
pre-existing ownership backlog, not a new parser defect; cleaning it up is a
separate task.  The unit tests run with leak detection enabled and are clean.

## Verification

- Regular build: succeeds with no new warnings in the touched files; full
  CTest passes (`Mie`, `Fresnel`, `UnitTests`, `CliOptions`).
- Sanitizer build: `UnitTests` and `CliOptions` pass under ASan + UBSan with
  leak detection enabled.
- Sanitizer smoke test: SphericalShell `scuff-scatter` run completes with
  status 0 under ASan + UBSan after the two fixes above.

## Follow-up

- The application-library ownership cleanup (the 74 MB exit leak) is a
  candidate for a dedicated RAII pass; it should not block CLI migration.
- A TSan job remains future work, as does the warning-budget ratchet.  The
  truncation class is now clear, which removes one obstacle to turning on a
  warning budget for maintained sources.
