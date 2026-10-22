#define NOMINMAX
#include "dffimp.h"

const char *getAsciiStr(const TCHAR *str);
void setUserProps(ICustAttribContainer *cc, const TCHAR *datatype, rw::UserDataExtension *ud);

static const char *RWIO_QUAD_EDGE_DATA = "rwio_quad_edge_visibility";
static const Class_ID RGB_MULTIPLY_CLASS_ID(656, 0);

// stolen from RW
static void
GetFileName(char *outFileName, char *fullPathName)
{
	int strLen;
	char *start, *end;
	BOOL strippedext = FALSE;

	start = strrchr(fullPathName, '\\');
	if(start)
	    start++;
	else
	    start = fullPathName;
	end = strrchr(start, '.');
	if(end){
	    *end = '\0';
	    strippedext = TRUE;
	}else
	    end = start + strlen(start);
	strLen = end - start;
	if(strLen >= MAX_PATH)
		strLen = MAX_PATH-1;
	strncpy(outFileName, start, strLen + 1);
	outFileName[strLen] = '\0';
	if(strippedext) *end = '.';
}

static bool
findBitmapTexmap(Texmap *tex, BitmapTex **bitmap)
{
	if(tex == NULL || bitmap == NULL)
		return false;
	if(tex->ClassID() == Class_ID(BMTEX_CLASS_ID, 0)){
		*bitmap = (BitmapTex*)tex;
		return true;
	}

	// RWIO uses RGB_Multiply only as a viewport tint wrapper for diffuse maps.
	// Export the original bitmap inside it so DFF texture names stay unchanged.
	if(tex->ClassID() == RGB_MULTIPLY_CLASS_ID){
		for(int i = 0; i < tex->NumSubTexmaps(); i++)
			if(findBitmapTexmap(tex->GetSubTexmap(i), bitmap))
				return true;
	}
	return false;
}

static bool
bitmapHasAlpha(const MCHAR *filename)
{
	static std::vector<MSTR> alphaFiles;
	static std::vector<MSTR> opaqueFiles;

	MSTR file(filename);
	for(size_t i = 0; i < alphaFiles.size(); i++)
		if(alphaFiles[i] == file)
			return true;
	for(size_t i = 0; i < opaqueFiles.size(); i++)
		if(opaqueFiles[i] == file)
			return false;

	BitmapInfo bi(filename);
	Bitmap *bitmap = NULL;
	bool hasAlpha = false;
	if(TheManager && TheManager->LoadInto(&bi, &bitmap) == BMMRES_SUCCESS && bitmap){
		int width = bitmap->Width();
		int height = bitmap->Height();
		if(width > 0 && height > 0){
			std::vector<BMM_Color_64> pixels(width);
			for(int y = 0; y < height && !hasAlpha; y++){
				if(bitmap->GetPixels(0, y, width, pixels.data())){
					for(int x = 0; x < width; x++){
						if(pixels[x].a < 0xFFFF){
							hasAlpha = true;
							break;
						}
					}
				}
			}
		}
		bitmap->DeleteThis();
	}
	if(hasAlpha)
		alphaFiles.push_back(file);
	else
		opaqueFiles.push_back(file);
	return hasAlpha;
}

static bool
textureUsesFileAlpha(Texmap *tex)
{
	BitmapTex *bmtex = NULL;
	if(!findBitmapTexmap(tex, &bmtex))
		return false;
	return bmtex->GetAlphaSource() != ALPHA_NONE && bitmapHasAlpha(bmtex->GetMapName());
}

