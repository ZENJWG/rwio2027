#!/usr/bin/env python3
"""Portable algorithm tests: compiles extracted RWIO C++ functions with stub SDK types.

Run: python tests/test_algorithms.py
Requires a C++14 compiler (g++/clang++). Does not need the Autodesk SDK.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
main = (ROOT / "src/main.cpp").read_text()
imp = (ROOT / "src/import.cpp").read_text()
anim = (ROOT / "src/anim.cpp").read_text()

def between(source, begin, end):
    return source[source.index(begin):source.index(end, source.index(begin))]

sorts = between(main, "int\nsortByID(", "// Importer")
grid = between(imp, "struct RWIOProjectionGridKey\n", "static std::unordered_map<INode*, RWIOImportedDffAttributeSource>")
skin = between(imp, "struct RwioSkinVertexIndex {", "// Caller must DeleteMe()")
time_sort = between(anim, "int\nsortTime(", "struct NodeFrames")
cxx = r"""
#include <algorithm>
#include <cassert>
#include <cmath>
#include <climits>
#include <cfloat>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include <vector>
struct Node { int id; };
struct SortNode { int id; const char *name; int childNum; Node *node; };
using TimeValue = int;
struct Point3 { float x, y, z; Point3(float a, float b, float c) : x(a), y(b), z(c) {} };
namespace rw {
struct V3d { float x,y,z; };
struct MorphTarget { V3d *vertices; };
struct Geometry { int numVertices; MorphTarget morphTargets[1]; };
}
""" + sorts + time_sort + grid + skin + r"""
int main() {
    SortNode x{-1, "x", -1, nullptr}, y{-1, "y", -1, nullptr};
    assert(sortByID(&x, &y) == 0);
    assert(sortByChildNum(&x, &y) == 0);
    x.id = 2; y.id = 1;
    assert(sortByID(&x, &y) > 0);
    x.childNum = INT_MAX; y.childNum = INT_MIN;
    assert(sortByChildNum(&x, &y) < 0); // missing childNum sorts last
    x.childNum = INT_MAX; y.childNum = INT_MAX-1;
    assert(sortByChildNum(&x, &y) > 0);
    TimeValue a = INT_MAX, b = INT_MIN;
    assert(sortTime(&a, &b) > 0);
    assert(rwioProjectionQuantize(0.0006f) == 1);
    assert(rwioProjectionQuantize(-0.0006f) == -1);
    assert(rwioProjectionQuantize(1e30f) == INT_MAX-2);
    assert(rwioProjectionQuantize(-1e30f) == INT_MIN+2);
    rw::V3d verts[] = {{0,0,0},{1,2,3},{-5,-4,-3},{1,2,3}};
    rw::Geometry g{4, {{verts}}};
    RwioSkinVertexIndex index;
    buildSkinVertexIndex(&g, index);
    assert(findVertex(&g, index, Point3(1,2,3)) == 1);
    assert(findVertex(&g, index, Point3(-5,-4,-3)) == 2);
    assert(findVertex(&g, index, Point3(20,20,20)) >= 0); // fallback
    assert(findVertex(&g, index, Point3(1.0002f,2,3)) == 1);
    return 0;
}
"""
compiler = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
if not compiler:
    raise SystemExit("SKIP: no C++ compiler available")
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "test.cpp"
    binary = Path(tmp) / ("test.exe" if os.name == "nt" else "test")
    path.write_text(cxx)
    subprocess.run([compiler, "-std=c++14", "-O2", "-Wall", "-Wextra",
                    "-o", str(binary), str(path)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: comparator, time, quantization, and skin spatial-index tests")
