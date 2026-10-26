# C++23 modernization and CLI11 migration — 2026-10-26

Status: **planning note; implementation is recorded in the linked notes**.
This is a new workstream, separate from the completed CMake/Autotools
build-system migration.  Phase 1 implementation results are recorded in
`ai_notes/c++23-phase1-2026-10-26.md`; Stage A (the CLI11-backed
`ProcessOptions` adapter) is recorded in
`ai_notes/c++23-cli11-adapter-2026-10-26.md`; the ASan/UBSan pass and
warning cleanup are recorded in
`ai_notes/c++23-sanitizers-2026-10-26.md`; Stage B (typed CLI11 helpers and
the `scuff-tmatrix` migration) is recorded in
`ai_notes/c++23-typed-cli-2026-10-26.md`.

Baseline: commit `26e6ea22` (`[build] Remove Autotools in favor of CMake`).
The default CMake build is C++23 (`SCUFF_CXX_STANDARD=23`), builds cleanly,
and passes the Mie and Fresnel CTest suites.  The legacy warning backlog and
the hand-written command-line parser are the starting point for this plan.

The immediate goal is not a wholesale rewrite of 1.7 million lines.  It is to

1. remove the memory-safety and correctness defects that are already visible,
2. replace the libhrutil option parser with CLI11, preserving the documented
   command-line contract while adding a typed, testable interface,
3. modernize the C++ at the boundaries (parsing, strings, ownership,
   containers, diagnostics) without destabilizing the numerical kernels,
4. shrink the warning backlog to the point where CI can enforce it.

---

## 1. Scope and non-goals

### In scope

- `ProcessOptions` / `OptStruct` replacement with CLI11.
- `ProcessArguments` / `ArgStruct` retirement after its call sites are
  migrated.
- C-style string handling in maintained C++ sources: fixed buffers,
  `strcpy`/`strncpy`/`strcat`/`sprintf`, `sscanf`, static return buffers,
  unbounded varargs, path manipulation, and format-string validation.
- Concrete memory-safety, out-of-bounds, use-after-free, uninitialized-value,
  and wrong-result defects found during the survey.
- C++23 language/library cleanup in hand-written code: namespaces, TR1
  conditionals, `memset` on non-trivial types, raw ownership, `enum class`,
  `constexpr`, `std::string`/`std::string_view`, `std::vector`/`std::array`/
  `std::span`, `std::optional`/`std::expected`, `std::filesystem`,
  `std::format` (where supported by the CI compiler).
- Test and CI infrastructure needed to keep these changes safe.

### Out of scope / explicitly excluded

- The completed CMake/Autotools migration.  CMake remains the build system.
- SWIG/Python bindings (already removed and obsolete).
- Numerical algorithm changes in BEM, Ewald, Sommerfeld, interpolation, or
  quadrature code.  Modernization must not change results.
- Rewriting generated data tables (`clencurt.h`, `LebedevRules.cc`,
  `CCRules.cc`, `TValues.cc`, `G1G2Values.cc`, etc.).
- Hand-modernizing f2c-generated `zbsubs.c`, `machcon.c`, `drc3jm.c`, or the
  flex/bison-generated cmatheval parser/scanner.  These get a documented
  warning policy and only targeted correctness fixes.
- The dormant `scuff-cas2D` and `scuff-microstrip` trees.  They are not built
  and should not drive the schedule.  They can be fixed or deleted later.
- ABI stability guarantees.  The shared library SONAME is unchanged during
  this work, but source-level public headers will change (notably the removal
  of `using namespace std` from `libhrutil.h`).

---

## 2. Baseline measurements

From the post-Autotools clean build log (`/tmp/scuff-postremoval-build.log`)
and a survey of the tree:

| Measurement | Value |
|---|---:|
| Hand-written/generated C/C++ files under `libs/`, `applications/`, `tests/` | 230 `.cc`, 48 `.h`, 1 `.hh`, 14 `.c` |
| Total lines in those files | ~1.74 million (dominated by generated tables) |
| Build steps | 166 |
| Compiler warnings | 442 |
| `ProcessOptions` external call sites | 33 |
| `ProcessArguments` external call sites | 13 |
| Largest option array | 40 entries (`scuff-scatter`) |
| `std::string` uses | 0 |
| `std::unique_ptr` / `std::shared_ptr` uses | 0 / 0 |
| `std::array` / `std::optional` / `std::filesystem` uses | 0 / 0 / 0 |
| `std::vector` / `vector<` uses | 27 / 42 |
| `memset` calls | 259 |
| `memset` calls on `cdouble` arrays | ~97 |
| `strncpy` / `strcpy` / `strcat` / `sprintf` calls | 44 / 10 / 20 / 32 |
| `snprintf` / `sscanf` calls | 175 / 158 |
| Raw `new` / `delete` / `malloc` / `free` textual occurrences | 713 / 229 / 74 / 194 |

Warning breakdown:

| Warning | Count | Notes |
|---|---:|---|
| `-Wredundant-decls` | 224 | almost all in generated `zbsubs.c` |
| `-Wclass-memaccess` | 87 | mostly `memset` on `std::complex<double>` |
| `-Wstringop-truncation` | 21 | `strncpy` with no guaranteed NUL |
| `-Wregister` | 20 | generated flex scanner |
| `-Wformat-truncation=` | 17 | fixed-size filename buffers |
| `-Wmaybe-uninitialized` | 14 | mostly generated Fortran translation |
| `-Wwrite-strings` | 11 | cmatheval string literals passed as `char *` |
| `-Wunused-parameter` | 11 | |
| `-Wunused-variable` | 5 | |
| `-Wmissing-braces` | 4 | |
| `-Wuninitialized` | 3 | real: InterpND `LogLevel` |
| `-Wreturn-type` | 3 | real: cmatheval |
| `-Wrestrict` | 3 | real: overlapping `snprintf` |
| `-Wimplicit-fallthrough=` | 3 | cmatheval |
| `-Wsign-compare` | 2 | |
| `-Wmisleading-indentation` | 2 | |
| `-Warray-bounds=` | 2 | one real: `scuff-integrate` |
| `-Waddress` | 2 | always-true array checks |
| `-Wabsolute-value` | 2 | `abs` on `double` in generated code |
| `-Wuse-after-free` | 1 | real: SommerfeldIntegrator |
| `-Wshadow`, `-Wparentheses`, `-Wmissing-field-initializers`, `-Wdangling-else`, `-Wunused-function` | 1 each | |

These totals are a baseline, not a target.  The generated-code warnings
should be removed from the maintained-code budget first; the remaining
hand-written warnings are then small enough to fix and enforce.

---

## 3. Guiding principles

1. **Correctness before style.**  Fix memory-safety and wrong-result bugs
   first, each with a regression test or sanitizer repro.  Do not mix a bug
   fix with a large rename or formatting change.
2. **Compatibility first for the CLI.**  Existing scripts under `examples/`
   and `tests/` use mixed-case options (`--Omega`, `--omega`), option files
   piped on stdin, repeated options, and multi-argument coordinate options.
   The CLI11 migration must preserve these before adding new syntax.
3. **Adapter first, typed migration second.**  Replacing the parser under
   `OptStruct` immediately gives every application CLI11's diagnostics,
   validation, and help without rewriting 33 call sites at once.  Then migrate
   applications to typed CLI11 declarations one at a time.
4. **Keep numerics untouched.**  Modernize parsing, strings, ownership, and
   diagnostics at the edges.  Numerical kernels remain byte-for-byte
   equivalent unless a specific bug fix is intended.
5. **Do not hand-edit generated code.**  Suppress warnings per source or
   regenerate; only fix true UB (for example, a missing return) when the
   generated file is effectively vendored and cannot be regenerated.
6. **Small, reviewable commits.**  One subsystem or one cross-cutting
   mechanical change per commit.  The build and CTest suite should stay green
   at each step.

