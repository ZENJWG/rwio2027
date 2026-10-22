/*
    RWIO QuadrifyAll++ NO-DIALOG SAFE v49k NORMAL SHADING DEFAULTS

    Plugin helper only. Does not create or replace RWIO's dialog.

    v10 fix:
      - stops using blind autoEdge for quad recovery.
      - analyzes adjacent triangle pairs and hides only one chosen diagonal per triangle pair.
      - hard-keeps boundaries for material IDs, smoothing-group splits, angle thresholds, and CPV/prelight differences when accessible.
      - keeps RWIO transform/local-space bake.

    v13 fix:
      - all 0.001 welds are force-weld. Vertices are welded only when attribute maps,
        vertex color/prelight, material IDs, smoothing groups, and face-normal signatures match.
      - topology edge visibility can change, but UV coordinates, prelighting/CPV,
        and shading boundaries are not merged across.

    v26 change:
    v28 redo change:
      - snapshots original imported attribute map channels before any quadrify/weld pass.
      - restores original attribute maps after all quadrify passes finish.
      - v35 defers cached import attribute projection to a C++ post-script pass so projection still works even when MAXScript primitives are not registered/exposed.
*/

format "\n========== RWIO QuadrifyAll++ SAFE V49K NORMAL SHADING DEFAULTS FILEIN START ==========\n"

global QuadifyAll
global RwioQuadrifyAllPlusSelection
global QuadrifyAllPlusSelection
global BgaQuadrifyAllPlusSelection
global RwioQuadrifyAllSelection
global QuadifyAllSelection
global RwioQuadrifyAllPlusRunOnSelection
global RwioQuadrifyAllPlusSelfTest
global RwioQAP_CheckVersion
global getImportedDFFNodes
global selectImportedDFF
global rwioProjectDffNormals
global rwioProjectExplicitNormals
global rwioProjectDffAttributes

global RwioQAP_DeferCachedDffAttributeProjectionToImporter = true

global RwioQAP_Version = "SAFE_V49K_NORMAL_SHADING_DEFAULTS_20260706"
global RwioQAP_LogPath = undefined
global RwioQAP_DebugEnabled = false -- v46: off by default for large DFF speed; set true for troubleshooting
global RwioQAP_WeldThreshold = 0.001
global RwioQAP_AutoEdgeAngle = 24.0
-- Intelligent triangle pairing thresholds.
global RwioQAP_IntelligentMaxNormalAngle = 180.0 -- v25: normal angle not used for edge removal
global RwioQAP_IntelligentMaxDiagonalRatio = 0.85 -- v49h: importer-tweakable edge cleanup strength; lower is safer
global RwioQAP_PrelightTolerance = 0.005
global RwioQAP_DoFullHierarchyPreWeldPass = false -- v24: do NOT convert original imported nodes to Editable_Mesh during prepass
global RwioQAP_DoPreBombWeldVerify = true
global RwioQAP_DoClassicPostBombAutoEdgeThenWeld = false
global RwioQAP_DoIntelligentTrianglePairing = true
global RwioQAP_DoFinalTargetFinalize = false -- legacy weld+autoEdge path, keep disabled
global RwioQAP_DoFinalTargetPostWeld = false -- temp mesh edge recovery happens before swap; final poly weld runs after poly conversion
global RwioQAP_FinalEditablePolyWeld = true -- v25: weld final Editable_Poly vertices at 0.001
global RwioQAP_FinalEditablePolyWeldPasses = 3 -- v49b: reduced from 8
global RwioQAP_PreserveOriginalUVMapsAtEnd = false -- v34: disabled; C++ cached projection replaces slow MAXScript snapshot restore
global RwioQAP_PreserveUVMapStartChannel = -1 -- v34: legacy slow fallback start channel; cached C++ projection is preferred
global RwioQAP_UVRestoreFaceCenterWeight = 0.35 -- helps select original UV sample from the same local surface
global RwioQAP_TopoMaxScore = 62.0 -- v46: stricter edge hiding; avoid removing structural/non-quad edges
global RwioQAP_TopoMaxPrelightPenalty = 9999.0 -- v25 unused
global RwioQAP_TopoSharedDiagonalMinFactor = 0.78 -- v46: shared edge must be diagonal-like, not a structural side
global RwioQAP_TopoOtherDiagonalMinFactor = 0.58 -- v46: opposite diagonal must also look diagonal-like
global RwioQAP_TopoMaxQuadQuality = 42.0 -- v46: reject weak skewed/concave-looking triangle pairs before hiding edges
global RwioQAP_TopoMinConvexCrossFactor = 0.00002 -- v46: reject near-collinear/self-crossing projected quads
global RwioQAP_TopoMaxInternalAngle = 172.0
global RwioQAP_TopoMinInternalAngle = 8.0
global RwioQAP_TopoHardRejectUvSeams = true
global RwioQAP_TopoHardRejectPrelightSeams = false -- v25: prelight projection exists / no prelight edge blocking
global RwioQAP_TopoHardRejectSmoothingSplits = false -- v25: normals/smoothing not used for edge blocking
global RwioQAP_TopoRequireUvQuadDiagonal = false -- v25: UV-space diagonal-like rejection removed
global RwioQAP_TopoUvContinuityTolerance = 0.0005
global RwioQAP_TopoUvQuadMaxQuality = 42.0
global RwioQAP_TopoUvSharedDiagonalMinFactor = 0.78
global RwioQAP_TopoLogRejectedCandidates = false

fn RwioQAP_ToString v = (try(v as string)catch("<toString failed>"))

