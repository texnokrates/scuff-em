#!/usr/bin/env python3
"""One-shot helper used (2026-11) to prepare the public headers for
Doxygen: prose ``/* ... */`` comment blocks that immediately precede
class/struct/namespace/declaration statements are promoted to
``/** ... */`` Doxygen blocks so that the descriptions -- which were
always meant as documentation -- attach to the entities they describe.

Contiguous comment-only lines are merged into a single Doxygen block
(the sources often split one comment over several ``/* ... */`` lines);
banner comments (dashed separator lines) are left untouched.

Not part of the build; kept for provenance of the header comments.
"""

import re
import sys

DASH = re.compile(r"^/*-+[*/]*$")


def strip_markers(line):
    """Strip /* */ markers from a single-line comment, returning the
    inner text.  Returns None if the line is not a complete one-line
    block comment."""
    s = line.strip()
    m = re.match(r"^/(\*+)(.*)\1?/\s*$", s)
    if not m or "*" not in m.group(0)[:2]:
        return None
    if not s.endswith("*/"):
        return None
    inner = s[2:-2]
    return inner


def collect_block(lines, i):
    """Collect contiguous comment blocks starting at i.  Each block is
    either a one-line ``/* ... */`` or a multi-line block closed by a
    line whose tail is ``*/``.  Returns (raw_lines, inner_lines,
    next_index); inner_lines holds the comment body text of each
    physical line of the block."""
    raw, inner = [], []
    while i < len(lines):
        s = lines[i].strip()
        if not s.startswith("/*"):
            break
        if s.endswith("*/") and len(s) >= 4:
            raw.append(lines[i])
            inner.append(s[2:-2])
            i += 1
            # absorb immediately following one-line comment blocks
            nxt = lines[i].strip() if i < len(lines) else ""
            if nxt.startswith("/*") and nxt.endswith("*/") and "/**" not in nxt:
                continue
            break
        else:
            # multi-line block: scan to the closing line
            raw.append(lines[i])
            inner.append(s[2:])
            i += 1
            closed = False
            while i < len(lines) and not closed:
                s2 = lines[i]
                t2 = s2.strip()
                idx = t2.rfind("*/")
                if idx >= 0:
                    raw.append(lines[i])
                    inner.append(t2[:idx])
                    i += 1
                    closed = True
                else:
                    raw.append(lines[i])
                    inner.append(t2)
                    i += 1
            if not closed:
                break  # unterminated block: stop collecting
    return raw, inner, i


def is_banner(inner):
    """Banner blocks contain only dash-lines (section separators)."""
    return all(DASH.match(b.strip()) or b.strip() == "" or
               set(b.strip()) <= set("-=* ") or b.strip().startswith("-")
               for b in inner)


def is_banner_line(line):
    """True for a one-line comment that contains nothing but dashes."""
    s = line.strip()
    if not (s.startswith("/*") and s.endswith("*/")):
        return False
    return is_banner([s[2:-2]])


def trim_banners(raw, inner):
    """Drop leading/trailing banner-only lines from a collected block."""
    lo, hi = 0, len(inner)
    def bannerish(b):
        t = b.strip()
        return t == "" or set(t) <= set("-=* ")
    while lo < hi and bannerish(inner[lo]):
        lo += 1
    while hi > lo and bannerish(inner[hi - 1]):
        hi -= 1
    return raw[lo:hi], inner[lo:hi]


def promote(text):
    out = []
    i = 0
    lines = text.split("\n")
    promoted = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if stripped.startswith("/*") and not stripped.startswith("/**"):
            raw, inner, j = collect_block(lines, i)
            raw_t, inner_t = trim_banners(raw, inner)
            # next meaningful line after the block: skip blank lines and
            # trailing banner-only comment lines
            k = j
            while k < len(lines):
                t = lines[k].strip()
                if t == "" or is_banner_line(lines[k]):
                    k += 1
                    continue
                break
            nxt = lines[k].strip() if k < len(lines) else ""
            is_decl = bool(re.match(r"^(class|struct|namespace|enum)\b", nxt))
            is_member = bool(re.match(r"^[A-Za-z_].*[;({=]", nxt))
            if (is_decl or is_member) and inner_t and not is_banner(inner_t):
                indent = re.match(r"\s*", raw_t[0]).group(0)
                # drop trailing star/separator runs and blank remnants
                body = [re.sub(r"[*=\-]+\s*$", "", b).strip() for b in inner_t]
                body = [b for b in body if b]
                if not body:
                    out.extend(raw)
                else:
                    out.append(indent + "/** " + body[0])
                    for b in body[1:]:
                        out.append(indent + "    " + b)
                    out.append(indent + " */")
                    promoted += 1
            else:
                out.extend(raw)
            i = j
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out), promoted


def main():
    for path in sys.argv[1:]:
        text = open(path).read()
        new, n = promote(text)
        open(path, "w").write(new)
        print("%-45s promoted %d blocks" % (path, n))


if __name__ == "__main__":
    main()
