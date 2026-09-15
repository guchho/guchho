#!/usr/bin/env python3
"""Generate C++ Unicode range tables for Guchho from Unicode.org UCD files.

This script downloads DerivedCoreProperties.txt for two Unicode versions
(ES5=5.1.0 and ESNext=latest) plus DerivedGeneralCategory.txt for the
printable ranges version, and produces src/core/unicode/range_table.cpp with
five RangeTable constants: four ID_Start/ID_Continue tables and kPrint
(categories L, M, N, P, S plus ASCII space).

Usage:
    python tools/generate_unicode.py                          # use cached or download
    python tools/generate_unicode.py --download               # force re-download
    python tools/generate_unicode.py --unicode-version 16.0.0 # custom ESNext version
"""

import argparse
import os
import re
import sys
import urllib.request
from typing import Dict, List, Tuple

# Configuration constants
CACHE_DIR = os.path.join(os.path.dirname(__file__), "ucd")
OUTPUT = os.path.join(os.path.dirname(__file__),
                      os.pardir, "src", "core", "unicode", "range_table.cpp")

# Unicode version constants
ES5_VERSION = "5.1.0"
ESNEXT_VERSION = "latest"

# Print categories configuration
PRINT_VERSION = "16.0.0"
PRINT_CATEGORIES = [
    "Lu", "Ll", "Lt", "Lm", "Lo",
    "Mn", "Mc", "Me",
    "Nd", "Nl", "No",
    "Pc", "Pd", "Ps", "Pe", "Pi", "Pf", "Po",
    "Sm", "Sc", "Sk", "So",
]

# Unicode Character Database URLs
UCD_URLS = {
    "DerivedCoreProperties": "https://www.unicode.org/Public/{version}/ucd/DerivedCoreProperties.txt",
    "DerivedGeneralCategory": "https://www.unicode.org/Public/{version}/ucd/extracted/DerivedGeneralCategory.txt",
}

# Type alias for Unicode ranges (start, end, stride)
Range = Tuple[int, int, int]


def ucd_path(filename: str, version: str) -> str:
    """Construct the local cache path for a UCD file.
    
    Returns the path where a downloaded UCD file is stored locally.
    
    Input: filename="DerivedCoreProperties", version="5.1.0"
    Output: "tools/ucd/DerivedCoreProperties-5.1.0.txt"
    
    Edge cases:
    - Returns same path regardless of whether file exists
    - Version string is appended directly without validation
    """
    return os.path.join(CACHE_DIR, f"{filename}-{version}.txt")


def download_ucd(filename: str, version: str, force: bool = False) -> str:
    """Download a UCD file from unicode.org, using cache when available.
    
    Downloads the file to local cache directory. If file already exists and
    force=False, returns cached path immediately.
    
    Input: filename="DerivedCoreProperties", version="5.1.0", force=False
    Output: "tools/ucd/DerivedCoreProperties-5.1.0.txt" (cached or newly downloaded)
    
    Edge cases:
    - If download fails but cached file exists, returns cached file
    - If download fails and no cached file, re-raises exception
    - force=True bypasses cache check
    """
    path = ucd_path(filename, version)
    if not force and os.path.exists(path):
        return path

    url = UCD_URLS[filename].format(version=version)
    print(f"  Downloading {url}...")
    try:
        urllib.request.urlretrieve(url, path)
    except Exception as e:
        if os.path.exists(path):
            print(f"  Download failed ({e}), using cached {path}")
            return path
        raise
    return path


def parse_ucd(path: str, property_name: str) -> List[Range]:
    """Parse a UCD file and extract ranges for a specific property.
    
    Reads a Unicode Character Database file and extracts all codepoint ranges
    that have the specified property name.
    
    Input: path="tools/ucd/DerivedCoreProperties-5.1.0.txt", property_name="ID_Start"
    Output: [(0x41, 0x5A, 1), (0x61, 0x7A, 1), ...]  # ASCII letters
    
    Edge cases:
    - Skips empty lines and comment lines (starting with #)
    - Handles both single codepoints (U+0041) and ranges (U+0041..U+005A)
    - Uses error="replace" to handle malformed UTF-8 gracefully
    - Returns empty list if no matches found
    """
    entry_re = re.compile(
        r'^([0-9A-Fa-f]{4,})(?:\.\.([0-9A-Fa-f]{4,}))?\s*;\s*' + property_name + r'\b'
    )
    ranges = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = entry_re.match(line)
            if m:
                lo = int(m.group(1), 16)
                if m.group(2):
                    hi = int(m.group(2), 16)
                else:
                    hi = lo
                ranges.append((lo, hi, 1))
    return ranges


