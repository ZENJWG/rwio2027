# RWIO 2027 / Visual Studio 2026 — build fixes

## Latest fix (librwgta Premake `WinMain`)

The newest Windows build log now gets past the previous `StaticRuntime`
error. `librw` compiles and writes `rw.lib`, but Premake aborts before
generating the `librwgta` solution:

```
[2/3] Building librwgta (64-bit, null backend, Release)...
Error: invalid value 'WinMain' for flags
```

Upstream `librwgta/premake5.lua` uses `flags { "WinMain" }` in several
unrelated Windows GUI tool projects. Even though RWIO only needs the
`librwgta` static library, Premake parses these project definitions and
rejects the obsolete flag.

`tools/ensure-premake-release.ps1` now converts each instance to
`entrypoint "WinMainCRTStartup"` before generation. The CRT-aware entry point
preserves the GUI applications' intended WinMain entry behavior, rather than
skipping C/C++ runtime initialization. The change is idempotent and works on
both already-patched and freshly cloned dependencies.

The existing Release configuration and `StaticRuntime` compatibility patches
remain unchanged. Do not remove `deps`, and do not reinstall Visual Studio or
Autodesk SDK.

## Installation

Extract the full ZIP into your RWIO project, or copy `build.bat` and
`tools/ensure-premake-release.ps1` from the smaller patch ZIP.
Run `./build.bat` from PowerShell. Confirm the builder prints:

```
Replaced 7 unsupported WinMain flags: ...\librwgta\premake5.lua
```

(Upstream currently contains seven such entries; the exact count may change.)

**Validation:** Structural Python regression tests passed, including patch
idempotence and multiple occurrences. The Windows executable/SDK build still
needs to run on your computer.

---


## Latest fix (librwgta Premake `StaticRuntime`)

The log shows `librw` **already compiled successfully**, producing
`deps\librw\lib\win-amd64-null\Release\rw.lib`. The second dependency fails
*during Premake generation*, before MSBuild:

```
[2/3] Building librwgta (64-bit, null backend, Release)...
Error: invalid value 'StaticRuntime' for flags
```

The current upstream `librwgta/premake5.lua` uses `flags { "StaticRuntime" }`
inside `filter "configurations:ReleaseStatic"`. This legacy flag is invalid for
the bundled Premake 5 binary. The `tools/ensure-premake-release.ps1` helper now
**also** replaces this legacy flag with `staticruntime "On"`.

The helper remains idempotent: rerunning `build.bat` handles already-patched
`deps` directories as well as fresh clones. It applies the adjustment to the
dependencies' local `premake5.lua` files, not the RWIO plugin's Premake file.

`ReleaseStatic` retains the static runtime requested upstream; RWIO continues to
select `Release|win-amd64-null` to avoid mixing /MT and /MD. No SDK or Visual
Studio reinstall is necessary. **Keep the existing `deps` directory.**

## Previous fix (solution configuration mapping)

The prior builder incorrectly searched a `.vcxproj` for a raw solution config
`Release|win-amd64-null`. Premake maps that into a project configuration such as
`Release win-amd64-null|x64`. The builder now reads the solution configurations
and maps/builds the named target through MSBuild on the generated solution.

## Run

Extract the full ZIP and replace the project files, or copy its `build.bat`
and `tools/` folder to the existing project. From PowerShell:

```powershell
.\build.bat
```

If dependency downloads already exist in `deps/`, the script reuses them.

## Validation / limitations

- Verified source ZIP structure and content consistency checks.
- Verified the patched helper recognizes the exact upstream legacy flag and
  can leave an already-correct config untouched.
- A full VS 2026 + 3ds Max 2027 SDK compilation requires a Windows machine and
  **has not been run in this environment**.
- If MSBuild fails in phase 2 or 3, please provide the first `error Cxxxx` or
  `LNKxxxx` line along with a few preceding lines; third-party warnings alone
  do not mean the build failed.

## RWIO collision-header C2011 fix (2026-10-22)

Observed in Windows VS 2026 logs after both `rw.lib` and `rwgta.lib` built:
`gta::CSphere`, `gta::CBox`, `gta::CColModel`, etc. were
redefined from upstream `librwgta/src/collisions.h`, followed by
C2027, C7732, and C3078 cascade errors.

Root cause: `src/dffimp.h` included both `<rwgta.h>` and
`<collisions.h>`. Current upstream `rwgta.h` includes `collisions.h`,
and the latter has no header guard.

Fix: Remove the redundant `<collisions.h>` include from `src/dffimp.h`.
Also add `#pragma once` to protect the plugin's own umbrella header.
Leave the downloaded dependency tree unchanged.

No change to `build.bat` is required for this error. Re-run
`build.bat` with the existing dependencies; MSBuild will reuse
up-to-date library objects.

Validation: static project tests and collision-header regression check
pass in a non-Windows environment. Final 3ds Max SDK compilation,
linking and host-runtime tests still require the user's Windows machine.
