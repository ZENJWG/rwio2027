#!/usr/bin/env python3
'''Source-level compatibility checks; does not execute PowerShell/Premake.'''
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
patch = (ROOT / "tools/ensure-premake-release.ps1").read_text(encoding="utf-8")
bat = (ROOT / "build.bat").read_text(encoding="utf-8")
assert "legacyPattern" in patch
assert "winMainPattern" in patch
assert 'entrypoint "WinMainCRTStartup"' in patch
assert 'staticruntime "On"' in patch
assert 'configurations { "Release", "Debug", "ReleaseStatic" }' in patch
assert "Premake compatibility already applied" in patch
assert "ensure-premake-release.ps1" in bat
assert "Release|win-amd64-null" in (ROOT / "tools/build-dependency.ps1").read_text()

upstream = (
    'workspace "librwgta"\n'
    '    configurations { "Release", "Debug" }\n'
    '    filter { "system:windows" }\n'
    '        configurations { "ReleaseStatic" }\n'
    '    filter "configurations:ReleaseStatic"\n'
    '        flags { "StaticRuntime" }\n'
    'project "selanna3d"\n'
    '    kind "WindowedApp"\n'
    '    flags { "WinMain" }\n'
    'project "lcsview"\n'
    '    kind "WindowedApp"\n'
    '    flags { "WinMain" }\n'
)
cfg = r'(?m)^([ \t]*)configurations\s*\{\s*"ReleaseStatic"\s*\}'
legacy = r'''(?m)^([ \t]*)flags\s*\{\s*["']StaticRuntime["']\s*\}'''
winmain = r'''(?m)^([ \t]*)flags\s*\{\s*["']WinMain["']\s*\}'''
def patch_reference(source):
    source = re.sub(cfg, r'\1configurations { "Release", "Debug", "ReleaseStatic" }', source)
    source = re.sub(legacy, r'\1staticruntime "On"', source)
    source = re.sub(winmain, r'\1entrypoint "WinMainCRTStartup"', source)
    return source

fixed = patch_reference(upstream)
assert 'configurations { "Release", "Debug", "ReleaseStatic" }' in fixed
assert 'staticruntime "On"' in fixed
assert 'flags { "StaticRuntime" }' not in fixed
assert 'flags { "WinMain" }' not in fixed
assert fixed.count('entrypoint "WinMainCRTStartup"') == 2
assert patch_reference(fixed) == fixed
print("PASS: Premake compatibility patch structure and reference patterns")