def parse_categories(path: str) -> Dict[str, List[Range]]:
    """Parse a DerivedGeneralCategory file and group ranges by category.
    
    Reads a Unicode General Category file and organizes codepoint ranges
    into a dictionary keyed by category name (e.g., "Lu", "Nd", "Pc").
    
    Input: path="tools/ucd/DerivedGeneralCategory-16.0.0.txt"
    Output: {"Lu": [(0x41, 0x5A, 1)], "Nd": [(0x30, 0x39, 1)], ...}
    
    Edge cases:
    - Skips empty lines and comment lines (starting with #)
    - Handles both single codepoints and ranges
    - Returns empty dict if no categories found
    - Category names are case-sensitive and must match exactly
    """
    entry_re = re.compile(
        r'^([0-9A-Fa-f]{4,})(?:\.\.([0-9A-Fa-f]{4,}))?\s*;\s*([A-Za-z]+)'
    )
    categories: Dict[str, List[Range]] = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = entry_re.match(line)
            if m:
                lo = int(m.group(1), 16)
                hi = int(m.group(2), 16) if m.group(2) else lo
                categories.setdefault(m.group(3), []).append((lo, hi, 1))
    return categories


def merge(ranges: List[Range]) -> List[Range]:
    """Merge adjacent or overlapping ranges into contiguous blocks.
    
    Takes a list of ranges and combines consecutive ranges that are adjacent
    (hi+1 == next_lo) and have the same stride. This reduces the number of
    range entries needed in the final output.
    
    Input: [(0x41, 0x42, 1), (0x43, 0x45, 1), (0x50, 0x52, 1)]
    Output: [(0x41, 0x45, 1), (0x50, 0x52, 1)]
    
    Edge cases:
    - Returns empty list if input is empty
    - Handles single-element lists
    - Preserves stride when merging (must match)
    - Sorts by start codepoint before merging
    """
    if not ranges:
        return ranges
    sorted_ranges = sorted(ranges, key=lambda r: r[0])
    merged = [sorted_ranges[0]]
    for lo, hi, stride in sorted_ranges[1:]:
        prev = merged[-1]
        if lo == prev[1] + 1 and stride == prev[2]:
            merged[-1] = (prev[0], hi, stride)
        else:
            merged.append((lo, hi, stride))
    return merged


def split_r16_r32(ranges: List[Range]) -> Tuple[List[Range], List[Range]]:
    """Split ranges into 16-bit and 32-bit Unicode codepoint groups.
    
    Divides a list of ranges into two groups: ranges that fit within 16 bits
    (codepoints ≤ 0xFFFF) and ranges that require 32 bits (codepoints > 0xFFFF).
    Ranges that span the 16/32-bit boundary are split at 0xFFFF.
    
    Input: [(0x41, 0x5A, 1), (0x10000, 0x1000F, 1)]
    Output: ([(0x41, 0x5A, 1)], [(0x10000, 0x1000F, 1)])
    
    Edge cases:
    - Ranges spanning 0xFFFF are split into two separate ranges
    - Empty input returns two empty lists
    - Preserves stride for each resulting range
    """
    r16 = []
    r32 = []
    for lo, hi, stride in ranges:
        if hi <= 0xFFFF:
            r16.append((lo, hi, stride))
        elif lo > 0xFFFF:
            r32.append((lo, hi, stride))
        else:
            r16.append((lo, 0xFFFF, stride))
            r32.append((0x10000, hi, stride))
    return r16, r32


def compute_latin_offset(r16: List[Range]) -> int:
    """Compute the number of ranges that fit entirely within Latin-1 (0x00-0xFF).
    
    Counts consecutive ranges from the start of r16 where the end codepoint
    is ≤ 0xFF. This offset is used for fast lookup of Latin-1 characters.
    
    Input: [(0x00, 0x7F, 1), (0x80, 0xFF, 1), (0x100, 0x1FF, 1)]
    Output: 2
    
    Edge cases:
    - Returns 0 if first range exceeds 0xFF
    - Returns len(r16) if all ranges fit within 0xFF
    - Stops counting at first range that exceeds 0xFF
    """
    count = 0
    for lo, hi, stride in r16:
        if hi <= 0xFF:
            count += 1
        else:
            break
    return count