fn RwioQAP_GetLogPath =
(
    if RwioQAP_LogPath == undefined do
    (
        local base = undefined
        try(base = getDir #temp)catch(base = undefined)
        if base == undefined or base == "" do try(base = getDir #userScripts)catch(base = getDir #maxRoot)
        RwioQAP_LogPath = base + "\\rwio_quadrifyall_plus_v49h_default_normals_dialog.log"
    )
    RwioQAP_LogPath
)

fn RwioQAP_ResetLog =
(
    local p = RwioQAP_GetLogPath()
    try(deleteFile p)catch()
    format "RWIO QuadrifyAll++ v49h default normals dialog log: %\n" p
    p
)

fn RwioQAP_Log msg =
(
    if RwioQAP_DebugEnabled do
    (
        local line = ("[RWIO QA+ SAFEv26 " + (timeStamp() as string) + "] " + msg)
        format "%\n" line
        try
        (
            local paths = #()
            append paths (RwioQAP_GetLogPath())
            local base = undefined
            try(base = getDir #temp)catch(base = undefined)
            if base != undefined and base != "" do
            (
                appendIfUnique paths (base + "\\rwio_quadrifyall_plus_debug.log")
                appendIfUnique paths (base + "\\rwio_quadrifyall_plus_dropin_debug.log")
            )
            for p in paths do
            (
                local f = openFile p mode:"a"
                format "%\n" line to:f
                close f
            )
        )
        catch(format "[RWIO QA+ SAFEv26] Could not write log: %\n" (getCurrentException()))
    )
)

fn RwioQAP_LogException stage node:undefined =
(
    local n = ""
    if node != undefined do n = " node=" + (try(node.name)catch("<bad node>"))
    RwioQAP_Log ("EXCEPTION at " + stage + n + ": " + (getCurrentException() as string))
    local st = ""
    try(st = getCurrentExceptionStackTrace())catch(st = "")
    if st != undefined and st != "" do RwioQAP_Log ("STACK: " + st)
)

fn RwioQAP_ApplyDiagonalRatioPolicyV49E v =
(
    global RwioQAP_IntelligentMaxDiagonalRatio
    global RwioQAP_TopoSharedDiagonalMinFactor
    global RwioQAP_TopoOtherDiagonalMinFactor
    global RwioQAP_TopoMaxQuadQuality
    global RwioQAP_TopoMaxScore

    local vv = RwioQAP_IntelligentMaxDiagonalRatio
    try(vv = v as float)catch()
    if vv < 0.0 do vv = 0.0
    if vv > 2.0 do vv = 2.0

    -- v49f: the raw diagonal mismatch ratio is mathematically capped at 1.0.
    -- Values above 1.0 now mean "more aggressive than permissive": the mismatch
    -- gate is effectively disabled and the earlier diagonal/quality safety gates
    -- are relaxed too.
    RwioQAP_IntelligentMaxDiagonalRatio = vv
    RwioQAP_TopoSharedDiagonalMinFactor = 0.90 - (vv * 0.2666667) -- 0.85 -> 0.70, 1.00 -> 0.633, 2.00 -> 0.367
    RwioQAP_TopoOtherDiagonalMinFactor = 0.72 - (vv * 0.3111111)  -- 0.85 -> 0.487, 1.00 -> 0.409, 2.00 -> 0.098
    RwioQAP_TopoMaxQuadQuality = 30.0 + (vv * 26.6667)           -- 0.85 -> 50,   1.00 -> 56.7, 2.00 -> 83.3
    RwioQAP_TopoMaxScore = 30.0 + (vv * 71.1111)                 -- 0.85 -> 83.3,   1.00 -> 101.1, 2.00 -> 172.2

    if RwioQAP_TopoSharedDiagonalMinFactor < 0.0 do RwioQAP_TopoSharedDiagonalMinFactor = 0.0
    if RwioQAP_TopoOtherDiagonalMinFactor < 0.0 do RwioQAP_TopoOtherDiagonalMinFactor = 0.0

    RwioQAP_Log ("Q++ edge strength policy active: mismatch=" + (vv as string) + " sharedMin=" + (RwioQAP_TopoSharedDiagonalMinFactor as string) + " otherMin=" + (RwioQAP_TopoOtherDiagonalMinFactor as string) + " maxQuadQuality=" + (RwioQAP_TopoMaxQuadQuality as string) + " maxScore=" + (RwioQAP_TopoMaxScore as string))
    vv
)

fn RwioQAP_SetImporterDiagonalRatio v =
(
    RwioQAP_ApplyDiagonalRatioPolicyV49E v
)

fn RwioQAP_Exec symbolName =
(
    local v = undefined
    try(v = execute symbolName)catch(v = undefined)
    v
)

fn RwioQAP_SymbolLine symbolName =
(
    local v = RwioQAP_Exec symbolName
    symbolName + " value=" + (RwioQAP_ToString v) + " classOf=" + (RwioQAP_ToString (try(classOf v)catch(undefined)))
)

fn RwioQAP_DumpEnvironment =
(
    RwioQAP_Log "---------------- environment dump ----------------"
    RwioQAP_Log ("maxVersion=" + (RwioQAP_ToString (try(maxVersion())catch(undefined))))
    RwioQAP_Log ("Version=" + RwioQAP_Version)
    RwioQAP_Log "ConvertToPoly policy: guarded; skipped when node is already Editable_Poly."
    RwioQAP_Log ("Final Editable_Poly weld policy: enabled=" + (RwioQAP_FinalEditablePolyWeld as string) + " passes=" + (RwioQAP_FinalEditablePolyWeldPasses as string) + " threshold=" + (RwioQAP_WeldThreshold as string))
    RwioQAP_Log "v49 policy: project/import attributes once in the final C++ direct-cache projection pass; edge hiding uses importer-tweakable diagonal policy."
    RwioQAP_Log ("LogPath=" + (RwioQAP_GetLogPath()))
    RwioQAP_Log ("FORCE-WELD ALL VERTICES: weld ignores UV/prelight/material/smoothing/normal attribute splits. ")
    RwioQAP_Log ("FullHierarchyPreWeld=" + (RwioQAP_DoFullHierarchyPreWeldPass as string) + " PreBombVerify=" + (RwioQAP_DoPreBombWeldVerify as string) + " ClassicPostBombAutoEdgeThenWeld=" + (RwioQAP_DoClassicPostBombAutoEdgeThenWeld as string) + " FinalTargetFinalize=" + (RwioQAP_DoFinalTargetFinalize as string) + " FinalTargetPostWeld=" + (RwioQAP_DoFinalTargetPostWeld as string) + " WeldThreshold=" + (RwioQAP_WeldThreshold as string) + " AutoEdgeAngle=" + (RwioQAP_AutoEdgeAngle as string) + " IntelligentMaxNormalAngle=" + (RwioQAP_IntelligentMaxNormalAngle as string) + " MaxDiagonalRatio=" + (RwioQAP_IntelligentMaxDiagonalRatio as string) + " sharedMin=" + (RwioQAP_TopoSharedDiagonalMinFactor as string) + " otherMin=" + (RwioQAP_TopoOtherDiagonalMinFactor as string) + " maxQuadQuality=" + (RwioQAP_TopoMaxQuadQuality as string) + " maxScore=" + (RwioQAP_TopoMaxScore as string) + " (importer-tweakable)")
    RwioQAP_Log ("IntentionalTriangleGuard hardUV=" + (RwioQAP_TopoHardRejectUvSeams as string) + " hardPrelight=" + (RwioQAP_TopoHardRejectPrelightSeams as string) + " hardSmoothing=" + (RwioQAP_TopoHardRejectSmoothingSplits as string) + " requireUvQuad=" + (RwioQAP_TopoRequireUvQuadDiagonal as string) + " maxScore=" + (RwioQAP_TopoMaxScore as string) + " sharedEdgeDiaglike=REMOVED uvDiaglike=REMOVED normalsPrelight=IGNORED")
    for s in #("QuadifyAll", "RwioQuadrifyAllPlusSelection", "getImportedDFFNodes", "selectImportedDFF", "Bomb", "bindSpaceWarp", "snapShot", "Unwrap_UVW", "getFaceMatID", "setFaceMatID", "meshop.autoEdge", "meshop.weldVertsByThreshold", "meshop.getMapSupport", "meshop.getMapFace", "meshop.getMapVert", "meshop.getFacesUsingVert", "setEdgeVis", "getEdgeVis", "getFaceNormal", "getFaceSmoothGroup", "Editable_Poly", "Editable_Mesh", "PolyMeshObject", "GeometryClass", "Skin", "rwioProjectDffNormals", "rwioProjectExplicitNormals", "rwioProjectDffAttributes") do RwioQAP_Log (RwioQAP_SymbolLine s)
    RwioQAP_Log "--------------------------------------------------"
)

fn RwioQAP_NodeInfo node =
(
    if not (isValidNode node) then "<invalid node>"
    else
    (
        local c = try(classOf node)catch(undefined)
        local bc = try(classOf node.baseObject)catch(undefined)
        local fc = try(node.faces.count)catch(try(node.mesh.faces.count)catch(-1))
        local vc = try(node.verts.count)catch(try(node.mesh.verts.count)catch(-1))
        local ec = try(node.edges.count)catch(try(node.mesh.edges.count)catch(-1))
        "name='" + node.name + "' class=" + (RwioQAP_ToString c) + " base=" + (RwioQAP_ToString bc) + " faces=" + (fc as string) + " verts=" + (vc as string) + " edges=" + (ec as string)
    )
)

fn RwioQAP_BBoxString node =
(
    local s = "<bbox unavailable>"
    try
    (
        local bb = nodeGetBoundingBox node node.transform
        s = "min=" + (bb[1] as string) + " max=" + (bb[2] as string)
    )
    catch()
    s
)

fn RwioQAP_IsMeshLike node =
(
    local ok = false
    try
    (
        if isValidNode node do
        (
            local bo = try(node.baseObject)catch(undefined)
            ok = (try(isKindOf node GeometryClass)catch(false)) or (bo != undefined and ((try(isKindOf bo Editable_Poly)catch(false)) or (try(isKindOf bo Editable_Mesh)catch(false)) or (try(isKindOf bo PolyMeshObject)catch(false))))
        )
    )
    catch(RwioQAP_LogException "IsMeshLike" node:node)
    ok
)

fn RwioQAP_HasSkin node =
(
    local found = false
    try(for m in node.modifiers do if (try(classOf m == Skin)catch(false)) do found = true)catch()
    found
)

fn RwioQAP_SafeDelete v = (try(delete v)catch(RwioQAP_LogException "SafeDelete"))

fn RwioQAP_CaptureState node =
(
    #(
        try(node.name)catch(""), try(node.material)catch(undefined), try(node.gbufferchannel)catch(undefined),
        try(node.wirecolor)catch(undefined), try(node.showVertexColors)catch(undefined), try(node.vertexColorType)catch(undefined),
        try(node.isHidden)catch(false), try(node.isFrozen)catch(false), try(node.transform)catch(matrix3 1),
        try(node.objectOffsetPos)catch([0,0,0]), try(node.objectOffsetRot)catch(quat 0 0 0 1), try(node.objectOffsetScale)catch([1,1,1])
    )
)

fn RwioQAP_RestoreState node state restoreTransform:true =
(
    if isValidNode node and state != undefined do
    (
        try(node.name = state[1])catch(); try(node.material = state[2])catch(); try(node.gbufferchannel = state[3])catch(); try(node.wirecolor = state[4])catch()
        try(node.showVertexColors = state[5])catch(); try(node.vertexColorType = state[6])catch(); try(node.isHidden = state[7])catch(); try(node.isFrozen = state[8])catch()
        if restoreTransform do
        (
            try(node.transform = state[9])catch(); try(node.objectOffsetPos = state[10])catch(); try(node.objectOffsetRot = state[11])catch(); try(node.objectOffsetScale = state[12])catch()
        )
    )
)

fn RwioQAP_CollectHierarchy node arr =
(
    if isValidNode node do
    (
        local h = try(getHandleByAnim node)catch(undefined)
        local exists = false
        for n in arr where isValidNode n do if (try(getHandleByAnim n)catch(undefined)) == h do exists = true
        if not exists do append arr node
        for c in node.children do RwioQAP_CollectHierarchy c arr
    )
)

fn RwioQAP_CopyMaterialIDs source target =
(
    try
    (
        local sf = try(source.faces.count)catch(0)
        local tf = try(target.faces.count)catch(0)
        if sf == tf and sf > 0 then
        (
            for i = 1 to sf do setFaceMatID target i (getFaceMatID source i)
            RwioQAP_Log ("CopyMaterialIDs OK, faceCount=" + (sf as string))
        )
        else RwioQAP_Log ("CopyMaterialIDs skipped, sourceFaces=" + (sf as string) + " targetFaces=" + (tf as string))
    )
    catch(RwioQAP_LogException "CopyMaterialIDs")
)

fn RwioQAP_GetObjectTM node =
(
    local tm = matrix3 1
    try(tm = node.objectTransform)catch(try(tm = node.transform)catch(tm = matrix3 1))
    tm
)

fn RwioQAP_QuantFloatString v scale:100000.0 =
(
    local q = 0
    try(q = (v * scale) as integer)catch(q = 0)
    q as string
)

fn RwioQAP_QuantPointString p scale:100000.0 =
(
    (RwioQAP_QuantFloatString p.x scale:scale) + "," + (RwioQAP_QuantFloatString p.y scale:scale) + "," + (RwioQAP_QuantFloatString p.z scale:scale)
)

fn RwioQAP_SortUniqueStringArray arr =
(
    local out = #()
    for v in arr do appendIfUnique out (v as string)
    try(sort out)catch()
    out
)

fn RwioQAP_ArrayJoinStrings arr sep:";" =
(
    local s = ""
    for i = 1 to arr.count do
    (
        if i > 1 do s += sep
        s += arr[i]
    )
    s
)

fn RwioQAP_FaceCornerForVert node faceIndex vertIndex =
(
    local corner = 0
    try
    (
        local f = getFace node faceIndex
        if (f[1] as integer) == vertIndex then corner = 1
        else if (f[2] as integer) == vertIndex then corner = 2
        else if (f[3] as integer) == vertIndex then corner = 3
    )
    catch(corner = 0)
    corner
)

fn RwioQAP_VertexAttributeSignature node vertIndex =
(
    -- This is the key protection: two same-position verts are welded only when their
    -- material/smoothing/face-normal/UV/prelight signatures match. This preserves UV seams,
    -- CPV/prelighting seams and normal/shading boundaries.
    local mats = #()
    local smgs = #()
    local normals = #()
    local maps = #()
    try
    (
        local faces = meshop.getFacesUsingVert node vertIndex
        for f in faces do
        (
            try(append mats ((getFaceMatID node f) as string))catch()
            try(append smgs ((getFaceSmoothGroup node f) as string))catch()
            try
            (
                local n = getFaceNormal node f
                if (length n) > 0.000001 do append normals (RwioQAP_QuantPointString (normalize n) scale:1000.0)
            )catch()

            local corner = RwioQAP_FaceCornerForVert node f vertIndex
            if corner > 0 do
            (
                local maxMap = 1
                try(maxMap = (meshop.getNumMaps node) - 1)catch(maxMap = 1)
                if maxMap < 1 do maxMap = 1
                for ch = 0 to maxMap do
                (
                    local supported = false
                    try(supported = meshop.getMapSupport node ch)catch(supported = false)
                    if supported do
                    (
                        try
                        (
                            local mf = meshop.getMapFace node ch f
                            local mi = mf[corner] as integer
                            local mv = meshop.getMapVert node ch mi
                            append maps ((ch as string) + ":" + (RwioQAP_QuantPointString mv scale:100000.0))
                        )catch()
                    )
                )
            )
        )
    )
    catch(RwioQAP_LogException ("VertexAttributeSignature failed vert=" + (vertIndex as string)) node:node)

    local matStr = RwioQAP_ArrayJoinStrings (RwioQAP_SortUniqueStringArray mats)
    local smgStr = RwioQAP_ArrayJoinStrings (RwioQAP_SortUniqueStringArray smgs)
    local nStr = RwioQAP_ArrayJoinStrings (RwioQAP_SortUniqueStringArray normals)
    local mapStr = RwioQAP_ArrayJoinStrings (RwioQAP_SortUniqueStringArray maps)
    "mat{" + matStr + "}|smg{" + smgStr + "}|n{" + nStr + "}|map{" + mapStr + "}"
)

fn RwioQAP_VertexPositionBucketKey node vertIndex =
(
    local p = [0,0,0]
    try(p = getVert node vertIndex)catch()
    -- Bucket scale is intentionally tied to 0.001 weld threshold. Exact DFF duplicates collapse;
    -- unrelated near-but-different vertices stay apart unless both position bucket and attributes match.
    local scale = 1.0 / RwioQAP_WeldThreshold
    (RwioQAP_QuantPointString p scale:scale)
)

struct RwioQAP_AttrWeldGroup (key, bits, count, maxIndex)

fn RwioQAP_CompareAttrWeldGroup a b =
(
    if a.maxIndex > b.maxIndex then -1 else if a.maxIndex < b.maxIndex then 1 else 0
)

fn RwioQAP_WeldMeshNodeVertices node label =
(
    -- v15: FORCE-WELD ALL. This intentionally does NOT check UVs, prelighting/CPV,
    -- material IDs, smoothing groups, or normal signatures. It restores the requested
    -- RWIO/QuadrifyAll+ behavior: every mesh/object gets weldVertsByThreshold 0.001.
    local ok = false
    try
    (
        if not (isValidNode node) do throw "node invalid"
        if not (RwioQAP_IsMeshLike node) do throw "node is not mesh-like"
        local beforeClass = try((classOf node) as string)catch("?")
        local beforeBase = try((classOf node.baseObject) as string)catch("?")
        local beforeVerts = try(node.verts.count)catch(try(node.mesh.verts.count)catch(0))
        local beforeFaces = try(node.faces.count)catch(try(node.mesh.faces.count)catch(0))
        local beforeEdges = try(node.edges.count)catch(try(node.mesh.edges.count)catch(0))
        RwioQAP_Log (label + " FORCE-WELD ALL begin node='" + node.name + "' class=" + beforeClass + " base=" + beforeBase + " faces=" + (beforeFaces as string) + " verts=" + (beforeVerts as string) + " edges=" + (beforeEdges as string) + " threshold=" + (RwioQAP_WeldThreshold as string))

        convertToMesh node
        local vc = try(node.verts.count)catch(0)
        if vc > 0 then
        (
            meshop.weldVertsByThreshold node #{1..vc} RwioQAP_WeldThreshold
            update node
            RwioQAP_Log (label + " meshop.weldVertsByThreshold ALL OK node='" + node.name + "'")
        )
        else
        (
            RwioQAP_Log (label + " FORCE-WELD skipped; zero verts after convertToMesh node='" + node.name + "'")
        )

        local afterClass = try((classOf node) as string)catch("?")
        local afterBase = try((classOf node.baseObject) as string)catch("?")
        local afterVerts = try(node.verts.count)catch(try(node.mesh.verts.count)catch(0))
        local afterFaces = try(node.faces.count)catch(try(node.mesh.faces.count)catch(0))
        local afterEdges = try(node.edges.count)catch(try(node.mesh.edges.count)catch(0))
        RwioQAP_Log (label + " FORCE-WELD ALL END node='" + node.name + "' class " + beforeClass + "/" + beforeBase + " -> " + afterClass + "/" + afterBase + ", faces " + (beforeFaces as string) + " -> " + (afterFaces as string) + ", verts " + (beforeVerts as string) + " -> " + (afterVerts as string) + ", edges " + (beforeEdges as string) + " -> " + (afterEdges as string))
        ok = true
    )
    catch
    (
        RwioQAP_LogException (label + " FORCE-WELD ALL failed") node:node
        ok = false
    )
    ok
)


fn RwioQAP_ClassicBgaAutoEdgeThenWeld node label =
(
    local ok = false
    try
    (
        RwioQAP_Log (label + " CLASSIC BGA finalize begin: " + RwioQAP_NodeInfo node)
        convertToMesh node
        local idList = #()
        local facesList = #()
        try
        (
            for i = 1 to node.faces.count do appendIfUnique idList (getFaceMatID node i)
            sort idList
            if idList.count > 1 do
            (
                for i = 1 to idList.count do append facesList #()
                for i = 1 to node.faces.count do
                (
                    local idx = findItem idList (getFaceMatID node i)
                    if idx > 0 do append facesList[idx] i
                )
            )
        )
        catch(RwioQAP_LogException (label + " classic material-id collect") node:node)

        local ec = try(node.edges.count)catch(0)
        if ec > 0 do meshop.autoEdge node #{1..ec} RwioQAP_AutoEdgeAngle type:#SetClear
        RwioQAP_Log (label + " CLASSIC order step 1: autoEdge 24 type:#SetClear BEFORE weld edges=" + (ec as string))

        RwioQAP_WeldMeshNodeVertices node (label + " CLASSIC order step 2 FORCE-WELD ALL 0.001 AFTER autoEdge")

        if idList.count != 0 and facesList.count != 0 do
        (
            local faceMax = try(node.faces.count)catch(0)
            for i = 1 to facesList.count do
            (
                for j = 1 to facesList[i].count do
                (
                    if facesList[i][j] <= faceMax do setFaceMatID node facesList[i][j] idList[i]
                )
            )
            RwioQAP_Log (label + " CLASSIC material IDs restored groups=" + (idList.count as string))
        )

        RwioQAP_Log (label + " CLASSIC BGA finalize end: " + RwioQAP_NodeInfo node)
        ok = true
    )
    catch
    (
        RwioQAP_LogException (label + " CLASSIC BGA finalize failed") node:node
        ok = false
    )
    ok
)


struct RwioQAP_QuadCandidate
(
    score,
    f1,
    e1,
    f2,
    e2,
    angle,
    ratio,
    materialId,
    quadQuality,
    prelightPenalty,
    smoothingPenalty,
    diagFactor
)

fn RwioQAP_CompareQuadCandidate a b =
(
    if a.score < b.score then -1 else if a.score > b.score then 1 else 0
)

fn RwioQAP_EdgeKey a b =
(
    local mn = if a < b then a else b
    local mx = if a < b then b else a
    (mn as string) + ":" + (mx as string)
)

fn RwioQAP_FaceVertArray node faceIndex =
(
    local f = getFace node faceIndex
    #(f.x as integer, f.y as integer, f.z as integer)
)

fn RwioQAP_LocalEdgeVerts fv edgeIndex =
(
    case edgeIndex of
    (
        1: #(fv[1], fv[2])
        2: #(fv[2], fv[3])
        3: #(fv[3], fv[1])
        default: #(fv[1], fv[2])
    )
)

fn RwioQAP_OppositeVert fv a b =
(
    local r = undefined
    for v in fv where v != a and v != b do r = v
    r
)

fn RwioQAP_FaceNormalSafe node fv =
(
    local n = [0,0,1]
    try
    (
        local p1 = getVert node fv[1]
        local p2 = getVert node fv[2]
        local p3 = getVert node fv[3]
        local c = cross (p2 - p1) (p3 - p1)
        if (length c) > 0.000001 then n = normalize c
    )
    catch()
    n
)

fn RwioQAP_AngleBetweenVecDeg v1 v2 useAbs:false =
(
    local a = 0.0
    try
    (
        if (length v1) > 0.000001 and (length v2) > 0.000001 then
        (
            local d = dot (normalize v1) (normalize v2)
            if useAbs do d = abs d
            if d > 1.0 do d = 1.0
            if d < -1.0 do d = -1.0
            a = acos d
        )
    )
    catch(a = 0.0)
    a
)

fn RwioQAP_FaceNormalAngleDeg n1 n2 =
(
    RwioQAP_AngleBetweenVecDeg n1 n2 useAbs:false
)

fn RwioQAP_SmoothingPenalty node f1 f2 =
(
    local penalty = 0.0
    try
    (
        local s1 = getFaceSmoothGroup node f1
        local s2 = getFaceSmoothGroup node f2
        if s1 != 0 and s2 != 0 then
        (
            if ((bit.and s1 s2) == 0) do penalty = 1.0
        )
    )
    catch(penalty = 0.0)
    penalty
)

fn RwioQAP_LocalIndexOfVert fv vertId =
(
    local idx = 0
    for i = 1 to 3 where fv[i] == vertId do idx = i
    idx
)

fn RwioQAP_MapVertForFaceVert node channel faceIndex localIndex =
(
    local mv = undefined
    try
    (
        local mf = meshop.getMapFace node channel faceIndex
        case localIndex of
        (
            1: mv = mf.x as integer
            2: mv = mf.y as integer
            3: mv = mf.z as integer
        )
    )
    catch(mv = undefined)
    mv
)

fn RwioQAP_MapContinuityPenalty node channel f1 e1 f2 e2 tolerance:0.015 =
(
    -- Returns continuous 0..1-ish penalty, not hard reject. DFF seams/prelight splits can exist
    -- even inside a quad, so this should bias scoring, not blindly keep the diagonal visible.
    local penalty = 0.0
    try
    (
        if (meshop.getMapSupport node channel) then
        (
            local fv1 = RwioQAP_FaceVertArray node f1
            local fv2 = RwioQAP_FaceVertArray node f2
            local ev = RwioQAP_LocalEdgeVerts fv1 e1
            local maxDelta = 0.0
            for sharedVert in ev do
            (
                local li1 = RwioQAP_LocalIndexOfVert fv1 sharedVert
                local li2 = RwioQAP_LocalIndexOfVert fv2 sharedVert
                if li1 > 0 and li2 > 0 do
                (
                    local mv1 = RwioQAP_MapVertForFaceVert node channel f1 li1
                    local mv2 = RwioQAP_MapVertForFaceVert node channel f2 li2
                    if mv1 != undefined and mv2 != undefined do
                    (
                        local c1 = meshop.getMapVert node channel mv1
                        local c2 = meshop.getMapVert node channel mv2
                        local d = length (c1 - c2)
                        if d > maxDelta do maxDelta = d
                    )
                )
            )
            if maxDelta > tolerance do penalty = maxDelta / tolerance
            if penalty > 1.0 do penalty = 1.0
        )
    )
    catch(penalty = 0.0)
    penalty
)

fn RwioQAP_MapSharedEdgeMaxDelta node channel f1 e1 f2 e2 =
(
    local maxDelta = 0.0
    try
    (
        if (meshop.getMapSupport node channel) then
        (
            local fv1 = RwioQAP_FaceVertArray node f1
            local fv2 = RwioQAP_FaceVertArray node f2
            local ev = RwioQAP_LocalEdgeVerts fv1 e1
            for sharedVert in ev do
            (
                local li1 = RwioQAP_LocalIndexOfVert fv1 sharedVert
                local li2 = RwioQAP_LocalIndexOfVert fv2 sharedVert
                if li1 > 0 and li2 > 0 do
                (
                    local mv1 = RwioQAP_MapVertForFaceVert node channel f1 li1
                    local mv2 = RwioQAP_MapVertForFaceVert node channel f2 li2
                    if mv1 != undefined and mv2 != undefined do
                    (
                        local c1 = meshop.getMapVert node channel mv1
                        local c2 = meshop.getMapVert node channel mv2
                        local d = length (c1 - c2)
                        if d > maxDelta do maxDelta = d
                    )
                )
            )
        )
    )
    catch(maxDelta = 0.0)
    maxDelta
)

fn RwioQAP_MapPointForMeshVert node channel faceIndex vertId =
(
    local p = undefined
    try
    (
        if (meshop.getMapSupport node channel) then
        (
            local fv = RwioQAP_FaceVertArray node faceIndex
            local li = RwioQAP_LocalIndexOfVert fv vertId
            if li > 0 do
            (
                local mv = RwioQAP_MapVertForFaceVert node channel faceIndex li
                if mv != undefined do p = meshop.getMapVert node channel mv
            )
        )
    )
    catch(p = undefined)
    p
)

fn RwioQAP_Cross2D a b c =
(
    ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x))
)

