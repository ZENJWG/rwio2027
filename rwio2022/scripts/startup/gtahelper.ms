rwioStartupDir = getFilenamePath (getSourceFileName())
if not (try (RW_Mtl != undefined) catch false) do
	try (fileIn (rwioStartupDir + "RW_Material.ms")) catch ()
if not (try (GTA_Mtl != undefined) catch false) do
	try (fileIn (rwioStartupDir + "GTA_Material.ms")) catch ()

RwKamAlphaCacheFiles = #()
RwKamAlphaCacheValues = #()

fn RwBitmapHasAlphaForKam tex = (
	if tex == undefined or classof tex != Bitmaptexture do
		return false
	if tex.alphaSource == 2 do
		return false
	if tex.alphaSource == 1 do
		return true
	if tex.filename == undefined or tex.filename == "" do
		return false
	if not (doesFileExist tex.filename) do
		return false
	cacheIndex = findItem RwKamAlphaCacheFiles tex.filename
	if cacheIndex > 0 do
		return RwKamAlphaCacheValues[cacheIndex]

	local bmp = undefined
	local hasAlpha = false
	try (
		bmp = openBitmap tex.filename
		if bmp != undefined do
			hasAlpha = bmp.hasAlpha
	) catch (
		hasAlpha = false
	)
	if bmp != undefined do
		try (close bmp) catch ()
	append RwKamAlphaCacheFiles tex.filename
	append RwKamAlphaCacheValues hasAlpha
	hasAlpha
)

fn RwToKam m = (
	if (classof m) == Multimaterial do (
		for i = 1 to m.materialList.count do
			m.materialList[i] = RwToKam m.materialList[i]
		return m
	)
	if (classof m) != RW_Mtl do
		return m
	kam = GTA_Mtl()
	kam.amb = m.sp_ambient
	kam.spc = m.sp_specular
	kam.dif = m.sp_diffuse
	kam.color = m.color
	kam.colormap = m.texmap_texture
	kam.use_colormap = kam.colormap != undefined
	kam.alpha = m.coloralpha*255
	if isProperty kam #alphamap do (
		if RwBitmapHasAlphaForKam m.texmap_texture then (
			kam.alphamap = copy m.texmap_texture
			kam.alphamap.alphaSource = 0
			kam.alphamap.monoOutput = 1
			kam.alphamap.premultAlpha = false
			try (kam.alphamap.reload()) catch ()
			if isProperty kam #use_alphamap do
				kam.use_alphamap = true
		) else if isProperty kam #use_alphamap do
			kam.use_alphamap = false
	)
	kam.use_RF = m.matfxeffect == 3
	kam.Reflection = m.envmap_amount*100
	kam.reflectionmap = m.texmap_envmap
	kam.use_reflectionmap = kam.reflectionmap != undefined

	kam.use_SAS = m.enSpec
-- 	kam.specular
	kam.specularmap = m.specMap
	kam.use_specularmap = kam.specularmap != undefined

	kam.use_SI = m.enEnv
-- 	kam.spec_alpha
 	kam.spec_power = m.specularity*100
	kam.blend = m.shininess*100
	kam
)

utility GtaHelper "GTA helper" (
	button convMat "RW to GTA materials" width:140 align:#center
	on convMat pressed do ( 
		try (
			for o in objects do (
				o.material = RwToKam o.material
			)
		) catch (
			messageBox "GTA_Material.ms was not loaded, so GTA_Mtl is unavailable." title:"RWIO"
		)
	)
)
