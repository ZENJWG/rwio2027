rwioStartupDir = getFilenamePath (getSourceFileName())
if not (try (RW_Mtl != undefined) catch false) do
	try (fileIn (rwioStartupDir + "RW_Material.ms")) catch ()

global RwOpenPbrHasOpenPbr
global RwOpenPbrMapEnabled
global RwOpenPbrSourceBitmap
global RwOpenPbrBitmapHasAlpha
global RwOpenPbrAlphaMonoCopy
global RwOpenPbrUsesWhiteBase
global RwOpenPbrTintedBaseMap
global RwToOpenPBR
global OpenPBRToRw
global RwOpenPbrConvertScene
global RwOpenPbrConversionCount

RwOpenPbrConversionCount = 0

global RwOpenPbrHasOpenPbr = fn RwOpenPbrHasOpenPbr dummy = (
	try (OpenPBR_Material != undefined) catch false
)

global RwOpenPbrMapEnabled = fn RwOpenPbrMapEnabled tex enabledProp = (
	tex != undefined and (enabledProp == undefined or enabledProp == true)
)

global RwOpenPbrSourceBitmap = fn RwOpenPbrSourceBitmap tex = (
	if tex == undefined then
		undefined
	else if (classof tex) == RGB_Multiply and tex.map2 != undefined then
		tex.map2
	else
		tex
)

global RwOpenPbrBitmapHasAlpha = fn RwOpenPbrBitmapHasAlpha tex = (
	local source = RwOpenPbrSourceBitmap tex
	if source == undefined or (classof source) != Bitmaptexture do
		return false
	if source.alphaSource == 2 do
		return false
	if source.alphaSource == 1 do
		return true
	if source.filename == undefined or source.filename == "" do
		return false
	if not (doesFileExist source.filename) do
		return false

	local bmp = undefined
	local hasAlpha = false
	try (
		bmp = openBitmap source.filename
		if bmp != undefined do
			hasAlpha = bmp.hasAlpha
	) catch (
		hasAlpha = false
	)
	if bmp != undefined do
		try (close bmp) catch ()
	hasAlpha
)

global RwOpenPbrAlphaMonoCopy = fn RwOpenPbrAlphaMonoCopy tex = (
	local source = RwOpenPbrSourceBitmap tex
	if source != undefined and (classof source) == Bitmaptexture do (
		local alphaMap = copy source
		alphaMap.alphaSource = 0
		alphaMap.monoOutput = 1
		alphaMap.rgbOutput = 1
		alphaMap.premultAlpha = false
		try (alphaMap.reload()) catch ()
		return alphaMap
	)
	undefined
)

global RwOpenPbrUsesWhiteBase = fn RwOpenPbrUsesWhiteBase m = (
	m.color.r >= 254.5 and m.color.g >= 254.5 and m.color.b >= 254.5 and m.sp_diffuse >= 0.999
)

global RwOpenPbrTintedBaseMap = fn RwOpenPbrTintedBaseMap tex matColor = (
	if tex == undefined then undefined
	else (
		local tintMap = RGB_Multiply()
		tintMap.name = tex.name + " * RW color"
		tintMap.color1 = matColor
		tintMap.map2 = tex
		tintMap.map2Enabled = true
		tintMap.alphaFrom = 2
		tintMap
	)
)

global RwToOpenPBR = fn RwToOpenPBR m = (
	if (classof m) == Multimaterial do (
		for i = 1 to m.materialList.count do
			m.materialList[i] = RwToOpenPBR m.materialList[i]
		return m
	)
	if (classof m) != RW_Mtl do
		return m
	if not (RwOpenPbrHasOpenPbr undefined) do
		return m

	local pbr = OpenPBR_Material()
	RwOpenPbrConversionCount += 1
	pbr.name = m.name
	pbr.base_color = m.color
	pbr.base_weight = m.sp_diffuse
	pbr.specular_weight = m.sp_specular
	local clampedSpec = m.sp_specular
	if clampedSpec < 0.0 do clampedSpec = 0.0
	if clampedSpec > 1.0 do clampedSpec = 1.0
	pbr.specular_roughness = 1.0 - clampedSpec

	pbr.base_color_map = RwOpenPbrTintedBaseMap m.texmap_texture m.color
	pbr.base_color_map_on = pbr.base_color_map != undefined
	if m.coloralpha < 0.999 or RwOpenPbrBitmapHasAlpha m.texmap_texture then (
		pbr.geometry_opacity_map = RwOpenPbrAlphaMonoCopy m.texmap_texture
		pbr.geometry_opacity_map_on = pbr.geometry_opacity_map != undefined
	) else (
		pbr.geometry_opacity_map = undefined
		pbr.geometry_opacity_map_on = false
	)

	local usesBump = m.matfxeffect == 2 or m.matfxeffect == 4
	local usesEnv = m.matfxeffect == 3 or m.matfxeffect == 4
	if usesBump and m.texmap_bumpmap != undefined do (
		pbr.bump_map = m.texmap_bumpmap
		pbr.bump_map_on = true
		pbr.bump_map_amt = m.bumpmap_amount
	)
	if usesEnv and m.texmap_envmap != undefined do (
		pbr.coat_color_map = m.texmap_envmap
		pbr.coat_color_map_on = true
		pbr.coat_weight = m.envmap_amount
	)
	if m.enEnv do (
		if m.shininess > pbr.coat_weight do
			pbr.coat_weight = m.shininess
	)
	if m.enSpec do (
		pbr.specular_weight = m.specularity
		pbr.specular_color_map = m.specMap
		pbr.specular_color_map_on = pbr.specular_color_map != undefined
	)
	pbr
)