---

## 4. CLI11 migration

### 4.1 Why CLI11

`libhrutil` currently implements its own parser in `ProcessOptions.cc` and
`ProcessArguments.cc`.  The API is a `void *`-based descriptor array
(`OptStruct`/`ArgStruct`) plus a hand-written tokenizer.  It has no format
string validation, no typed conversions, no help generation, and several
confirmed defects (Section 5).  CLI11 provides:

- typed `add_option` / `add_flag` with standard-library and user-defined
  conversions, including `std::complex<double>` (one or two values, `i`/`j`);
- `->expected(N)` and `->expected(Nmin, Nmax)` for multi-value options;
- `->take_last()`, `->take_all()`, and multi-option policies for repeated
  options;
- `->ignore_case()` at application or option level, which matches the
  existing `StrCaseCmp` behavior;
- validators, required/needs/excludes relationships, and better diagnostics;
- generated `--help`, `--version` (if enabled), and config-file support;
- a header-only, C++11-compatible implementation with a permissive license.

CLI11 is a good fit because it can reproduce the legacy contract through an
adapter and then support a cleaner typed API without a second parser.

### 4.2 Version and acquisition

**Recommendation: vendor the single-header release of CLI11 2.7.2.**

- Upstream: <https://github.com/CLIUtils/CLI11>
- Release asset: `CLI11.hpp` from tag `v2.7.2` (released 2026-08-02).
- License: BSD-3-Clause; compatible with GPL-2.0-or-later.  Retain the
  copyright notice and license text.
- Vendor under `third_party/CLI11/`:

  ```text
  third_party/CLI11/CLI11.hpp
  third_party/CLI11/LICENSE
  third_party/CLI11/README.md   # version, URL, SHA256, update procedure
  ```

- Add a CMake option `SCUFF_USE_SYSTEM_CLI11` (default `OFF`).  When `ON`,
  use `find_package(CLI11 2.4 CONFIG REQUIRED)` and link `CLI11::CLI11`.
  When `OFF`, create an interface target pointing at the vendored header.

Why vendoring rather than `FetchContent`:

- builds are reproducible and work offline;
- no network access or download step at configure time;
- no surprise version changes between builds;
- the single header is small and easy to audit.

Packagers who prefer the distribution's CLI11 can enable
`SCUFF_USE_SYSTEM_CLI11=ON`.  The minimum accepted system version should be
the oldest version that provides the APIs used by the adapter; 2.4 is a
reasonable floor, but the vendored 2.7.2 is the reference.

### 4.3 CMake integration

Add to `cmake/ScuffOptions.cmake`:

```cmake
option(SCUFF_USE_SYSTEM_CLI11 "Use a system CLI11 instead of the vendored header" OFF)
```

Add to `cmake/ScuffDependencies.cmake` (or a small `ScuffCLI11.cmake`):

```cmake
add_library(scuff_cli11 INTERFACE)
if(SCUFF_USE_SYSTEM_CLI11)
  find_package(CLI11 2.4 CONFIG REQUIRED)
  target_link_libraries(scuff_cli11 INTERFACE CLI11::CLI11)
else()
  target_include_directories(scuff_cli11 INTERFACE
    "${PROJECT_SOURCE_DIR}/third_party/CLI11")
endif()
```

Link it **privately**:

```cmake
target_link_libraries(scuff_hrutil PRIVATE scuff_cli11)
```

For applications that include CLI11 directly during the typed migration, add
`scuff_cli11` to their private link libraries (or to the helper used by
`scuff_add_application`).  Do not make it part of the installed `scuff`
target's public interface: no installed public header should include CLI11.
Add `third_party/CLI11/LICENSE` to the install documentation tree (for
example `share/doc/scuff-em/`) so binary redistributors retain the notice.

### 4.4 Compatibility contract

The first replacement of `ProcessOptions` must preserve the following.  Each
item gets an explicit test.

| Behavior | Legacy behavior | CLI11 strategy |
|---|---|---|
| Option spelling | `--Name`, case-insensitive (`StrCaseCmp`) | `app.option_defaults()->ignore_case()`; keep canonical spellings |
| Option names in stdin files | `Name arg...` (no `--`) | accept `Name` and `--Name`; normalize to `--Name` |
| Option arguments | all following tokens until next `--` token | `->expected(N)` / `->take_all()`; pre-scan known arities if needed |
| Multi-value options | `NumArgs` values per occurrence | `->expected(N)` with `type_size(N)`; adapter groups flat results in chunks of `N` |
| Repeated options | `MaxInstances` semantics: last wins with warning for 1, error beyond max | `take_last` or adapter-level count + warning/error; preserve both |
| Boolean flags | `--Flag` only; extra value is an error | `add_flag`; an extra value is an unexpected argument |
| Negative numbers | accepted because only `--` starts a new option | CLI11 recognizes `-1`, `-.5`, etc. as values; add a test for `-inf`/`-nan` |
| Single-dash values | accepted as option arguments | test; if CLI11 rejects a needed value form, pre-normalize known options to `--opt=value` |
| `--` terminator | treated as an unknown option | decide explicitly; likely reject for compatibility or document the change |
| Unknown option | print usage and exit 1 | catch `CLI::ParseError`, call `OSUsage` (exit 1) during adapter phase |
| Missing/extra args | usage error | CLI11 error, mapped to the same exit status |
| Stdin ordering | stdin options first, then argv | build token vector in that order; last-wins behavior is preserved |
| `AbortOnUnknown=false`, `ZeroArgs=true` | supported but unused in-tree | not required for adapter; remove after downstream check |
| `ProcessOptions(const char *)` | broken and unused | remove or reimplement on top of the token-vector parser |
| `ProcessArguments` | older one-argument parser, no stdin/repeats | migrate call sites to typed CLI11; do not preserve long term |
| Help | none; usage printed only on error | add `--help` (and optionally `--version`); keep usage on errors |

Important compatibility details:

- **Case:** the examples use lowercase (`--omega`, `--transfile`, `--capfile`,
  `--datacolumn`) while the option arrays use camel case.  This works today
  only because of `StrCaseCmp`.  `ignore_case()` is mandatory.
- **Stdin:** `examples/SphericalShell/Args` and `examples/SolidSphere/Args*`
  contain lines such as `omega 0.001` and `pwDirection 1.0 0.0 0.0`.  These
  are not CLI11 TOML config files.  Read and translate them, or implement a
  custom CLI11 config formatter; do not point `set_config()` at them.
- **Negative coordinates:** a `--psLocation -1.0 -2.0 0.5` invocation must
  continue to work.  CLI11's numeric short-option detection handles this,
  but it needs an explicit test.
- **Single-dash filenames:** a geometry file called `-mesh.msh` is unusual,
  but the legacy parser accepts it as an argument.  If CLI11 rejects it,
  either document the change or pre-normalize known options to `--opt=value`.
- **Partial numeric strings:** the legacy parser uses `sscanf("%le")`, so
  `1foo` is accepted as `1`.  The modernized parser should reject trailing
  garbage.  This is a deliberate tightening; add a release note and tests.
- **Complex syntax:** `S2CD` accepts `3+4i`, `3+4j`, `3-4I`, `3i`, and
  two-argument forms via repeated options.  CLI11's complex converter is
  close but must be tested for uppercase `I`/`J` and the two-token form.

### 4.5 Architecture

#### Stage A — CLI11-backed legacy adapter

Keep `OptStruct` and the `ProcessOptions` entry point for all existing
applications.  Replace the implementation with a parser that:

1. resets every `NumInstances` counter;
2. reads stdin option lines (if stdin is not a tty), translates each line to
   `--Name arg...`, and appends the tokens to a `std::vector<std::string>`
   before the command-line tokens;