static rw::Texture*
convertTexture(Texmap *tex, Texmap *mask)
{
	using namespace rw;

	char buf[MAX_PATH];
	BitmapTex *bmtex = NULL;
	TCHAR *name = NULL;
	TCHAR *maskname = NULL;
	if(!findBitmapTexmap(tex, &bmtex))
		return NULL;
	name = (TCHAR*)bmtex->GetMapName();
	if(mask){
		BitmapTex *bmmask = NULL;
		if(findBitmapTexmap(mask, &bmmask))
			maskname = (TCHAR*)bmmask->GetMapName();
	}

	Texture *rwt = Texture::create(NULL);
#ifdef _UNICODE
	char fuckmax[MAX_PATH];
	wcstombs(fuckmax, name, MAX_PATH);
	fuckmax[MAX_PATH-1] = '\0';
	GetFileName(buf, fuckmax);
#else
	GetFileName(buf, name);
#endif
	strncpy(rwt->name, buf, 31);
	rwt->name[31] = '\0';
	if(maskname){
#ifdef _UNICODE
		char fuckmax[MAX_PATH];
		wcstombs(fuckmax, maskname, MAX_PATH);
		fuckmax[MAX_PATH-1] = '\0';
		GetFileName(buf, fuckmax);
#else
		GetFileName(buf, maskname);
#endif
		strncpy(rwt->mask, buf, 31);
		rwt->mask[31] = '\0';
	}else if(textureUsesFileAlpha(tex)){
		strncpy(rwt->mask, rwt->name, 31);
		rwt->mask[31] = '\0';
	}

	int filter = Texture::NEAREST;
	int wrapu = Texture::CLAMP;
	int wrapv = Texture::CLAMP;
	switch(bmtex->GetFilterType()){
	case FILTER_SAT:
		filter = Texture::LINEAR;
		break;
	case FILTER_PYR:
		filter = Texture::LINEARMIPLINEAR;
		break;
	}
	int tiling = bmtex->GetTextureTiling();
	if(tiling & U_WRAP)
		wrapu = Texture::WRAP;
	if(tiling & U_MIRROR)
		wrapu = Texture::MIRROR;
	if(tiling & V_WRAP)
		wrapv = Texture::WRAP;
	if(tiling & V_MIRROR)
		wrapv = Texture::MIRROR;
	rwt->filterAddressing = (wrapv << 12) | (wrapu << 8) | filter;

	if(DFFExport::exportObjNames && DFFExport::exportObjNames_Tex){
		int ud = UserDataArray::textureAdd(rwt, getAsciiStr(DFFExport::exportObjNames_Entry), USERDATASTRING, 1);
		UserDataArray::textureGet(rwt, ud)->setString(0, getAsciiStr(tex->GetName()));
	}
	setUserProps(tex->GetCustAttribContainer(), _T("Texture"), UserDataExtension::get(rwt));

	return rwt;
}

static float
clamp01(float value)
{
	if(value < 0.0f)
		return 0.0f;
	if(value > 1.0f)
		return 1.0f;
	return value;
}

static bool
isOpenPBRMaterial(Mtl *mtl)
{
	if(mtl == NULL)
		return false;

	MSTR className = mtl->ClassName();
	return _tcsstr(className.data(), _T("OpenPBR")) != NULL;
}

static IParamBlock2*
findParamBlockWithParam(Animatable *anim, const MCHAR *prop)
{
	if(anim == NULL)
		return NULL;
	for(int i = 0; i < anim->NumParamBlocks(); i++){
		IParamBlock2 *pb = anim->GetParamBlock(i);
		if(pb && pb->GetDesc() && pb->GetDesc()->NameToIndex(prop) >= 0)
			return pb;
	}
	return NULL;
}

static bool
getParamColor(Animatable *anim, const MCHAR *prop, Color &value)
{
	IParamBlock2 *pb = findParamBlockWithParam(anim, prop);
	Interval valid = FOREVER;
	return pb && pb->GetValueByName(prop, 0, value, valid);
}

static bool
getParamFloat(Animatable *anim, const MCHAR *prop, float &value)
{
	IParamBlock2 *pb = findParamBlockWithParam(anim, prop);
	Interval valid = FOREVER;
	return pb && pb->GetValueByName(prop, 0, value, valid);
}

static bool
getParamBool(Animatable *anim, const MCHAR *prop, bool &value)
{
	IParamBlock2 *pb = findParamBlockWithParam(anim, prop);
	Interval valid = FOREVER;
	BOOL b = FALSE;
	if(pb && pb->GetValueByName(prop, 0, b, valid)){
		value = b != FALSE;
		return true;
	}
	return false;
}

