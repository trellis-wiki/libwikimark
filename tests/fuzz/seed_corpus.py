#!/usr/bin/env python3
"""
Generate a seed corpus for the libwikimark fuzzer from the spec
test suite. Each test's 'markdown' field becomes one corpus file.

Usage:
    python3 seed_corpus.py <output-dir>
"""

import hashlib
import json
import sys
from pathlib import Path

TESTS_DIR = Path(__file__).resolve().parent.parent.parent.parent / "wikimark" / "tests"

SUITE_FILES = [
    "wikimark-spec.json",
    "wikimark-extra.json",
    "upstream/commonmark-spec.json",
    "upstream/gfm-spec.json",
    "upstream/gfm-extensions.json",
]


def main() -> int:
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <output-dir>", file=sys.stderr)
        return 2

    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)

    count = 0
    for suite_file in SUITE_FILES:
        path = TESTS_DIR / suite_file
        if not path.exists():
            print(f"  skip: {path} (not found)", file=sys.stderr)
            continue
        tests = json.loads(path.read_text())
        for t in tests:
            md = t.get("markdown", "")
            if not md:
                continue
            data = md.encode("utf-8")
            name = hashlib.sha256(data).hexdigest()[:16]
            (out / name).write_bytes(data)
            count += 1

    # Also add a few hand-crafted pathological inputs.
    pathological = [
        # Deeply nested templates
        "{{a " * 50 + "}}" * 50 + "\n",
        # Very long wiki link
        "[[" + "A" * 10000 + "]]\n",
        # Mixed delimiters
        "{{a [[b]] ${c} {d} |e| [[f|g]] }}\n",
        # Frontmatter with deeply nested YAML
        "---\n" + "  a:\n" * 64 + "    x: 1\n" + "---\n\nBody\n",
        # Unclosed everything
        "{{ [[ ${ { | ''' '' `\n",
        # UTF-8 BOM + frontmatter
        "\xef\xbb\xbf---\ntitle: BOM\n---\n\n[[Page]]\n",
    ]
    for i, p in enumerate(pathological):
        (out / f"pathological_{i:03d}").write_bytes(p.encode("utf-8"))
        count += 1

    print(f"Wrote {count} seed files to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