3. builds a `CLI::App` with one option per `OptStruct` entry:
   - `PA_BOOL` -> `add_flag`;
   - everything else -> `add_option("--Name", std::vector<std::string>)`
     with `->expected(NumArgs)`, `->take_all()`,
     `->allow_extra_args(false)`, and `->type_size(NumArgs)`;
4. parses with CLI11, catching `CLI::ParseError` and mapping it to
   `OSUsage(...)` so exit status and the existing usage text remain stable;
5. walks the parsed results in declaration order, groups each option's flat
   values into `NumArgs` chunks, enforces `MaxInstances`, and stores into the
   legacy `void *` storage using the existing conversions (`S2CD`, numeric
   parsing, `strdupEC`), with all of the fixes from Section 5 applied;
6. maintains an internal `ParseOptions` function that returns an error
   structure instead of calling `exit()`, so it can be unit tested.  The
   public `ProcessOptions` is a thin wrapper that calls `OSUsage` on error.

Sketch:

```cpp
struct ParsedOption {
  OptStruct *OS;
  std::vector<std::string> Values;
};

ParseResult ParseOptions(const std::vector<std::string> &Tokens,
                         OptStruct *OSArray);
void ProcessOptions(int argc, char *argv[], OptStruct *OSArray,
                    bool AbortOnUnknown = true, bool ZeroArgs = false);
```

Do not keep a second hand-written parser in the build.  Git history is the
reference for the old implementation.

#### Stage B — typed per-application migration

For each application, replace the `OptStruct` array with typed declarations:

```cpp
CLI::App App{"..."};
App.option_defaults()->ignore_case();

std::string GeoFile;
App.add_option("--geometry", GeoFile, "geometry file")
   ->required()
   ->take_last();
std::vector<std::complex<double>> OmegaVals;
App.add_option("--Omega", OmegaVals, "angular frequency")
   ->expected(1)
   ->take_all();
std::vector<double> pwDir;
App.add_option("--pwDirection", pwDir, "plane wave direction")
   ->expected(3)
   ->take_all();
bool DSIFarField = false;
App.add_flag("--DSIFarField", DSIFarField, "retain only far-field contributions");

if (auto Status=scuff::cli::Parse(App,argc,argv))
 return *Status;
```

Use `std::optional` for values that must distinguish "not supplied" from a
default, `std::vector` for repeated options, and `std::string` for paths.
Keep the old option spellings and aliases (`mesh`/`meshfile`, `omega`/
`Omega`) exactly as they are.  Remove fixed `MAXFREQ`/`MAXPW`/`MAXPS` arrays
only when the surrounding code can use the vectors; do not change numerical
interfaces unnecessarily.

`scuff-tmatrix` is the first migration: its small option set validated the
shared helper, the stdin/argv ordering, and the `S2CD`-based complex option
support.  `scuff-scatter` follows: it has the largest option array (40
entries), the most complex repeated/multi-value options, and is exercised by
the SphericalShell/SolidSphere examples.  That migration will stress every
part of the compatibility contract and must add a multi-value complex helper
before the `pwPolarization`, `gbPolarization`, and `psStrength` options can
move.

#### Stage C — remove legacy APIs

After all maintained applications, tests, and developer programs are
migrated:

- delete `OptStruct`, `ProcessOptions`, `OSUsage`, `AppendOSUsageMessage`;
- delete `ArgStruct`, `ProcessArguments`, `ASUsage`;
- remove `ProcessOptions.cc` and `ProcessArguments.cc` from
  `libs/libhrutil` and the aggregate library;
- remove any now-unused `PA_*` constants or replace them with a typed
  `enum class` in the new API;
- keep `libhrutil.h` free of CLI11 includes so downstream users do not need
  CLI11 headers.

### 4.6 Stdin option files

The current reader is intentionally simple:

```text
# comment
geometry       SphericalShell.scuffgeo
omega          0.001
pwDirection    1.0 0.0 0.0
```

It reads line by line with `fgets`, tokenizes on whitespace, ignores blank
lines and lines whose first token starts with `#`, and treats the first token
as the option name without a leading `--`.  Errors are reported as
`stdin:<line>: <message>`.

Preserve this format.  Implementation options:

- **Recommended:** read the lines in the adapter, translate to a token vector,
  and parse the combined vector with CLI11.  Keep a parallel vector of source
  locations so errors can still say `stdin:<line>:`.
- Optional later: add a `--config` option using CLI11's native TOML/INI
  support.  Do not replace the legacy `Args` format with TOML; existing
  examples depend on it.

Quoting is not supported today and need not be added.  Document that each
line is one option followed by whitespace-separated arguments.

---

## 5. Confirmed defects and warning-derived findings

Severity key:

- **Critical** — memory corruption or undefined behavior on a plausible,
  built code path.
- **High** — wrong results, crash on an error path, or silent acceptance of
  invalid input.
- **Medium** — incorrect diagnostics, leak, thread-safety issue, or edge case.
- **Low** — latent/dormant code, style-adjacent warning, or usability issue.

### 5.1 Critical

| ID | Location | Finding | Fix / test |
|---|---|---|---|
| C-01 | `applications/scuff-neq/scuff-integrate.cc:398-402` | `ivec RowStarts(1,0)` creates a vector of size 1, then `RowStarts[NumParmValues++] = nr` writes index 1 on the first parameter change.  This is a heap out-of-bounds write; GCC reports `-Warray-bounds`. | Start with `RowStarts.push_back(0)`, use `push_back(nr)` for each change, then `push_back(DataMatrix->NR)`.  Add a test with a multi-parameter data file. |
| C-02 | `libs/libSubstrate/SommerfeldIntegrator.cc:255-258` | `Integral2` is deleted, then `Integral2[0]` is passed to `CD2S()` in the verbose log.  Use-after-free. | Copy the value before `delete[]`, or log `Integral[0]`/the pre-delete value.  Add an ASan test that exercises the `MaxEvalB>0` branch with verbose logging. |
| C-03 | `libs/libStaticSolver/PostProcessing.cc:291-295` | `snprintf(FileName, MAXSTR, "%s...", FileName, ...)` uses the destination as a source argument.  Overlapping objects are undefined behavior (`-Wrestrict`).  The same pattern appears at 291, 293, 295. | Build the new name in a separate buffer (`NewFileName`) or a `std::string`, then replace `FileName`; check truncation.  Test `--SeparateOutputFiles` with transform/excitation labels. |
| C-04 | `libs/libMatProp/cmatheval/node.c:162,324,1352` | Non-void functions can reach the closing brace without returning a value (`-Wreturn-type`).  This is UB when the path is reached. | Add the missing returns / `abort()` fallback; add a cmatheval unit test that exercises the affected node paths.  Treat this file as vendored; keep the patch minimal. |
| C-05 | `libs/libMDInterp/Interp1D.cc:530,596`; `Interp2D.cc:488,538`; `Interp3D.cc:516,570`; `Interp4D.cc:558,615` | `ErrExit("could not open file %s");` omits the `FileName` argument.  `ErrExit` reads a nonexistent vararg, so the error path is undefined behavior. | Add `, FileName`; add a test that opens a nonexistent interpolation file and checks the message. |
| C-06 | `libs/libhrutil/ProcessOptions.cc:269` + `libs/libhrutil/libhrutil.cc:228-235` | `Tokenize(char *, char **, int)` writes at most `MaxTokens` pointers but returns the full token count.  The stdin path then passes `NumTokens-1` as the argument count, so a line with more than `MAXTOK` tokens makes `ProcessOption` index past `Tokens[]`. | Change the contract (return stored count plus a truncation flag, or fail loudly), and make all callers check capacity.  Add a test with a >50-token option line. |
| C-07 | `libs/libhrutil/libhrutil.cc:279-284` | `fopenPath` can write `FullFileName[n++] = '/'` when `n == MAXSTR`, one past the end, and can leave `FullFileName` unterminated when the path component fills the buffer. | Bounds-check each component; on overflow, report an error or truncate safely.  Unit-test a 1000-character path component and a long filename. |
| C-08 | `libs/libhrutil/libhrutil.cc:733-764` | `CD2S` uses `snprintf(zStr, MAXSTR, ...)` while `zStr` points into `static char Strings[100][50]`; the bound is 1000 for a 50-byte object.  `strncpy(zStr, format, STRLEN)` can also leave the result unterminated. | Use `sizeof` of the actual buffer, guarantee termination, and keep a per-thread ring buffer.  Add tests with long formats and large values. |
| C-09 | `libs/libhrutil/libhrutil.cc:93-132` | `RemoveDirectories`, `RemoveExtension`, and `GetFileExtension` use `strncpy(buffer, ..., MAXSTR/999)` without guaranteeing a trailing NUL when the source fills the buffer.  Downstream `printf("%s")` can overread. | Use `snprintf` or copy with explicit termination.  Change the parameters to `const char *`.  Add tests with exactly `MAXSTR-1`/`MAXSTR` characters. |