static Texmap*
getParamTexmap(Animatable *anim, const MCHAR *prop)
{
	IParamBlock2 *pb = findParamBlockWithParam(anim, prop);
	Interval valid = FOREVER;
	Texmap *map = NULL;
	if(pb && pb->GetValueByName(prop, 0, map, valid))
		return map;
	return NULL;
}

static Texmap*
getOpenPBREnabledMap(Mtl *mtl, const MCHAR *prop)
{
	Texmap *map = getParamTexmap(mtl, prop);
	if(map == NULL)
		return NULL;

	MCHAR enableProp[128];
	_sntprintf(enableProp, 128, _T("%s_on"), prop);
	enableProp[127] = '\0';
	bool enabled = true;
	getParamBool(mtl, enableProp, enabled);
	return enabled ? map : NULL;
}

static void
storeQuadEdgeData(rw::Geometry *geo, Mesh *mesh)
{
	if(!DFFExport::exportQuadData || geo == NULL || mesh == NULL || mesh->numFaces <= 0)
		return;

	bool hasHiddenEdge = false;
	for(int i = 0; i < mesh->numFaces && !hasHiddenEdge; i++)
		for(int j = 0; j < 3; j++)
			if(!mesh->faces[i].getEdgeVis(j)){
				hasHiddenEdge = true;
				break;
			}
	if(!hasHiddenEdge)
		return;

	int slot = rw::UserDataArray::geometryFindIndex(geo, RWIO_QUAD_EDGE_DATA);
	if(slot >= 0)
		rw::UserDataArray::geometryRemove(geo, slot);
	slot = rw::UserDataArray::geometryAdd(geo, RWIO_QUAD_EDGE_DATA, rw::USERDATAINT, mesh->numFaces);
	rw::UserDataArray *edgeData = rw::UserDataArray::geometryGet(geo, slot);
	if(edgeData == NULL)
		return;
	for(int i = 0; i < mesh->numFaces; i++){
		int flags = 0;
		for(int j = 0; j < 3; j++)
			if(mesh->faces[i].getEdgeVis(j))
				flags |= 1 << j;
		edgeData->setInt(i, flags);
	}
}