def build_table(ranges: List[Range]) -> Tuple[List[Range], List[Range], int]:
    """Process raw ranges into optimized format for C++ output.
    
    Merges adjacent ranges, splits into 16-bit and 32-bit groups, and
    computes the Latin-1 offset for fast character lookup.
    
    Input: [(0x41, 0x42, 1), (0x43, 0x45, 1), (0x10000, 0x1000F, 1)]
    Output: ([(0x41, 0x45, 1)], [(0x10000, 0x1000F, 1)], 0)
    
    Edge cases:
    - Empty input returns ([], [], 0)
    - Handles ranges that need splitting at 16/32-bit boundary
    """
    merged = merge(ranges)
    r16, r32 = split_r16_r32(merged)
    latin = compute_latin_offset(r16)
    return r16, r32, latin


def fmt_r16(lo: int, hi: int, stride: int) -> str:
    """Format a range as a 16-bit C++ initializer list entry.
    
    Converts range tuple to hex-formatted string for Range16 arrays.
    
    Input: lo=0x41, hi=0x5A, stride=1
    Output: "{0x0041, 0x005a, 1},"
    
    Edge cases:
    - Uses 4-digit hex formatting for consistency
    - Preserves lowercase hex digits
    """
    return f"{{0x{lo:04x}, 0x{hi:04x}, {stride}}},"


def fmt_r32(lo: int, hi: int, stride: int) -> str:
    """Format a range as a 32-bit C++ initializer list entry.
    
    Converts range tuple to hex-formatted string for Range32 arrays.
    
    Input: lo=0x10000, hi=0x1000F, stride=1
    Output: "{0x10000, 0x1000f, 1},"
    
    Edge cases:
    - Uses variable-width hex formatting
    - Preserves lowercase hex digits
    """
    return f"{{0x{lo:x}, 0x{hi:x}, {stride}}},"


def emit_array(name: str, ranges: List[Range], fmt) -> str:
    """Generate C++ static constexpr array declaration for range data.
    
    Creates a C++ array declaration with properly formatted range entries.
    The format function determines whether entries are formatted as 16-bit
    or 32-bit values.
    
    Input: name="kIDStartR16", ranges=[(0x41, 0x5A, 1)], fmt=fmt_r16
    Output: "static constexpr Range16 kIDStartR16[] = {\n    {0x0041, 0x005a, 1},\n};"
    
    Edge cases:
    - Empty ranges produces empty array declaration
    - Format function name determines C++ type (Range16 vs Range32)
    - Preserves hex formatting from format function
    """
    out = [f"static constexpr {fmt.__name__.replace('fmt_', '').replace('r16', 'Range16').replace('r32', 'Range32')} {name}[] = {{"]
    for lo, hi, stride in ranges:
        out.append(f"    {fmt(lo, hi, stride)}")
    out.append("};")
    return "\n".join(out)


def generate_cpp(tables: dict, esnext_version: str) -> str:
    """Generate complete C++ source file with Unicode range tables.
    
    Takes processed table data and generates a complete C++ source file with
    static constexpr arrays and extern const RangeTable declarations.
    
    Input: tables={"idStartES5": (r16, r32, latin)}, esnext_version="latest"
    Output: Complete C++ source code string
    
    Edge cases:
    - Empty tables produces minimal valid C++ file
    - Handles both empty R16 and R32 arrays
    - Preserves proper C++ syntax and formatting
    - Includes necessary headers and namespace declarations
    """
    out = [
        '// Automatically generated by tools/generate_unicode.py.',
        f'// ES5 (Unicode {ES5_VERSION}) + ESNext (Unicode {esnext_version}) ID_Start/ID_Continue;',
        f'// kPrint = printable ranges (categories L/M/N/P/S + U+0020, Unicode {PRINT_VERSION}).',
        '// Do not edit.',
        '#include "guchho/unicode.hpp"',
        '',
        'namespace guchho::unicode {',
        '',
    ]

    # Emit static constexpr arrays for each table
    for name, (r16, r32, latin) in tables.items():
        s = name
        if s.startswith('id'):
            cname = f"kID{s[2:]}"
        else:
            cname = f"k{s[0].upper()}{s[1:]}"
        if r16:
            out.append(emit_array(f"{cname}R16", r16, fmt_r16))
        else:
            out.append(f"static constexpr Range16 {cname}R16[] = {{}};")
        out.append("")
        if r32:
            out.append(emit_array(f"{cname}R32", r32, fmt_r32))
        else:
            out.append(f"static constexpr Range32 {cname}R32[] = {{}};")
        out.append("")

    # Emit extern const RangeTable declarations
    for name, (r16, r32, latin) in tables.items():
        s = name
        if s.startswith('id'):
            cname = f"kID{s[2:]}"
        else:
            cname = f"k{s[0].upper()}{s[1:]}"
        out.append(
            f"extern const RangeTable {cname} = {{\n"
            f"    {latin},\n"
            f"    {cname}R16,\n"
            f"    {len(r16)},\n"
            f"    {cname}R32,\n"
            f"    {len(r32)},\n"
            f"}};"
        )
        out.append("")

    out.append("}")
    out.append("")
    return "\n".join(out)


