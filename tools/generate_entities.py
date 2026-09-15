#!/usr/bin/env python3
"""Generate the HTML named character reference table.

Downloads entities.json from the WHATWG HTML spec, parses every named
character reference (including legacy semicolon-less names), sorts the
names byte-wise, and produces src/core/entities/entities_table.cpp
holding an EntityEntry array for longest-match binary-search decoding.

Usage:
    python tools/generate_entities.py             # use cached or download
    python tools/generate_entities.py --download  # force re-download
"""

import argparse
import json
import os
import urllib.request

CACHE_DIR = os.path.join(os.path.dirname(__file__), "entities")
OUTPUT = os.path.join(os.path.dirname(__file__),
                      os.pardir, "src", "core", "entities",
                      "entities_table.cpp")

ENTITIES_URL = "https://html.spec.whatwg.org/entities.json"
MAX_CODE_POINTS = 2


def download_entities(force: bool = False) -> str:
    path = os.path.join(CACHE_DIR, "entities.json")
    if not force and os.path.exists(path):
        return path

    print(f"  Downloading {ENTITIES_URL}...")
    try:
        urllib.request.urlretrieve(ENTITIES_URL, path)
    except Exception as e:
        if os.path.exists(path):
            print(f"  Download failed ({e}), using cached {path}")
            return path
        raise
    return path


def parse_entries(path: str):
    """Return (name, codepoints) tuples sorted by name; names lack '&'."""
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)

    entries = []
    for key, value in data.items():
        assert key.startswith("&"), key
        codepoints = value["codepoints"]
        assert 1 <= len(codepoints) <= MAX_CODE_POINTS, key
        entries.append((key[1:], codepoints))

    entries.sort()
    return entries


def fmt_entry(name: str, codepoints) -> str:
    padded = list(codepoints) + [0] * (MAX_CODE_POINTS - len(codepoints))
    cps = ", ".join(f"0x{cp:x}" if cp else "0" for cp in padded)
    return f'    {{"{name}", {{{cps}}}, {len(codepoints)}}},' 


def generate_cpp(entries) -> str:
    max_name_len = max(len(name) for name, _ in entries)
    out = [
        "// Generated from the WHATWG named character reference data",
        "// (https://html.spec.whatwg.org/entities.json). Do not edit.",
        '#include "guchho/entities.hpp"',
        "",
        "namespace guchho::entities {",
        "",
        f"const size_t kEntityCount = {len(entries)};",
        f"const size_t kMaxEntityNameLength = {max_name_len};",
        f"const size_t kMaxEntityCodePoints = {MAX_CODE_POINTS};",
        "",
        "const EntityEntry kEntities[kEntityCount] = {",
    ]
    out.extend(fmt_entry(name, cps) for name, cps in entries)
    out.append("};")
    out.append("")
    out.append("}  // namespace guchho::html")
    out.append("")
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(
        description="Generate HTML entity table for C++"
    )
    parser.add_argument("--download", action="store_true",
                        help="Force re-download entities.json")
    args = parser.parse_args()

    os.makedirs(CACHE_DIR, exist_ok=True)
    path = download_entities(force=args.download)
    print(f"  Cached at {path}")

    entries = parse_entries(path)
    cpp = generate_cpp(entries)

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(cpp)
    max_name_len = max(len(name) for name, _ in entries)
    print(f"Wrote {os.path.normpath(OUTPUT)} "
          f"({len(entries)} entries, max name length {max_name_len})")


if __name__ == "__main__":
    main()