global OpenPBRToRw = fn OpenPBRToRw m = (
	if (classof m) == Multimaterial do (
		for i = 1 to m.materialList.count do
			m.materialList[i] = OpenPBRToRw m.materialList[i]
		return m
	)
	if not (RwOpenPbrHasOpenPbr undefined) do
		return m
	if (classof m) != OpenPBR_Material do
		return m
	if not (try (RW_Mtl != undefined) catch false) do
		return m

	rwSuppressDelegateUpdates = true
	local rw = RW_Mtl()
	RwOpenPbrConversionCount += 1
	rw.name = m.name
	rw.color = m.base_color
	rw.sp_diffuse = m.base_weight
	rw.sp_specular = m.specular_weight
	rw.coloralpha = if m.geometry_opacity_map != undefined then 0.5 else 1.0
	rw.texmap_texture = RwOpenPbrSourceBitmap m.base_color_map

	local hasBump = RwOpenPbrMapEnabled m.bump_map m.bump_map_on
	local hasEnv = RwOpenPbrMapEnabled m.coat_color_map m.coat_color_map_on
	if hasBump and hasEnv then
		rw.matfxeffect = 4
	else if hasBump then
		rw.matfxeffect = 2
	else if hasEnv then
		rw.matfxeffect = 3
	else
		rw.matfxeffect = 1

	if hasBump do (
		rw.texmap_bumpmap = m.bump_map
		rw.bumpmap_amount = m.bump_map_amt
	)
	if hasEnv do (
		rw.texmap_envmap = m.coat_color_map
		rw.envmap_amount = m.coat_weight
		rw.enEnv = true
		rw.shininess = m.coat_weight
	)

	if RwOpenPbrMapEnabled m.specular_color_map m.specular_color_map_on or m.specular_weight > 0.0001 do (
		rw.enSpec = true
		rw.specularity = m.specular_weight
		rw.specMap = m.specular_color_map
	)
	rwSuppressDelegateUpdates = false
	rw.color = rw.color
	rw.texmap_texture = rw.texmap_texture
	rw.matfxeffect = rw.matfxeffect
	rw.enEnv = rw.enEnv
	rw.enSpec = rw.enSpec
	rw
)

global RwOpenPbrConvertScene = fn RwOpenPbrConvertScene converterFn = (
	RwOpenPbrConversionCount = 0
	local nodes = if selection.count > 0 then (selection as array) else (objects as array)
	with redraw off
	(
		for o in nodes where o.material != undefined do (
			local oldMat = o.material
			local newMat = converterFn oldMat
			if newMat != oldMat do
			o.material = newMat
		)
	)
	RwOpenPbrConversionCount
)

utility RwOpenPbrConverter "RW <-> OpenPBR Converter" (
	button rwToPbr "RW to OpenPBR materials" width:160 align:#center
	button pbrToRw "OpenPBR to RW materials" width:160 align:#center

	on rwToPbr pressed do (
		if not (try (RW_Mtl != undefined) catch false) then
			messageBox "RW_Material.ms was not loaded, so RW_Mtl is unavailable." title:"RWIO"
		else if not (RwOpenPbrHasOpenPbr undefined) then
			messageBox "OpenPBR_Material is unavailable in this 3ds Max session." title:"RWIO"
		else (
			local converted = RwOpenPbrConvertScene RwToOpenPBR
			messageBox ((converted as string) + " material(s) converted.") title:"RWIO"
		)
	)

	on pbrToRw pressed do (
		if not (RwOpenPbrHasOpenPbr undefined) then
			messageBox "OpenPBR_Material is unavailable in this 3ds Max session." title:"RWIO"
		else if not (try (RW_Mtl != undefined) catch false) then
			messageBox "RW_Material.ms was not loaded, so RW_Mtl is unavailable." title:"RWIO"
		else (
			local converted = RwOpenPbrConvertScene OpenPBRToRw
			messageBox ((converted as string) + " material(s) converted.") title:"RWIO"
		)
	)
)
