#!/usr/bin/env python3
"""Static target/compatibility checks; these do not validate 3ds Max 2022 at runtime."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
pm = (ROOT / "premake5.lua").read_text()
bat = (ROOT / "build.bat").read_text()
hdr = (ROOT / "src/dffimp.h").read_text()
imp = (ROOT / "src/import.cpp").read_text()
rc = (ROOT / "src/dffimp.rc").read_text()
main = (ROOT / "src/main.cpp").read_text()
helper = (ROOT / "tools/build-dependency.ps1").read_text()
finder = (ROOT / "tools/find-v141-msbuild.ps1").read_text()
readme = (ROOT / "README.txt").read_text()

assert 'targetname "rwio2022"' in pm
assert 'project "rwio2022"' in pm
assert 'cppdialect "C++14"' in pm
assert 'toolset "v141"' in pm and 'toolset "v145"' not in pm
assert '10.0.17134.0' in pm
assert 'MAXSDK2022' in pm and 'MAXSDK2027' not in pm
assert 'rwio2022.dli' in bat and 'rwio2027.dli' not in bat
assert 'MAXSDK2022_URL' in bat
assert 'RWIO_NO_INSTALL' in bat
assert '/p:PlatformToolset=v141' in bat and 'v145' not in bat
assert '/p:PlatformToolset=v141' in helper
assert 'v141' in finder and 'Toolset.props' in finder
assert '#pragma once' in hdr
assert 'MAX_RELEASE != 24000' in hdr
assert 'MakeStandardMaterial' in hdr and 'MakeOpenPBRMaterial' not in hdr
assert 'DFFImport::MakeStandardMaterial' in imp
assert 'mat = MakeStandardMaterial(m);' in imp
assert 'Import as Standard Materials' in rc
assert 'Import as OpenPBR Materials' not in rc
assert 'importOpenPBRMaterials' in main, "Preserve setting alias for existing scripts"
assert not (ROOT / "scripts/startup/rw_openpbr_converter.ms").exists()
assert (ROOT / "scripts/optional-newer-max/rw_openpbr_converter.ms").exists()
assert (ROOT / "scripts/startup/rwio_dff_batch.ms").exists()
assert 'RwioDffBatchOpenImportDialog' in (ROOT/"scripts/startup/rwio_dff_batch.ms").read_text()
assert 'RwioDffBatchOpenExportDialog' in (ROOT/"scripts/startup/rwio_dff_batch.ms").read_text()
assert 'UNTESTED' in readme and 'does not have 3ds Max 2022' in readme
assert '2022' in (ROOT/"PORTING_NOTES_2022.md").read_text()
print("PASS: 2022 target, ABI, toolchain, material fallback, scripts, and README constraints")
