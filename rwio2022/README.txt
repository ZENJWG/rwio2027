UNTESTED 3DS MAX 2022 COMPATIBILITY PORT
=======================================
This 2022 port is UNTESTED because the author does NOT have 3ds Max 2022
installed. Static checks do not prove plugin loadability, import/export
correctness, or script UI operation in the 2022 host.

RWIO for 3ds Max 2022
=====================

This is a source port of Aap's RWIO plugin for Autodesk 3ds Max 2022 - forked by Zeneric (aka Tz2times). It
imports and exports RenderWare DFF/COL/animation data used by GTA-era assets,
with local updates for current Max/Visual Studio tooling, 3ds Max 2022 Standard material fallback, RWIO quad display metadata, QuadrifyAll++ post-import cleanup,
and several performance fixes.


Install
-------

Copy the built plugin:

    lib/amd64/Release/rwio2022.dli

to your 3ds Max 2022 plugins folder, for example:

    C:/Program Files/Autodesk/3ds Max 2022/Plugins

If you use a custom plugin folder, add that folder to the 3ds Max plugin paths.

Copy the startup scripts:

    scripts/startup/RW_Material.ms
    scripts/startup/gtahelper.ms
    scripts/startup/rwio_quadrifyall_plus.ms
    scripts/startup/rwio_dff_batch.ms
    scripts/startup/bipedmatch.ms

to your 3ds Max 2022 user startup scripts folder, for example:

    C:/Users/<user>/AppData/Local/Autodesk/3dsMax/2022 - 64bit/ENU/scripts/startup

You can also place them in another Max startup script path if your install uses
shared scripts.

`RW_Material.ms` is required for the custom RW material UI and RW material
import/export metadata.

`gtahelper.ms` is optional and only useful when Kam's `GTA_Material.ms` /
`GTA_Mtl` material is installed.

`rwio_quadrifyall_plus.ms` integrates QuadrifyAll+ v1.2 by Branko Zivkovic with
RWIO-specific improvements by Zeneric. It is used by the importer dialog's
optional `QuadrifyAll++` post-import weld/quadrify pass.

`rwio_dff_batch.ms` provides optional folder-based batch import/export dialogs.
Batch export changes the selection while exporting and restores it afterward.
Use a copy of your scene when testing this unverified helper.

`scripts/optional-newer-max/rw_openpbr_converter.ms` is NOT loaded in 2022;
native OpenPBR_Material was introduced much later and is absent in Max 2022.

`bipedmatch.ms` is an optional Vice City-style biped matching helper.


Build
-----

**IMPORTANT: 2022 IS UNTESTED.** The author does not have 3ds Max 2022
installed, so there has been NO 2022 host/runtime/scene-roundtrip validation.
The C++ port has not been compiled here against the 2022 Autodesk SDK either.

Build from Windows PowerShell:

    .\build.bat

Required for the 2022 target:
- Autodesk 3ds Max 2022 SDK (R24, MAX_RELEASE 24000). A 2027 SDK will not work.
- MSVC **v141** (Visual Studio 2017 platform toolset); use an installation
  exposing v141 via Visual Studio 2017/2019/2022 or VS 2026 *if supported*.
  Visual Studio 2026's default v145 is NOT a substitute.
- Windows 10 SDK 10.0.17134.0, as specified by Autodesk.
- Git and PowerShell to download/build librw and librwgta.
- Bundled premake5.exe.

The builder searches for the actual v141 toolset in installed Visual Studio
instances. It may attempt to install v141 through Visual Studio Installer,
but VS 2026 may not offer it; install an older compatible VS side-by-side if
necessary. Use `set RWIO_NO_INSTALL=1` to disable this installation attempt.

The builder can download the SDK if you provide an authorized HTTPS ZIP URL:

    set MAXSDK2022_URL=https://your-authorized-sdk-download/sdk.zip

Otherwise download the **2022 SDK** from Autodesk's 3ds Max API SDK/tools page,
install/extract it, then set MAXSDK2022 to the folder containing `include`.
Official page: https://aps.autodesk.com/developer/overview/3ds-max-api

