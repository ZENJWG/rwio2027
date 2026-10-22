#!/usr/bin/env python3
"""Catch the duplicate collision-struct definitions seen with librwgta."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
header = (ROOT / "src/dffimp.h").read_text(encoding="utf-8")
assert re.search(r"(?m)^\s*#pragma\s+once\s*$", header), "dffimp.h needs an include guard"
assert re.search(r"(?m)^\s*#\s*include\s*[<\"]rwgta\.h[>\"]", header), "rwgta.h is required"
assert not re.search(r"(?m)^\s*#\s*include\s*[<\"]collisions\.h[>\"]", header), (
    "Do not include collisions.h separately: upstream rwgta.h already includes it "
    "and collisions.h is not include-guarded (MSVC C2011)."
)
print("PASS: collision struct headers are not redundantly included")