### 5.2 High

| ID | Location | Finding | Fix / test |
|---|---|---|---|
| H-01 | `libs/libhrutil/libhrutil.cc:706-725` | `S2CD` checks `tolower(c1)=='j'` in both `nConv==4` branches where `tolower(c2)` is intended.  Values such as `1+2j` are rejected (or fall through), while the `i` form works. | Fix to `tolower(c2)`; add a table-driven test for `3+4i`, `3+4j`, `3-4I`, `3-4J`, `3i`, `3j`, real values, and malformed inputs. |
| H-02 | `applications/scuff-ldos/scuff-ldos.cc:97` | `--Lambda` stores its values in `OmegaVals`, the same array used by `--Omega`.  Both counters start at zero, so a command using both options overwrites entries and `GetOmegaList` receives aliased arrays. | Add a separate `LambdaVals[MAXFREQ]` and pass it to `GetOmegaList`.  Add a test that mixes `--Omega` and `--Lambda`. |
| H-03 | `applications/scuff-scatter/scuff-scatter.cc:227-233` | `IncFieldList *IFList=0; if (IFList!=0 && IF!=0)` is always false.  The intended incompatibility check between `--IFFile` and other incident-field sources never fires, so `--IFFile` together with `--pwDirection` etc. silently ignores one source. | Check `IFFile != 0 && IF != 0` (or restructure); test both orders and both source types. |
| H-04 | `libs/libMDInterp/Interp1D.cc:523-525`, `Interp2D.cc:481-483`, `Interp3D.cc:509-511` | File-based constructors read `LogLevel` before assigning it.  `LogLevel` is an uninitialized member (`-Wuninitialized`); the object's behavior depends on stack garbage. | Give `LogLevel` an in-class default (`= LMDI_LOGLEVEL_TERSE`) or set it first in each constructor.  Do the same for any other members used before assignment. |
| H-05 | `libs/libhrutil/ProcessOptions.cc:336-341` | `ProcessOptions(const char *ArgString, ...)` tokenizes into `Tokens[0..N-1]` and calls the `argc/argv` overload, which skips `argv[0]`.  The first option is dropped; a one-token string is ignored. | Reimplement on the token-vector parser with a synthetic program name, or delete the overload (it is unused in-tree). |
| H-06 | 28 `ErrExit` calls with format specifiers but no arguments; 17 use `"%s:%i:"` without `__FILE__,__LINE__` | Examples: `GaussianBeam.cc:81,83,85`, `IncField.cc:141`, `LBWrappers.cc:1001`, `AssembleBEMMatrix.cc:358,693,695`, `AssembleBEMMatrix2018.cc:220,222`, `ExpandCurrentDistribution.cc:102`, `SurfaceSurfaceInteractions.cc:661`, `scuffSolver.cc:161,366-368`, `InterpND` file constructors.  `ErrExit` is not declared with a printf format attribute, so the compiler cannot catch these. | Add `__attribute__((format(printf, 1, 2)))` (portable macro) to `ErrExit`, `Warn`, `Log`, `LogC`, `LogThread`, `MutexLog`, `vstrdup`, `vstrappend`, and similar wrappers; fix the resulting diagnostics.  This is both a bug fix and a permanent guard. |
| H-07 | `libs/libhrutil/libhrutil.cc:787-788` | `z2s(cdouble, char *zStr)` does an unbounded `strcpy` into a caller-supplied buffer.  Any caller that underestimates the required size gets a stack/heap overflow. | Add a size-taking overload and deprecate the old one, or return a `std::string`/thread-local buffer.  Audit callers. |
| H-08 | `libs/libhrutil/libhrutil.cc:776-780`, `835`, `877-879`, `854-860` | `SetDefaultCD2SFormat` uses `strncpy(..., 99)` without guaranteed termination; `CreateUniqueFile` copies into the caller's `FileName` with `MAXSTR-1` and no guaranteed NUL; `SetCodeMarker` uses `strncpy(..., MAXSTR)` with no NUL; `GetHostName` calls `gethostname` without forcing termination. | Use `snprintf`/explicit termination; use a fixed `std::array` and `size-1`.  Add boundary tests. |
| H-09 | `libs/libhrutil/ProcessOptions.cc:184-206` | Boolean metadata is inconsistent: `NumArgs` is ignored for `PA_BOOL`, so a `PA_BOOL` entry with `NumArgs=1` works as a flag but the usage text prints `xx`; `if (ni>1)` is unreachable for the common `MaxInstances` paths. | Normalize `NumArgs=0` for booleans when building the adapter; remove the unreachable check; add a test that every `PA_BOOL` entry is declared with zero args. |
| H-10 | `libs/libhrutil/ProcessOptions.cc:198` | Repeated single-instance `PA_STRING` options `strdup` over the previous pointer without freeing it.  Long-running callers or repeated parses leak. | Free the old value only if the storage contract guarantees ownership (document it), or use a `std::string`-based adapter.  At minimum, note the leak and avoid it in new APIs. |
| H-11 | `libs/libhrutil/ProcessArguments.cc` | `nConv` is uninitialized when the switch has no matching type (no `PA_CDOUBLE` support); string defaults/values are `strdup`ed without freeing previous values; numeric parsing accepts prefixes via `sscanf`. | Migrate call sites to CLI11; do not extend the legacy parser.  Until then, reject unsupported types loudly. |

### 5.3 Medium / low / latent

