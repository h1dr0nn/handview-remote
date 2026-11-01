#!/usr/bin/env python3
"""Print the CHANGELOG.md section for one version (used as the release body).

    python tools/package/changelog_section.py 0.1.0 [--changelog CHANGELOG.md]

Exits 1 with a message when the section is missing or empty.
"""

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def extract_section(text: str, version: str) -> str:
    lines = text.splitlines()
    heading = re.compile(r"^## \[" + re.escape(version) + r"\]")
    any_heading = re.compile(r"^## \[")
    link_ref = re.compile(r"^\[[^\]]+\]:\s")
    start = next((i for i, line in enumerate(lines) if heading.match(line)), None)
    if start is None:
        raise ValueError(f"no '## [{version}]' section in the changelog")
    body = []
    for line in lines[start + 1:]:
        if any_heading.match(line):
            break
        body.append(line)
    while body and (not body[-1].strip() or link_ref.match(body[-1])):
        body.pop()
    section = "\n".join(body).strip()
    if not section:
        raise ValueError(f"the '## [{version}]' section is empty")
    return section + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("version", help="version without the leading v, e.g. 0.1.0")
    parser.add_argument("--changelog", default=str(REPO_ROOT / "CHANGELOG.md"))
    args = parser.parse_args()
    try:
        text = Path(args.changelog).read_text(encoding="utf-8")
        sys.stdout.write(extract_section(text, args.version))
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
