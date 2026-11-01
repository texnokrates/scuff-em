# Documentation: LaTeX memos repaired, Doxygen build, CMake integration (2026-11-01)

## Summary

The documentation deliverables asked for: (1) all LaTeX technical memos in
`tex/` (= `doc/docs/tex/`) compile again; (2) a Doxygen build that combines
the API reference (from header comments) with the markdown documentation
tree as hyperlinked Doxygen pages; (3) build machinery for both in CMake.

Commits: `739b1ff4` ([doc] Make all 21 technical memos build again), plus the
Doxygen/CMake commit (this note's commit).

## Part 1: LaTeX repair

Root cause of the "everything fails" state: two preamble files lived only in
Homer Reid's private texmf tree and were never committed —
`tex/HRCommands.tex` (shared packages + personal macros, `\input` by
`scufftex.tex`) and `tex/physcmds.tex` (`\input` by Fresnel.tex). Nine
figure files were also never committed. Recovery method: reconstructed the
macro definitions from usage in the sources and compared against the
rendered memos (committed PDFs + homerreid.github.io/scuff-em-documentation/
tex/*.pdf, which the user pointed out). Key recovered definitions:

- `\nn` is `\nonumber\\` — the row break is INCLUDED (proved by eq. (17) of
  the rendered scuff-neq memo; 60+ align blocks depend on it).
- `\pf{a}{b}` = `\left(\frac{a}{b}\right)`, `\sbf` = square-bracket variant.
- `\numeq{Tag}{body}` = labeled equation; `\qtq` = `\qquad\mbox{..}\qquad`;
  `\whereupon` = `\Longrightarrow`; `\atan`, `\Tr` = upright operators.
- `\subs/\sups/\subt/\supt` = upright scriptsize sub/superscripts.
- `verbcode` = shaded, framed verbatim block (implemented with `listings`).

Additional repairs: `\include`→`\input` in scufftex.tex (preamble cannot
`\include`); float/mathtools/array packages for `[H]`, `dcases`, `>{}` cols;
`\vbchi \wh \INP \VMV \vbhatt` added to HRCommands (duplicate later local
definitions removed from NearFields/scuff-neq/scuff-neq2); five `align`
blocks had rows merged by a missing `\\` after `\nn` (modern amsmath errors
out; these were pre-existing latent typos — fixed in place); stale committed
.aux/.toc files poisoned reruns (untracked + gitignored). Figures: five
recovered from the documentation site (port cartoons, substrate current
figure), StaticSubstrate.pdf recreated from DielectricSubstrateCartoon.png,
SigmaLayers.pdf redrawn in TikZ, SiOnE2Phi/Ez.pdf are labeled placeholders
(content unrecoverable). PFT.tex references two section files that never
existed in any commit (EMTPFT2.tex, MomentPFT.tex — their content survives
only in the committed rendered PFT.pdf); the `\input`s are commented out.

All 21 memos build with `latexmk -pdf`; freshly rendered PDFs are committed
(12 of them had never been committed before).

## Part 2: Doxygen

- `doc/Doxyfile.in` (configured by CMake): INPUT = libs/ (API from public
  headers, implementation headers excluded), doc/docs (+ doc/docs/examples
  explicitly — Doxygen's built-in `examples` dir skipping would otherwise
  drop it), doc/dox (glue pages). MathJax 2 from CDN for the TeX math in
  the markdown. Full warnings log to build/doc/doxygen-warnings.log
  (~1300 lines, all pre-existing content quirks: TeX commands in md math,
  `<span>` inside links, 15 duplicate section labels).
- `doc/doxygen/md-filter.py` (INPUT_FILTER): makes the mkdocs-flavored
  markdown digestible — expands `{!file.md!}` includes, strips `{#anchors}`,
  rebases root-absolute `/paths` to file-relative, defuses dangling links
  (Doxygen otherwise maps them onto arbitrary same-named symbols — verified
  against real output), converts `[[wikilinks]]` to links or plain text,
  and prepends `\mainpage` to doc/docs/index.md.
- `doc/dox/`: api-overview.dox (library table with \ref links to classes),
  technical-memos.dox (all 21 memos, links to tex/*.pdf copied into the
  HTML tree), building-docs.dox.
- Cross-links verified: main page 9 memo links, memo page 21 links — all
  resolve; api_overview → libscuff/examples page refs resolve.
- `doc/doxygen/promote-comments.py`: one-shot helper (kept for provenance,
  not part of the build) that promoted 180 prose `/* */` blocks in the 15
  public headers to `/** */`, merging Homer's split-one-line-per-sentence
  comment style and skipping banner frames. Hand-written briefs added for
  HMatrix, HVector, MatProp, StaticSolver, scuffSolver (the original comment
  was truncated mid-sentence in the source itself). Gotcha: Doxygen attaches
  a comment to whatever declaration FOLLOWS — a forward declaration between
  comment and class steals it (HMatrix case), and a `#define` in between
  does too (StaticSolver case).

## Part 3: CMake

- `cmake/ScuffDocs.cmake`, `include`d from the top-level CMakeLists before
  the feature summary (adds `Doxygen docs:` / `LaTeX memos:` lines).
- Target `memos`: latexmk -pdf over the 21 memo basenames, in-place in
  doc/docs/tex (the rendered PDFs are tracked in git, so in-place is the
  established workflow). Default on when latexmk+pdflatex found.
- Target `docs`: configure_file(Doxyfile.in) → build/doc/Doxyfile, run
  doxygen, then copy *.pdf (only PDFs; artifacts are ignored) from
  doc/docs/tex to build/doc/html/tex via a tiny generated cmake -P script.
  DEPENDS memos when enabled. Default on when Doxygen ≥ 1.9 found.
- Options `SCUFF_BUILD_DOCS` / `SCUFF_BUILD_MEMOS`.
- Gotcha: option() caches — a first configure with a broken default caches
  OFF; `cmake -USCUFF_BUILD_DOCS` to reset.

## Verification

- Full rebuild + CTest in build/: 7/7 pass (headers changed only in
  comments; build-dev not rebuilt — no API change).
- `cmake --build build --target docs` and `--target memos` both work.
- No doxygen config warnings (obsolete tags removed).

## Remaining work / notes

- The wiki-style `doc/README.md` now documents the CMake doc build; the
  historical mkdocs workflow notes remain at the top.
- Doxygen warning log is worth an occasional skim; duplicate md section
  labels (15) could be uniquified someday.
- doc/docs/tex is the REAL directory; top-level `tex` is a symlink to it
  (pre-existing arrangement — the `doc/docs/tex` copy tracked in git is the
  same files; mkdocs and the memo build both consume it there).
- PFT sections 6–7 (EMTPFT2, MomentPFT) exist only in rendered PFT.pdf;
  a future contributor could re-typeset them from the PDF if ever needed.
