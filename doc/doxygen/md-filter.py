#!/usr/bin/env python3
r"""Doxygen input filter for the SCUFF-EM documentation sources.

The narrative documentation in doc/docs was originally written for the
mkdocs tool, which understands a few syntax extensions that plain
markdown (and Doxygen's markdown processor) do not.  This filter
rewrites those extensions to portable markdown so the same sources can
be used for both builds:

* ``{!file.md!}``       -- mkdocs markdown-include directives are expanded
                           by inlining the referenced file (searched
                           relative to the documentation root).
* ``[[name]]``          -- mkdocs wikilinks are converted to ordinary
                           markdown links when a documentation page with
                           a matching name exists, and to plain text
                           otherwise.  Link texts such as ``[[1]](url)``
                           are recognized as such and left alone.
* ``{#anchor}``         -- mkdocs attr_list header anchors are stripped
                           (Doxygen generates its own anchors).
* ``/root/relative``    -- link targets that are absolute with respect
                           to the documentation root are rewritten to
                           paths relative to the file being processed;
                           links whose targets do not exist are defused
                           into plain text (Doxygen would otherwise map
                           them onto arbitrary same-named symbols).
* ``index.md``          -- the documentation home page is turned into
                           the Doxygen main page by prepending a
                           \mainpage directive.

Non-markdown files (headers, sources) are passed through unchanged.
"""

import os
import re
import sys

DOCS_ROOT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         "docs")


def page_index():
    """Map lowercased page stems -> paths relative to DOCS_ROOT."""
    index = {}
    for dirpath, dirnames, filenames in os.walk(DOCS_ROOT):
        dirnames[:] = [d for d in dirnames if d not in (".git",)]
        for fn in filenames:
            if fn.endswith(".md"):
                stem = os.path.splitext(fn)[0]
                index.setdefault(stem.lower(), os.path.relpath(os.path.join(dirpath, fn),
                                                               DOCS_ROOT))
    return index


def expand_includes(text, depth=0):
    """Inline mkdocs {!file!} include directives."""
    if depth > 3:
        return ""

    def _expand(match):
        name = match.group(1).strip()
        if not name:
            return ""
        candidate = os.path.join(DOCS_ROOT, name)
        if not os.path.isfile(candidate):
            return ""
        included = open(candidate, encoding="utf-8", errors="replace").read()
        return expand_includes(included, depth + 1)

    return re.sub(r"\{!([^}]*)!\}", _expand, text)


def strip_anchors(text):
    return re.sub(r"\{#[^}]*\}", "", text)


def resolve_target(target, current_dir):
    """Return a path of `target` relative to current_dir, or None if
    the referenced file does not exist.  Non-documentation files
    (images, PDFs, ...) are returned as-is when they cannot be found
    because they may be materialized at HTML-generation time."""
    if target.startswith("/"):
        # root-relative with respect to the documentation root
        resolved = os.path.join(DOCS_ROOT, target[1:])
        rel = os.path.relpath(resolved, current_dir)
    else:
        # strip any #fragment for the existence test
        resolved = os.path.normpath(os.path.join(current_dir, target.split("#")[0]))
        rel = target
    if os.path.isfile(resolved):
        return rel
    return None


def rewrite_links(text, current_dir):
    """Rewrite root-absolute link targets to file-relative form; defuse
    links to targets that do not exist."""
    def _inline(match):
        label, target = match.group(1), match.group(2)
        # external URLs and mailto links stay untouched
        if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith("#"):
            return match.group(0)
        rel = resolve_target(target, current_dir)
        if rel is None:
            # dangling target: keep the label as plain text
            return re.sub(r"\\_", "_", label).lstrip("!").strip("`")
        return "[%s](%s)" % (label, rel)

    # inline links; images are [label](target) with a preceding '!'
    # which stays attached to the bracket in group 1 handling above,
    # so handle them in the same pass (the '!' is outside the match).
    text = re.sub(r"((?:!?)\[[^\]]*\])\s*\(\s*(<[^>]*>|[^)\s]+)\s*\)", _inline, text)

    # link-reference definitions: [name]: /root/relative/path
    def _refdef(match):
        name, target = match.group(1), match.group(2).strip()
        if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or not target.startswith("/"):
            return match.group(0)
        rel = resolve_target(target, current_dir)
        if rel is None:
            return match.group(0)
        return "[%s]: %s" % (name, rel)

    text = re.sub(r"^(\[[^\]]+\]):\s*(\S+)\s*$", _refdef, text, flags=re.M)
    return text


def convert_wikilinks(text, index, current_dir):
    """Convert mkdocs wikilinks to markdown links or plain text."""
    def _convert(match):
        name = match.group(1)
        target = index.get(name.lower())
        if target:
            rel = os.path.relpath(os.path.join(DOCS_ROOT, target), current_dir)
            return "[%s](%s)" % (name, rel)
        return name

    return re.sub(r"\[\[([^\[\]]+)\]\](?!\()", _convert, text)


def main():
    if len(sys.argv) != 2:
        sys.stderr.write("usage: md-filter.py <file>\n")
        return 1
    path = sys.argv[1]
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        sys.stderr.write("md-filter: cannot read %s: %s\n" % (path, e))
        return 1

    if not path.endswith(".md"):
        sys.stdout.write(text)
        return 0

    current_dir = os.path.dirname(os.path.abspath(path))
    text = expand_includes(text)
    text = strip_anchors(text)
    text = rewrite_links(text, current_dir)
    text = convert_wikilinks(text, page_index(), current_dir)

    # the documentation home page becomes the Doxygen main page
    if os.path.abspath(path) == os.path.join(DOCS_ROOT, "index.md"):
        text = "\\mainpage scuff-em documentation\n\n" + text

    sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
