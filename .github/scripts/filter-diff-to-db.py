#!/usr/bin/env python3
"""Filter a unified diff down to the files present in a compile_commands.json.

The changed-lines clang-tidy gate can only lint files for which the build
produced a compile command.  Headers are never compilation entries, and some
changed programs (orphaned dev programs under libs/*/t*.cc, the unbuilt
examples) belong to no CMake target at all.  clang-tidy would parse those
files with fallback "no flags" and emit spurious 'file not found'
clang-diagnostic-error noise, so their hunks are dropped here and
clang-tidy-diff.py only ever sees lintable files.  (Same effect as
clang-tidy-diff.py -only-check-in-db, which LLVM 18 does not yet support.)

Usage:
  git diff -U0 BASE...HEAD | filter-diff-to-db.py <build-dir> | clang-tidy-diff.py ...
"""

import json
import os
import sys


def main() -> None:
    build_dir = sys.argv[1] if len(sys.argv) > 1 else "build"
    with open(os.path.join(build_dir, "compile_commands.json")) as db_file:
        db = {os.path.abspath(entry["file"]) for entry in json.load(db_file)
              if "file" in entry}

    # Emit whole per-file sections ("diff --git ..." up to the next one),
    # keeping a section only if its '+++ b/<path>' file is lintable.
    section: list[str] = []
    keep = False
    for line in sys.stdin:
        if line.startswith("diff --git "):
            if keep:
                sys.stdout.write("".join(section))
            section, keep = [line], False
            continue
        if line.startswith("+++ ") and not keep:
            path = line[4:].strip()
            if path != "/dev/null":
                path = path[2:] if path.startswith("b/") else path
                keep = os.path.abspath(path) in db
        section.append(line)
    if keep:
        sys.stdout.write("".join(section))


if __name__ == "__main__":
    main()