static rw::Material*
convertMaterial(Mtl *mtl)
{
	using namespace rw;
	Color c;
	rw::Material *rwm = rw::Material::create();
	Interval valid = FOREVER;

	Texmap *difftex = NULL;
	Texmap *masktex = NULL;
	Texmap *envtex = NULL;
	Texmap *bumptex = NULL;
	Texmap *dualtex = NULL;
	float bumpCoef = 0.0f, envCoef = 0.0f;
	int srcblend = 0, dstblend = 0;
	int effects = MatFX::NOTHING;
	// standard material
	if(mtl->ClassID() == Class_ID(DMTL_CLASS_ID, 0)){
		StdMat* std = (StdMat*)mtl;
		c = std->GetDiffuse(0);
		rwm->color.red   = c.r*255.0f;
		rwm->color.green = c.g*255.0f;
		rwm->color.blue  = c.b*255.0f;
		rwm->color.alpha = std->GetOpacity(0)*255.0f;
		rwm->surfaceProps.specular = std->GetShinStr(0);
		if(std->MapEnabled(ID_DI))
			difftex = std->GetSubTexmap(ID_DI);
		if(std->MapEnabled(ID_OP))
			masktex = std->GetSubTexmap(ID_OP);
		if(std->MapEnabled(ID_BU))
			bumptex = std->GetSubTexmap(ID_BU);
		if(std->MapEnabled(ID_RL))
			envtex = std->GetSubTexmap(ID_RL);
		bumpCoef = std->GetTexmapAmt(ID_BU, 0);
		envCoef = std->GetTexmapAmt(ID_RL, 0);
		if(bumptex)
			effects |= MatFX::BUMPMAP;
		if(envtex)
			effects |= MatFX::ENVMAP;
	}else if(isOpenPBRMaterial(mtl)){
		c = Color(1.0f, 1.0f, 1.0f);
		getParamColor(mtl, _T("base_color"), c);
		rwm->color.red   = clamp01(c.r)*255.0f;
		rwm->color.green = clamp01(c.g)*255.0f;
		rwm->color.blue  = clamp01(c.b)*255.0f;
		rwm->color.alpha = 255.0f;

		rwm->surfaceProps.ambient = 1.0f;
		rwm->surfaceProps.diffuse = 1.0f;
		rwm->surfaceProps.specular = 0.0f;
		getParamFloat(mtl, _T("base_weight"), rwm->surfaceProps.diffuse);
		getParamFloat(mtl, _T("specular_weight"), rwm->surfaceProps.specular);

		difftex = getOpenPBREnabledMap(mtl, _T("base_color_map"));
		if(difftex == NULL)
			difftex = getParamTexmap(mtl, _T("base_color_map"));
		masktex = getOpenPBREnabledMap(mtl, _T("geometry_opacity_map"));
		bumptex = getOpenPBREnabledMap(mtl, _T("bump_map"));
		envtex = getOpenPBREnabledMap(mtl, _T("coat_color_map"));

		bumpCoef = 1.0f;
		envCoef = 0.0f;
		getParamFloat(mtl, _T("bump_map_amt"), bumpCoef);
		getParamFloat(mtl, _T("coat_weight"), envCoef);
		if(bumptex)
			effects |= MatFX::BUMPMAP;
		if(envtex)
			effects |= MatFX::ENVMAP;

		if(envtex || envCoef > 0.0001f){
			gta::EnvMat *env = new gta::EnvMat;
			*PLUGINOFFSET(gta::EnvMat*, rwm, gta::envMatOffset) = env;
			env->scaleX = 8.0f;
			env->scaleY = 8.0f;
			env->transScaleX = 8.0f;
			env->transScaleY = 8.0f;
			env->shininess = clamp01(envCoef)*255.0f;
		}

		Texmap *specmap = getOpenPBREnabledMap(mtl, _T("specular_color_map"));
		rw::Texture *spectex = convertTexture(specmap, NULL);
		if(spectex){
			gta::SpecMat *spec = new gta::SpecMat;
			*PLUGINOFFSET(gta::SpecMat*, rwm, gta::specMatOffset) = spec;
			spec->specularity = rwm->surfaceProps.specular;
			spec->texture = spectex;
		}
	}else if(mtl->ClassID() == Class_ID(0x29b71842, 0x52508b70)){	// my very own RW Material
		IParamBlock2 *pb = mtl->GetParamBlock(0);
		float alpha;
		int hasEnv, hasSpec;
		pb->GetValue(mat_color, 0, c, valid);
		pb->GetValue(mat_coloralpha, 0, alpha, valid);
		rwm->color.red   = c.r*255.0f;
		rwm->color.green = c.g*255.0f;
		rwm->color.blue  = c.b*255.0f;
		rwm->color.alpha = alpha*255.0f;
		pb->GetValue(mat_sp_ambient, 0, rwm->surfaceProps.ambient, valid);
		pb->GetValue(mat_sp_diffuse, 0, rwm->surfaceProps.diffuse, valid);
		pb->GetValue(mat_sp_specular, 0, rwm->surfaceProps.specular, valid);
		pb->GetValue(mat_texmap_texture, 0, difftex, valid);

		pb->GetValue(mat_matfxeffect, 0, effects, valid);
		effects -= 1;
		pb->GetValue(mat_texmap_envmap, 0, envtex, valid);
		pb->GetValue(mat_envmap_amount, 0, envCoef, valid);
		pb->GetValue(mat_texmap_bumpmap, 0, bumptex, valid);
		pb->GetValue(mat_bumpmap_amount, 0, bumpCoef, valid);
		pb->GetValue(mat_texmap_pass2, 0, dualtex, valid);
		pb->GetValue(mat_pass2_srcblend, 0, srcblend, valid);
		pb->GetValue(mat_pass2_destblend, 0, dstblend, valid);

		// R* extensions
		IParamBlock2 *pbr = mtl->GetParamBlock(1);
		pbr->GetValue(mat_enEnv, 0, hasEnv, valid);
		if(hasEnv){
			float f;
			gta::EnvMat *env = new gta::EnvMat;
			*PLUGINOFFSET(gta::EnvMat*, rwm, gta::envMatOffset) = env;
			pbr->GetValue(mat_scaleX, 0, f, valid);
			env->scaleX = f*8.0f;
			pbr->GetValue(mat_scaleY, 0, f, valid);
			env->scaleY = f*8.0f;
			pbr->GetValue(mat_transScaleX, 0, f, valid);
			env->transScaleX = f*8.0f;
			pbr->GetValue(mat_transScaleY, 0, f, valid);
			env->transScaleY = f*8.0f;
			pbr->GetValue(mat_shininess, 0, f, valid);
			env->shininess = f*255.0f;
		}

		pbr->GetValue(mat_enSpec, 0, hasSpec, valid);
		if(hasSpec){
			Texmap *specmap;
			float specularity;
			pbr->GetValue(mat_specMap, 0, specmap, valid);
			rw::Texture *spectex = convertTexture(specmap, NULL);
			if(spectex){
				gta::SpecMat *spec = new gta::SpecMat;
				*PLUGINOFFSET(gta::SpecMat*, rwm, gta::specMatOffset) = spec;
				pbr->GetValue(mat_specularity, 0, specularity, valid);
				spec->specularity = specularity;
				spec->texture = spectex;
			}
		}
	}
	rwm->texture = convertTexture(difftex, masktex);

	if(effects != MatFX::NOTHING){
		MatFX::setEffects(rwm, effects);
		MatFX *matfx = MatFX::get(rwm);
		if(effects == MatFX::BUMPMAP || effects == MatFX::BUMPENVMAP){
			matfx->setBumpTexture(convertTexture(bumptex, NULL));
			matfx->setBumpCoefficient(bumpCoef);
		}
		if(effects == MatFX::ENVMAP || effects == MatFX::BUMPENVMAP){
			matfx->setEnvTexture(convertTexture(envtex, NULL));
			matfx->setEnvCoefficient(envCoef);
		}
		if(effects == MatFX::DUAL || effects == MatFX::DUALUVTRANSFORM){
			matfx->setDualTexture(convertTexture(dualtex, NULL));
			matfx->setDualSrcBlend(srcblend);
			matfx->setDualDestBlend(dstblend);
		}
	}

	if(DFFExport::exportObjNames && DFFExport::exportObjNames_Mat){
		int ud = UserDataArray::materialAdd(rwm, getAsciiStr(DFFExport::exportObjNames_Entry), USERDATASTRING, 1);
		UserDataArray::materialGet(rwm, ud)->setString(0, getAsciiStr(mtl->GetName()));
	}
	setUserProps(mtl->GetCustAttribContainer(), _T("Material"), UserDataExtension::get(rwm));

	return rwm;
}

