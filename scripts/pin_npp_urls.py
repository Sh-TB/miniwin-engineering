#!/usr/bin/env python3
"""pin_npp_urls.py -- re-pin the two Notepad++ manifest rows (SWEEP-094).

Both assets were downloaded and their SHA-256 re-measured byte-for-byte
against the hashes pinned in SWEEP-092 (verification transcript in the
SWEEP-094 issue report). This tool rewrites ONLY those two rows, replacing
the UNVERIFIED_URL placeholder with the exact release-asset URL, and updates
the source_page_url + redistribution_notes. Tab-safe: reads/writes raw text.

Usage: pin_npp_urls.py <manifest.tsv>
Exit codes: 0 ok, 2 row missing / hash mismatch vs expectation.
"""
import sys

# expected pins (verified 2026-10-28 by download + sha256sum)
EXPECTED = {
    "npp.installer.x64": {
        "sha256": "26f3bcced788fadbac4e7e577b430ea2ce6060cf3bcad993ef4612ecd7d743da",
        "url": "https://github.com/notepad-plus-plus/notepad-plus-plus/releases/download/v8.9.8.1/npp.8.9.8.1.Installer.x64.exe",
    },
    "npp.portable.x64": {
        "sha256": "beddf5548e75c97930d1575ba1c7d113e6e2b33d0a200310e734ca08e225f414",
        "url": "https://github.com/notepad-plus-plus/notepad-plus-plus/releases/download/v8.9.8.1/npp.8.9.8.1.portable.x64.zip",
    },
}
PAGE = "https://github.com/notepad-plus-plus/notepad-plus-plus/releases/tag/v8.9.8.1"
NOTE = ("URL re-pinned SWEEP-094: exact asset downloaded, SHA-256 re-measured "
        "and matched the pinned hash byte-for-byte (2026-10-28)")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "tests/corpus/manifest.tsv"
    with open(path, "r", encoding="utf-8", newline="") as f:
        lines = f.read().split("\n")
    fixed = 0
    for i, line in enumerate(lines):
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 13 or cols[0] not in EXPECTED:
            continue
        exp = EXPECTED[cols[0]]
        if cols[6] != exp["sha256"]:
            sys.stderr.write("hash drift for %s: %s\n" % (cols[0], cols[7]))
            return 2
        cols[3] = exp["url"]          # source_url
        cols[4] = PAGE                # source_page_url
        cols[11] = NOTE               # redistribution_notes (0-based: field 12)
        lines[i] = "\t".join(cols)
        fixed += 1
    if fixed != len(EXPECTED):
        sys.stderr.write("expected 2 rows, fixed %d\n" % fixed)
        return 2
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(lines))
    print("pinned %d rows, tabs preserved" % fixed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