fn RwioQAP_UvQuadDiagonalLooksValid node f1 e1 f2 e2 a b c d =
(
    -- If UV support is absent, do not block. If present, require the shared edge to look like
    -- a true quad diagonal in UV space too. Human-made triangulation usually carries different
    -- UV gradients/islands, so this keeps those visible triangles intact.
    local ok = true
    try
    (
        if RwioQAP_TopoRequireUvQuadDiagonal and (meshop.getMapSupport node 1) then
        (
            ok = false
            local ua = RwioQAP_MapPointForMeshVert node 1 f1 a
            local ub = RwioQAP_MapPointForMeshVert node 1 f1 b
            local uc = RwioQAP_MapPointForMeshVert node 1 f1 c
            local ud = RwioQAP_MapPointForMeshVert node 1 f2 d
            if ua != undefined and ub != undefined and uc != undefined and ud != undefined do
            (
                local sideC = RwioQAP_Cross2D ua ub uc
                local sideD = RwioQAP_Cross2D ua ub ud
                if (sideC * sideD) < 0.0 then
                (
                    local diagShared = length (ua - ub)
                    local sideMax = RwioQAP_Max4 (length(uc-ua)) (length(ua-ud)) (length(ud-ub)) (length(ub-uc))
                    local diagFactor = if sideMax > 0.000001 then diagShared / sideMax else 0.0
                    local uvQ = RwioQAP_QuadQuality ua ub uc ud
                    if diagFactor >= RwioQAP_TopoUvSharedDiagonalMinFactor and uvQ <= RwioQAP_TopoUvQuadMaxQuality do ok = true
                )
            )
        )
    )
    catch(ok = true)
    ok
)

