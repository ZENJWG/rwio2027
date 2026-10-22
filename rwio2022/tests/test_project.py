#!/usr/bin/env python3
"""Verify source and bundled startup script remain in sync; no SDK required."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/main.cpp").read_text()
startup = (ROOT / "scripts/startup/rwio_quadrifyall_plus.ms").read_text()
match = re.search(r'LR"RWIOQAP\((.*?)\)RWIOQAP"', source, re.S)
assert match, "Embedded QuadrifyAll++ MAXScript raw string missing"
assert match.group(1).strip() == startup.strip(), (
    "Embedded QuadrifyAll++ and startup .ms drifted; sync them before shipping"
)

# Ensure the output artifact name agrees between the project and the builder.
premake = (ROOT / "premake5.lua").read_text()
build = (ROOT / "build.bat").read_text()
assert 'targetname "rwio2022"' in premake
assert 'targetextension ".dli"' in premake
assert 'rwio2022.dli' in build
print("PASS: embedded MAXScript and external script are synchronized; build output matches")