| ID | Location | Finding | Fix / test |
|---|---|---|---|
| M-01 | `libs/libhrutil/ProcessOptions.cc:98` | `PA_CDOUBLE` usage display calls `z2s(*((double *)OS->Storage))`, reading only the real part and implicitly converting to complex. | Cast to `cdouble *`.  Add a usage-output test for a complex option. |
| M-02 | `libs/libhrutil/ProcessOptions.cc:312` | The loop condition reads `argv[narg+nt+1]` before checking `(narg+nt+1)<argc`.  It relies on `argv[argc] == nullptr`; synthetic argv arrays and non-conforming callers can read out of bounds. | Reorder the condition; use `nt < MAXTOK && (narg+nt+1) < argc && argv[...]`. |
| M-03 | `libs/libhrutil/ProcessOptions.cc` (`MaxInstances == 0`) | An unlimited instance count with fixed-size storage arrays can overflow.  Current uses are booleans, but the API permits it for strings/numerics. | Treat `0` as "unlimited only for booleans" or require an explicit capacity; validate against the declared array size in typed migrations. |
| M-04 | `libs/libhrutil/libhrutil.cc:228-235` | `Tokenize(..., MaxTokens=0)` writes `Tokens[0]` because the loop checks `nt == MaxTokens-1` after writing. | Handle `MaxTokens <= 0` explicitly. |
| M-05 | `libs/libhrutil/libhrutil.cc:706-725` | `S2CD` accepts trailing garbage (`1foo` as `1`, `1+2junk` as `1+2i`).  This can silently accept invalid geometry/material input. | Require the parse to consume the whole string; preserve only the documented `i`/`j` suffixes.  Add malformed-input tests. |
| M-06 | `libs/libhrutil/libhrutil.cc:733-764` | `CD2S` uses a process-global ring buffer and a non-atomic `BufPtr`.  Concurrent logging can race and return a buffer that is overwritten by another thread. | Make the ring `thread_local`; keep the API.  Add a threaded test under TSan or ASan. |
| M-07 | `libs/libhrutil/libhrutil.cc:93-146`, `845-860` | `RemoveDirectories`, `RemoveExtension`, `GetFileExtension`, `GetFileBase`, `GetTimeString`, and `GetHostName` return pointers to static buffers (some rotating).  They are not thread-safe and have lifetime surprises. | Prefer `thread_local` buffers as a low-risk fix; add `std::string`/`std::filesystem` versions for new code. |
| M-08 | `libs/libscuff/AssembleBEMMatrix2018.cc:43`, `libs/libscuff/EquivalentEdgePairs.cc:142,286` | `ToStr`/`EdgePair::Str` use small static buffers; callers that combine two calls in one `printf` can get aliasing or truncation. | Use `thread_local` and check `snprintf` return; migrate new code to `std::string`. |
| M-09 | `libs/libStaticSolver/PostProcessing.cc:285-288` | `strncpy(FileName, BaseFileName, MAXSTR-1)` and `strncpy(Extension, GetFileExtension(...), MAXSTR-1)` can leave the destination unterminated. | Use `snprintf`/`std::string`; combine with C-03. |
| M-10 | `libs/libscuff/BORTObject.cc:97,103,109` | `strncpy(BaseObjectName, 100, Tokens[1])` has the source and size arguments swapped (address 100 is treated as the source).  The file is **not built** by CMake, so this is latent, but it would crash or corrupt memory if revived. | Fix to `strncpy(dest, Tokens[1], sizeof(dest)-1)`; or delete the dormant file with the rest of the obsolete BORT support. |
| M-11 | `libs/libscuff/PanelPanelInteractions.cc:245,253` | `Args->GradH != 0` and `Args->dHdT != 0` test the address of fixed arrays, which is always non-null (`-Waddress`).  The checks are dead. | Remove the address checks; use `NumGradientComponents`/`NumTorqueAxes` alone, or add explicit boolean flags. |
| M-12 | `libs/libMDInterp/Interp1D.cc:131-137`, `Interp4D.cc:175-179` | Misleading indentation suggests `nt++`/the inner loop is guarded by the preceding `if`/`else`; the compiler warns. | Add braces and re-indent.  Confirm the intended control flow with a unit test if behavior is uncertain. |
| M-13 | `applications/scuff-static/BMAccelerator.cc:239-244` | Dangling `else` binds to the inner `if`, not the outer `for`/`if`.  The warning is legitimate even if the current binding is intended. | Add braces; add a comment describing the intended loop. |
| M-14 | `libs/libSpherical/zbsubs.c:7103,9467` | `abs()` is applied to `double` values; the integer `abs` is selected (`-Wabsolute-value`).  This is generated f2c code, but the result can be wrong for large/small values. | Replace with `fabs` (targeted patch) or regenerate; add a numerical regression if this code path is exercised. |
| M-15 | `libs/libMatProp/cmatheval/scanner.c` etc. | `register` storage class and string literals passed to `char *` (`-Wregister`, `-Wwrite-strings`); generated code is not valid strict C++23. | Regenerate with a modern flex/bison, change prototypes to `const char *`, or document per-source suppressions.  Do not hand-edit the whole generated file. |
| M-16 | `libs/libSGJC/hcubature.c`, `libs/libSpherical/*.c` | Third-party/generated code accounts for most remaining warnings (`-Wredundant-decls`, `-Wmaybe-uninitialized`, `-Wshadow`). | Mark as generated/vendored with per-source warning flags; do not include them in the maintained-code warning budget. |
| M-17 | `libs/libhrutil/ProcessArguments.cc:53-55`, `ProcessOptions.cc:79-81` | `sprintf`/`strcat` build usage strings in fixed buffers.  Option names are short today, so this is low risk, but it is the same pattern as the string bugs above. | Replace with `snprintf`/`std::string` while rewriting the usage code for CLI11. |
| M-18 | `applications/scuff-cas2D/...`, `applications/scuff-microstrip/...` | Obsolete/dormant code contains additional `strcpy`/`strncpy`/TR1/Python-era patterns. | Leave out of scope until a decision is made to revive or delete the applications. |

### 5.4 Observations that are not defects but matter for the migration

- `ProcessOptions` is called only once per process in maintained code; there
  is no multi-pass parse to preserve.  The only repeated textual occurrence
  in `ProcessOptions.cc` is the recursive string-overload call.
- `AbortOnUnknown=true` and `ZeroArgs=false` are the only behaviors used
  in-tree.  The parameters can be retired after a downstream compatibility
  check.
- Option names in stdin files are case-insensitive and omit `--`; command
  line names require `--`.  The inconsistency is historical but harmless if
  the adapter accepts both forms.
- Boolean options declared with `NumArgs=1` (`scuff-cas3D`, `scuff-ldos`,
  `scuff-static`) work as flags because the parser special-cases `PA_BOOL`,
  but the generated usage text is wrong.  Normalize these in the adapter and
  fix the declarations during typed migration.
- `S2CD`'s uppercase `I`/`J` support and two-token complex forms are part of
  the de facto CLI contract; do not lose them when switching to CLI11's
  complex converter.

---

## 6. C++23 modernization backlog

This is deliberately organized by risk, not by language feature.  The first
items remove hazards; the later items improve expressiveness.

### 6.1 Diagnostics and format safety

- Add a portable printf-format attribute to all varargs wrappers:

  ```cpp
  #if defined(__GNUC__) || defined(__clang__)
  #  define SCUFF_PRINTF_FORMAT(fmt, first) __attribute__((format(printf, fmt, first)))
  #else
  #  define SCUFF_PRINTF_FORMAT(fmt, first)
  #endif

  [[gnu::format(printf, 1, 2)]]
  void ErrExit(const char *format, ...);
  ```

  Use the attribute on `ErrExit`, `Warn`, `Log`, `LogC`, `LogThread`,
  `MutexLog`, `vstrdup`, `vstrappend`, `vstrncat`, `vfopen`, `vmkdir`,
  `vsystem`, and `vsetenv`.  This immediately catches missing arguments and
  type mismatches.  Build with `-Wformat=2` for maintained sources.
- Replace `sprintf`/`strcat` usage-string construction with `snprintf` or
  `std::format`.
- Add `[[nodiscard]]` to functions that allocate or parse (`strdupEC`,
  `mallocEC`, new parser functions, `S2CD` if it is converted to an optional
  result).
- Mark fallthrough cases with `[[fallthrough]]`; fix the cmatheval and other
  warnings where the fallthrough is intentional.

### 6.2 Strings and paths

- Prefer `std::string` for owned strings and `std::string_view` for read-only
  parameters in new APIs.  Keep `const char *` at the C ABI boundary.
- Use `std::filesystem::path` for directory/extension/base-name operations.
  Replace the static-buffer helpers with:
  - `std::string RemoveDirectories(std::string_view)`;
  - `std::string GetFileBase(std::string_view)`;
  - `std::string GetFileExtension(std::string_view)`;
  - `std::string RemoveExtension(std::string_view)`.
  Keep the old C-style wrappers as deprecated shims until call sites are
  migrated.