def main():
    """Main entry point for the Unicode table generator.
    
    Parses command-line arguments, downloads UCD files, processes them into
    range tables, and generates the C++ output file.
    
    Input: Command-line arguments (optional)
    Output: Writes range_table.cpp to src/core/unicode/
    
    Edge cases:
    - Handles download failures gracefully (exits with error)
    - Uses cached files when available
    - Supports custom Unicode versions via command-line
    - Creates output directory if it doesn't exist
    """
    # Parse command-line arguments
    parser = argparse.ArgumentParser(
        description="Generate Unicode range tables for C++"
    )
    parser.add_argument("--download", action="store_true",
                        help="Force re-download UCD files")
    parser.add_argument("--unicode-version", default=None,
                        help="ESNext Unicode version (e.g. 16.0.0)")
    args = parser.parse_args()

    esnext = args.unicode_version or ESNEXT_VERSION

    # Create cache directory if it doesn't exist
    os.makedirs(CACHE_DIR, exist_ok=True)

    # Download required UCD files
    downloads = [
        ("DerivedCoreProperties", "ES5", ES5_VERSION),
        ("DerivedCoreProperties", "ESNext", esnext),
        ("DerivedGeneralCategory", "Print", PRINT_VERSION),
    ]
    for filename, label, version in downloads:
        print(f"Getting {label} (Unicode {version})...")
        try:
            path = download_ucd(filename, version, force=args.download)
        except Exception as e:
            print(f"  Failed to download Unicode {version} {filename}: {e}",
                  file=sys.stderr)
            sys.exit(1)
        print(f"  Cached at {path}")

    # Build ID_Start tables for both ES5 and ESNext
    tables = {}
    for label, version in [("idStartES5", ES5_VERSION), ("idStartESNext", esnext)]:
        path = ucd_path("DerivedCoreProperties", version)
        raw = parse_ucd(path, "ID_Start")
        r16, r32, latin = build_table(raw)
        tables[label] = (r16, r32, latin)
        print(f"  {label}: LatinOffset={latin}, R16={len(r16)}, R32={len(r32)}")

    # Build ID_Continue tables for both ES5 and ESNext
    for label, version in [("idContinueES5", ES5_VERSION), ("idContinueESNext", esnext)]:
        path = ucd_path("DerivedCoreProperties", version)
        raw = parse_ucd(path, "ID_Continue")
        r16, r32, latin = build_table(raw)
        tables[label] = (r16, r32, latin)
        print(f"  {label}: LatinOffset={latin}, R16={len(r16)}, R32={len(r32)}")

    # Build print table from general categories
    gc = parse_categories(ucd_path("DerivedGeneralCategory", PRINT_VERSION))
    raw = [(0x20, 0x20, 1)]
    for category in PRINT_CATEGORIES:
        raw.extend(gc.get(category, []))
    r16, r32, latin = build_table(raw)
    tables["print"] = (r16, r32, latin)
    print(f"  print: LatinOffset={latin}, R16={len(r16)}, R32={len(r32)}")

    # Generate and write the C++ output file
    cpp = generate_cpp(tables, esnext)
    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8") as f:
        f.write(cpp)
    print(f"\nWrote {OUTPUT} ({len(cpp)} bytes)")


if __name__ == "__main__":
    main()
