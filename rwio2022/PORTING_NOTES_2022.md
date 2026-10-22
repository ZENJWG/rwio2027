# RWIO 2022 compatibility port — build/runtime validation required

> **UNTESTED IN 3DS MAX 2022.** The author does not have 3ds Max 2022 installed. This source port has not been compiled against the Autodesk 2022 SDK, loaded in 3ds Max 2022, or used for import/export roundtrips there. The supplied source-level tests cannot substitute for those checks.

## Port decisions

| Concern | Port behavior |
|---|---|
| SDK/ABI | Target Autodesk R24 (MAX_RELEASE=24000), hard fail if other SDK selected |
| Compiler | MSVC **v141** toolset (not v145); VS 2026 may lack v141 |
| Windows SDK | Prefer 10.0.17134.0; optional override is unverified |
| Output | `rwio2022.dli`, separate from the 2027 plugin |
| Dependencies | Rebuild librw/librwgta for x64 `win-amd64-null` Release under v141 |
| Runtime | `/MD` shared CRT for all three libraries |
| Material import | `RW_Mtl` by default. Standard material is available for basic diffuse/alpha textures |
| Missing RW Material | Fallback to Standard instead of dereferencing a missing scripted material |
| OpenPBR | Native OpenPBR absent in 2022; newer-Max converter moved out of startup |
| MAXScript alias | `dffImp.importOpenPBRMaterials` retained only as a **Standard material** alias |
| QuadrifyAll++ | Embedded and startup script identical; still requires 2022 runtime tests |
| Batch tool | New optional startup script implements UI entry points referenced in C++ plugin |

### Source-level checks run outside Max

`tests/test_project.py`, `tests/test_build_compat.py`,
`tests/test_collision_header.py`, `tests/test_algorithms.py`,
and `tests/test_max2022_port.py`.

### Essential acceptance testing on a 2022 host

1. Build with **2022** SDK and MSVC v141; resolve any SDK API/compiler errors.
2. Place `rwio2022.dli` under Max 2022 plugins; confirm it loads without missing DLL errors.
3. Start Max 2022 with the included startup MAXScripts and check Listener for exceptions.
4. Import small/large DFFs with texture, UV, prelight, night colors, and normals.
5. Import with RW material and Standard material modes; verify expected difference in MatFX fidelity.
6. Test QuadrifyAll++ and saved quad data with topology, UV seams, skin weights, normals, and materials.
7. Test DFF export and reimport; compare faces, maps, hierarchy, collision, animation, and skinning.
8. Test COL/ANM import/export, empty and malformed assets, and all batch dialogs on disposable files.
9. Check viewport materials with missing textures; test undo/redo and scene reload.
10. Confirm release plugin output is named `rwio2022.dli` and not mixed with `rwio2027.dli`.

If any step fails, consider 2022 unsupported until it is corrected; passing static tests is not a guarantee.

## Official references

- Autodesk 2022 SDK requirements: https://help.autodesk.com/cloudhelp/2022/ENU/Max-Developer-Help/about_the_3ds_max_sdk/sdk_requirements.html
- Autodesk 2022 API revision: https://help.autodesk.com/cloudhelp/2022/ENU/Max-Developer-Help/cpp_ref/group___version_macros.html
- Autodesk 2022 porting: https://blog.autodesk.io/porting-a-c-plugin-to-3ds-max-2022-and-installing-the-3ds-max-2022-c-wizard/
- Autodesk SDK download: https://aps.autodesk.com/developer/overview/3ds-max-api
- Native OpenPBR introduced in 2025.3: https://help.autodesk.com/cloudhelp/2026/ENU/AR-3DSMax/files/arnold-3dsmax/ax-release-notes/ax-5x/arnold_for_3ds_max_5750_html.html