- Replace fixed `char buf[MAXSTR]` locals with `std::array<char, MAXSTR>` or
  `std::string`; always size `snprintf` with `buf.size()`, never with an
  unrelated `MAXSTR`.
- Make static return buffers `thread_local` as an interim fix; do not add
  new static-buffer APIs.
- Audit every remaining `strncpy` for termination.  Prefer `snprintf` or a
  small `CopyString` helper that guarantees a NUL.

### 6.3 Containers, ownership, and arrays

- Use `std::vector<T>` for dynamically sized option values and
  `std::array<T,N>` for fixed-size numeric vectors where the size is a
  compile-time constant.
- Use `std::span<T>` for functions that currently take `T *` plus a length.
  Start with new/changed APIs; do not churn numerical signatures.
- Use `std::unique_ptr<T[]>`/`std::vector<T>` for local buffers instead of
  raw `new[]`/`delete[]`.  Audit `new`/`delete` in functions touched by bug
  fixes first.
- Introduce a small `FilePtr` RAII wrapper for `FILE *` in `libhrutil` and
  use it in new code; converting all `fopen`/`fclose` pairs is optional.
- Replace `memset(ptr, 0, n * sizeof(cdouble))` with
  `std::fill_n(ptr, n, cdouble{0.0, 0.0})` or value-initialization.  This
  removes all `-Wclass-memaccess` warnings and avoids object-representation
  assumptions.  For POD arrays, `std::fill` is also clearer than `memset`.
- Replace `memcpy` on complex arrays with `std::copy` where practical; keep
  `memcpy` only for trivially copyable buffers and document why.
- Prefer `std::optional<T>` for "not supplied" values and
  `std::expected<T, Error>` for parsers that can fail without exiting.
  Do not use exceptions for normal CLI errors; CLI11 already has a
  `ParseError` exception hierarchy.

### 6.4 Namespaces, types, and constants

- Remove `using namespace std;` from `libs/libhrutil/libhrutil.h`.  This is a
  public header and currently pollutes every translation unit.  Add explicit
  `std::` qualifications (or narrow `using std::complex;` declarations in
  implementation files).  Expect a large but mechanical diff; do it as a
  standalone commit with no behavior change.
- Replace `#define MAXSTR 1000` with `inline constexpr std::size_t MAXSTR =
  1000;` (or `std::size_t` constants per module).  Same for `MAXTOK`,
  `MAXFREQ`, etc. where they are C++ constants.
- Replace `M_PI` with `std::numbers::pi_v<double>` in C++ files; keep the
  macro fallback for C sources.  Make `DEG2RAD`/`RAD2DEG` `constexpr`.
- Replace the `ISNAN` macro with `std::isnan` in C++ code.
- Replace `PA_*` integer macros with an `enum class OptionType` after the
  legacy parser is removed.  Until then, keep the macros for `OptStruct`
  source compatibility.
- Remove TR1/`HAVE_CXX11` branches:
  - `libs/libscuff/EquivalentEdgePairs.cc`
  - `libs/libscuff/FIBBICache.cc`
  - `libs/libscuff/FIPPICache.cc`
  Use `std::unordered_map` unconditionally.  Delete the corresponding
  configure checks once no source uses them.
- Replace `typedef struct`/C-style typedefs with `struct`/`using` in
  hand-written headers where it does not change the ABI.
- Use `nullptr`, `override`, `constexpr`, `noexcept`, and `[[nodiscard]]`
  consistently in new/changed code.  Do not mass-convert old code just for
  style.

### 6.5 Threading and global state

- `CD2S`, `GetFileBase`, `GetTimeString`, `GetHostName`, and the path
  helpers use process-global static buffers.  Make them `thread_local` or
  return `std::string`; add a concurrency test under the sanitizer/TSan job.
- `DefaultCD2SFormat` is global configuration.  Document that it must be set
  before worker threads start; consider making it an immutable value captured
  at startup.
- Audit logging in OpenMP/pthread regions for calls to non-thread-safe
  helpers.  The fix may be as small as `thread_local` buffers; do not add
  locks around formatting unless necessary.
- `SetCodeMarker`/signal handling is not async-signal-safe.  This is outside
  the main migration, but note it when touching the signal code.

### 6.6 Generated and third-party code policy

- Do not modernize generated quadrature tables.  They are data, not logic.
- Treat f2c output (`zbsubs.c`, `machcon.c`, `drc3jm.c`) and cmatheval
  parser/scanner as vendored.  Add per-source warning options in CMake:
  `-Wno-redundant-decls`, `-Wno-register`, `-Wno-absolute-value`,
  `-Wno-maybe-uninitialized`, etc., as appropriate.  Fix only true UB.
- Treat `libSGJC/hcubature.c` as third-party; suppress its warnings per
  source rather than editing it.
- Keep a short `third_party/README.md` (or comments in CMake) explaining
  which files are generated/vendored and how to regenerate or update them.

### 6.7 Minimum language/toolchain policy

- The build already defaults to C++23.  Decide whether `SCUFF_CXX_STANDARD`
  should continue to offer 17/20 after the modernization.  Once C++23-only
  library features are used, remove 17 (and possibly 20) from the cache
  choices or fail early with a clear message.
- Recommend a minimum of GCC 13+ and Clang 17+ for C++23 library support;
  verify `std::expected`, `std::format`, and `<print>` availability before
  using them.  Prefer C++20 facilities when C++23 adds little and compiler
  support is uneven.
- Keep `CMAKE_CXX_EXTENSIONS ON` for POSIX interfaces (`strcasecmp`,
  `strtok_r`, `gethostname`, etc.) unless a strict `-std=c++23` build is
  explicitly required.  If strict mode is desired, isolate POSIX calls behind
  small wrappers.

---

## 7. Testing strategy

### 7.1 Unit tests

Add a `tests/unit/` target (or `libs/libhrutil/tests/`) linked against the
aggregate `scuff` library and `scuff_common`.  It should have its own `main`
and use CTest.  Initial suites:

1. **CLI compatibility**
   - case-insensitive option names (`--omega`, `--Omega`, `--OMEGA`);
   - option names with and without `--` in stdin lines;
   - comments, blank lines, and `stdin:<line>:` errors;
   - repeated options and `MaxInstances` (last wins, warning, overflow error);
   - multi-argument options (`--pwDirection 1 0 0`, `--psLocation -1 -2 0.5`);
   - negative numbers and `-inf`/`-nan` if accepted today;
   - `--opt=value` if CLI11 supports it (new capability, not required);
   - missing and extra arguments;
   - unknown option exit status;
   - `--help` exit status and key text;
   - boolean flags, including the normalized `NumArgs=0` behavior.
2. **Complex parsing**
   - `S2CD` table: real, imaginary, `i`/`j`, uppercase, `+`/`-`, two-token
     forms, malformed suffixes, overflow.
   - `CD2S` with default and custom formats, long format strings, large
     values, and concurrent calls.
3. **String/path helpers**
   - `RemoveDirectories`, `RemoveExtension`, `GetFileExtension`,
     `GetFileBase`, `fopenPath`, `CreateUniqueFile`, `SetCodeMarker`,
     `GetHostName` at boundary lengths and with no extension/slash.
4. **Tokenize**
   - zero, one, exactly `MaxTokens`, and `MaxTokens+1` tokens; ensure no
     writes past the array and that callers see a truncation indication.
5. **Regression tests for Section 5**
   - a small data file for `scuff-integrate` with multiple parameter sets;
   - a SommerfeldIntegrator invocation with `MaxEvalB>0` and verbose logging;
   - `StaticSolver::PlotChargeDensity` with separate output files;
   - `scuff-ldos --Omega ... --Lambda ...`;
   - `scuff-scatter --IFFile ... --pwDirection ...`;
   - a nonexistent interpolation file to exercise `ErrExit`.

