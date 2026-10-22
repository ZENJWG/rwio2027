# RWIO 2027: source audit and optimization pass

Scope: supplied RWIO project (five C++ import/export modules, plugin/UI code,
five startup MAXScripts, Premake and `build.bat`). This is an **incremental,
source-level stability/performance pass**, not an Autodesk-certified build.

## Changes applied

| Area | Severity | Fix | Effect |
|---|---|---|---|
| `src/main.cpp` | High | Fixed hierarchy qsort comparators so two missing IDs/child numbers compare equal; replaced subtractive comparisons with overflow-safe comparisons | Prevents invalid comparison ordering |
| `src/anim.cpp`, `src/export.cpp` | Medium | Stable child sorting; corrected child name metadata | Equal sorting keys now retain scene child order, improving animation hierarchy predictability |
| `src/anim.cpp` | Medium | Overflow-safe time comparator and guard for exporting an empty hierarchy | Avoids invalid animation construction |
| `src/collision.cpp` | High | Validate geometry before conversion, handle failure, convert each candidate mesh **once**, and release temporary conversions | Avoids crashes on unsupported objects and reduces collision-export conversion overhead |
| `src/collision.cpp` | High | Calculate rotated box AABB using all eight transformed corners | Correct bounds under rotation |
| `src/collision.cpp` | Medium | Null-tolerant collision custom-attribute readers; release model/name on output open failure | Safer error paths |
| `src/geoexp.cpp` | High | Ensure multi/sub-object material list never has zero materials | Avoids `faceMtlIndex % 0` |
| `src/geoexp.cpp` | High | Guard failed/null TriObject conversions in geometry and skin export | Avoids dereferencing bad conversions |
| `src/geoexp.cpp` | Medium | Attach/allocate RW skin only once Max skin context and triangle mesh conversion are verified | Fewer partially initialized skin allocations |
| `src/export.cpp` | Medium | Guard missing/null param blocks and custom-attribute type strings | Avoids crashes on unusual scene metadata |
| `src/export.cpp` | High | Restore RW platform after success **and** early export failures | Prevents subsequent exports/imports inheriting incorrect platform state |
| `src/import.cpp` | Medium | Track ownership of temporary triangulations during skin weight import | Closes a conversion-object leak |
| `src/import.cpp` | Performance | Spatial hash for skin nearest-vertex lookup; preserve old linear nearest fallback for unmatched locations | Common-case nearest-vertex matching changes from O(M×N) to near O(M+N) for M Max vertices, N RW vertices |
| `src/import.cpp` | Medium | Unsigned hashing plus finite/range guards for quantization | Avoids integer overflow UB on extreme coordinates |
| `tests/` | Quality | SDK-independent compiled C++ regression tests and embedded-script synchronization check | Repeatable partial validation |

### Tests performed

- `python tests/test_algorithms.py` — **PASS**, compiles extracted comparator,
  quantizer, spatial-index C++ implementation and runs assertions with g++ C++20.
- `python tests/test_project.py` — **PASS**, compares the embedded QuadrifyAll++
  script with the shipped `.ms` script and verifies project output naming.
- ZIP file list and source structure validated.
- Not performed: Windows `MSBuild`, Autodesk SDK linking, loading DLL in 3ds Max,
  real DFF/COL/ANM round trips, UI interaction, viewport comparisons.

### Important known remaining issues / opportunities

1. **Missing optional batch-dialog script:** `src/main.cpp` tries to load
   `rwio_dff_batch.ms`, which does not exist in the supplied archive. Batch
   dialog commands may fail. A real compatible MAXScript implementation is
   needed; it was not invented or added without scene-level testing.
2. **Filename handling:** `getAsciiStr()` and several exporters convert Unicode
   paths using fixed `MAX_PATH` byte buffers and the current locale. Non-ASCII
   and long paths may be truncated or fail. Fixing this properly requires
   coordinated changes to librw StreamFile or an SDK-compatible path I/O adapter.
3. **Global state:** librw's platform/version and some importer material/texture
   state are global. More RAII restoration and concurrency testing would be
   desirable, especially around exceptional/failed writes.
4. **QuadrifyAll++:** The large script is duplicated as an embedded raw string
   in `src/main.cpp`; changes to either copy must remain synchronized. A test
   now detects drift. The destructive modifier/geometry operations still require
   extensive scene-level regression tests before algorithm changes.
5. **Skinned geometry:** The improved vertex nearest-neighbour index preserves
   the old fallback, but overlapping vertices at identical locations can still
   have ambiguous skin weights. A future design could carry exact source-vertex
   mappings throughout mesh generation rather than infer them by position.
6. **Collision bounds:** `calculateBounds` changes the root transform and
   selection to query a scene box. This could have UI/animation side effects;
   consider calculating from evaluated geometry entirely in local coordinates.
7. **DFF vertex deduplication:** `gta::findSAVertex()` may become expensive for
   very large meshes; changing it requires preserving GTA's exact attribute
   equality/mask semantics and is left for a measured, golden-file backed pass.
8. **SDK/dependencies:** `build.bat` remains reliant on access to the official
   Autodesk 3ds Max 2027 SDK. Cloning upstream librw/librwgta from HEAD is not
   reproducible across dates; pin tested commits after a successful Windows build.
9. **Crash and fidelity tests:** Verify materials with zero submaterials,
   broken/non-triangulatable geometry, deep hierarchies with unset `childNum`,
   rotated COL boxes, skinned characters, large vertex counts, and DFF/ANM
   import-export fidelity against reference assets.

## Suggested Windows verification checklist

1. Back up your original RWIO directory. Do not install this experimental
   plugin over a known working `.dli` without a backup.
2. Extract updated archive and run `build.bat` using VS 2026 and the licensed
   3ds Max 2027 SDK.
3. Open 3ds Max 2027 with a clean test scene. Check plugin registration and
   MAXScript startup output. Test importing/exporting DFF, ANM, and COL.
4. Use representative reference assets and compare exported binary/model
   results (positions, UVs, vertex colors, normals, materials, bone IDs/weights).
5. Test twice in the same Max session after a deliberately failing export to
   catch leaked RW global state; test rotated collision boxes and scenes with
   many skinned vertices.
6. Time model import and skin-weight assignment on large characters against
   the original plugin. Performance gains are theoretical until measured in Max.

Do not claim full runtime compatibility based on these isolated tests alone.

## 2026-10-22: Remove redundant librwgta collision header

- Eliminated duplicate `<collisions.h>` inclusion from `src/dffimp.h`,
  which caused C2011 collision structure redefinitions when using
  the current upstream `librwgta`.
- Added a header guard via `#pragma once`.
- Added `tests/test_collision_header.py` to catch this recurrence.
- Both library dependencies now build according to the submitted
  Windows log; final plugin compilation remains to be verified.