static void
convertMaterials(rw::Geometry *geo, INode *node)
{
	using namespace rw;

	Mtl *mtl = node->GetMtl();
	rw::Material *mat;
	if(mtl == NULL){
		mat = rw::Material::create();
		geo->matList.appendMaterial(mat);
		DWORD wire = node->GetWireColor();
		mat->color.red = GetRValue(wire);
		mat->color.green = GetGValue(wire);
		mat->color.blue = GetBValue(wire);
	}else if(mtl->ClassID() == Class_ID(MULTI_CLASS_ID, 0)){
		for(int i = 0; i < mtl->NumSubMtls(); i++){
			Mtl *submtl = mtl->GetSubMtl(i);
			if(submtl)
				geo->matList.appendMaterial(convertMaterial(submtl));
			else
				geo->matList.appendMaterial(rw::Material::create());
		}
	}else
		geo->matList.appendMaterial(convertMaterial(mtl));
	// Multi/Sub-Object materials can have zero slots; prevent modulo by zero
	// when assigning face material IDs during geometry conversion.
	if(geo->matList.numMaterials == 0)
		geo->matList.appendMaterial(rw::Material::create());
}

// Gets the first n consecutive UV channels
static int
getNumUVSets(Mesh *mesh)
{
	int n = 0;
	for(int i = 0; i < 8; i++){
		if(mesh->mapFaces(MAP_TEXCOORD0+i) &&
		   mesh->getNumMapVerts(MAP_TEXCOORD0+i))
			n++;
		else
			break;
	}
	return n;
}