fn RwioQAP_LogCandidateReject reason f1 f2 =
(
    if RwioQAP_TopoLogRejectedCandidates do
    (
        try(RwioQAP_Log ("candidate rejected: " + reason + " f1=" + (f1 as string) + " f2=" + (f2 as string)))catch()
    )
)

fn RwioQAP_InteriorAngleDeg pPrev pCur pNext =
(
    RwioQAP_AngleBetweenVecDeg (pPrev - pCur) (pNext - pCur) useAbs:false
)

fn RwioQAP_Max4 a b c d =
(
    local m = a
    if b > m do m = b
    if c > m do m = c
    if d > m do m = d
    m
)

fn RwioQAP_Min4 a b c d =
(
    local m = a
    if b < m do m = b
    if c < m do m = c
    if d < m do m = d
    m
)

fn RwioQAP_QuadOrderStrictConvexV46 pts n =
(
    local ok = false
    try
    (
        if pts.count == 4 then
        (
            local l1 = length (pts[2] - pts[1])
            local l2 = length (pts[3] - pts[2])
            local l3 = length (pts[4] - pts[3])
            local l4 = length (pts[1] - pts[4])
            local scale2 = (l1 + l2 + l3 + l4)
            scale2 = scale2 * scale2
            if scale2 > 0.00000001 and (length n) > 0.000001 then
            (
                local nn = normalize n
                local c1 = dot (cross (pts[2] - pts[1]) (pts[3] - pts[2])) nn
                local c2 = dot (cross (pts[3] - pts[2]) (pts[4] - pts[3])) nn
                local c3 = dot (cross (pts[4] - pts[3]) (pts[1] - pts[4])) nn
                local c4 = dot (cross (pts[1] - pts[4]) (pts[2] - pts[1])) nn
                local eps = scale2 * RwioQAP_TopoMinConvexCrossFactor
                local pos = (c1 > eps and c2 > eps and c3 > eps and c4 > eps)
                local neg = (c1 < -eps and c2 < -eps and c3 < -eps and c4 < -eps)
                ok = pos or neg
            )
        )
    )
    catch(ok = false)
    ok
)

fn RwioQAP_QuadStrictConvexV46 pa pb pc pd n =
(
    local q1 = RwioQAP_QuadOrderStrictConvexV46 #(pc, pa, pd, pb) n
    local q2 = RwioQAP_QuadOrderStrictConvexV46 #(pc, pb, pd, pa) n
    q1 or q2
)

fn RwioQAP_QuadOrderQuality pts =
(
    local q = 99999.0
    try
    (
        local p1 = pts[1]
        local p2 = pts[2]
        local p3 = pts[3]
        local p4 = pts[4]
        local l1 = length (p2 - p1)
        local l2 = length (p3 - p2)
        local l3 = length (p4 - p3)
        local l4 = length (p1 - p4)
        local maxL = RwioQAP_Max4 l1 l2 l3 l4
        local minL = RwioQAP_Min4 l1 l2 l3 l4
        if minL < 0.000001 do return q
        local aspect = maxL / minL
        local a1 = RwioQAP_InteriorAngleDeg p4 p1 p2
        local a2 = RwioQAP_InteriorAngleDeg p1 p2 p3
        local a3 = RwioQAP_InteriorAngleDeg p2 p3 p4
        local a4 = RwioQAP_InteriorAngleDeg p3 p4 p1
        local minA = RwioQAP_Min4 a1 a2 a3 a4
        local maxA = RwioQAP_Max4 a1 a2 a3 a4
        if minA < RwioQAP_TopoMinInternalAngle or maxA > RwioQAP_TopoMaxInternalAngle do return q
        local e1 = p2 - p1
        local e2 = p3 - p2
        local e3 = p4 - p3
        local e4 = p1 - p4
        local par1 = RwioQAP_AngleBetweenVecDeg e1 e3 useAbs:true
        local par2 = RwioQAP_AngleBetweenVecDeg e2 e4 useAbs:true
        local anglePenalty = (abs(a1-90.0) + abs(a2-90.0) + abs(a3-90.0) + abs(a4-90.0)) * 0.08
        local parallelPenalty = (par1 + par2) * 0.35
        local aspectPenalty = if aspect > 1.0 then (aspect - 1.0) * 2.0 else 0.0
        q = anglePenalty + parallelPenalty + aspectPenalty
    )
    catch(q = 99999.0)
    q
)

fn RwioQAP_QuadQuality pa pb pc pd =
(
    local q1 = RwioQAP_QuadOrderQuality #(pc, pa, pd, pb)
    local q2 = RwioQAP_QuadOrderQuality #(pc, pb, pd, pa)
    if q1 < q2 then q1 else q2
)

