# CLI11 2.7.2 (vendored)

This directory contains the single-header release of
[CLI11](https://github.com/CLIUtils/CLI11), used by `libhrutil` and, during
the typed migration, by individual SCUFF-EM applications.

- Upstream: <https://github.com/CLIUtils/CLI11>
- Release: `v2.7.2`
- Source asset: `CLI11.hpp` from the GitHub release
- `CLI11.hpp` SHA256:
  `ffa9a30da295c5858fb5f91f9f45771bab09471d7010a34c7c68c857a330dd76`
- `LICENSE` SHA256:
  `cfc76368aef8f51868fa1cda6bcfa26e583406a4cf8e48e725e7624df24d8855`
- License: BSD-3-Clause (see `LICENSE`)

## Why vendored

Vendoring keeps configure and build steps reproducible and offline.  A
single header is easy to audit and has no install-time dependencies.

## System CLI11

Packagers who prefer the distribution's CLI11 can configure with:

```sh
cmake -S . -B build -DSCUFF_USE_SYSTEM_CLI11=ON
```

This uses `find_package(CLI11 2.4 CONFIG REQUIRED)` and links
`CLI11::CLI11`.  The vendored 2.7.2 header remains the reference version.

## Updating

1. Download the new `CLI11.hpp` from the chosen upstream release.
2. Download the matching `LICENSE` from the same tag.
3. Update the version, hashes, and release date in this file.
4. Rebuild and run the CLI compatibility tests.
5. Do not patch the header locally; report upstream issues instead.

## Installed notice

`LICENSE` is installed under `share/doc/scuff-em/CLI11-LICENSE` so binary
redistributors retain the notice.