static bool
hasSkinModifier(INode *node)
{
	Object *obj = node->GetObjectRef();
	while(obj && obj->SuperClassID() == GEN_DERIVOB_CLASS_ID){
		IDerivedObject *derived = (IDerivedObject*)obj;
		for(int i = 0; i < derived->NumModifiers(); i++)
			if(derived->GetModifier(i)->ClassID() == SKIN_CLASSID)
				return true;
		obj = derived->GetObjRef();
	}
	return false;
}

static bool
getTriExplicitNormal(Mesh *mesh, int face, int corner, Point3 &normal)
{
	if(mesh == NULL)
		return false;
	MeshNormalSpec *normalSpec = mesh->GetSpecifiedNormals();
	if(normalSpec == NULL || face >= normalSpec->GetNumFaces())
		return false;
	MeshNormalFace &normalFace = normalSpec->Face(face);
	if(!normalFace.GetSpecified(corner))
		return false;
	int normalId = normalFace.GetNormalID(corner);
	if(normalId < 0 || normalId >= normalSpec->GetNumNormals())
		return false;
	normal = normalSpec->Normal(normalId);
	return true;
}

static bool
getPolyExplicitNormal(::Object *obj, int face, int corner, Point3 &normal)
{
	if(obj == NULL || obj->ClassID() != polyObjectClassID)
		return false;
	PolyObject *poly = (PolyObject*)obj;
	MNMesh &mnmesh = poly->GetMesh();
	if(face < 0 || face >= mnmesh.numf || corner < 0 || mnmesh.f[face].deg != 3)
		return false;
	MNNormalSpec *normalSpec = mnmesh.GetSpecifiedNormals();
	if(normalSpec == NULL || face >= normalSpec->GetNumFaces())
		return false;
	normalSpec->SetParent(&mnmesh);
	normalSpec->CheckNormals();
	MNNormalFace &normalFace = normalSpec->Face(face);
	if(!normalFace.GetSpecified(corner))
		return false;
	int normalId = normalFace.GetNormalID(corner);
	if(normalId < 0 || normalId >= normalSpec->GetNumNormals())
		return false;
	normal = normalSpec->Normal(normalId);
	return true;
}

static bool
getSourceExplicitNormal(::Object *sourceObj, Mesh *mesh, int face, int corner, Point3 &normal)
{
	if(getTriExplicitNormal(mesh, face, corner, normal))
		return true;
	return getPolyExplicitNormal(sourceObj, face, corner, normal);
}