fn RwioQAP_MakeQuadCandidate node adj =
(
    -- v25: edge-removal analysis uses only material boundary, optional UV seam boundary,
    -- and geometric quad confidence. It intentionally ignores prelight/CPV, smoothing,
    -- normal angle, UV-space diagonal-like, and shared-edge diagonal-like blockers.
    local cand = undefined
    try
    (
        if adj.count == 2 then
        (
            local f1 = adj[1][1]
            local e1 = adj[1][2]
            local f2 = adj[2][1]
            local e2 = adj[2][2]
            local mat1 = try(getFaceMatID node f1)catch(0)
            local mat2 = try(getFaceMatID node f2)catch(0)
            if mat1 != mat2 then
            (
                RwioQAP_LogCandidateReject "material boundary" f1 f2
            )
            else
            (
                local uvDelta = RwioQAP_MapSharedEdgeMaxDelta node 1 f1 e1 f2 e2
                if RwioQAP_TopoHardRejectUvSeams and uvDelta > RwioQAP_TopoUvContinuityTolerance then
                (
                    RwioQAP_LogCandidateReject ("UV seam delta=" + (uvDelta as string)) f1 f2
                )
                else
                (
                    local fv1 = RwioQAP_FaceVertArray node f1
                    local fv2 = RwioQAP_FaceVertArray node f2
                    local ev = RwioQAP_LocalEdgeVerts fv1 e1
                    local a = ev[1]
                    local b = ev[2]
                    local c = RwioQAP_OppositeVert fv1 a b
                    local d = RwioQAP_OppositeVert fv2 a b
                    if c != undefined and d != undefined and c != d then
                    (
                        local pa = getVert node a
                        local pb = getVert node b
                        local pc = getVert node c
                        local pd = getVert node d
                        local n = RwioQAP_FaceNormalSafe node fv1
                        if (length n) < 0.000001 do n = [0,0,1]
                        n = normalize n
                        local sideC = dot (cross (pb - pa) (pc - pa)) n
                        local sideD = dot (cross (pb - pa) (pd - pa)) n
                        if (sideC * sideD) >= 0.0 then
                        (
                            RwioQAP_LogCandidateReject "opposite vertices not across shared edge" f1 f2
                        )
                        else
                        (
                            local diagShared = length (pa - pb)
                            local diagOther = length (pc - pd)
                            local sideMax = RwioQAP_Max4 (length (pc - pa)) (length (pc - pb)) (length (pd - pa)) (length (pd - pb))
                            local sharedFactor = if sideMax > 0.000001 then diagShared / sideMax else 0.0
                            local otherFactor = if sideMax > 0.000001 then diagOther / sideMax else 0.0
                            if sharedFactor < RwioQAP_TopoSharedDiagonalMinFactor then
                            (
                                RwioQAP_LogCandidateReject ("shared edge is not diagonal-like factor=" + (sharedFactor as string)) f1 f2
                            )
                            else if otherFactor < RwioQAP_TopoOtherDiagonalMinFactor then
                            (
                                RwioQAP_LogCandidateReject ("opposite diagonal too small factor=" + (otherFactor as string)) f1 f2
                            )
                            else if not (RwioQAP_QuadStrictConvexV46 pa pb pc pd n) then
                            (
                                RwioQAP_LogCandidateReject "projected quad not strictly convex" f1 f2
                            )
                            else
                            (
                                local mx = if diagShared > diagOther then diagShared else diagOther
                                local ratio = 0.0
                                if mx > 0.000001 do ratio = (abs(diagShared - diagOther)) / mx
                                if ratio > RwioQAP_IntelligentMaxDiagonalRatio then
                                (
                                    RwioQAP_LogCandidateReject ("diagonal mismatch ratio=" + (ratio as string)) f1 f2
                                )
                                else
                                (
                                    local quadQ = RwioQAP_QuadQuality pa pb pc pd
                                    if quadQ >= 99998.0 or quadQ > RwioQAP_TopoMaxQuadQuality then
                                    (
                                        RwioQAP_LogCandidateReject ("invalid/weak/concave quad quality=" + (quadQ as string)) f1 f2
                                    )
                                    else
                                    (
                                        local score = (ratio * 42.0) + (quadQ * 1.20) + ((1.0 - sharedFactor) * 18.0) + ((1.0 - otherFactor) * 10.0)
                                        if score <= RwioQAP_TopoMaxScore then
                                        (
                                            cand = RwioQAP_QuadCandidate score:score f1:f1 e1:e1 f2:f2 e2:e2 angle:0.0 ratio:ratio materialId:mat1 quadQuality:quadQ prelightPenalty:0.0 smoothingPenalty:0.0 diagFactor:sharedFactor
                                        )
                                        else
                                        (
                                            RwioQAP_LogCandidateReject ("low confidence score=" + (score as string)) f1 f2
                                        )
                                    )
                                )
                            )
                        )
                    )
                )
            )
        )
    )
    catch(cand = undefined)
    cand
)

fn RwioQAP_IntelligentTrianglePairingFinalize node label =
(
    local ok = false
    try
    (
        RwioQAP_Log (label + " INTELLIGENT finalize begin: " + RwioQAP_NodeInfo node)
        convertToMesh node

        RwioQAP_WeldMeshNodeVertices node (label + " INTELLIGENT step 1")

        local faceCount = try(node.faces.count)catch(0)
        local keys = #()
        local adjs = #()
        for f = 1 to faceCount do
        (
            local fv = RwioQAP_FaceVertArray node f
            for e = 1 to 3 do
            (
                local ev = RwioQAP_LocalEdgeVerts fv e
                local key = RwioQAP_EdgeKey ev[1] ev[2]
                local idx = findItem keys key
                if idx == 0 do
                (
                    append keys key
                    append adjs #()
                    idx = keys.count
                )
                append adjs[idx] #(f,e)
            )
        )

        local candidates = #()
        local boundaryEdges = 0
        for i = 1 to adjs.count do
        (
            if adjs[i].count == 2 then
            (
                local c = RwioQAP_MakeQuadCandidate node adjs[i]
                if c != undefined do append candidates c
            )
            else boundaryEdges += 1
        )
        qsort candidates RwioQAP_CompareQuadCandidate

        -- Start from fully visible triangles. Then hide only selected, paired diagonals.
        for f = 1 to faceCount do
        (
            for e = 1 to 3 do try(setEdgeVis node f e true)catch(RwioQAP_LogException "setEdgeVis true failed" node:node)
        )

        local faceUsed = #()
        for f = 1 to faceCount do append faceUsed false
        local hidden = 0
        for c in candidates do
        (
            if (not faceUsed[c.f1]) and (not faceUsed[c.f2]) do
            (
                try(setEdgeVis node c.f1 c.e1 false)catch()
                try(setEdgeVis node c.f2 c.e2 false)catch()
                faceUsed[c.f1] = true
                faceUsed[c.f2] = true
                hidden += 1
            )
        )
        update node
        RwioQAP_Log (label + " INTELLIGENT step 2: triangle-pair analysis candidates=" + (candidates.count as string) + " hiddenDiagonals=" + (hidden as string) + " boundaryOrNonmanifoldEdges=" + (boundaryEdges as string) + " faces=" + (faceCount as string))
        RwioQAP_Log (label + " INTELLIGENT finalize end: " + RwioQAP_NodeInfo node)
        ok = true
    )
    catch
    (
        RwioQAP_LogException (label + " INTELLIGENT finalize failed") node:node
        ok = false
    )
    ok
)


fn RwioQAP_FinalizeMeshNodeWeldThenAutoEdge node label convertBackToPoly:true =
(
    local ok = false
    try
    (
        RwioQAP_Log (label + " FINALIZE begin: " + RwioQAP_NodeInfo node)
        RwioQAP_WeldMeshNodeVertices node (label + " WELD 0.001")
        RwioQAP_AutoEdgeMeshNode node (label + " AUTOEDGE 24")
        if convertBackToPoly do try(convertToPoly node)catch(RwioQAP_LogException (label + " convertToPoly after finalize") node:node)
        RwioQAP_Log (label + " FINALIZE end: " + RwioQAP_NodeInfo node)
        ok = true
    )
    catch
    (
        RwioQAP_LogException (label + " FINALIZE failed") node:node
        ok = false
    )
    ok
)

fn RwioQAP_PreWeldHierarchyMeshNodes meshNodes =
(
    local welded = 0
    local failed = 0
    RwioQAP_Log ("================ FULL HIERARCHY PRE-WELD PASS ENTER, meshNodes=" + (try(meshNodes.count as string)catch("?")) + " ================")
    try
    (
        for n in meshNodes where isValidNode n do
        (
            if RwioQAP_HasSkin n then RwioQAP_Log ("FULL PREPASS skip Skin node='" + n.name + "'")
            else
            (
                if (RwioQAP_WeldMeshNodeVertices n "FULL PREPASS 0.001") then (welded += 1) else (failed += 1)
            )
        )
    )
    catch(RwioQAP_LogException "PreWeldHierarchyMeshNodes")
    RwioQAP_Log ("================ FULL HIERARCHY PRE-WELD PASS EXIT welded=" + (welded as string) + " failed=" + (failed as string) + " ================")
    welded
)

fn RwioQAP_IsEditablePolyNode node =
(
    local ok = false
    try(ok = (isKindOf node.baseObject Editable_Poly))catch(ok = false)
    ok
)

fn RwioQAP_PolyVertCount node =
(
    local n = 0
    try(n = polyop.getNumVerts node)catch(try(n = node.verts.count)catch(n = 0))
    n
)

fn RwioQAP_CountApproxDuplicatePolyVerts node threshold =
(
    local dup = 0
    try
    (
        local keys = #()
        local vc = RwioQAP_PolyVertCount node
        local scale = if threshold > 0.0 then (1.0 / threshold) else 1000.0
        for i = 1 to vc do
        (
            local p = undefined
            try(p = polyop.getVert node i)catch(try(p = getVert node i)catch(p = undefined))
            if p != undefined do
            (
                local k = (((floor ((p.x * scale) + 0.5)) as integer) as string) + "," + (((floor ((p.y * scale) + 0.5)) as integer) as string) + "," + (((floor ((p.z * scale) + 0.5)) as integer) as string)
                if (findItem keys k) == 0 then append keys k else dup += 1
            )
        )
    )catch(dup = -1)
    dup
)

