#!/usr/bin/env python3
"""Binary comparison helper for Valeton/Ampero CLO research. No third-party dependencies."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

EXPECTED = 0x2288


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def printable_magic(data: bytes) -> str:
    return ''.join(chr(b) if 32 <= b <= 126 else '.' for b in data[:4])


def common_prefix(a: bytes, b: bytes) -> int:
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return i
    return n


def common_suffix(a: bytes, b: bytes) -> int:
    n = min(len(a), len(b))
    for i in range(1, n + 1):
        if a[-i] != b[-i]:
            return i - 1
    return n


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare two fixed-size CLO files byte by byte.")
    parser.add_argument("a", type=Path)
    parser.add_argument("b", type=Path)
    parser.add_argument("--max-diffs", type=int, default=64)
    args = parser.parse_args()

    a = args.a.read_bytes()
    b = args.b.read_bytes()

    for label, path, data in (("A", args.a, a), ("B", args.b, b)):
        print(f"{label}: {path}")
        print(f"  size   : {len(data)} bytes (0x{len(data):X}) {'OK' if len(data) == EXPECTED else '!= 0x2288'}")
        print(f"  magic  : {printable_magic(data)}")
        print(f"  prefix : {data[:16].hex(' ')}")
        print(f"  sha256 : {sha256(data)}")

    n = min(len(a), len(b))
    diffs = [(i, a[i], b[i]) for i in range(n) if a[i] != b[i]]
    diffs.extend((i, a[i] if i < len(a) else None, b[i] if i < len(b) else None)
                 for i in range(n, max(len(a), len(b))))

    print()
    print(f"equal bytes in overlap : {n - sum(1 for i in range(n) if a[i] != b[i])}/{n}")
    print(f"different positions    : {len(diffs)}")
    print(f"common prefix          : {common_prefix(a, b)} bytes")
    print(f"common suffix          : {common_suffix(a, b)} bytes")

    if not diffs:
        print("RESULT: files are byte-for-byte identical.")
        return 0

    print(f"\nFirst {min(args.max_diffs, len(diffs))} differences:")
    for offset, av, bv in diffs[: args.max_diffs]:
        atext = "--" if av is None else f"{av:02X}"
        btext = "--" if bv is None else f"{bv:02X}"
        print(f"  0x{offset:04X}: {atext} -> {btext}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
