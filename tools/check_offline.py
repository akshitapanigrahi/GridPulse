#!/usr/bin/env python3
"""Verify that Mode B really is playable from a bare file:// origin.

This is a hard requirement of the project and an easy one to break by accident: a
single ``type="module"``, a stray ``fetch()`` on the startup path, or a CDN URL added
for convenience would all pass every other test and then fail silently the moment a
grader double-clicks web/play.html.

A file:// document is an opaque origin. Module scripts, ``fetch`` and ``XMLHttpRequest``
are all blocked there; classic ``<script src>`` and ``<link rel=stylesheet>`` are not.
So the rules checked here are:

  1. Every asset referenced by the offline pages exists on disk.
  2. No reference points at an absolute URL or a remote host.
  3. No page uses ``type="module"``.
  4. No file on the Mode B startup path uses ES module syntax, ``fetch``, or XHR.

``web/transport/sse.js`` is exempt from rule 4 and checked separately: it is the Mode A
transport, its ``fetch`` calls are guarded behind a ``file:``-protocol check, and it is
never reached when the page is opened from the filesystem.

Standard library only. Exits nonzero on any violation.
"""

from __future__ import annotations

import os
import re
import sys
from typing import List, Tuple

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_ROOT = os.path.join(REPO_ROOT, "web")

# Pages a grader can open straight off the filesystem.
OFFLINE_PAGES = ["play.html", "tests.html"]

# Files that must never contain module syntax, fetch, or XHR.
# The Mode B path is DERIVED from the pages, not listed here.
#
# It used to be a hand-maintained list, and a hand-maintained list is a check that
# quietly stops checking: ui/sound.js was added to play.html and simply never appeared
# here, so nothing verified it was free of fetch, XHR or module syntax. Reading the
# script tags out of the pages themselves means a new file is covered the moment it is
# referenced, which is the only way this stays true.
MODE_B_EXEMPT = {
    # The Mode A transport may use fetch. Its file:// guard is verified separately.
    "transport/sse.js",
}

# The Mode A transport. Exempt from the fetch ban, but its guard is verified.
MODE_A_TRANSPORT = "transport/sse.js"

ASSET_RE = re.compile(r"""(?:src|href)\s*=\s*["']([^"']+)["']""", re.IGNORECASE)
SCRIPT_RE = re.compile(r"""<script[^>]*\bsrc\s*=\s*["']([^"']+)["']""", re.IGNORECASE)
MODULE_RE = re.compile(r"""type\s*=\s*["']module["']""", re.IGNORECASE)
REMOTE_RE = re.compile(r"^(?:[a-z][a-z0-9+.-]*:)?//", re.IGNORECASE)

BANNED_PATTERNS: List[Tuple[str, str]] = [
    (r"^\s*import\s+[\w{*]", "ES module `import` - blocked on a file:// origin"),
    (r"^\s*export\s+(?:default|const|function|class|\{)", "ES module `export`"),
    (r"\bfetch\s*\(", "fetch() - blocked on a file:// origin"),
    (r"\bXMLHttpRequest\b", "XMLHttpRequest - blocked on a file:// origin"),
    (r"\bimport\s*\(", "dynamic import() - blocked on a file:// origin"),
]


def mode_b_path() -> List[str]:
    """Every local .js the offline pages load, in the order they load it."""
    scripts: List[str] = []
    for page in OFFLINE_PAGES:
        page_path = os.path.join(WEB_ROOT, page)
        if not os.path.isfile(page_path):
            continue
        with open(page_path, "r", encoding="utf-8") as handle:
            html = handle.read()
        for ref in SCRIPT_RE.findall(html):
            if REMOTE_RE.match(ref) or ref.startswith("data:"):
                continue
            if ref in MODE_B_EXEMPT or ref in scripts:
                continue
            scripts.append(ref)
    return scripts


def check_pages() -> List[str]:
    problems: List[str] = []
    for page in OFFLINE_PAGES:
        page_path = os.path.join(WEB_ROOT, page)
        if not os.path.isfile(page_path):
            problems.append("%s: missing" % page)
            continue
        with open(page_path, "r", encoding="utf-8") as handle:
            html = handle.read()

        if MODULE_RE.search(html):
            problems.append('%s: uses type="module", which a file:// origin blocks' % page)

        for ref in ASSET_RE.findall(html):
            if ref.startswith("#") or ref.startswith("data:"):
                continue
            if REMOTE_RE.match(ref):
                problems.append("%s: references a remote asset %r" % (page, ref))
                continue
            if ref.startswith("/"):
                problems.append(
                    "%s: references %r by absolute path, which resolves to the "
                    "filesystem root under file://" % (page, ref)
                )
                continue
            target = os.path.normpath(os.path.join(WEB_ROOT, ref))
            if not os.path.isfile(target):
                problems.append("%s: references %r which does not exist" % (page, ref))
    return problems


def check_sources() -> List[str]:
    problems: List[str] = []
    sources = mode_b_path()
    if not sources:
        return ["no scripts found in the offline pages - the check is not checking"]
    for rel in sources:
        path = os.path.join(WEB_ROOT, rel)
        if not os.path.isfile(path):
            problems.append("%s: missing from the Mode B path" % rel)
            continue
        with open(path, "r", encoding="utf-8") as handle:
            lines = handle.readlines()
        for number, line in enumerate(lines, 1):
            # Comments describe these hazards at length; only real code counts.
            stripped = line.strip()
            if stripped.startswith("*") or stripped.startswith("//"):
                continue
            for pattern, why in BANNED_PATTERNS:
                if re.search(pattern, line):
                    problems.append("%s:%d: %s\n      %s" % (rel, number, why, stripped))
    return problems


def check_mode_a_guard() -> List[str]:
    """The Mode A transport may use fetch, but must never reach it from file://."""
    path = os.path.join(WEB_ROOT, MODE_A_TRANSPORT)
    if not os.path.isfile(path):
        return ["%s: missing" % MODE_A_TRANSPORT]
    with open(path, "r", encoding="utf-8") as handle:
        source = handle.read()
    if "'file:'" not in source and '"file:"' not in source:
        return [
            "%s: uses fetch but has no file: protocol guard, so opening play.html "
            "from disk would throw" % MODE_A_TRANSPORT
        ]
    return []


def main() -> int:
    problems = check_pages() + check_sources() + check_mode_a_guard()
    if problems:
        print("file:// reachability check FAILED\n", file=sys.stderr)
        for problem in problems:
            print("  - %s" % problem, file=sys.stderr)
        print(
            "\nMode B must be playable by double-clicking web/play.html with no "
            "server, no build step and no network.",
            file=sys.stderr,
        )
        return 1

    print(
        "file:// reachability OK: %d pages, %d source files, all assets local and "
        "present, no module syntax, no fetch/XHR on the Mode B path"
        % (len(OFFLINE_PAGES), len(mode_b_path()))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