rw::Geometry*
DFFExport::convertGeometry(INode *node, int **vertexmap)
{
	using namespace rw;

	::Object *obj = node->EvalWorldState(0).obj;
	if(obj == NULL || !obj->CanConvertToType(triObjectClassID))
		return nil;
	TriObject *tri = (TriObject*)obj->ConvertToType(0, triObjectClassID);
	// This happens with "Spray" objects for instance...
	// TODO: figure out a better way of handling this
	if(tri == nil)
		return nil;
	::Mesh *mesh = &tri->mesh;

	//lprintf("geometry: %s\n", obj->GetObjectName());

	int mask = 1;
	int numTris = mesh->numFaces;
	int numVertices = 0;
	int prelitUDProp;
	if(!node->GetUserPropInt(_T("prelit"), prelitUDProp))
		prelitUDProp = -1;

	int32 flags = Geometry::POSITIONS;
	if(exportTristrip)
		flags |= Geometry::TRISTRIP;
	if(exportLit)
		flags |= Geometry::LIGHT;
	if(exportNormals){
		mask |= 0x10;
		flags |= Geometry::NORMALS;
	}
	if(exportPrelit || prelitUDProp > 0) {
		mask |= 0x100;
		flags |= Geometry::PRELIT;
	}
	if(exportExtraColors)
		mask |= 0x200;
	int numUV = getNumUVSets(mesh);
	// TODO: allow more!
	// limited by the SA vertex below atm...
	if(numUV > 2)	numUV = 2;
	if(numUV == 1)
		flags |= Geometry::TEXTURED;
	if(numUV > 1)
		flags |= Geometry::TEXTURED2;
	for(int i = 0; i < numUV; i++)
		mask |= 0x1000 << i;

	// if skin
	//	mask |= 0x10000;

	//lprintf(_T(" %x %d %d\n"), flags, numTris*3, numTris);
	flags |= numUV << 16;
	Geometry *geo = Geometry::create(numTris*3, numTris, flags);
	if(mask & 0x200)
		gta::allocateExtraVertColors(geo);
	convertMaterials(geo, node);
	if(geo->hasColoredMaterial())
		geo->flags |= Geometry::MODULATE;
	geo->numVertices = 0;

	int *remap = NULL;
	bool preserveSkinVertexIdentity = DFFExport::exportSkin && hasSkinModifier(node);
	if(vertexmap){
		*vertexmap = remap = new int[numTris*3];
		for(int i = 0; i < numTris*3; i++)
			remap[i] = -1;
	}

	mesh->checkNormals(TRUE);
	gta::SaVert v;
	for(int i = 0; i < numTris; i++){
		for(int j = 0; j < 3; j++){
			// vertex
			int meshVertIdx = mesh->faces[i].v[j];
			int idx = meshVertIdx;
			v.p.x = mesh->verts[meshVertIdx].x;
			v.p.y = mesh->verts[meshVertIdx].y;
			v.p.z = mesh->verts[meshVertIdx].z;

			// normal
			if(mask & 0x10){
				// Get render normal
				RVertex *rv = &mesh->getRVert(idx);
				int numN = rv->rFlags & NORCT_MASK;
				Point3 p3n = mesh->getFaceNormal(i);
				if(numN == 1)
					p3n = rv->rn.getNormal();
				else
					for(int k = 0; k < numN; k++)
						if(rv->ern[k].getSmGroup() &
						   mesh->faces[i].getSmGroup()){
							p3n = rv->ern[k].getNormal();
							break;
						}
				// If there's an explicit normal, use that instead.
				getSourceExplicitNormal(obj, mesh, i, j, p3n);
				v.n.x = p3n.x;
				v.n.y = p3n.y;
				v.n.z = p3n.z;
			}

			// prelight
			if(mask & 0x100){
				if(mesh->vcFace){
					idx = mesh->vcFace[i].t[j];
					v.c.red   = mesh->vertCol[idx].x*255;
					v.c.green = mesh->vertCol[idx].y*255;
					v.c.blue  = mesh->vertCol[idx].z*255;
				}else
					v.c.red = v.c.green = v.c.blue = 255;
				if(mesh->mapFaces(MAP_ALPHA)){
					idx = mesh->mapFaces(MAP_ALPHA)[i].t[j];
					v.c.alpha = mesh->mapVerts(MAP_ALPHA)[idx].x*255;
				}else
					v.c.alpha = 255;
			}

			// SA extra colors
			if(mask & 0x200){
				if(mesh->mapFaces(MAP_EXTRACOLORS)){
					idx = mesh->mapFaces(MAP_EXTRACOLORS)[i].t[j];
					UVVert *col = mesh->mapVerts(MAP_EXTRACOLORS);
					v.c1.red   = col[idx].x*255;
					v.c1.green = col[idx].y*255;
					v.c1.blue  = col[idx].z*255;
				}else
					v.c1.red = v.c1.green = v.c1.blue = 255;
				if(mesh->mapFaces(MAP_EXTRAALPHA)){
					idx = mesh->mapFaces(MAP_EXTRAALPHA)[i].t[j];
					v.c1.alpha = mesh->mapVerts(MAP_EXTRAALPHA)[idx].x*255;
				}else
					v.c1.alpha = 255;
			}

			// tex coords
			for(int k = 0; k < numUV; k++){
				idx = mesh->mapFaces(MAP_TEXCOORD0+k)[i].t[j];
				UVVert *uv = mesh->mapVerts(MAP_TEXCOORD0+k);
				// ugly and wrong
				if(k == 0){
					v.t.u = uv[idx].x;
					v.t.v = 1.0f-uv[idx].y;
				}
				if(k == 1){
					v.t1.u = uv[idx].x;
					v.t1.v = 1.0f-uv[idx].y;
				}
			}
			idx = gta::findSAVertex(geo, NULL, mask, &v);
			if(idx >= 0 && preserveSkinVertexIdentity && remap && remap[idx] != meshVertIdx)
				idx = -1;
			if(idx < 0){
				idx = geo->numVertices++;
				insertSAVertex(geo, idx, mask, &v);
			}
			geo->triangles[i].v[j] = idx;
			if(remap)
				remap[idx] = meshVertIdx;
		}
		MtlID id = mesh->getFaceMtlIndex(i);
		geo->triangles[i].matId = id % geo->matList.numMaterials;
	}

	storeQuadEdgeData(geo, mesh);

	geo->buildMeshes();
	geo->removeUnusedMaterials();

	if(tri != obj)
		tri->DeleteMe();

	if(DFFExport::exportObjNames && DFFExport::exportObjNames_Geo){
		int ud = UserDataArray::geometryAdd(geo, getAsciiStr(DFFExport::exportObjNames_Entry), USERDATASTRING, 1);
		char shapename[128];
		snprintf(shapename, 128, "%sShape", getAsciiStr(node->GetName()));
		UserDataArray::geometryGet(geo, ud)->setString(0, shapename);
	}
	ICustAttribContainer *attribs = node->GetCustAttribContainer();
	if(attribs == nil){
		::Object *base = getBaseObject(node);
		if(base)
			attribs = base->GetCustAttribContainer();
	}
	setUserProps(attribs, _T("Object"), UserDataExtension::get(geo));

	return geo;
}