For code that calls `exit()` on error, either fork a child and assert the exit
status/message, or refactor the internals to return an error object and test
that directly.  The latter is strongly preferred for the new parser.

### 7.2 Integration tests

- Keep the existing Mie and Fresnel CTest tests as the numerical guardrail.
- Add a short `scuff-scatter` smoke test using
  `examples/SphericalShell/Args` and a minimal geometry.  This directly
  exercises the stdin option-file path and multi-value options.
- Add a `scuff-analyze --mesh tests/Mie/Sphere_327.msh` smoke test (with a
  timeout) to exercise an installed application's option handling.
- Run at least one test with `SCUFF_THREAD_BACKEND=OpenMP` and one with
  `Pthreads`; the parser and string helpers are used from both.
- Verify exit statuses for invalid options, missing required options, and
  `--help`.

### 7.3 Sanitizers and static analysis

- Add a CI job with `SCUFF_ENABLE_SANITIZERS=ON` (ASan + UBSan) that builds
  and runs the unit tests plus the short smoke tests.  Do not run the full Mie
  suite under ASan initially (runtime); use a reduced frequency list if
  needed.
- Add a TSan job once the `CD2S`/static-buffer fixes land; run a small
  multithreaded formatting test.
- Add an optional `clang-tidy` CI job with `bugprone-*`, `modernize-*`,
  `performance-*`, and `readability-*` checks.  Start non-blocking, then
  make changed-files blocking.
- Local `scan-build` remains useful; `clang-format` is available and can be
  applied to touched files only.
- Add `-Wformat=2` and the printf attributes first; this catches a whole
  class of defects without external tools.

### 7.4 Warning budget

- Record the baseline (442 warnings) in CI as a comment or artifact.
- Remove generated-code warnings from the count with per-source flags.
- Fix the remaining hand-written warnings in groups (complex `memset`,
  truncation, return-type, format, uninitialized).
- Once the maintained sources are clean, enable `SCUFF_WARNINGS_AS_ERRORS=ON`
  for the library/application targets in at least one CI job.  Keep an escape
  hatch for known generated-code warnings.

---

## 8. Commit sequence

The exact order can shift, but each step should be independently buildable
and testable.

### Phase 0 — planning and guardrails

1. `[ai_notes] Plan C++23 and CLI11 migration` (this note).
2. `[tests] Add unit-test target and CLI compatibility harness`.
3. `[build] Add sanitizer CI job and warning-budget reporting`.

### Phase 1 — correctness fixes (before the parser rewrite)

4. `[libhrutil] Fix string-helper bounds and termination` (C-07, C-08, C-09,
   H-07, H-08, M-04, M-07).
5. `[libhrutil] Fix S2CD parsing and add complex-number tests` (H-01, M-05).
6. `[libhrutil] Fix Tokenize contract and stdin overrun` (C-06).
7. `[libhrutil] Add printf format attributes and fix missing arguments`
   (H-06).
8. `[libMDInterp] Initialize log levels and fix error messages` (C-05, H-04,
   M-12).
9. `[libSubstrate] Fix SommerfeldIntegrator use-after-free` (C-02).
10. `[libStaticSolver] Fix overlapping filename construction` (C-03, M-09).
11. `[scuff-neq] Fix RowStarts out-of-bounds write` (C-01).
12. `[scuff-ldos] Store --Lambda values separately` (H-02).
13. `[scuff-scatter] Fix incident-field list validation` (H-03).
14. `[libMatProp] Fix cmatheval missing returns and fallthrough` (C-04).
15. `[libscuff] Fix or delete dormant BORTObject string copies` (M-10).

### Phase 2 — CLI11 foundation

16. `[build] Vendor CLI11 2.7.2 and add scuff_cli11` (Section 4.2/4.3).
17. `[libhrutil] Reimplement ProcessOptions on CLI11` (Stage A adapter;
    includes C-06/H-09/M-01/M-02 fixes and the string-overload decision).
18. `[tests] Add CLI compatibility and stdin option-file tests`.
19. `[libhrutil] Add typed parser helpers and remove dead overloads`
    (after downstream check of `AbortOnUnknown`/`ZeroArgs`).

### Phase 3 — typed application migration

20. `[scuff-scatter] Migrate to typed CLI11 options` (largest array; first).
21. `[scuff-analyze] Migrate to typed CLI11 options`.
22. `[scuff-ldos] Migrate to typed CLI11 options`.
23. `[scuff-static] Migrate to typed CLI11 options`.
24. `[scuff-transmission] Migrate to typed CLI11 options`.
25. `[scuff-tmatrix] Migrate to typed CLI11 options`.
26. `[scuff-cas3D] Migrate to typed CLI11 options`.
27. `[scuff-rf]`, `[scuff-neq]`, `[scuff-spectrum]`,
    `[scuff-plotEpsMu]`, `[scuff-caspol]`, `[scuff-integrate]`.
28. `[tests] Migrate developer/test programs to CLI11`.
29. `[libhrutil] Remove OptStruct/ProcessOptions/ProcessArguments` after all
    call sites are gone.
30. `[build] Drop CLI11 from public headers and verify install tree`.

### Phase 4 — language/style modernization

31. `[libhrutil] Remove using namespace std from the public header`.
32. `[libscuff] Remove TR1 and HAVE_CXX11 conditionals`.
33. `[libs] Replace memset on complex arrays with std::fill`.
34. `[libhrutil] Convert string/path helpers to std::string/string_view/
    filesystem` (keep deprecated wrappers).
35. `[libs] Introduce std::vector/std::array/std::span/unique_ptr in
    touched code` (incremental, not a mass rewrite).
36. `[libhrutil] Make static formatting buffers thread_local and add a
    concurrency test`.
37. `[build] Enforce warnings-as-errors for maintained sources`; add
    clang-tidy job.
38. `[docs] Document C++23 requirements, CLI behavior, and CLI11 license`.

Each commit should run:

```bash
cmake --build build-cmake -j"$(nproc)"
ctest --test-dir build-cmake --output-on-failure
```

and, for the relevant phases, the sanitizer/unit-test jobs.  The Mie/Fresnel
suite should be run before merging any change that touches numerical code or
the aggregate library.

---

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| CLI11 changes user-visible behavior (case, stdin, negative numbers, repeated options) | Adapter + compatibility test matrix + staged migration; keep old usage/exit behavior on errors during Stage A. |
| Tightening numeric parsing breaks existing scripts | Preserve lenient parsing in the adapter initially; make strict parsing an explicit, documented change in typed migrations. |
| Vendored dependency policy is unwelcome | `SCUFF_USE_SYSTEM_CLI11` override; pin version and checksum; document update procedure; license is compatible. |
| `using namespace std` removal breaks downstream source code | Standalone commit, release note, and a short migration section in `doc/Installation.md`; no ABI change. |
| `memset`/`memcpy` changes alter numerical results | Use value-initialization/`std::fill` only for zeroing; verify with Mie/Fresnel and unit tests. |
| Generated-code warnings hide real bugs | Per-source suppression plus targeted fixes for true UB; keep the maintained-code warning budget separate. |
| Large mechanical diffs obscure review | One category per commit; no mass reformatting; use clang-format on touched files only. |
| Sanitizer runs are too slow | Run unit tests and short smoke tests under ASan/UBSan; use reduced frequency lists for full apps. |
| Public API churn | Keep C-style wrappers as deprecated shims during the transition; remove them only after all in-tree call sites and a documented deprecation window. |
| Dormant apps consume effort | Explicitly exclude `scuff-cas2D`/`scuff-microstrip` from the schedule; decide later whether to delete or revive. |
| Compiler support for C++23 library features varies | Verify in CI (GCC 13+/Clang 17+); prefer C++20 facilities where C++23 support is uneven. |

---

## 10. Decisions to confirm