Use Windows SDK override `RWIO_WINSDK_VERSION` only if you deliberately accept
untested deviation from the original Autodesk 2022 requirements.

Plugin output (if it compiles):
    lib/amd64/Release/rwio2022.dli

The Python source tests can run on another OS without 3ds Max:
    python tests/test_algorithms.py
    python tests/test_project.py
    python tests/test_build_compat.py
    python tests/test_max2022_port.py

A 2027-built rwio2027.dli is NOT binary compatible with 3ds Max 2022.


QuadrifyAll++
-------------

`QuadrifyAll++` is an optional post-import cleanup pass. It attempts to make
triangle-native RenderWare meshes easier to edit in 3ds Max by welding duplicate
vertices, hiding safe triangle diagonals, and preserving/restoring imported DFF
attributes after the topology pass.

The current importer exposes:

    QuadrifyAll++
        Q++ edge strength

`Q++ edge strength` controls how aggressively QuadrifyAll++ hides triangle
diagonals:

- 0.00 = safest / hides fewer edges
- 0.85 = current default
- 1.00 = permissive
- 2.00 = aggressive / unsafe

Higher values may recover more quad-like display, but they also increase the
risk of hiding structural edges. Lower values are safer for concave, skewed, or
boundary-sensitive geometry.

QuadrifyAll++ is designed to project DFF import attributes only once at the end
of the post-import pass. It preserves or restores:

- explicit normals / normal shading when available
- vertex color / prelight color
- vertex alpha
- illumination-style channel data
- UV map channels
- material IDs
- object transforms
- Editable Poly output


Quad Data Options
-----------------

RWIO includes quad display options for making triangle-native RenderWare meshes
easier to edit in 3ds Max.

`Quad Data` is an importer option. It is only available when the DFF contains
RWIO quad display data. When enabled, RWIO restores the saved Max triangle edge
visibility so imported Editable Poly objects display and edit like the original
quads, while keeping the underlying DFF/RW geometry as triangles. If the DFF has
no quad display data, the checkbox is disabled and its tooltip warns that no
quad data is available.

`Quad Data` is also an exporter option. When enabled, RWIO saves Max triangle
edge visibility as private geometry user data named:

    rwio_quad_edge_visibility

On re-import, RWIO can restore that saved quad display state through the
`Quad Data` importer option. Other RenderWare tools should ignore this metadata
and continue reading the normal triangle geometry.

Developer notes for DFF/RWIO interoperability:

- Store one integer per triangle.
- Store it as geometry user data named `rwio_quad_edge_visibility`.
- Use type `USERDATAINT`.
- The element count must equal `geometry->numTriangles`.
- Each integer stores the triangle's three edge-visibility bits.

Bit layout:

    bit 0 = triangle edge 0 visible
    bit 1 = triangle edge 1 visible
    bit 2 = triangle edge 2 visible

A value of `1` means visible. A value of `0` means hidden. For example:

    7 = all triangle edges visible
    6 = edge 0 hidden
    5 = edge 1 hidden
    3 = edge 2 hidden

This preserves quad display without changing the DFF from triangles into real
quads.


Quad Data in Blender and Other Tools
------------------------------------

3ds Max can represent RWIO quad data as hidden triangle-edge visibility. Blender
and many other DCC tools should treat the same data as quad reconstruction
metadata instead.

For Blender importers, the recommended behavior is:

1. Import the DFF vertices and triangles.
2. Read `rwio_quad_edge_visibility`.
3. Build an adjacency map from unordered triangle edges to triangle users.
4. Find adjacent triangle pairs whose shared edge is hidden on both triangles.
5. Validate that the pair is safe to merge:
   - same material
   - compatible UV seams
   - compatible color/alpha/prelight seams
   - compatible custom split normals
   - non-degenerate, non-crossing quad loop
6. Create one real Blender quad polygon for valid hidden-diagonal pairs.
7. Keep unpaired triangles as triangles.
8. Preserve UVs, colors, alpha, illumination, material IDs, and custom split
   normals per face corner/loop.

For Blender exporters:

- Export Blender triangles as one DFF triangle with all edges visible unless the
  add-on has stored custom RWIO edge flags.
- Export Blender quads as two DFF triangles and write
  `rwio_quad_edge_visibility` so the generated diagonal is hidden on both
  triangles.
- For n-gons, triangulate normally unless the add-on has a reliable custom quad
  pairing map.

Do not use Blender's generic tris-to-quads operator as a substitute for RWIO
quad data. The RWIO hidden-edge flags are the source of truth for original quad
intent.


Vertex Channel Display
----------------------

The importer can enable shaded vertex-channel display on imported nodes. The
dialog checkbox turns display on or off, and the adjacent selector chooses which
Max channel is shown:

- Color
- Illum
- Alpha

RWIO enables this option only when the DFF contains vertex channel data to show:
prelit vertex color/alpha data or Rockstar extra vertex color data. If the DFF
has no vertex color, illumination, or alpha channel data, the checkbox and
selector are disabled and the tooltip warns that there is no channel data to
display.

Vertex-channel display is a viewport/display choice. It does not delete other
imported vertex channels.


Materials in 3ds Max 2022
-------------------------

The importer uses RW_Material by default to preserve RenderWare material
extensions. A fallback "Import as Standard Materials" option creates native
3ds Max Standard (legacy) materials for basic color, alpha, and diffuse textures.

The old MAXScript setting `dffImp.importStdMaterials` continues to work.
`dffImp.importOpenPBRMaterials` remains as a backward-compatibility alias,
but in THIS 2022 port it also selects Standard materials, NOT OpenPBR.

When RW_Material.ms has not been loaded, RWIO tries to create a Standard
material instead of dereferencing an invalid custom-material pointer.
Standard material imports do not retain all RenderWare MatFX/env/spec data.
For highest fidelity, use RW_Material.ms and RW material import.

Native OpenPBR materials are unavailable in 2022. A newer-version converter
is archived in scripts/optional-newer-max/ for reference only; do not copy it
into the 2022 startup scripts folder.


Performance And Memory Notes
----------------------------

The importer reuses BitmapTex instances for repeated RW texture references
within a single import. This reduces duplicate texture objects in Max scenes,
cuts memory use for repeated materials, and avoids unnecessary bitmap setup work
while preserving UV channel behavior.

Mesh channel import uses exact bitwise hash lookups for repeated positions,
normals, UVs, vertex colors, and night colors. This replaces repeated linear
searches and improves large DFF import time without changing generated mesh
data.

The exporter keeps vertex deduplication for normal static geometry, but avoids
merging skinned vertices that come from different original mesh vertices. This
keeps memory efficient for static meshes while preventing skin-weight corruption
on character/rigged meshes.

QuadrifyAll++ uses optimized spatial lookup when projecting original DFF
attributes back onto the quadrified result. This avoids expensive full
corner-to-corner scans on large DFFs. It also skips duplicate normal projection
when attributes already carried projected normals.

Imported geometry is assembled through Max's fast Mesh API internally, then
converted to a real Editable Poly object before it is attached to the scene node.
QuadrifyAll++ also restores its final result as a real Editable Poly object, so
artists should not need to manually reapply Editable Poly after import.

DFF/RW geometry is triangle-native, so exported DFF files are still written as
triangles or triangle strips depending on the export option. RWIO quad display
data only restores/edit-preserves the intended quad display state in tools that
support it; it does not change the underlying RenderWare triangle geometry.


Compatibility Notes
-------------------

RWIO-specific metadata such as `rwio_quad_edge_visibility` is private extension
data. Tools that do not understand it should ignore it and continue reading the
normal DFF triangle geometry.

If another tool wants to interoperate with RWIO quad data, it should preserve the
user data array exactly and should not reorder triangles unless it also remaps
the per-triangle edge visibility array.


Optimization/audit update
-------------------------
See AUDIT_AND_CHANGES.md for source fixes, portable tests, and unresolved issues.
Run Python 3 tests/test_algorithms.py and tests/test_project.py for SDK-independent checks.
These checks do not replace a 3ds Max 2022 runtime regression test.