void
DFFExport::convertSkin(rw::Geometry *geo, rw::Frame *frame, INode *node, Modifier *mod, int *map)
{
	using namespace rw;

	ISkin *skin = (ISkin*)mod->GetInterface(I_SKIN);
	if(skin == NULL)
		return;

	int numBones = skin->GetNumBones();
	// NB: numBones need not be equal to numNodes!

	ISkinContextData *skindata = skin->GetContextInterface(node);
	if(skindata == NULL)
		return;
	::Object *obj = node->EvalWorldState(0).obj;
	if(obj == NULL || !obj->CanConvertToType(triObjectClassID))
		return;
	TriObject *tri = (TriObject*)obj->ConvertToType(0, triObjectClassID);
	if(tri == NULL)
		return;
	rw::Skin *rwskin = new rw::Skin;
	rw::Skin::set(geo, rwskin);
	rwskin->init(this->numNodes, numBones, geo->numVertices);
	float *weights = rwskin->weights;
	uint8 *indices = rwskin->indices;
	memset(weights, 0, geo->numVertices*4*sizeof(float));
	memset(indices, 0, geo->numVertices*4*sizeof(uint8));
	::Mesh *mesh = &tri->mesh;

	int numWeights;
	int idx;
	int *bonemap = new int[numBones];
	for(int i = 0; i < numBones; i++)
		bonemap[i] = 0;
	for(int i = 0; i < numBones; i++){
		INode *bone = skin->GetBone(i);
		for(int j = 0; j < this->numNodes; j++)
			if(this->nodearray[j].node == bone){
				bonemap[i] = j;
				break;
			}
		//lprintf("bone: %d %s\n", bonemap[i], bone->GetName());
	}
	for(int i = 0; i < geo->numVertices; i++){
		idx = map[i];
		numWeights = skindata->GetNumAssignedBones(idx);
		if(numWeights > 4) numWeights = 4;
		for(int j = 0; j < numWeights; j++){
			int bone = skindata->GetAssignedBone(idx, j);
			if(bone < 0 || bone >= numBones)
				continue;
			indices[i*4+j] = bonemap[bone];
			weights[i*4+j] = skindata->GetBoneWeight(idx, j);
		}
	}
	delete[] bonemap;

	if(tri != obj)
		tri->DeleteMe();

	/* calculate inverse bone matrices */
	rw::Matrix tmp;
	for(int i = 0; i < this->numNodes; i++){
		// this xforms coordinates in frame's (i.e. the atomic's) space to the node's space
		rw::Matrix::invert(&tmp, this->nodearray[i].frame->getLTM());
		rw::Matrix::mult((rw::Matrix*)&rwskin->inverseMatrices[i*16], frame->getLTM(), &tmp);
	}

	rwskin->findNumWeights(geo->numVertices);
	rwskin->findUsedBones(geo->numVertices);
}