These are the choices that affect the implementation order or user-visible
behavior.  The recommendations above are the default plan.

1. **CLI11 acquisition:** vendor the 2.7.2 single header (recommended) or use
   a system package/FetchContent?  Vendoring is assumed by the CMake sketch.
2. **Legacy adapter vs. immediate rewrite:** implement the CLI11-backed
   `OptStruct` adapter first (recommended) or migrate all 33 applications in
   one series?  The adapter gives an earlier safety/feature win.
3. **Strict numeric parsing:** reject trailing garbage (`1foo`) in the new
   parser (recommended) or preserve `sscanf` leniency for compatibility?
   This can be staged: adapter preserves leniency, typed apps become strict.
4. **`--` terminator:** continue treating a bare `--` as an unknown option
   (legacy behavior) or adopt the conventional end-of-options meaning?  The
   latter is a user-visible improvement but may change scripts.
5. **Compiler floor:** GCC 13+/Clang 17+ (recommended) or a newer floor that
   guarantees all C++23 library features used.
6. **Dormant applications:** keep `scuff-cas2D`/`scuff-microstrip` unbuilt for
   now (recommended) or delete them as part of this workstream?
7. **Public C-style string API:** keep deprecated wrappers (recommended) or
   remove them in the same release as the C++ replacements?

### Resolution status

The decisions that affect the current implementation are now settled:

1. **CLI11 acquisition — vendored.**  CLI11 2.7.2 is vendored under
   `third_party/CLI11/` with an optional `SCUFF_USE_SYSTEM_CLI11=ON`
   (`74d4f304`).  Builds remain offline and reproducible.
2. **Adapter first — yes.**  The CLI11-backed `OptStruct` adapter landed in
   `efafc09c`; typed application migration follows incrementally.
3. **Strict numeric parsing — yes.**  Trailing garbage is rejected by the
   adapter; the compatibility tests codify this tightening.
4. **Bare `--` — legacy unknown-option behavior retained.**  No script that
   relied on the old error changes meaning.
5. **Compiler floor — GCC 13+ / Clang 17+ for C++23 builds.**  CMake keeps
   the 17/20/23 choices for now, but the project does not promise that older
   compilers can build every C++23 facility used by future changes.  A hard
   CMake version check is deferred until a C++23-only library feature is
   actually adopted.
6. **Dormant applications — keep unbuilt.**  `scuff-cas2D` and
   `scuff-microstrip` remain in the tree but outside the maintained build.
   They are not part of the modernization or warning budget unless revived.
7. **Public C-style string API — keep until Stage C.**  The deprecated
   wrappers stay for downstream compatibility while call sites migrate.
   Removal is a separate, documented release decision after all maintained
   applications use the C++ replacements.

---

## 11. Appendix: CLI call-site inventory

`ProcessOptions` external call sites (33):

```text
applications/scuff-analyze/scuff-analyze.cc
applications/scuff-cas3D/scuff-cas3D.cc
applications/scuff-caspol/scuff-caspol.cc
applications/scuff-caspol/tPolModel.cc
applications/scuff-ldos/scuff-ldos.cc
applications/scuff-neq/scuff-integrate.cc
applications/scuff-neq/scuff-neq.cc
applications/scuff-plotEpsMu/scuff-plotEpsMu.cc
applications/scuff-rf/scuff-rf.cc
applications/scuff-scatter/scuff-scatter.cc
applications/scuff-spectrum/scuff-spectrum.cc
applications/scuff-static/scuff-static.cc
applications/scuff-tmatrix/scuff-tmatrix.cc
applications/scuff-transmission/scuff-transmission.cc
libs/libIncField/tPointSource.cc
libs/libMDInterp/tInterpND.cc
libs/libSubstrate/tSommerfeldIntegrand.cc
libs/libSubstrate/tests/tFullWaveSubstrate.cc
libs/libSubstrate/tests/tGTwiddle.cc
libs/libSubstrate/tests/tInterpolation.cc
libs/libSubstrate/tests/tInterpolationError.cc
libs/libSubstrate/tests/tMOISGFs.cc
libs/libhmat/tInvert.cc
libs/libhmat/tQR.cc
libs/libhmat/tlibhmat2.cc
libs/libscuffSolver/tests/t1BFMOIFields.cc
libs/libscuffSolver/tests/tEquivalentPairs.cc
libs/libTriInt/BZIntegration.cc
libs/libTriInt/tlibTriInt2.cc
tests/CheckSCUFFData.cc
tests/RunSCUFFTests.cc
examples/GetTMatrix/GetTMatrix.cc
examples/GetTMatrix/tSphericalMoments.cc
```

`ProcessArguments` external call sites (13):

```text
applications/scuff-plotEpsMu/scuff-plotEpsMu.cc
libs/libhmat/tInvert.cc
libs/libhmat/tlibhmat2.cc
libs/libhmat/tMultiply.cc
libs/libhmat/tzgemm.cc
libs/libhrutil/PASnippet.cc
libs/libhrutil/tProcessArguments.cc
libs/libMDInterp/tInterp1D.cc
libs/libMDInterp/tInterp2D.cc
libs/libMDInterp/tInterp3D.cc
libs/libMDInterp/WriteMatrix.cc
libs/libMDInterp/WriteMatrixLP.cc
libs/libTriInt/tlibTriInt2.cc
```

Notes:

- `scuff-microstrip` also contains `ProcessOptions` calls, but it is not
  built; it is excluded from the 33 maintained call sites.
- `ProcessArguments` is used mostly by developer/test programs and by the
  installed `scuff-plotEpsMu` application.  Migrate `scuff-plotEpsMu` with the
  applications, then convert or delete the developer programs.
- `ProcessOptions` is also called internally by its string overload; that
  overload is unused in maintained code.

---

## 12. Appendix: high-value warning locations

The following are worth checking first when the warning cleanup starts:

```text
libs/libMatProp/cmatheval/node.c:162,324,1352          missing return
libs/libMatProp/cmatheval/node.c:354,1192,1198        implicit fallthrough
libs/libMatProp/cmatheval/scanner.c:1362              register
libs/libMatProp/cmatheval/matheval.c:286,301,316,332  string literal to char *
libs/libMDInterp/Interp1D.cc:131,525                  misleading indentation, uninitialized LogLevel
libs/libMDInterp/Interp2D.cc:483                      uninitialized LogLevel
libs/libMDInterp/Interp3D.cc:511                      uninitialized LogLevel
libs/libMDInterp/Interp4D.cc:175                      misleading indentation
libs/libSubstrate/SommerfeldIntegrator.cc:258         use-after-free
libs/libStaticSolver/PostProcessing.cc:291,293,295    overlapping snprintf
libs/libscuff/PanelPanelInteractions.cc:245,253       always-true address checks
libs/libSpherical/zbsubs.c:7103,9467                  abs on double
applications/scuff-neq/scuff-integrate.cc:402         vector out-of-bounds
applications/scuff-static/BMAccelerator.cc:239        dangling else
applications/scuff-spectrum/scuff-spectrum.cc:222,272,301  filename truncation
libs/libhmat/C2ML.cc:113,116,187,195                  filename truncation
libs/libhmat/TextIO.cc:63,224,292                     strncpy truncation
libs/libhmat/Sort.cc:139                              array-bounds warning (verify)
```

---

## 13. References

- CLI11 upstream: <https://github.com/CLIUtils/CLI11>
- CLI11 options documentation:
  <https://cliutils.github.io/CLI11/book-options.html>
- CLI11 2.7.2 release:
  <https://github.com/CLIUtils/CLI11/releases/tag/v2.7.2>
- Prior build-system notes:
  - `ai_notes/cmake-migration-plan-2026-10-26.md`
  - `ai_notes/cmake-implementation-2026-10-26.md`
  - `ai_notes/autotools-removal-2026-10-26.md`