fn RwioQAP_WeldEditablePolyVertices node label =
(
    local ok = false
    try
    (
        if not (isValidNode node) do throw "invalid node"
        RwioQAP_ConvertToPolyOnlyIfNeeded node (label + " convert-to-poly-before-poly-weld")
        local beforeClass = try((classOf node) as string)catch("?")
        local beforeBase = try((classOf node.baseObject) as string)catch("?")
        local beforeVerts = RwioQAP_PolyVertCount node
        local beforeFaces = try(polyop.getNumFaces node)catch(try(node.faces.count)catch(0))
        local beforeEdges = try(polyop.getNumEdges node)catch(try(node.edges.count)catch(0))
        local beforeDup = RwioQAP_CountApproxDuplicatePolyVerts node RwioQAP_WeldThreshold
        RwioQAP_Log (label + " EDITABLE_POLY STABLE WELD begin node='" + node.name + "' class=" + beforeClass + " base=" + beforeBase + " faces=" + (beforeFaces as string) + " verts=" + (beforeVerts as string) + " edges=" + (beforeEdges as string) + " approxDupVerts=" + (beforeDup as string) + " threshold=" + (RwioQAP_WeldThreshold as string))
        try(polyop.setWeldThreshold node RwioQAP_WeldThreshold)catch(try(node.weldThreshold = RwioQAP_WeldThreshold)catch())
        local pass = 0
        local stable = false
        local lastVerts = -1
        while (pass < RwioQAP_FinalEditablePolyWeldPasses and not stable) do
        (
            pass += 1
            local vc = RwioQAP_PolyVertCount node
            if vc <= 0 then
            (
                stable = true
                RwioQAP_Log (label + " EDITABLE_POLY WELD pass " + (pass as string) + " skipped; zero verts")
            )
            else
            (
                local beforePass = vc
                local welded = false
                try(polyop.weldVertsByThreshold node #{1..vc} RwioQAP_WeldThreshold; welded = true)catch()
                if not welded do try(polyop.weldVertsByThreshold node #{1..vc}; welded = true)catch()
                if not welded do try
                (
                    -- Last-resort UI/API style: select all verts and call button op where available.
                    subObjectLevel = 1
                    polyop.setVertSelection node #{1..vc}
                    node.weldThreshold = RwioQAP_WeldThreshold
                    node.EditablePoly.buttonOp #WeldSelected
                    welded = true
                )catch()
                update node
                local afterPass = RwioQAP_PolyVertCount node
                RwioQAP_Log (label + " EDITABLE_POLY WELD pass " + (pass as string) + ": verts " + (beforePass as string) + " -> " + (afterPass as string) + " apiOk=" + (welded as string))
                if (afterPass == beforePass) or (afterPass == lastVerts) do stable = true
                lastVerts = afterPass
            )
        )
        local afterClass = try((classOf node) as string)catch("?")
        local afterBase = try((classOf node.baseObject) as string)catch("?")
        local afterVerts = RwioQAP_PolyVertCount node
        local afterFaces = try(polyop.getNumFaces node)catch(try(node.faces.count)catch(0))
        local afterEdges = try(polyop.getNumEdges node)catch(try(node.edges.count)catch(0))
        local afterDup = RwioQAP_CountApproxDuplicatePolyVerts node RwioQAP_WeldThreshold
        RwioQAP_Log (label + " EDITABLE_POLY STABLE WELD END node='" + node.name + "' class " + beforeClass + "/" + beforeBase + " -> " + afterClass + "/" + afterBase + ", faces " + (beforeFaces as string) + " -> " + (afterFaces as string) + ", verts " + (beforeVerts as string) + " -> " + (afterVerts as string) + ", edges " + (beforeEdges as string) + " -> " + (afterEdges as string) + ", approxDupVerts " + (beforeDup as string) + " -> " + (afterDup as string) + ", stable=" + (stable as string))
        ok = true
    )
    catch
    (
        RwioQAP_LogException (label + " EDITABLE_POLY STABLE WELD failed") node:node
        ok = false
    )
    ok
)

fn RwioQAP_ConvertToPolyOnlyIfNeeded node label =
(
    local did = false
    try
    (
        if not (isValidNode node) do throw "node invalid"
        if RwioQAP_IsEditablePolyNode node then
        (
            RwioQAP_Log (label + " convertToPoly skipped: already Editable_Poly node=" + RwioQAP_NodeInfo node)
        )
        else
        (
            RwioQAP_Log (label + " convertToPoly needed: current=" + RwioQAP_NodeInfo node)
            convertToPoly node
            did = true
            RwioQAP_Log (label + " convertToPoly done: " + RwioQAP_NodeInfo node)
        )
    )
    catch(RwioQAP_LogException (label + " ConvertToPolyOnlyIfNeeded") node:node)
    did
)

fn RwioQAP_MakeBombSnapshot source =
(
    local tnt = undefined
    local result = undefined
    try
    (
        RwioQAP_Log ("Bomb path begin: " + RwioQAP_NodeInfo source)
        tnt = Bomb strength:0 gravity:0 chaos:0 seed:0 spin:0 detonation:0f minFragmentSize:2 maxFragmentSize:2 fallOffEnable:off isHidden:on isSelected:off
        RwioQAP_Log ("Bomb created: " + (RwioQAP_ToString tnt))
        bindSpaceWarp source tnt
        RwioQAP_Log "bindSpaceWarp OK. sliderTime=5f."
        sliderTime = 5f
        result = snapShot source
        RwioQAP_Log ("snapShot after Bomb result: " + RwioQAP_NodeInfo result)
    )
    catch
    (
        RwioQAP_LogException "MakeBombSnapshot" node:source
        result = undefined
    )
    try(if isValidNode tnt do delete tnt)catch()
    result
)

fn RwioQAP_BakeNodeMeshToTargetLocal sourceNode targetNode =
(
    local ok = false
    try
    (
        if not (isValidNode sourceNode) do throw "sourceNode invalid"
        if not (isValidNode targetNode) do throw "targetNode invalid"
        convertToMesh sourceNode
        local sourceTM = RwioQAP_GetObjectTM sourceNode
        local targetTM = RwioQAP_GetObjectTM targetNode
        local deltaTM = sourceTM * (inverse targetTM)
        local vcount = try(sourceNode.verts.count)catch(0)
        RwioQAP_Log ("BakeNodeMeshToTargetLocal begin source='" + sourceNode.name + "' target='" + targetNode.name + "' verts=" + (vcount as string))
        for v = 1 to vcount do setVert sourceNode v ((getVert sourceNode v) * deltaTM)
        update sourceNode
        try(sourceNode.transform = targetNode.transform)catch()
        ok = true
        RwioQAP_Log "BakeNodeMeshToTargetLocal OK."
    )
    catch
    (
        RwioQAP_LogException "BakeNodeMeshToTargetLocal" node:sourceNode
        ok = false
    )
    ok
)

fn QuadifyAll obj uv:true mtl:true fID:true oID:true =
(
    RwioQAP_Log "================ QuadifyAll ENTER ================"
    RwioQAP_Log ("args obj=" + (RwioQAP_NodeInfo obj) + " uv=" + (uv as string) + " mtl=" + (mtl as string) + " fID=" + (fID as string) + " oID=" + (oID as string))
    local oldTime = sliderTime
    local stage = "start"
    local source = obj
    local result = undefined
    local objMat = try(obj.material)catch(undefined)
    local objID = try(obj.gbufferchannel)catch(undefined)
    try
    (
        if not (isValidNode obj) do throw "QuadifyAll input object is invalid or undefined."
        stage = "optional Unwrap_UVW"
        if uv do
        (
            RwioQAP_Log "Adding Unwrap_UVW modifier to temp copy to preserve UVs."
            addmodifier source (Unwrap_UVW())
        )
        stage = "convertToMesh source"
        convertToMesh source
        RwioQAP_Log ("After convertToMesh: " + RwioQAP_NodeInfo source)
        if RwioQAP_DoPreBombWeldVerify then
        (
            RwioQAP_WeldMeshNodeVertices source "TEMP PRE-BOMB VERIFY 0.001"
        )
        else
        (
            RwioQAP_Log "TEMP PRE-BOMB weld disabled: v10 welds after Bomb before intelligent edge pairing."
        )
        stage = "Bomb snapshot"
        result = RwioQAP_MakeBombSnapshot source
        if not (isValidNode result) do throw "Bomb snapshot failed."
        result.name = uniquename ((try(obj.name)catch("object")) + "_quadrified")
        stage = "delete consumed source"
        try(if isValidNode source do delete source)catch(RwioQAP_LogException "QuadifyAll.deleteSource" node:source)
        stage = "convertToMesh result"
        convertToMesh result
        RwioQAP_Log ("Result after convertToMesh: " + RwioQAP_NodeInfo result)
        stage = "preserve face material IDs"
        RwioQAP_Log "Material IDs are preserved; intelligent pairing uses material IDs as hard boundaries."
        stage = "intelligent triangle-pair quad finalize"
        if RwioQAP_DoIntelligentTrianglePairing then
        (
            RwioQAP_IntelligentTrianglePairingFinalize result "POST-BOMB RESULT"
        )
        else if RwioQAP_DoClassicPostBombAutoEdgeThenWeld then
        (
            RwioQAP_ClassicBgaAutoEdgeThenWeld result "POST-BOMB RESULT"
        )
        else
        (
            RwioQAP_Log "POST-BOMB finalize disabled."
        )
        stage = "restore material/object id"
        if mtl then result.material = objMat else result.material = undefined
        if oID do try(result.gbufferchannel = objID)catch()
    )
    catch
    (
        RwioQAP_LogException ("QuadifyAll stage='" + stage + "'") node:obj
        result = undefined
    )
    try(sliderTime = oldTime)catch()
    RwioQAP_Log ("QuadifyAll EXIT result=" + RwioQAP_NodeInfo result)
    result
)



struct RwioQAP_UVCornerSample (pos, center, uv)

fn RwioQAP_GetMapFaceCornerIndexV28 mf corner =
(
    case corner of
    (
        1: (try(mf.x as integer)catch(0))
        2: (try(mf.y as integer)catch(0))
        3: (try(mf.z as integer)catch(0))
        default: 0
    )
)

fn RwioQAP_GetFaceCornerPosV28 node faceIndex corner =
(
    local p = [0,0,0]
    try
    (
        local f = getFace node faceIndex
        local vi = case corner of (1:(f.x as integer); 2:(f.y as integer); 3:(f.z as integer); default:(f.x as integer))
        p = getVert node vi
    )catch()
    p
)

fn RwioQAP_FaceCenterV28 node faceIndex =
(
    local c = [0,0,0]
    try
    (
        c = ((RwioQAP_GetFaceCornerPosV28 node faceIndex 1) + (RwioQAP_GetFaceCornerPosV28 node faceIndex 2) + (RwioQAP_GetFaceCornerPosV28 node faceIndex 3)) / 3.0
    )catch()
    c
)

fn RwioQAP_SetMapFace3V28 node channel faceIndex a b c =
(
    local ok = false
    try(meshop.setMapFace node channel faceIndex [a,b,c]; ok = true)catch()
    if not ok do try(meshop.setMapFace node channel faceIndex (point3 a b c); ok = true)catch()
    ok
)

fn RwioQAP_BuildUVCornerSamplesV28 source channel label =
(
    local samples = #()
    try
    (
        if meshop.getMapSupport source channel then
        (
            local sf = try(source.faces.count)catch(0)
            for f = 1 to sf do
            (
                local mf = undefined
                try(mf = meshop.getMapFace source channel f)catch(mf = undefined)
                if mf != undefined do
                (
                    local fc = RwioQAP_FaceCenterV28 source f
                    for c = 1 to 3 do
                    (
                        local mi = RwioQAP_GetMapFaceCornerIndexV28 mf c
                        if mi > 0 do
                        (
                            local uv = undefined
                            try(uv = meshop.getMapVert source channel mi)catch(uv = undefined)
                            if uv != undefined do append samples (RwioQAP_UVCornerSample pos:(RwioQAP_GetFaceCornerPosV28 source f c) center:fc uv:uv)
                        )
                    )
                )
            )
        )
    )catch(RwioQAP_LogException (label + " build UV samples channel=" + (channel as string)) node:source)
    samples
)

fn RwioQAP_FindBestUVSampleV28 samples p fc =
(
    local best = undefined
    local bestScore = 1e30
    try
    (
        for s in samples do
        (
            local score = (distance p s.pos) + ((distance fc s.center) * RwioQAP_UVRestoreFaceCenterWeight)
            if score < bestScore do
            (
                bestScore = score
                best = s
            )
        )
    )catch(best = undefined)
    if best != undefined then best.uv else [0,0,0]
)

fn RwioQAP_RestoreOneUVMapChannelV28 source target channel label =
(
    local ok = false
    try
    (
        if not (meshop.getMapSupport source channel) then
        (
            RwioQAP_Log (label + " attribute-map restore skipped channel " + (channel as string) + ": source unsupported")
        )
        else
        (
            local samples = RwioQAP_BuildUVCornerSamplesV28 source channel label
            local tf = try(target.faces.count)catch(0)
            if samples.count <= 0 or tf <= 0 then
            (
                RwioQAP_Log (label + " attribute-map restore skipped channel " + (channel as string) + ": samples=" + (samples.count as string) + " targetFaces=" + (tf as string))
            )
            else
            (
                try(meshop.setMapSupport target channel true)catch()
                try(meshop.buildMapFaces target channel true)catch()
                try(meshop.setNumMapVerts target channel (tf * 3) keep:false)catch(try(meshop.setNumMapVerts target channel (tf * 3))catch())
                try(meshop.setNumMapFaces target channel tf keep:false)catch(try(meshop.setNumMapFaces target channel tf)catch())
                for f = 1 to tf do
                (
                    local base = (f - 1) * 3
                    local fc = RwioQAP_FaceCenterV28 target f
                    local uv1 = RwioQAP_FindBestUVSampleV28 samples (RwioQAP_GetFaceCornerPosV28 target f 1) fc
                    local uv2 = RwioQAP_FindBestUVSampleV28 samples (RwioQAP_GetFaceCornerPosV28 target f 2) fc
                    local uv3 = RwioQAP_FindBestUVSampleV28 samples (RwioQAP_GetFaceCornerPosV28 target f 3) fc
                    try(meshop.setMapVert target channel (base + 1) uv1)catch()
                    try(meshop.setMapVert target channel (base + 2) uv2)catch()
                    try(meshop.setMapVert target channel (base + 3) uv3)catch()
                    RwioQAP_SetMapFace3V28 target channel f (base + 1) (base + 2) (base + 3)
                )
                ok = true
                RwioQAP_Log (label + " attribute-map restore channel " + (channel as string) + " OK samples=" + (samples.count as string) + " targetFaces=" + (tf as string) + " mapVerts=" + ((tf * 3) as string))
            )
        )
    )catch(RwioQAP_LogException (label + " restore UV channel=" + (channel as string)) node:target)
    ok
)


fn RwioQAP_AddUniqueChannelV33 channels ch =
(
    local exists = false
    for c in channels where c == ch do exists = true
    if not exists do append channels ch
    channels
)

fn RwioQAP_GetSourceMapChannelsForRestoreV33 source =
(
    local channels = #()
    -- Important RWIO/DFF channels:
    --   -1 = RWIO/GTA extra vertex colors when present
    --    0 = Max CPV / vertex color / prelight display channel
    --    1..N = UV sets and regular map channels
    --    9 = RWIO extra alpha in this plugin
    for ch in #(-2, -1, 0, 1, 9) do
    (
        local supported = false
        try(supported = meshop.getMapSupport source ch)catch(supported = false)
        if supported do RwioQAP_AddUniqueChannelV33 channels ch
    )
    local maxMap = 1
    try(maxMap = (meshop.getNumMaps source) - 1)catch(maxMap = 1)
    if maxMap < 1 do maxMap = 1
    for ch = 1 to maxMap do
    (
        local supported = false
        try(supported = meshop.getMapSupport source ch)catch(supported = false)
        if supported do RwioQAP_AddUniqueChannelV33 channels ch
    )
    channels
)

fn RwioQAP_RestoreUVMapsFromSnapshotV28 uvSource target label =
(
    local restored = 0
    try
    (
        if not (isValidNode uvSource) do throw "invalid uvSource"
        if not (isValidNode target) do throw "invalid target"
        RwioQAP_Log (label + " ATTRIBUTE MAP PRESERVE restore begin src=" + RwioQAP_NodeInfo uvSource + " target=" + RwioQAP_NodeInfo target)
        convertToMesh uvSource
        convertToMesh target
        local channels = RwioQAP_GetSourceMapChannelsForRestoreV33 uvSource
        RwioQAP_Log (label + " attribute-map restore channels=" + (channels as string))
        for ch in channels do
        (
            local supported = false
            try(supported = meshop.getMapSupport uvSource ch)catch(supported = false)
            if supported do
            (
                if RwioQAP_RestoreOneUVMapChannelV28 uvSource target ch label do restored += 1
            )
        )
        update target
        RwioQAP_ConvertToPolyOnlyIfNeeded target (label + " after attribute-map restore")
        RwioQAP_Log (label + " ATTRIBUTE MAP PRESERVE restore end restoredChannels=" + (restored as string) + " target=" + RwioQAP_NodeInfo target)
    )catch(RwioQAP_LogException (label + " ATTRIBUTE MAP PRESERVE restore failed") node:target)
    restored
)



fn RwioQAP_ProjectDffAttributesIfAvailable sourceNode targetNode label =
(
    local ok = false
    try
    (
        if not (isValidNode sourceNode) do throw "invalid source node"
        if not (isValidNode targetNode) do throw "invalid target node"
        if (try(rwioProjectDffAttributes == undefined)catch(true)) then
        (
            RwioQAP_Log (label + " cached DFF attribute projection skipped: rwioProjectDffAttributes primitive unavailable")
        )
        else
        (
            ok = (try(rwioProjectDffAttributes sourceNode targetNode)catch(undefined)) != undefined
            update targetNode
            RwioQAP_Log (label + " cached DFF attribute projection ok=" + (ok as string) + " target=" + RwioQAP_NodeInfo targetNode)
        )
    )
    catch
    (
        RwioQAP_LogException (label + " cached DFF attribute projection failed") node:targetNode
        ok = false
    )
    ok
)


fn RwioQAP_ProjectDffNormalsIfAvailable sourceNode targetNode label =
(
    local ok = false
    try
    (
        if not (isValidNode sourceNode) do throw "invalid source node"
        if not (isValidNode targetNode) do throw "invalid target node"
        if (try(rwioProjectDffNormals == undefined)catch(true)) then
        (
            RwioQAP_Log (label + " DFF explicit normal projection skipped: rwioProjectDffNormals primitive unavailable")
        )
        else
        (
            ok = (try(rwioProjectDffNormals sourceNode targetNode)catch(undefined)) != undefined
            update targetNode
            RwioQAP_Log (label + " DFF explicit normal projection ok=" + (ok as string) + " target=" + RwioQAP_NodeInfo targetNode)
        )
    )
    catch
    (
        RwioQAP_LogException (label + " DFF explicit normal projection failed") node:targetNode
        ok = false
    )
    ok
)


fn RwioQAP_ProjectExplicitNormalsFromSnapshotIfAvailable sourceSnapshot targetNode label =
(
    local ok = false
    try
    (
        if not (isValidNode sourceSnapshot) do throw "invalid explicit-normal source snapshot"
        if not (isValidNode targetNode) do throw "invalid target node"
        if (try(rwioProjectExplicitNormals == undefined)catch(true)) then
        (
            RwioQAP_Log (label + " explicit normal snapshot projection skipped: rwioProjectExplicitNormals primitive unavailable")
        )
        else
        (
            ok = (try(rwioProjectExplicitNormals sourceSnapshot targetNode)catch(undefined)) != undefined
            update targetNode
            RwioQAP_Log (label + " explicit normal snapshot projection ok=" + (ok as string) + " source=" + RwioQAP_NodeInfo sourceSnapshot + " target=" + RwioQAP_NodeInfo targetNode)
        )
    )
    catch
    (
        RwioQAP_LogException (label + " explicit normal snapshot projection failed") node:targetNode
        ok = false
    )
    ok
)




fn RwioQAP_QuadifyNodeInPlace node =
(
    local ok = false
    local state = undefined
    local src = undefined
    local q = undefined
    local uvOriginal = undefined
    local stage = "start"
    local originalBBox = ""
    local finalBBox = ""
    try
    (
        RwioQAP_Log ("Process in-place begin: " + RwioQAP_NodeInfo node)
        originalBBox = RwioQAP_BBoxString node
        RwioQAP_Log ("Original bbox: " + originalBBox)
        if not (RwioQAP_IsMeshLike node) do
        (
            RwioQAP_Log "Skip: not mesh-like."
            return false
        )
        if RwioQAP_HasSkin node do
        (
            RwioQAP_Log "Skip: Skin modifier found; not welding/quadrifying skinned mesh."
            return false
        )
        state = RwioQAP_CaptureState node
        try(node.isFrozen = false)catch()
        if RwioQAP_PreserveOriginalUVMapsAtEnd do
        (
            stage = "snapshot original import attributes"
            uvOriginal = copy node
            if isValidNode uvOriginal then
            (
                uvOriginal.name = uniquename (node.name + "_qa_original_attr_snapshot")
                try(uvOriginal.parent = undefined)catch()
                try(uvOriginal.isHidden = true)catch()
                convertToMesh uvOriginal
                RwioQAP_Log ("ORIGINAL ATTRIBUTE MAP PRESERVE snapshot before any quadrify: " + RwioQAP_NodeInfo uvOriginal)
            )
            else
            (
                RwioQAP_Log "ORIGINAL ATTRIBUTE MAP PRESERVE snapshot failed: copy returned invalid"
            )
        )
        stage = "copy original node"
        src = copy node
        if not (isValidNode src) do throw "Could not copy original node."
        src.name = uniquename (node.name + "_qa_src")
        try(src.parent = undefined)catch()
        try(src.isHidden = true)catch()
        RwioQAP_Log ("Temp copy made: " + RwioQAP_NodeInfo src)
        RwioQAP_Log ("Temp copy bbox: " + RwioQAP_BBoxString src)
        stage = "QuadifyAll temp copy"
        q = QuadifyAll src uv:true mtl:true fID:true oID:true
        src = undefined
        if not (isValidNode q) do throw "QuadifyAll returned undefined/invalid."
        try(q.isHidden = true)catch()
        RwioQAP_Log ("Quadrified temp result: " + RwioQAP_NodeInfo q)
        RwioQAP_Log ("Quadrified temp bbox before bake: " + RwioQAP_BBoxString q)
        stage = "bake temp mesh to original local space"
        if not (RwioQAP_BakeNodeMeshToTargetLocal q node) do throw "Failed to bake quadrified mesh into target local space."
        RwioQAP_Log ("Quadrified temp bbox after bake: " + RwioQAP_BBoxString q)
        stage = "convert temp quadrified result to Editable_Poly before swap"
        -- v31: do NOT project normals onto q here; later convert/weld/swap steps can
        -- recalculate or discard MeshNormalSpec data.  Projection is intentionally
        -- the final geometry operation on the target node.
        RwioQAP_ConvertToPolyOnlyIfNeeded q "TEMP RESULT BEFORE SWAP"
        if RwioQAP_FinalEditablePolyWeld do
        (
            RwioQAP_WeldEditablePolyVertices q "TEMP RESULT AFTER CONVERTTOPOLY 0.001"
        )
        stage = "swap Editable_Poly baseobject"
        swap node.baseobject q.baseobject
        RwioQAP_RestoreState node state restoreTransform:true
        stage = "final Editable_Poly guard + final poly weld"
        if RwioQAP_IsEditablePolyNode node then
        (
            RwioQAP_Log ("FINAL TARGET v28 already Editable_Poly after swap. node=" + RwioQAP_NodeInfo node)
        )
        else
        (
            RwioQAP_Log "FINAL TARGET v28 was not Editable_Poly after swap; guarded convertToPoly fallback."
            RwioQAP_ConvertToPolyOnlyIfNeeded node "FINAL TARGET v28 fallback"
            RwioQAP_RestoreState node state restoreTransform:true
        )
        if RwioQAP_FinalEditablePolyWeld do
        (
            RwioQAP_WeldEditablePolyVertices node "FINAL TARGET POST-SWAP EDITABLE_POLY 0.001"
            RwioQAP_RestoreState node state restoreTransform:true
        )
        stage = "final cached projection of original import attributes"
        local attrProjected = false
        if RwioQAP_DeferCachedDffAttributeProjectionToImporter then
        (
            RwioQAP_Log "FINAL CACHED DFF ATTRIBUTE RESTORE deferred to importer C++ post-script pass"
        )
        else
        (
            attrProjected = RwioQAP_ProjectDffAttributesIfAvailable node node "FINAL CACHED DFF ATTRIBUTE RESTORE"
            if attrProjected do
            (
                RwioQAP_RestoreState node state restoreTransform:true
            )
        )
        if RwioQAP_PreserveOriginalUVMapsAtEnd and isValidNode uvOriginal do
        (
            stage = "legacy slow original import attribute map restore"
            RwioQAP_RestoreUVMapsFromSnapshotV28 uvOriginal node "FINAL ORIGINAL ATTRIBUTE MAP RESTORE"
            RwioQAP_RestoreState node state restoreTransform:true
        )
        stage = "final projection of original explicit normals"
        -- v32: normals now use the same source-snapshot projection model as the
        -- existing UV/map-channel restore.  First project from the original
        -- imported snapshot; if that snapshot has no specified normals, fall back
        -- to the C++ DFF normal cache captured during import.
        local normalProjected = attrProjected -- cached attribute projection also carries explicit normals when the importer captured them
        if RwioQAP_DeferCachedDffAttributeProjectionToImporter then
        (
            RwioQAP_Log "FINAL EXPLICIT NORMAL RESTORE deferred to importer C++ post-script pass"
        )
        else
        (
            if (not normalProjected) and isValidNode uvOriginal do
            (
                normalProjected = RwioQAP_ProjectExplicitNormalsFromSnapshotIfAvailable uvOriginal node "FINAL EXPLICIT NORMAL SNAPSHOT RESTORE"
            )
            if not normalProjected do
            (
                normalProjected = RwioQAP_ProjectDffNormalsIfAvailable node node "FINAL DFF NORMAL CACHE RESTORE"
            )
            if normalProjected do
            (
                RwioQAP_RestoreState node state restoreTransform:true
            )
        )
        finalBBox = RwioQAP_BBoxString node
        RwioQAP_Log ("Final bbox: " + finalBBox)
        RwioQAP_Log ("BBox compare original -> final: " + originalBBox + " -> " + finalBBox)
        ok = true
    )
    catch
    (
        RwioQAP_LogException ("QuadifyNodeInPlace stage='" + stage + "'") node:node
        ok = false
    )
    if isValidNode src do RwioQAP_SafeDelete src
    if isValidNode q do RwioQAP_SafeDelete q
    if isValidNode uvOriginal do RwioQAP_SafeDelete uvOriginal
    if state != undefined do RwioQAP_RestoreState node state restoreTransform:true
    RwioQAP_Log ("Process in-place end ok=" + (ok as string) + " node=" + RwioQAP_NodeInfo node)
    ok
)

fn RwioQAP_AsNodeArray nodes =
(
    local roots = #()
    if nodes == undefined then roots = selection as array
    else
    (
        try(roots = nodes as array)catch(roots = #(nodes))
    )
    roots
)

fn RwioQuadrifyAllPlusSelection nodes diagRatio:undefined =
(
    RwioQAP_Log "================ RwioQuadrifyAllPlusSelection ENTER ================"
    if diagRatio != undefined then
    (
        RwioQAP_SetImporterDiagonalRatio diagRatio
    )
    else
    (
        try(RwioQAP_SetImporterDiagonalRatio dffImp.quadrifyAllPlusDiagonalRatio)catch(RwioQAP_Log "Q++ dialog diagonal ratio unavailable; keeping script default")
    )
    RwioQAP_Log ("Q++ active MaxDiagonalRatio=" + (RwioQAP_IntelligentMaxDiagonalRatio as string))
    local oldSelection = selection as array
    local oldTime = sliderTime
    local roots = #()
    local allNodes = #()
    local candidates = 0
    local processed = 0
    try
    (
        roots = RwioQAP_AsNodeArray nodes
        RwioQAP_Log ("Root count=" + (try(roots.count as string)catch("?")))
        for n in roots where isValidNode n do
        (
            RwioQAP_Log ("Root: " + RwioQAP_NodeInfo n)
            RwioQAP_CollectHierarchy n allNodes
        )
        RwioQAP_Log ("Hierarchy count=" + (allNodes.count as string))
        local meshNodes = #()
        for n in allNodes where isValidNode n do if RwioQAP_IsMeshLike n do
        (
            candidates += 1
            append meshNodes n
        )
        RwioQAP_Log ("Mesh candidate count before full pre-weld pass=" + (meshNodes.count as string))
        undo off with redraw off
        (
            if RwioQAP_DoFullHierarchyPreWeldPass do RwioQAP_PreWeldHierarchyMeshNodes meshNodes
            for n in meshNodes where isValidNode n do if RwioQAP_QuadifyNodeInPlace n do processed += 1
        )
    )
    catch(RwioQAP_LogException "RwioQuadrifyAllPlusSelection")
    try(select oldSelection)catch()
    try(sliderTime = oldTime)catch()
    local msg = "RWIO QuadrifyAll++ safe v49f edge strength 0.85 effective summary: roots=" + (roots.count as string) + ", hierarchy=" + (allNodes.count as string) + ", candidates=" + (candidates as string) + ", processed=" + (processed as string) + ". Log=" + (RwioQAP_GetLogPath())
    RwioQAP_Log msg
    format "%\n" msg
    RwioQAP_Log "================ RwioQuadrifyAllPlusSelection EXIT ================="
    processed
)

fn QuadrifyAllPlusSelection nodes = (RwioQAP_Log "ALIAS QuadrifyAllPlusSelection -> RwioQuadrifyAllPlusSelection"; RwioQuadrifyAllPlusSelection nodes)
fn BgaQuadrifyAllPlusSelection nodes = (RwioQAP_Log "ALIAS BgaQuadrifyAllPlusSelection -> RwioQuadrifyAllPlusSelection"; RwioQuadrifyAllPlusSelection nodes)
fn RwioQuadrifyAllSelection nodes = (RwioQAP_Log "ALIAS RwioQuadrifyAllSelection -> RwioQuadrifyAllPlusSelection"; RwioQuadrifyAllPlusSelection nodes)
fn QuadifyAllSelection nodes = (RwioQAP_Log "ALIAS QuadifyAllSelection -> RwioQuadrifyAllPlusSelection"; RwioQuadrifyAllPlusSelection nodes)
fn RwioQuadrifyAllPlusRunOnSelection = (RwioQuadrifyAllPlusSelection (selection as array))

fn RwioQAP_FallbackGetImportedDFFNodes =
(
    local arr = #()
    try(arr = selection as array)catch(arr = #())
    try(RwioQAP_Log ("fallback getImportedDFFNodes() returning current selection count=" + (arr.count as string)))catch()
    arr
)

fn RwioQAP_FallbackSelectImportedDFF =
(
    try(RwioQAP_Log ("fallback selectImportedDFF() used; preserving current selection count=" + ((selection as array).count as string)))catch()
    ok
)

fn RwioQAP_EnsureRwioImportNodeFallbacks =
(
    local needGet = false
    local needSelect = false
    try(needGet = (getImportedDFFNodes == undefined))catch(needGet = true)
    try(needSelect = (selectImportedDFF == undefined))catch(needSelect = true)
    if needGet then
    (
        RwioQAP_Log "PLUGIN PRIMITIVE MISSING: getImportedDFFNodes undefined. Installing current-selection fallback."
        getImportedDFFNodes = RwioQAP_FallbackGetImportedDFFNodes
    )
    else RwioQAP_Log ("Plugin primitive exists: getImportedDFFNodes class=" + (try((classOf getImportedDFFNodes) as string)catch("?")))
    if needSelect then
    (
        RwioQAP_Log "PLUGIN PRIMITIVE MISSING: selectImportedDFF undefined. Installing no-op fallback."
        selectImportedDFF = RwioQAP_FallbackSelectImportedDFF
    )
    else RwioQAP_Log ("Plugin primitive exists: selectImportedDFF class=" + (try((classOf selectImportedDFF) as string)catch("?")))
)

fn RwioQAP_CheckVersion =
(
    format "RWIO QuadrifyAll+ loaded version: %\n" RwioQAP_Version
    format "Log path: %\n" (RwioQAP_GetLogPath())
    RwioQAP_Log ("CheckVersion called. Version=" + RwioQAP_Version)
    RwioQAP_Version
)

fn RwioQuadrifyAllPlusSelfTest =
(
    RwioQAP_ResetLog()
    RwioQAP_EnsureRwioImportNodeFallbacks()
    RwioQAP_DumpEnvironment()
    format "RWIO QuadrifyAll++ safe v49f edge strength 0.85 self-test done. Log: %\n" (RwioQAP_GetLogPath())
)

RwioQAP_ResetLog()
RwioQAP_EnsureRwioImportNodeFallbacks()
RwioQAP_DumpEnvironment()
format "RWIO QuadrifyAll++ NO-DIALOG SAFE v49k NORMAL SHADING DEFAULTS loaded. Version=%, QuadifyAll class=%, selection entry class=%. Log: %\n" RwioQAP_Version (try(classOf QuadifyAll)catch(undefined)) (try(classOf RwioQuadrifyAllPlusSelection)catch(undefined)) (RwioQAP_GetLogPath())
format "========== RWIO QuadrifyAll++ SAFE V49K NORMAL SHADING DEFAULTS FILEIN END ==========\n"