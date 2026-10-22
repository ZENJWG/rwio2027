global rwSuppressDelegateUpdates = false
global rwAlphaCacheFiles = #()
global rwAlphaCacheValues = #()
global rwAlphaMapCacheKeys = #()
global rwAlphaMapCacheMaps = #()

plugin Material RW_Mtl
    name:"RW Material"
    classID:#(0x29b71842, 0x52508b70)
	extends:Standard replaceUI:true version:1
(
	fn rwBitmapHasAlpha tex = (
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
		local cacheIndex = findItem rwAlphaCacheFiles tex.filename
		if cacheIndex > 0 do
			return rwAlphaCacheValues[cacheIndex]

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
		append rwAlphaCacheFiles tex.filename
		append rwAlphaCacheValues hasAlpha
		hasAlpha
	)

	fn rwAlphaMapCacheKey tex = (
		if tex == undefined then undefined
		else if tex.filename != undefined and tex.filename != "" then tex.filename
		else tex
	)

	fn rwGetAlphaOpacityMap tex = (
		local key = rwAlphaMapCacheKey tex
		local cacheIndex = findItem rwAlphaMapCacheKeys key
		if cacheIndex > 0 and rwAlphaMapCacheMaps[cacheIndex] != undefined do
			return rwAlphaMapCacheMaps[cacheIndex]

		local alphamap = copy tex
		alphamap.alphaSource = 0
		alphamap.monoOutput = 1
		alphamap.rgbOutput = 1
		alphamap.premultAlpha = false
		append rwAlphaMapCacheKeys key
		append rwAlphaMapCacheMaps alphamap
		alphamap
	)

	fn rwUpdateDelegateDiffuse tex matColor diffuseAmount = (
		if rwSuppressDelegateUpdates do return false
		if delegate.diffuseMap == undefined or (classof delegate.diffuseMap) != RGB_Multiply do (
			delegate.diffuseMap = RGB_Multiply()
			delegate.diffuseMapEnable = true
		)
		delegate.diffuseMap.color1 = matColor*diffuseAmount
		delegate.diffuseMap.map2 = tex
		delegate.diffuseMap.map2Enabled = true
		delegate.diffuseMap.alphaFrom = 2
		delegate.diffuse = matColor*diffuseAmount
		delegate.showInViewport = true
	)

	fn rwUpdateDelegateOpacity tex alphaAmount = (
		if rwSuppressDelegateUpdates do return false
		try (delegate.opacity = alphaAmount*100.0) catch ()
		if rwBitmapHasAlpha tex then (
			local alphamap = rwGetAlphaOpacityMap tex
			try (delegate.opacityMap = alphamap) catch ()
			try (delegate.opacityMapEnable = true) catch ()
		) else (
			try (delegate.opacityMap = undefined) catch ()
			try (delegate.opacityMapEnable = false) catch ()
		)
	)

	fn rwClearDelegatePreview = (
		if rwSuppressDelegateUpdates do return false
		try (delegate.bumpMap = undefined) catch ()
		try (delegate.bumpMapEnable = false) catch ()
		try (delegate.reflectionMap = undefined) catch ()
		try (delegate.reflectionMapEnable = false) catch ()
		try (delegate.selfIllumMap = undefined) catch ()
		try (delegate.selfIllumMapEnable = false) catch ()
		try (delegate.specularLevelMap = undefined) catch ()
		try (delegate.specularLevelMapEnable = false) catch ()
		try (delegate.specularLevel = 0.0) catch ()
		try (delegate.glossiness = 0.0) catch ()
	)

	parameters main rollout:params (
		color 				type:#color default:white 	ui:color
		coloralpha				type:#float default:1.0     ui:alpha
		sp_ambient					type:#float default:1.0 	ui:amb
		sp_diffuse				type:#float default:1.0 	ui:dif
		sp_specular                type:#float default:1.0 	ui:spec
		texmap_texture 			type:#TextureMap  			ui:texture

		matfxeffect   type:#integer default:1 ui:fxtype
		texmap_envmap   type:#TextureMap ui:envmap
		envmap_amount  type:#float default:0.5 ui:envcoef
		texmap_bumpmap  type:#TextureMap ui:bumpmap
		bumpmap_amount type:#float default:0.5 ui:bumpcoef
		texmap_pass2    type:#TextureMap ui:dpmap
		pass2_srcblend type:#integer default:2 ui:srcblend
		pass2_destblend type:#integer default:2 ui:destblend

		on color set val do (
			rwUpdateDelegateDiffuse texmap_texture val sp_diffuse
		)
		on sp_diffuse set val do (
			rwUpdateDelegateDiffuse texmap_texture color val
		)
		on coloralpha set val do (
			rwUpdateDelegateOpacity texmap_texture val
		)
		on texmap_texture set tex do (
			rwUpdateDelegateDiffuse tex color sp_diffuse
			rwUpdateDelegateOpacity tex coloralpha
		)
	)
	parameters rsExt rollout:rsExt (
		enEnv type:#boolean default:false ui:enEnv
		enSpec type:#boolean default:false ui:enSpec

		scaleX      type:#float default:1.0 ui:sclX
		scaleY      type:#float default:1.0 ui:sclY
		transScaleX type:#float default:1.0 ui:trSclX
		transScaleY type:#float default:1.0 ui:trSclY
		shininess   type:#float default:0.25 ui:shininess

		specularity  type:#float default:0.5 ui:specularity
		specMap     type:#TextureMap ui:specmap

	)
	--parameters render rollout:render (
	--	pluginID      type:#integer default:0 ui:pluginID
	--	pluginData    type:#integer default:0 ui:pluginData
	--)
	rollout params  "RenderWare Material" (
		group "RpMaterial Parameters" (
			label           colorlbl    "Color" pos:[57, 30]
 			colorpicker		color 		"" width:51 height:16 pos:[89, 28]
			label           alphalbl    "Alpha" pos:[224, 30]
			spinner			alpha		""	width:49 height:16 type:#float range:[0,1.0,1.0] pos:[263, 28]

			label           surfp1lbl   "Surface" pos:[44, 62]
			label           surfp2lbl   "Properties" pos:[32, 75]
			label           amblbl      "Ambient" pos:[96, 50]
			spinner 		amb         ""	range:[0,1,0] width:49 height:16 pos:[92, 67]
			label           diflbl      "Diffuse" pos:[182, 50]
			spinner 		dif         ""	range:[0,1,0] width:49 height:16 pos:[177, 67]
			label           speclbl     "Specular" pos:[267, 50]
			spinner 		spec        ""	range:[0,1,0] width:49 height:16 pos:[263, 67]

			label           texlbl      "Texture" pos:[42, 98]
			mapbutton		texture     "None" pos:[92, 93] width:219 height:21
		)
		group "MatFX Options" (
			label        fxlbl "Effect Type" pos:[25,147]
			dropdownlist fxtype "" pos:[92, 145] width:219 height:21 items:#("rpMATFXEFFECTNULL","rpMATFXEFFECTBUMPMAP","rpMATFXEFFECTENVMAP","rpMATFXEFFECTBUMPENVMAP","rpMATFXEFFECTDUAL","rpMATFXEFFECTUVTRANSFORM","rpMATFXEFFECTDUALUVTRANSFORM")
			label        envmaplbl "Env Map" pos:[39,173]
			mapbutton    envmap    "None" pos:[92, 171] width:168 height:21
			spinner      envcoef   ""	range:[0,1,0.5] width:49 height:16 pos:[263, 174]
			label        bumpmaplbl "Bump Map" pos:[32,199]
			mapbutton    bumpmap   "None" pos:[92, 197] width:168 height:21
			spinner      bumpcoef  ""	range:[0,1,0.5] width:49 height:16 pos:[263, 200]
			label        dpmaplbl "Dual Pass" pos:[35,225]
			mapbutton    dpmap     "None" pos:[92, 223] width:219 height:21
			label        srclbl "Src Blend" pos:[37,251]
			dropdownlist srcblend "" pos:[92, 249] width:219 height:21 items:#("rpBLENDZERO", "rpBLENDONE", "rpBLENDSRCCOLOR", "rpBLENDINVSRCCOLOR", "rpBLENDSRCALPHA", "rpBLENDINVSRCALPHA", "rpBLENDDESTALPHA", "rpBLENDINVDESTALPHA", "rpBLENDDESTCOLOR", "rpBLENDINVDESTCOLOR", "rpBLENDSRCALPHASAT")
			label        destlbl "Dest Blend" pos:[30,274]
			dropdownlist destblend "" pos:[92, 271] width:219 height:21 items:#("rpBLENDZERO", "rpBLENDONE", "rpBLENDSRCCOLOR", "rpBLENDINVSRCCOLOR", "rpBLENDSRCALPHA", "rpBLENDINVSRCALPHA", "rpBLENDDESTALPHA", "rpBLENDINVDESTALPHA", "rpBLENDDESTCOLOR", "rpBLENDINVDESTCOLOR", "rpBLENDSRCALPHASAT")
		)
 	)
	rollout rsExt "Rockstar Extensions" (
		group "Env mat Parameters" (
			checkbox  enEnv "Enable" pos:[20, 30]
			label     shinlbl "Shininess" pos:[208,30]
			spinner   shininess ""	range:[0,1,0.25] width:49 height:16 pos:[263, 30]
			label     sclXlbl "ScaleX" pos:[28,53]
			spinner   sclX ""	range:[-1000,1000,1.0] width:49 height:16 pos:[17, 70]
			label     sclYlbl "ScaleY" pos:[110,53]
			spinner   sclY ""	range:[-1000,1000,1.0] width:49 height:16 pos:[99, 70]
			label     tsclXlbl "TransSclX" pos:[183,53]
			spinner   trSclX ""	range:[-1000,1000,1.0] width:49 height:16 pos:[181, 70]
			label     tsclYlbl "TransSclY" pos:[265,53]
			spinner   trSclY ""	range:[-1000,1000,1.0] width:49 height:16 pos:[263, 70]
		)
		group "Spec mat Parameters" (
			checkbox  enSpec "Enable" pos:[20, 122]
			label     speclbl "Specularity" pos:[200,122]
			spinner   specularity ""	range:[0,1,0.5] width:49 height:16 pos:[263, 122]
			label     specmaplbl "Spec Map" pos:[30,151]
			mapbutton specmap "None" pos:[92, 148] width:219 height:21
		)
	)
	--rollout render "Right to Render" (
	--		label idlbl "Plugin" pos:[10, 12]
	--		editText pluginID "" pos:[50, 10] width:100
	--		label datalbl "Data" pos:[185, 12]
	--		editText pluginData "" pos:[220, 10] width:100
	--)

	on create do (
		this.name = delegate.name
		if not rwSuppressDelegateUpdates do (
			delegate.diffuseMap = RGB_Multiply()
			delegate.diffuseMapEnable = true
			rwUpdateDelegateDiffuse texmap_texture color sp_diffuse
			rwUpdateDelegateOpacity texmap_texture coloralpha
			rwClearDelegatePreview()
		)
	)
	on load do (
		this.name = delegate.name
		if not rwSuppressDelegateUpdates do (
			rwUpdateDelegateDiffuse texmap_texture color sp_diffuse
			rwUpdateDelegateOpacity texmap_texture coloralpha
			rwClearDelegatePreview()
		)
	)
)
