#define NOMINMAX
#include "dffimp.h"
#include <commctrl.h>
#include <GetCOREInterface.h>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <float.h>
#include <cmath>
#include <climits>

int DFFImport::convertHierarchy = 1;
int DFFImport::autoSmooth = 0;
int DFFImport::prepend;
float DFFImport::smoothingAngle = 45.0f;
int DFFImport::explicitNormals = 1;
int DFFImport::importStdMaterials = 0;
int DFFImport::fixKam = 0;
int DFFImport::vertexChannelDisplay = 0;
int DFFImport::vertexChannelType = nvct_color;
int DFFImport::recoverQuads = 1;
int DFFImport::quadrifyAllPlus = 0;
float DFFImport::quadrifyAllPlusDiagonalRatio = 0.85f;
int DFFImport::debugImport = 0;

std::vector<INode*> DFFImport::lastImported;

static const char *RWIO_QUAD_EDGE_DATA = "rwio_quad_edge_visibility";

// Autodesk documents map channel 0 as Vertex Color and channel 1 as the default texture map.
// The old RWIO import/display path was pointing shaded vertex-channel display at channel 1,
// so projected prelight/CPV looked unchanged in the viewport even when vertCol/vcFace existed.
static const int RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL = 0;

static float
rwioClampQapDiagonalRatio(float v)
{
	if(v < 0.0f)
		return 0.0f;
	if(v > 2.0f)
		return 2.0f;
	return v;
}

static bool geometryHasVertexChannelData(rw::Geometry *g);

static double
rwioSeconds(void)
{
	return GetTickCount64()/1000.0;
}

static void
rwioImportDebug(const TCHAR *fmt, ...)
{
	if(!DFFImport::debugImport || the_listener == NULL || the_listener->edit_stream == NULL)
		return;

	va_list args;
	va_start(args, fmt);
	TCHAR message[1024];
	_vsntprintf(message, 1024, fmt, args);
	message[1023] = '\0';
	the_listener->edit_stream->printf(_T("[RWIO import] "));
	the_listener->edit_stream->printf(_T("%s"), message);
	the_listener->edit_stream->printf(_T("\n"));
	the_listener->edit_stream->flush();
	va_end(args);
}

struct RWIOImportTimer
{
	const TCHAR *label;
	double start;

	RWIOImportTimer(const TCHAR *label) : label(label), start(rwioSeconds())
	{
		rwioImportDebug(_T("BEGIN %s"), label);
	}

	~RWIOImportTimer()
	{
		rwioImportDebug(_T("END   %s: %.3f s"), label, rwioSeconds() - start);
	}
};

char*
getUserName(rw::UserDataExtension *ud)
{
	using namespace rw;
	int32 i;
	UserDataArray *ar;
	int32 n = ud->getCount();
	for(i = 0; i < n; i++){
		ar = ud->get(i);
		if(strcmp(ar->name, "name") == 0 && ar->datatype == rw::USERDATASTRING)
			return ar->getString(0);
	}
	return nil;
}

static int
findFirstVertex(rw::V3d *verts, int n, rw::V3d *v)
{
	// compare as int because NaN != NaN (not seen with vertices yet, but be safe)
	rw::uint32 *iverts, *iv;
	iv = (rw::uint32*)v;
	iverts = (rw::uint32*)verts;
	for(int i = 0; i < n; i++) {
		if(iverts[0] == iv[0] && iverts[1] == iv[1] && iverts[2] == iv[2])
			return i;
		iverts += 3;
	}
	/* shouldn't happen */
	return -1;
}

static void
copyMaxString(MCHAR *dst, const char *src, size_t dstCount)
{
	if(dstCount == 0)
		return;
#ifdef _UNICODE
	mbstowcs(dst, src ? src : "", dstCount);
#else
	strncpy(dst, src ? src : "", dstCount);
#endif
	dst[dstCount-1] = '\0';
}

struct TextureCacheEntry
{
	rw::Texture *texture;
	char name[32];
	int mapChannel;
	BitmapTex *bitmap;
	bool foundFile;
	bool reloaded;
};

static std::vector<TextureCacheEntry> textureCache;
static MCHAR currentImportDirectory[MAX_PATH];

static void
clearTextureCache(void)
{
	textureCache.clear();
}

static void
setCurrentImportDirectory(const TCHAR *filename)
{
	currentImportDirectory[0] = '\0';
	if(filename == NULL)
		return;

	_tcsncpy(currentImportDirectory, filename, MAX_PATH-1);
	currentImportDirectory[MAX_PATH-1] = '\0';
	MCHAR *slash = _tcsrchr(currentImportDirectory, '\\');
	MCHAR *fslash = _tcsrchr(currentImportDirectory, '/');
	if(fslash && (!slash || fslash > slash))
		slash = fslash;
	if(slash)
		slash[1] = '\0';
	else
		currentImportDirectory[0] = '\0';
}

static bool
fileExists(const MCHAR *filename)
{
	return filename && filename[0] && GetFileAttributes(filename) != INVALID_FILE_ATTRIBUTES;
}

struct UInt3Key
{
	rw::uint32 x, y, z;
	bool operator==(const UInt3Key &o) const { return x == o.x && y == o.y && z == o.z; }
};

struct UInt2Key
{
	rw::uint32 x, y;
	bool operator==(const UInt2Key &o) const { return x == o.x && y == o.y; }
};

struct UInt3KeyHash
{
	size_t operator()(const UInt3Key &k) const
	{
		size_t h = k.x;
		h = h*16777619u ^ k.y;
		h = h*16777619u ^ k.z;
		return h;
	}
};

struct UInt2KeyHash
{
	size_t operator()(const UInt2Key &k) const
	{
		size_t h = k.x;
		h = h*16777619u ^ k.y;
		return h;
	}
};

static rw::uint32
floatBits(float f)
{
	rw::uint32 u;
	memcpy(&u, &f, sizeof(u));
	return u;
}

static UInt3Key
v3dKey(const rw::V3d &v)
{
	return { floatBits(v.x), floatBits(v.y), floatBits(v.z) };
}

static UInt2Key
texCoordKey(const rw::TexCoords &v)
{
	return { floatBits(v.u), floatBits(v.v) };
}

struct MeshEdgeKey
{
	int a, b;
	bool operator==(const MeshEdgeKey &o) const { return a == o.a && b == o.b; }
};

struct MeshEdgeKeyHash
{
	size_t operator()(const MeshEdgeKey &k) const
	{
		return size_t(k.a)*16777619u ^ size_t(k.b);
	}
};

static MeshEdgeKey
meshEdgeKey(int a, int b)
{
	if(a > b){
		int t = a;
		a = b;
		b = t;
	}
	return { a, b };
}

struct MeshEdgeUse
{
	int face;
	int edge;
};

struct PendingAtomicNode
{
	INode *parent;
	INode *node;
	Matrix3 tm;
};

static UInt3Key
colorKey(const rw::RGBA &c)
{
	rw::uint32 rgba = (rw::uint32)c.red |
	                  ((rw::uint32)c.green << 8) |
	                  ((rw::uint32)c.blue << 16) |
	                  ((rw::uint32)c.alpha << 24);
	return { rgba, 0, 0 };
}

static void
buildFirstV3dIndices(rw::V3d *values, int count, int *indices)
{
	std::unordered_map<UInt3Key, int, UInt3KeyHash> first;
	first.reserve(count);
	for(int i = 0; i < count; i++){
		UInt3Key key = v3dKey(values[i]);
		auto inserted = first.emplace(key, i);
		indices[i] = inserted.first->second;
	}
}

static void
buildFirstTexCoordIndices(rw::TexCoords *values, int count, int *indices)
{
	std::unordered_map<UInt2Key, int, UInt2KeyHash> first;
	first.reserve(count);
	for(int i = 0; i < count; i++){
		UInt2Key key = texCoordKey(values[i]);
		auto inserted = first.emplace(key, i);
		indices[i] = inserted.first->second;
	}
}

static void
buildFirstColorIndices(rw::RGBA *values, int count, int *indices)
{
	std::unordered_map<UInt3Key, int, UInt3KeyHash> first;
	first.reserve(count);
	for(int i = 0; i < count; i++){
		UInt3Key key = colorKey(values[i]);
		auto inserted = first.emplace(key, i);
		indices[i] = inserted.first->second;
	}
}

static TextureCacheEntry*
findTextureCacheEntry(BitmapTex *bitmap)
{
	for(size_t i = 0; i < textureCache.size(); i++)
		if(textureCache[i].bitmap == bitmap)
			return &textureCache[i];
	return NULL;
}

static void
markTextureForReload(BitmapTex *bitmap)
{
	TextureCacheEntry *entry = findTextureCacheEntry(bitmap);
	if(entry)
		entry->reloaded = false;
}

static void
reloadTextureIfFound(BitmapTex *bitmap)
{
	TextureCacheEntry *entry = findTextureCacheEntry(bitmap);
	if(entry && entry->foundFile && !entry->reloaded){
		double t0 = rwioSeconds();
		bitmap->fnReload();
		entry->reloaded = true;
		rwioImportDebug(_T("texture reload %.3f s: %s"), rwioSeconds() - t0, bitmap->GetMapName());
	}
}

static int
findFirstTexCoord(rw::TexCoords *verts, int n, rw::TexCoords *v)
{
	// compare as int because NaN != NaN and some files have that
	rw::uint32 *iverts, *iv;
	iv = (rw::uint32*)v;
	iverts = (rw::uint32*)verts;
	for(int i = 0; i < n; i++) {
		if(iverts[0] == iv[0] && iverts[1] == iv[1])
			return i;
		iverts += 2;
	}
	/* shouldn't happen */
	return -1;
}

static int
findFirstColor(rw::RGBA *verts, int n, rw::RGBA *v)
{
	for(int i = 0; i < n; i++) {
		if(rw::equal(*verts, *v))
			return i;
		verts++;
	}
	/* shouldn't happen */
	return -1;
}

static void
animateTexture(rw::Animation *anim, BitmapTex *tex)
{
	using namespace rw;
	if(tex == NULL)
		return;
	UVAnimCustomData *cust = (UVAnimCustomData*)anim->customData;
	StdUVGen *uvgen = tex->GetUVGen();
	int channel = uvgen->GetMapChannel();
	if(cust->nodeToUVChannel[0] != channel-1)
		return;
	if(anim->interpInfo->id != 0x1C1){
		lprintf(_T("warning: cannot import animations of type %x\n"),
		        anim->interpInfo->id);
		return;
	}
	extendAnimRange(anim->duration);
	UVAnimKeyFrame *kf = (UVAnimKeyFrame*)anim->keyframes;
	AnimateOn();
	for(int32 i = 0; i < anim->numFrames; i++){
		TimeValue t = SecToTicks(kf->time);
		uvgen->SetWAng(kf->uv[0], t);
		uvgen->SetUScl(kf->uv[1], t);
		uvgen->SetVScl(kf->uv[2], t);
		// no skew :/
		uvgen->SetUOffs(-kf->uv[4], t);
		uvgen->SetVOffs(kf->uv[5], t);
		kf++;
	}
	AnimateOff();
}

bool
makeTexturePath(MCHAR *in, MCHAR *out)
{
	MCHAR test[MAX_PATH];
	const MCHAR *exsts[] = { _M(".png"), _M(".tga"), _M(".bmp"), _M(".jpg"), _M(".dds"), NULL };
	for(int i = 0; exsts[i]; i++){
		_tcscpy(test, in);
		_tcscat(test, exsts[i]);
#if MAX_API_NUM >= 60	// 2022
		if(BMMGetFullFilename(test, out, MAX_PATH))
#else
		if(BMMGetFullFilename(test, out))
#endif
			return true;
		if(currentImportDirectory[0]){
			_tcscpy(test, currentImportDirectory);
			_tcscat(test, in);
			_tcscat(test, exsts[i]);
			if(fileExists(test)){
				_tcscpy(out, test);
				return true;
			}
		}
	}
	if(currentImportDirectory[0]){
		_tcscpy(out, currentImportDirectory);
		_tcscat(out, in);
	}else
		_tcscpy(out, in);
	_tcscat(out, exsts[0]);
	return false;
}

static BitmapTex*
makeTexture(rw::Texture *tex, int mapChannel = 1)
{
	using namespace rw;
	static MCHAR bmpname[MAX_PATH];
	static MCHAR fullname[MAX_PATH];

	BitmapTex *bmtex;
	if(tex == NULL)
		return NULL;

	for(size_t i = 0; i < textureCache.size(); i++)
		if(textureCache[i].texture == tex &&
		   textureCache[i].mapChannel == mapChannel &&
		   strncmp(textureCache[i].name, tex->name, 32) == 0)
			return textureCache[i].bitmap;

	bmtex = NewDefaultBitmapTex();

	copyMaxString(bmpname, tex->name, MAX_PATH);
	double pathStart = rwioSeconds();
	bool foundTextureFile = makeTexturePath(bmpname, fullname);
	rwioImportDebug(_T("texture path %.3f s: %s -> %s%s"), rwioSeconds() - pathStart, bmpname, fullname, foundTextureFile ? _T(" (found)") : _T(" (fallback)"));
	bmtex->SetMapName(fullname);
	bmtex->GetUVGen()->SetMapChannel(mapChannel);

	switch(tex->filterAddressing & 0xFF){
	case Texture::NEAREST:
		bmtex->SetFilterType(FILTER_NADA);
		break;
	case Texture::LINEAR:
		bmtex->SetFilterType(FILTER_SAT);
		break;
	case Texture::LINEARMIPLINEAR:
		bmtex->SetFilterType(FILTER_PYR);
		break;
	}
	switch(tex->filterAddressing>>8 & 0xF){
	case Texture::WRAP:
		bmtex->GetUVGen()->SetFlag(U_WRAP, 1);
		break;
	case Texture::MIRROR:
		bmtex->GetUVGen()->SetFlag(U_MIRROR, 1);
		break;
	}
	switch(tex->filterAddressing>>12 & 0xF){
	case Texture::WRAP:
		bmtex->GetUVGen()->SetFlag(V_WRAP, 1);
		break;
	case Texture::MIRROR:
		bmtex->GetUVGen()->SetFlag(V_MIRROR, 1);
		break;
	}

	char *name = getUserName(UserDataExtension::get(tex));
	if(name)
		bmtex->SetName(getMaxStr(name));

	TextureCacheEntry entry = {};
	entry.texture = tex;
	strncpy(entry.name, tex->name, 31);
	entry.name[31] = '\0';
	entry.mapChannel = mapChannel;
	entry.bitmap = bmtex;
	entry.foundFile = foundTextureFile;
	entry.reloaded = false;
	textureCache.push_back(entry);
	return bmtex;
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
textureHasAlpha(BitmapTex *tex)
{
	return tex && bitmapHasAlpha(tex->GetMapName());
}

static void
useTextureAlpha(BitmapTex *tex)
{
	if(tex){
		tex->SetAlphaSource(ALPHA_FILE);
		tex->SetAlphaAsMono(TRUE);
		tex->SetAlphaAsRGB(FALSE);
		tex->SetPremultAlpha(FALSE);
		markTextureForReload(tex);
	}
}

static Mtl*
createOpenPBRMaterial(void)
{
	FPValue result;
	if(ExecuteMAXScriptScript(_T("OpenPBR_Material()"), MAXScript::ScriptSource::Dynamic, TRUE, &result) &&
	   result.type == TYPE_MTL && result.mtl)
		return result.mtl;
	return NewDefaultStdMat();
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

static void
executeQuietMAXScript(const MCHAR *script)
{
	ExecuteMAXScriptScript(script, MAXScript::ScriptSource::Dynamic, TRUE, NULL);
}

static void
setRWDelegateUpdatesSuppressed(bool suppress)
{
	executeQuietMAXScript(suppress ? _T("rwSuppressDelegateUpdates = true") : _T("rwSuppressDelegateUpdates = false"));
}

static void
refreshRWMaterialPreview(Mtl *mat)
{
	if(mat == NULL)
		return;

	MCHAR script[768];
	_stprintf(script,
		_T("rwioRWMat = getAnimByHandle %IuP; ")
		_T("if rwioRWMat != undefined do (")
		_T("rwSuppressDelegateUpdates = false; ")
		_T("rwioRWMap = rwioRWMat.texmap_texture; ")
		_T("rwioRWMat.texmap_texture = rwioRWMap; ")
		_T("rwioRWMat.color = rwioRWMat.color)"),
		(size_t)Animatable::GetHandleByAnim(mat));
	executeQuietMAXScript(script);
}

static void
setOpenPBRColor(Mtl *mat, const MCHAR *prop, int r, int g, int b)
{
	IParamBlock2 *pb = findParamBlockWithParam(mat, prop);
	if(pb)
		pb->SetValueByName(prop, Color(r/255.0f, g/255.0f, b/255.0f), 0);
}

static void
setOpenPBRFloat(Mtl *mat, const MCHAR *prop, float value)
{
	IParamBlock2 *pb = findParamBlockWithParam(mat, prop);
	if(pb)
		pb->SetValueByName(prop, value, 0);
}

static void
setOpenPBRMap(Mtl *mat, const MCHAR *prop, Texmap *map, bool enable = true)
{
	if(mat == NULL || map == NULL)
		return;
	IParamBlock2 *pb = findParamBlockWithParam(mat, prop);
	if(pb)
		pb->SetValueByName(prop, map, 0);

	MCHAR enableProp[128];
	_stprintf(enableProp, _T("%s_on"), prop);
	pb = findParamBlockWithParam(mat, enableProp);
	if(pb)
		pb->SetValueByName(enableProp, enable ? TRUE : FALSE, 0);

	MCHAR script[768];
	_stprintf(script, _T("rwioOpenPBRMat = getAnimByHandle %IuP; rwioOpenPBRMap = getAnimByHandle %IuP; if rwioOpenPBRMat != undefined and rwioOpenPBRMap != undefined do (setProperty rwioOpenPBRMat #%s rwioOpenPBRMap; setProperty rwioOpenPBRMat #%s_on %s)"),
		(size_t)Animatable::GetHandleByAnim(mat),
		(size_t)Animatable::GetHandleByAnim(map),
		prop, prop, enable ? _T("true") : _T("false"));
	executeQuietMAXScript(script);
	mat->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
}

static void
setOpenPBROpacityMapFromDiffuse(Mtl *mat, BitmapTex *diffmap, bool forceOpacity)
{
	if(mat == NULL || diffmap == NULL)
		return;

	MCHAR script[4096];
	_stprintf(script,
		_T("rwioOpenPBRMat = getAnimByHandle %IuP; ")
		_T("rwioOpenPBROpacitySource = getAnimByHandle %IuP; ")
		_T("rwioOpenPBRForceOpacity = %s; ")
		_T("rwioOpenPBRBitmapHasAlpha = false; ")
		_T("if rwioOpenPBRMat != undefined and rwioOpenPBROpacitySource != undefined then (")
		_T("if rwioOpenPBRForceOpacity then rwioOpenPBRBitmapHasAlpha = true else (")
		_T("try (")
		_T("if rwioOpenPBROpacitySource.alphaSource == 1 then rwioOpenPBRBitmapHasAlpha = true ")
		_T("else if rwioOpenPBROpacitySource.alphaSource != 2 and rwioOpenPBROpacitySource.filename != undefined and rwioOpenPBROpacitySource.filename != \"\" and doesFileExist rwioOpenPBROpacitySource.filename then (")
		_T("rwioOpenPBRBitmap = openBitmap rwioOpenPBROpacitySource.filename; ")
		_T("if rwioOpenPBRBitmap != undefined do rwioOpenPBRBitmapHasAlpha = rwioOpenPBRBitmap.hasAlpha; ")
		_T("if rwioOpenPBRBitmap != undefined do close rwioOpenPBRBitmap")
		_T(")) catch (rwioOpenPBRBitmapHasAlpha = false)")
		_T("); ")
		_T("if rwioOpenPBRBitmapHasAlpha then (")
		_T("rwioOpenPBROpacityMap = copy rwioOpenPBROpacitySource; ")
		_T("try(rwioOpenPBROpacityMap.name = (rwioOpenPBROpacitySource.name + \" alpha\"))catch(); ")
		_T("rwioOpenPBROpacityMap.alphaSource = 0; ")
		_T("rwioOpenPBROpacityMap.monoOutput = 1; ")
		_T("rwioOpenPBROpacityMap.rgbOutput = 1; ")
		_T("rwioOpenPBROpacityMap.premultAlpha = false; ")
		_T("try(rwioOpenPBROpacityMap.reload())catch(); ")
		_T("setProperty rwioOpenPBRMat #geometry_opacity_map rwioOpenPBROpacityMap; ")
		_T("setProperty rwioOpenPBRMat #geometry_opacity_map_on true; ")
		_T("try(rwioOpenPBRMat.showInViewport = true)catch()")
		_T("))"),
		(size_t)Animatable::GetHandleByAnim(mat),
		(size_t)Animatable::GetHandleByAnim(diffmap),
		forceOpacity ? _T("true") : _T("false"));
	executeQuietMAXScript(script);
	mat->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
}

static void
setOpenPBRBaseColorMap(Mtl *mat, BitmapTex *diffmap, rw::Material *rwmat)
{
	if(mat == NULL || diffmap == NULL || rwmat == NULL)
		return;

	MCHAR script[2048];
	_stprintf(script,
		_T("rwioOpenPBRMat = getAnimByHandle %IuP; ")
		_T("rwioOpenPBRBitmap = getAnimByHandle %IuP; ")
		_T("if rwioOpenPBRMat != undefined and rwioOpenPBRBitmap != undefined do (")
		_T("rwioOpenPBRTintMap = RGB_Multiply(); ")
		_T("rwioOpenPBRTintMap.name = (rwioOpenPBRBitmap.name + \" * RW color\"); ")
		_T("rwioOpenPBRTintMap.color1 = color %d %d %d; ")
		_T("rwioOpenPBRTintMap.map2 = rwioOpenPBRBitmap; ")
		_T("rwioOpenPBRTintMap.map2Enabled = true; ")
		_T("rwioOpenPBRTintMap.alphaFrom = 2; ")
		_T("setProperty rwioOpenPBRMat #base_color_map rwioOpenPBRTintMap; ")
		_T("setProperty rwioOpenPBRMat #base_color_map_on true; ")
		_T("try(rwioOpenPBRMat.showInViewport = true)catch())"),
		(size_t)Animatable::GetHandleByAnim(mat),
		(size_t)Animatable::GetHandleByAnim(diffmap),
		rwmat->color.red, rwmat->color.green, rwmat->color.blue);
	executeQuietMAXScript(script);
	mat->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
}

static bool
rwMaterialNeedsOpacityMap(rw::Material *m, BitmapTex *diffmap)
{
	if(m == NULL || diffmap == NULL)
		return false;
	bool materialAlpha = m->color.alpha < 255;
	bool hasMaskName = m->texture && m->texture->mask[0];
	bool fileAlpha = textureHasAlpha(diffmap);
	bool needsOpacity = materialAlpha || hasMaskName || fileAlpha;
	rwioImportDebug(_T("opacity check: tex=%s path=%s matAlpha=%d mask=%d fileAlpha=%d -> %d"),
		m->texture ? getMaxStr(m->texture->name) : _T("<none>"),
		diffmap ? diffmap->GetMapName() : _T("<none>"),
		materialAlpha ? 1 : 0,
		hasMaskName ? 1 : 0,
		fileAlpha ? 1 : 0,
		needsOpacity ? 1 : 0);
	return needsOpacity;
}

static MSTR
materialBaseName(rw::Material *mat, int index)
{
	if(mat && mat->texture && mat->texture->name[0])
		return getMaxStr(mat->texture->name);

	MCHAR fallback[64];
	_stprintf(fallback, _T("Material #%d"), index + 1);
	return MSTR(fallback);
}

static MSTR
numberedMaterialName(const MSTR &base, int count)
{
	if(count <= 1)
		return base;

	MSTR name = base;
	MCHAR suffix[32];
	_stprintf(suffix, _T(" #%d"), count);
	name += suffix;
	return name;
}

Mtl*
DFFImport::MakeGTAMaterial(rw::Material *m)
{
	using namespace rw;
	using namespace gta;

	RWIOImportTimer timer(_T("MakeGTAMaterial"));
	setRWDelegateUpdatesSuppressed(true);
	Mtl *mat = (Mtl*)ifc->CreateInstance(SClass_ID(MATERIAL_CLASS_ID), Class_ID(0x29b71842, 0x52508b70));
	if(mat == NULL || mat->NumParamBlocks() < 2 ||
       mat->GetParamBlock(0) == NULL || mat->GetParamBlock(1) == NULL){
        setRWDelegateUpdatesSuppressed(false);
        return MakeStandardMaterial(m);
    }
    IParamBlock2 *pb = mat->GetParamBlock(0);

	pb->SetValue(mat_color, 0, Color(m->color.red/255.0f, m->color.green/255.0f, m->color.blue/255.0f));
	pb->SetValue(mat_coloralpha, 0, m->color.alpha/255.0f);
	pb->SetValue(mat_sp_ambient, 0, m->surfaceProps.ambient);
	pb->SetValue(mat_sp_diffuse, 0, m->surfaceProps.diffuse);
	pb->SetValue(mat_sp_specular, 0, m->surfaceProps.specular);

	BitmapTex *diffmap = NULL;
	if(m->texture){
		diffmap = makeTexture(m->texture);
		if(m->texture->mask[0] || textureHasAlpha(diffmap))
			useTextureAlpha(diffmap);
		mat->GetParamBlock(0)->SetValue(mat_texmap_texture, 0, diffmap);
		reloadTextureIfFound(diffmap);
	}
	mat->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED, TRUE);

	MatFX *matfx = *PLUGINOFFSET(MatFX*, m, matFXGlobals.materialOffset);
	int32 idx;
	BitmapTex *bmtex;
	BitmapTex *dualmap = NULL;
	if(matfx){
		pb->SetValue(mat_matfxeffect, 0, (int)matfx->type+1);
		switch(matfx->type){
		case MatFX::BUMPMAP:
			idx = matfx->getEffectIndex(MatFX::BUMPMAP);
			assert(idx >= 0);
			bmtex = makeTexture(matfx->fx[idx].bump.bumpedTex);
			pb->SetValue(mat_texmap_bumpmap, 0, bmtex);
			pb->SetValue(mat_bumpmap_amount, 0, matfx->fx[idx].bump.coefficient);
			break;
	
		case MatFX::ENVMAP:
			idx = matfx->getEffectIndex(MatFX::ENVMAP);
			assert(idx >= 0);
			bmtex = makeTexture(matfx->fx[idx].env.tex);
			pb->SetValue(mat_texmap_envmap, 0, bmtex);
			pb->SetValue(mat_envmap_amount, 0, matfx->fx[idx].env.coefficient);
			break;
	
		case MatFX::DUAL:
			idx = matfx->getEffectIndex(MatFX::DUAL);
			assert(idx >= 0);
			dualmap = makeTexture(matfx->fx[idx].dual.tex, 2);
			pb->SetValue(mat_texmap_pass2, 0, dualmap);
			pb->SetValue(mat_pass2_srcblend, 0, matfx->fx[idx].dual.srcBlend);
			pb->SetValue(mat_pass2_destblend, 0, matfx->fx[idx].dual.dstBlend);
			break;
	
		case MatFX::BUMPENVMAP:
			idx = matfx->getEffectIndex(MatFX::ENVMAP);
			assert(idx >= 0);
			bmtex = makeTexture(matfx->fx[idx].env.tex);
			pb->SetValue(mat_texmap_envmap, 0, bmtex);
			pb->SetValue(mat_envmap_amount, 0, matfx->fx[idx].env.coefficient);
			idx = matfx->getEffectIndex(MatFX::BUMPMAP);
			assert(idx >= 0);
			bmtex = makeTexture(matfx->fx[idx].bump.bumpedTex);
			pb->SetValue(mat_texmap_bumpmap, 0, bmtex);
			pb->SetValue(mat_bumpmap_amount, 0, matfx->fx[idx].bump.coefficient);
			break;
		}
	}

	// R* extensions
	IParamBlock2 *pbr = mat->GetParamBlock(1);
	EnvMat *env = *PLUGINOFFSET(EnvMat*, m, envMatOffset);
	if(env){
		pbr->SetValue(mat_enEnv, 0, TRUE, 0);
		pbr->SetValue(mat_scaleX, 0, env->scaleX/8.0f);
		pbr->SetValue(mat_scaleY, 0, env->scaleY/8.0f);
		pbr->SetValue(mat_transScaleX, 0, env->transScaleX/8.0f);
		pbr->SetValue(mat_transScaleY, 0, env->transScaleY/8.0f);
		pbr->SetValue(mat_shininess, 0, env->shininess/255.0f);
	}

	SpecMat *spec = *PLUGINOFFSET(SpecMat*, m, specMatOffset);
	if(spec){
		pbr->SetValue(mat_enSpec, 0, TRUE, 0);
		pbr->SetValue(mat_specularity, 0, spec->specularity);
		bmtex = makeTexture(spec->texture);
		pbr->SetValue(mat_specMap, 0, bmtex);
	}

	UVAnim *uvanim = PLUGINOFFSET(UVAnim, m, uvAnimOffset);
	for(int j = 0; j < 8; j++)
		if(uvanim->interp[j]){
			animateTexture(uvanim->interp[j]->currentAnim, diffmap);
			animateTexture(uvanim->interp[j]->currentAnim, dualmap);
		}

	setRWDelegateUpdatesSuppressed(false);
	refreshRWMaterialPreview(mat);
	mat->NotifyDependents(FOREVER, PART_TEXMAP | PART_MTL, REFMSG_CHANGE);

	return mat;
}

Mtl*
DFFImport::MakeStandardMaterial(rw::Material *m)
{
    // 3ds Max 2022 does not have native OpenPBR_Material.
    // Standard material is a deliberately reduced-fidelity alternative to RW_Material:
    // advanced RenderWare extensions are preserved only when importing as RW_Material.
    RWIOImportTimer timer(_T("MakeStandardMaterial"));
    StdMat *mat = (StdMat*)NewDefaultStdMat();
    if(mat == NULL || m == NULL)
        return mat;

    mat->SetDiffuse(Color(m->color.red/255.0f,
                          m->color.green/255.0f,
                          m->color.blue/255.0f), 0);
    mat->SetOpacity(m->color.alpha/255.0f, 0);
    mat->SetShinStr(m->surfaceProps.specular, 0);

    if(m->texture){
        BitmapTex *diffmap = makeTexture(m->texture);
        if(diffmap){
            if(m->texture->mask[0] || textureHasAlpha(diffmap))
                useTextureAlpha(diffmap);
            mat->SetSubTexmap(ID_DI, diffmap);
            mat->EnableMap(ID_DI, TRUE);
            reloadTextureIfFound(diffmap);
        }
    }

    mat->SetMtlFlag(MTL_TEX_DISPLAY_ENABLED, TRUE);
    mat->NotifyDependents(FOREVER, PART_TEXMAP | PART_MTL, REFMSG_CHANGE);
    return mat;
}

void
DFFImport::makeMaterials(rw::Atomic *a, INode *inode)
{
	using namespace rw;
	using namespace gta;

	double t0 = rwioSeconds();
	Geometry *g = a->geometry;
	MultiMtl *multi = NULL;

	if(g->matList.numMaterials == 0)
		return;	// can this happen?
	if(g->matList.numMaterials > 1){
		multi = NewDefaultMultiMtl();
		multi->SetNumSubMtls(g->matList.numMaterials);
	}

	std::vector<MSTR> baseNames(g->matList.numMaterials);
	std::vector<int> totalNameCounts(g->matList.numMaterials, 0);
	for(int32 i = 0; i < g->matList.numMaterials; i++){
		baseNames[i] = materialBaseName(g->matList.materials[i], i);
		for(int32 j = 0; j < g->matList.numMaterials; j++)
			if(materialBaseName(g->matList.materials[j], j) == baseNames[i])
				totalNameCounts[i]++;
	}

	for(int32 i = 0; i < g->matList.numMaterials; i++){
		rw::Material *m = g->matList.materials[i];
		Mtl *mat;
		if(importStdMaterials)
			mat = MakeStandardMaterial(m);
		else
			mat = MakeGTAMaterial(m);

		int occurrence = 1;
		for(int32 j = 0; j <= i; j++)
			if(baseNames[j] == baseNames[i])
				occurrence++;
		occurrence--;
		MSTR matName = totalNameCounts[i] > 1 ?
			numberedMaterialName(baseNames[i], occurrence) :
			baseNames[i];
		mat->SetName(matName);

		if(g->matList.numMaterials == 1){
			inode->SetMtl(mat);
			rwioImportDebug(_T("makeMaterials %d material(s): %.3f s"), g->matList.numMaterials, rwioSeconds() - t0);
			return;
		}
		multi->SetSubMtlAndName(i, mat, matName);
	}
	inode->SetMtl(multi);
	rwioImportDebug(_T("makeMaterials %d material(s): %.3f s"), g->matList.numMaterials, rwioSeconds() - t0);
}

static void
setVertexChannelDisplay(INode *node, rw::Geometry *g, bool enabledForThisImport)
{
	if(node && enabledForThisImport && geometryHasVertexChannelData(g)){
		node->SetCVertMode(TRUE);
		node->SetShadeCVerts(TRUE);
		node->SetVertexColorType(DFFImport::vertexChannelType);
		node->SetVertexColorMapChannel(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL);
	}
}

static void
ensureAdvancedViewportTransparency(void)
{
	Interface7 *ip7 = GetCOREInterface7();
	if(ip7 == NULL)
		return;

	if(ip7->GetActiveViewportTransparencyLevel() < 2)
		ip7->SetActiveViewportTransparencyLevel(2);
	if(ip7->GetActiveViewportRenderLevel() != SMOOTH_HIGHLIGHT)
		ip7->SetActiveViewportRenderLevel(SMOOTH_HIGHLIGHT);
	ip7->RedrawViewportsLater(GetCOREInterface()->GetTime());
}

// Assume Geometry's triangles have invalid matid. So try to find them in Mesh...
// ignoring winding
static void
SetFaceMat(rw::Geometry *g, int ix0, int ix1, int ix2, int mat)
{
	int i, tmp;
	if(ix0 == ix1 || ix0 == ix2 || ix1 == ix2) return;	// degenerate
	// sort so we can compare more easily
	if(ix0 > ix1){ tmp = ix0; ix0 = ix1; ix1 = tmp; }
	if(ix0 > ix2){ tmp = ix0; ix0 = ix2; ix2 = tmp; }
	if(ix1 > ix2){ tmp = ix1; ix1 = ix2; ix2 = tmp; }
	for(i = 0; i < g->numTriangles; i++){
		int jx0 = g->triangles[i].v[0];
		int jx1 = g->triangles[i].v[1];
		int jx2 = g->triangles[i].v[2];
		if(jx0 > jx1){ tmp = jx0; jx0 = jx1; jx1 = tmp; }
		if(jx0 > jx2){ tmp = jx0; jx0 = jx2; jx2 = tmp; }
		if(jx1 > jx2){ tmp = jx1; jx1 = jx2; jx2 = tmp; }
		if(ix0 == jx0 && ix1 == jx1 && ix2 == jx2){
			g->triangles[i].matId = mat;
			break;
		}
	}
}

static UInt3Key
sortedTriangleKey(int ix0, int ix1, int ix2)
{
	int tmp;
	if(ix0 > ix1){ tmp = ix0; ix0 = ix1; ix1 = tmp; }
	if(ix0 > ix2){ tmp = ix0; ix0 = ix2; ix2 = tmp; }
	if(ix1 > ix2){ tmp = ix1; ix1 = ix2; ix2 = tmp; }
	return { (rw::uint32)ix0, (rw::uint32)ix1, (rw::uint32)ix2 };
}

static void
FixMatAssignment(rw::Geometry *g)
{
	using namespace rw;

	if(g->meshHeader == nil)
		return;

	int i, j;
	std::unordered_map<UInt3Key, int, UInt3KeyHash> triangleLookup;
	triangleLookup.reserve(g->numTriangles);
	for(i = 0; i < g->numTriangles; i++){
		int ix0 = g->triangles[i].v[0];
		int ix1 = g->triangles[i].v[1];
		int ix2 = g->triangles[i].v[2];
		if(ix0 == ix1 || ix0 == ix2 || ix1 == ix2)
			continue;
		triangleLookup.emplace(sortedTriangleKey(ix0, ix1, ix2), i);
	}

	int inc = g->meshHeader->flags & MeshHeader::TRISTRIP ? 1 : 3;
	rw::Mesh *m = g->meshHeader->getMeshes();
	for(i = 0; i < g->meshHeader->numMeshes; i++){
		int mat = g->matList.findIndex(m->material);
		for(j = 0; j+2 < m->numIndices; j += inc){
			int ix0 = m->indices[j];
			int ix1 = m->indices[j+1];
			int ix2 = m->indices[j+2];
			if(ix0 == ix1 || ix0 == ix2 || ix1 == ix2)
				continue;
			auto it = triangleLookup.find(sortedTriangleKey(ix0, ix1, ix2));
			if(it != triangleLookup.end())
				g->triangles[it->second].matId = mat;
		}
		m++;
	}
}

static int
faceCornerForVert(Mesh *mesh, int face, int vert)
{
	for(int i = 0; i < 3; i++)
		if(mesh->faces[face].v[i] == vert)
			return i;
	return -1;
}

static bool
sameMapIndexOnVert(Mesh *mesh, int channel, int faceA, int faceB, int vert)
{
	TVFace *faces = mesh->mapFaces(channel);
	if(faces == NULL)
		return true;
	int cornerA = faceCornerForVert(mesh, faceA, vert);
	int cornerB = faceCornerForVert(mesh, faceB, vert);
	if(cornerA < 0 || cornerB < 0)
		return false;
	return faces[faceA].t[cornerA] == faces[faceB].t[cornerB];
}

static bool
sameNormalOnVert(Mesh *mesh, int faceA, int faceB, int vert)
{
	MeshNormalSpec *normalSpec = mesh->GetSpecifiedNormals();
	if(normalSpec == NULL || faceA >= normalSpec->GetNumFaces() || faceB >= normalSpec->GetNumFaces())
		return true;
	int cornerA = faceCornerForVert(mesh, faceA, vert);
	int cornerB = faceCornerForVert(mesh, faceB, vert);
	if(cornerA < 0 || cornerB < 0)
		return false;
	MeshNormalFace &normalFaceA = normalSpec->Face(faceA);
	MeshNormalFace &normalFaceB = normalSpec->Face(faceB);
	if(normalFaceA.GetSpecified(cornerA) != normalFaceB.GetSpecified(cornerB))
		return false;
	if(!normalFaceA.GetSpecified(cornerA))
		return true;
	return normalFaceA.GetNormalID(cornerA) == normalFaceB.GetNormalID(cornerB);
}

static bool
facesAreNearlyCoplanar(Mesh *mesh, int faceA, int faceB)
{
	Point3 normalA = mesh->getFaceNormal(faceA);
	Point3 normalB = mesh->getFaceNormal(faceB);
	float lenA = Length(normalA);
	float lenB = Length(normalB);
	if(lenA <= 0.0f || lenB <= 0.0f)
		return false;
	return DotProd(normalA/lenA, normalB/lenB) > 0.999f;
}

static bool
canHideSharedEdgeForQuad(Mesh *mesh, int faceA, int faceB, int edgeA)
{
	if(mesh->faces[faceA].getMatID() != mesh->faces[faceB].getMatID())
		return false;
	if(!facesAreNearlyCoplanar(mesh, faceA, faceB))
		return false;

	int va = mesh->faces[faceA].v[edgeA];
	int vb = mesh->faces[faceA].v[(edgeA+1)%3];
	if(!sameNormalOnVert(mesh, faceA, faceB, va) || !sameNormalOnVert(mesh, faceA, faceB, vb))
		return false;
	if(mesh->vcFace &&
	   (mesh->vcFace[faceA].t[faceCornerForVert(mesh, faceA, va)] != mesh->vcFace[faceB].t[faceCornerForVert(mesh, faceB, va)] ||
	    mesh->vcFace[faceA].t[faceCornerForVert(mesh, faceA, vb)] != mesh->vcFace[faceB].t[faceCornerForVert(mesh, faceB, vb)]))
		return false;
	for(int channel = 0; channel < MAX_MESHMAPS; channel++)
		if(mesh->mapSupport(channel) &&
		   (!sameMapIndexOnVert(mesh, channel, faceA, faceB, va) ||
		    !sameMapIndexOnVert(mesh, channel, faceA, faceB, vb)))
			return false;
	return true;
}

static void
recoverQuadDisplayEdges(Mesh *mesh)
{
	std::unordered_map<MeshEdgeKey, MeshEdgeUse, MeshEdgeKeyHash> edges;
	std::vector<bool> usedFaces(mesh->numFaces, false);
	edges.reserve(mesh->numFaces*3);
	for(int face = 0; face < mesh->numFaces; face++){
		for(int edge = 0; edge < 3; edge++){
			int a = mesh->faces[face].v[edge];
			int b = mesh->faces[face].v[(edge+1)%3];
			MeshEdgeKey key = meshEdgeKey(a, b);
			auto it = edges.find(key);
			if(it == edges.end()){
				edges.emplace(key, MeshEdgeUse{ face, edge });
				continue;
			}
			int otherFace = it->second.face;
			int otherEdge = it->second.edge;
			if(!usedFaces[face] && !usedFaces[otherFace] && canHideSharedEdgeForQuad(mesh, face, otherFace, edge)){
				mesh->faces[face].setEdgeVis(edge, FALSE);
				mesh->faces[otherFace].setEdgeVis(otherEdge, FALSE);
				usedFaces[face] = true;
				usedFaces[otherFace] = true;
			}
		}
	}
}

static bool
applyStoredQuadEdgeData(rw::Geometry *g, Mesh *mesh)
{
	int slot = rw::UserDataArray::geometryFindIndex(g, RWIO_QUAD_EDGE_DATA);
	if(slot < 0)
		return false;
	rw::UserDataArray *edgeData = rw::UserDataArray::geometryGet(g, slot);
	if(edgeData == NULL || edgeData->datatype != rw::USERDATAINT || edgeData->numElements != mesh->numFaces)
		return false;

	for(int i = 0; i < mesh->numFaces; i++){
		int flags = edgeData->getInt(i);
		for(int j = 0; j < 3; j++)
			mesh->faces[i].setEdgeVis(j, (flags & (1 << j)) != 0);
	}
	return true;
}

static bool
geometryHasStoredQuadEdgeData(rw::Geometry *g)
{
	if(g == NULL)
		return false;
	int slot = rw::UserDataArray::geometryFindIndex(g, RWIO_QUAD_EDGE_DATA);
	if(slot < 0)
		return false;
	rw::UserDataArray *edgeData = rw::UserDataArray::geometryGet(g, slot);
	return edgeData && edgeData->datatype == rw::USERDATAINT &&
	       edgeData->numElements == g->numTriangles;
}

static bool
clumpHasStoredQuadEdgeData(rw::Clump *c)
{
	if(c == NULL)
		return false;
	FORLIST(lnk, c->atomics){
		rw::Atomic *a = rw::Atomic::fromClump(lnk);
		if(a && geometryHasStoredQuadEdgeData(a->geometry))
			return true;
	}
	return false;
}

static bool
geometryHasNormalShadingData(rw::Geometry *g)
{
	return g && (g->flags & rw::Geometry::NORMALS) &&
	       g->morphTargets && g->morphTargets[0].normals;
}

static bool
clumpHasNormalShadingData(rw::Clump *c)
{
	if(c == NULL)
		return false;
	FORLIST(lnk, c->atomics){
		rw::Atomic *a = rw::Atomic::fromClump(lnk);
		if(a && geometryHasNormalShadingData(a->geometry))
			return true;
	}
	return false;
}

static bool
geometryHasVertexChannelData(rw::Geometry *g)
{
	if(g == NULL)
		return false;
	if((g->flags & rw::Geometry::PRELIT) && g->colors)
		return true;
	gta::ExtraVertColors *colordata = PLUGINOFFSET(gta::ExtraVertColors, g, gta::extraVertColorOffset);
	return colordata && colordata->nightColors;
}

static bool
clumpHasVertexChannelData(rw::Clump *c)
{
	if(c == NULL)
		return false;
	FORLIST(lnk, c->atomics){
		rw::Atomic *a = rw::Atomic::fromClump(lnk);
		if(a && geometryHasVertexChannelData(a->geometry))
			return true;
	}
	return false;
}


static float
rwioLengthSquared(const Point3 &p)
{
	return p.x*p.x + p.y*p.y + p.z*p.z;
}

static int
nearestDffFaceCornerForPosition(rw::Geometry *g, int face, const Point3 &p)
{
	if(g == NULL || face < 0 || face >= g->numTriangles)
		return 0;

	rw::V3d *gverts = g->morphTargets[0].vertices;
	int bestCorner = 0;
	float bestDist = FLT_MAX;
	for(int corner = 0; corner < 3; corner++){
		int vi = g->triangles[face].v[corner];
		Point3 gp(gverts[vi].x, gverts[vi].y, gverts[vi].z);
		float d = rwioLengthSquared(gp - p);
		if(d < bestDist){
			bestDist = d;
			bestCorner = corner;
		}
	}
	return bestCorner;
}

static void
applyDffExplicitNormals(rw::Geometry *g, Mesh *mesh)
{
	if(g == NULL || mesh == NULL || !geometryHasNormalShadingData(g))
		return;

	rw::V3d *gnorms = g->morphTargets[0].normals;
	mesh->SpecifyNormals();
	MeshNormalSpec *normalSpec = mesh->GetSpecifiedNormals();
	if(normalSpec == NULL)
		return;

	normalSpec->ClearNormals();
	normalSpec->SetNumFaces(mesh->numFaces);
	normalSpec->SetParent(mesh);

	int meshFaceCount = (int)mesh->numFaces;
	int dffTriangleCount = (g->numTriangles > 0x7fffffffU) ? 0x7fffffff : (int)g->numTriangles;
	int faceCount = (meshFaceCount < dffTriangleCount) ? meshFaceCount : dffTriangleCount;
	for(int face = 0; face < faceCount; face++){
		for(int corner = 0; corner < 3; corner++){
			int meshVert = mesh->faces[face].v[corner];
			Point3 p = mesh->verts[meshVert];
			int sourceCorner = nearestDffFaceCornerForPosition(g, face, p);
			int sourceVert = g->triangles[face].v[sourceCorner];
			rw::V3d normal = gnorms[sourceVert];

			Point3 maxNormal(normal.x, normal.y, normal.z);
			if(Length(maxNormal) > 0.000001f)
				maxNormal = Normalize(maxNormal);
			else
				maxNormal = mesh->getFaceNormal(face);

			normalSpec->SetNormal(face, corner, maxNormal);
			int normalId = normalSpec->Face(face).GetNormalID(corner);
			if(normalId >= 0)
				normalSpec->SetNormalExplicit(normalId, true);
		}
	}
	normalSpec->SetAllExplicit(true);
	normalSpec->SetFlag(MESH_NORMAL_MODIFIER_SUPPORT, true);
	normalSpec->CheckNormals();
	mesh->InvalidateGeomCache();
}

struct RWIOImportedDffNormalSample
{
	Point3 pos;
	Point3 normal;
	Point3 faceCenter;
	Point3 faceNormal;
	MtlID matId;
};

struct RWIOImportedDffNormalSource
{
	std::vector<RWIOImportedDffNormalSample> samples;
};

static std::unordered_map<INode*, RWIOImportedDffNormalSource> rwioImportedDffNormalSources;
static std::unordered_map<std::basic_string<TCHAR>, RWIOImportedDffNormalSource> rwioImportedDffNormalSourcesByName;

static std::basic_string<TCHAR>
rwioNodeNameKey(INode *node)
{
	if(node == NULL || node->GetName() == NULL)
		return std::basic_string<TCHAR>();
	return std::basic_string<TCHAR>(node->GetName());
}


static Point3 rwioMeshFaceCenter(Mesh *mesh, int face);
static Point3 rwioMeshFaceNormalSafe(Mesh *mesh, int face);
static bool rwioMeshHasSpecifiedNormals(Mesh *mesh);
static bool projectNormalSamplesToMesh(const std::vector<RWIOImportedDffNormalSample> &samples, Mesh *targetMesh);
static TriObject* rwioMakeProjectionTriObjectFromNode(INode *node, bool &temporary);
static bool rwioApplyProjectedNormalsToTarget(INode *targetNode, const std::vector<RWIOImportedDffNormalSample> &samples);
static ::Object* rwioMakeProjectedNormalEditablePolyObject(TriObject *tri);

struct RWIOImportedDffAttributeSample
{
	Point3 pos;
	Point3 faceCenter;
	Point3 faceNormal;
	MtlID matId;
	bool hasColor;
	Point3 color;
	bool hasNormal;
	Point3 normal;
	std::vector<Point3> mapValues;
};

struct RWIOImportedDffAttributeSource
{
	std::vector<int> mapChannels;
	std::vector<RWIOImportedDffAttributeSample> samples;
	bool hasColor;
	bool hasNormal;

	RWIOImportedDffAttributeSource() : hasColor(false), hasNormal(false) {}
};

static bool
rwioAttributeSourceHasDisplayVertexChannel(const RWIOImportedDffAttributeSource &source)
{
	if(source.hasColor)
		return true;
	for(size_t i = 0; i < source.mapChannels.size(); i++){
		int ch = source.mapChannels[i];
		if(ch == RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL || ch == MAP_ALPHA || ch == MAP_EXTRACOLORS || ch == MAP_EXTRAALPHA)
			return true;
	}
	return false;
}

struct RWIOProjectionGridKey
{
	int x;
	int y;
	int z;

	bool operator==(const RWIOProjectionGridKey &other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}
};

struct RWIOProjectionGridKeyHash
{
	size_t operator()(const RWIOProjectionGridKey &key) const
	{
		// Large odd constants keep neighbouring grid cells reasonably separated.
		// Unsigned arithmetic avoids UB for large or negative quantized coords.
		size_t hx = (size_t)(static_cast<unsigned int>(key.x) * 73856093u);
		size_t hy = (size_t)(static_cast<unsigned int>(key.y) * 19349663u);
		size_t hz = (size_t)(static_cast<unsigned int>(key.z) * 83492791u);
		return hx ^ hy ^ hz;
	}
};

static const float RWIO_PROJECTION_GRID_SIZE = 0.001f;
static const float RWIO_PROJECTION_GRID_INV = 1000.0f;

static int
rwioProjectionQuantize(float v)
{
	const double scaled = static_cast<double>(v) * RWIO_PROJECTION_GRID_INV;
	// Keep a one-cell margin for neighbourhood searches and avoid UB when
	// malformed source coordinates contain NaN/Inf or exceed int range.
	if(!std::isfinite(scaled)) return 0;
	if(scaled >= static_cast<double>(INT_MAX - 2)) return INT_MAX - 2;
	if(scaled <= static_cast<double>(INT_MIN + 2)) return INT_MIN + 2;
	return static_cast<int>(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

static RWIOProjectionGridKey
rwioProjectionKey(const Point3 &p)
{
	RWIOProjectionGridKey key;
	key.x = rwioProjectionQuantize(p.x);
	key.y = rwioProjectionQuantize(p.y);
	key.z = rwioProjectionQuantize(p.z);
	return key;
}

static std::unordered_map<INode*, RWIOImportedDffAttributeSource> rwioImportedDffAttributeSources;
static std::unordered_map<std::basic_string<TCHAR>, RWIOImportedDffAttributeSource> rwioImportedDffAttributeSourcesByName;

static void
rwioAddUniqueMapChannel(std::vector<int> &channels, int ch)
{
	for(size_t i = 0; i < channels.size(); i++)
		if(channels[i] == ch)
			return;
	channels.push_back(ch);
}

static void
rwioCollectSupportedSourceMapChannels(Mesh *mesh, std::vector<int> &channels)
{
	if(mesh == NULL)
		return;

	// RWIO/GTA channels used by the importer/exporter:
	//   MAP_ALPHA      = prelight vertex alpha
	//   MAP_EXTRACOLORS = SA night/extra vertex color RGB
	//   MAP_EXTRAALPHA  = SA night/extra vertex alpha
	//   MAP_TEXCOORD0.. = UV sets
	int preferred[] = { MAP_ALPHA, MAP_EXTRACOLORS, MAP_TEXCOORD0, MAP_TEXCOORD0 + 1, MAP_EXTRAALPHA };
	for(size_t i = 0; i < sizeof(preferred)/sizeof(preferred[0]); i++){
		int ch = preferred[i];
		if(mesh->mapSupport(ch) && mesh->mapFaces(ch) && mesh->mapVerts(ch))
			rwioAddUniqueMapChannel(channels, ch);
	}

	// Also catch additional UV channels without assuming the DFF has only two sets.
	for(int ch = MAP_TEXCOORD0; ch <= MAP_TEXCOORD0 + 15; ch++){
		if(mesh->mapSupport(ch) && mesh->mapFaces(ch) && mesh->mapVerts(ch))
			rwioAddUniqueMapChannel(channels, ch);
	}
}

static RWIOImportedDffAttributeSource
buildDffAttributeProjectionSource(Mesh *mesh)
{
	RWIOImportedDffAttributeSource source;
	if(mesh == NULL || mesh->numFaces <= 0)
		return source;

	rwioCollectSupportedSourceMapChannels(mesh, source.mapChannels);
	source.hasColor = (mesh->vcFace != NULL && mesh->vertCol != NULL);
	source.hasNormal = rwioMeshHasSpecifiedNormals(mesh);

	MeshNormalSpec *normalSpec = mesh->GetSpecifiedNormals();
	MeshNormalFace *normalFaces = normalSpec ? normalSpec->GetFaceArray() : NULL;
	int normalFaceCount = normalSpec ? normalSpec->GetNumFaces() : 0;

	source.samples.reserve(mesh->numFaces * 3);
	for(int face = 0; face < mesh->numFaces; face++){
		Point3 faceCenter = rwioMeshFaceCenter(mesh, face);
		Point3 faceNormal = rwioMeshFaceNormalSafe(mesh, face);
		for(int corner = 0; corner < 3; corner++){
			RWIOImportedDffAttributeSample sample;
			sample.pos = mesh->verts[mesh->faces[face].v[corner]];
			sample.faceCenter = faceCenter;
			sample.faceNormal = faceNormal;
			sample.matId = mesh->faces[face].getMatID();
			sample.hasColor = false;
			sample.color = Point3(1.0f, 1.0f, 1.0f);
			sample.hasNormal = false;
			sample.normal = faceNormal;
			sample.mapValues.resize(source.mapChannels.size(), Point3(0.0f, 0.0f, 0.0f));

			if(source.hasColor && mesh->vcFace){
				int colorIndex = mesh->vcFace[face].t[corner];
				sample.color = mesh->vertCol[colorIndex];
				sample.hasColor = true;
			}

			for(size_t ci = 0; ci < source.mapChannels.size(); ci++){
				int ch = source.mapChannels[ci];
				TVFace *mf = mesh->mapFaces(ch);
				UVVert *mv = mesh->mapVerts(ch);
				if(mf && mv){
					int mapIndex = mf[face].t[corner];
					sample.mapValues[ci] = mv[mapIndex];
				}
			}

			if(source.hasNormal && normalSpec && normalFaces && face < normalFaceCount){
				int normalId = normalFaces[face].GetNormalID(corner);
				if(normalId >= 0){
					sample.normal = normalSpec->Normal(normalId);
					if(Length(sample.normal) > 0.000001f)
						sample.normal = Normalize(sample.normal);
					else
						sample.normal = faceNormal;
					sample.hasNormal = true;
				}
			}

			source.samples.push_back(sample);
		}
	}

	return source;
}

static void
rememberDffAttributeProjectionSource(INode *node, const RWIOImportedDffAttributeSource &source)
{
	if(node == NULL || source.samples.empty())
		return;

	rwioImportedDffAttributeSources[node] = source;

	std::basic_string<TCHAR> nameKey = rwioNodeNameKey(node);
	if(!nameKey.empty())
		rwioImportedDffAttributeSourcesByName[nameKey] = source;
}

struct RWIOAttributeProjectionIndex
{
	std::unordered_map<RWIOProjectionGridKey, std::vector<int>, RWIOProjectionGridKeyHash> buckets;
};

static float
rwioAttributeSampleScore(const RWIOImportedDffAttributeSample &s, const Point3 &p, const Point3 &faceCenter, const Point3 &faceNormal, MtlID matId)
{
	float posScore = rwioLengthSquared(s.pos - p) * 1000000.0f;
	float centerScore = rwioLengthSquared(s.faceCenter - faceCenter) * 0.25f;
	float normalDot = DotProd(s.faceNormal, faceNormal);
	float normalScore = (1.0f - normalDot) * 0.05f;
	float matScore = (s.matId == matId) ? 0.0f : 10.0f;
	return posScore + centerScore + normalScore + matScore;
}

static void
rwioBuildAttributeProjectionIndex(const RWIOImportedDffAttributeSource &source, RWIOAttributeProjectionIndex &index)
{
	index.buckets.clear();
	index.buckets.reserve(source.samples.size());
	for(size_t i = 0; i < source.samples.size(); i++)
		index.buckets[rwioProjectionKey(source.samples[i].pos)].push_back((int)i);
}

static const RWIOImportedDffAttributeSample*
rwioFindBestAttributeSampleLinear(const RWIOImportedDffAttributeSource &source, const Point3 &p, const Point3 &faceCenter, const Point3 &faceNormal, MtlID matId)
{
	const RWIOImportedDffAttributeSample *best = NULL;
	float bestScore = FLT_MAX;
	for(size_t i = 0; i < source.samples.size(); i++){
		const RWIOImportedDffAttributeSample &s = source.samples[i];
		float score = rwioAttributeSampleScore(s, p, faceCenter, faceNormal, matId);
		if(score < bestScore){
			bestScore = score;
			best = &s;
		}
	}
	return best;
}

static const RWIOImportedDffAttributeSample*
rwioFindBestAttributeSampleIndexed(const RWIOImportedDffAttributeSource &source, const RWIOAttributeProjectionIndex &index, const Point3 &p, const Point3 &faceCenter, const Point3 &faceNormal, MtlID matId)
{
	const RWIOImportedDffAttributeSample *best = NULL;
	float bestScore = FLT_MAX;
	RWIOProjectionGridKey key = rwioProjectionKey(p);

	// Large DFFs were slow because this used to scan every source corner for every
	// target corner.  The grid normally finds only the samples at the same position.
	for(int dx = -1; dx <= 1; dx++){
		for(int dy = -1; dy <= 1; dy++){
			for(int dz = -1; dz <= 1; dz++){
				RWIOProjectionGridKey nk;
				nk.x = key.x + dx;
				nk.y = key.y + dy;
				nk.z = key.z + dz;
				std::unordered_map<RWIOProjectionGridKey, std::vector<int>, RWIOProjectionGridKeyHash>::const_iterator it = index.buckets.find(nk);
				if(it == index.buckets.end())
					continue;
				const std::vector<int> &bucket = it->second;
				for(size_t bi = 0; bi < bucket.size(); bi++){
					const RWIOImportedDffAttributeSample &s = source.samples[bucket[bi]];
					float score = rwioAttributeSampleScore(s, p, faceCenter, faceNormal, matId);
					if(score < bestScore){
						bestScore = score;
						best = &s;
					}
				}
			}
		}
	}

	// Rare fallback for heavily transformed/rounded targets.  This preserves the
	// old behaviour without making the common path O(n^2).
	if(best == NULL)
		best = rwioFindBestAttributeSampleLinear(source, p, faceCenter, faceNormal, matId);
	return best;
}

static bool
projectAttributeSamplesToMesh(const RWIOImportedDffAttributeSource &source, Mesh *targetMesh)
{
	if(source.samples.empty() || targetMesh == NULL || targetMesh->numFaces <= 0)
		return false;

	int faceCount = targetMesh->numFaces;
	int cornerCount = faceCount * 3;

	// Cache target corner matches once, then reuse them for CPV, all map channels,
	// and explicit normals.  This replaces the old MAXScript path that rescanned
	// every source sample once per channel.
	RWIOAttributeProjectionIndex attributeIndex;
	rwioBuildAttributeProjectionIndex(source, attributeIndex);

	std::vector<const RWIOImportedDffAttributeSample*> bestSamples;
	bestSamples.resize(cornerCount, NULL);
	for(int face = 0; face < faceCount; face++){
		Point3 faceCenter = rwioMeshFaceCenter(targetMesh, face);
		Point3 faceNormal = rwioMeshFaceNormalSafe(targetMesh, face);
		MtlID matId = targetMesh->faces[face].getMatID();
		for(int corner = 0; corner < 3; corner++){
			Point3 p = targetMesh->verts[targetMesh->faces[face].v[corner]];
			bestSamples[face * 3 + corner] = rwioFindBestAttributeSampleIndexed(source, attributeIndex, p, faceCenter, faceNormal, matId);
		}
	}

	if(source.hasColor){
		targetMesh->setNumVertCol(cornerCount);
		targetMesh->setNumVCFaces(faceCount);

		// Vertex Color is Max map channel 0.  Fill both Mesh::vertCol/vcFace and
		// explicit map channel 0 so Editable Poly conversion/display keeps prelight.
		targetMesh->setMapSupport(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL, TRUE);
		targetMesh->setNumMapVerts(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL, cornerCount);
		targetMesh->setNumMapFaces(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL, faceCount);
		TVFace *colorMapFaces = targetMesh->mapFaces(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL);

		for(int face = 0; face < faceCount; face++){
			int base = face * 3;
			for(int corner = 0; corner < 3; corner++){
				const RWIOImportedDffAttributeSample *s = bestSamples[base + corner];
				Point3 color = (s && s->hasColor) ? s->color : Point3(1.0f, 1.0f, 1.0f);
				targetMesh->vertCol[base + corner] = color;
				targetMesh->setMapVert(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL, base + corner, color);
			}
			targetMesh->vcFace[face].setTVerts(base, base + 1, base + 2);
			if(colorMapFaces)
				colorMapFaces[face].setTVerts(base, base + 1, base + 2);
		}
		// Make viewport/render CPV lookup use map channel 0, not UV map channel 1.
		targetMesh->setVCDisplayData(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL);
	}

	for(size_t ci = 0; ci < source.mapChannels.size(); ci++){
		int ch = source.mapChannels[ci];
		targetMesh->setMapSupport(ch, TRUE);
		targetMesh->setNumMapVerts(ch, cornerCount);
		targetMesh->setNumMapFaces(ch, faceCount);
		TVFace *mf = targetMesh->mapFaces(ch);
		if(mf == NULL)
			continue;

		for(int face = 0; face < faceCount; face++){
			int base = face * 3;
			for(int corner = 0; corner < 3; corner++){
				const RWIOImportedDffAttributeSample *s = bestSamples[base + corner];
				Point3 value(0.0f, 0.0f, 0.0f);
				if(s && ci < s->mapValues.size())
					value = s->mapValues[ci];
				targetMesh->setMapVert(ch, base + corner, value);
			}
			mf[face].setTVerts(base, base + 1, base + 2);
		}
	}

	if(source.hasNormal){
		std::vector<RWIOImportedDffNormalSample> normalSamples;
		normalSamples.reserve(source.samples.size());
		for(size_t i = 0; i < source.samples.size(); i++){
			if(!source.samples[i].hasNormal)
				continue;
			RWIOImportedDffNormalSample ns;
			ns.pos = source.samples[i].pos;
			ns.normal = source.samples[i].normal;
			ns.faceCenter = source.samples[i].faceCenter;
			ns.faceNormal = source.samples[i].faceNormal;
			ns.matId = source.samples[i].matId;
			normalSamples.push_back(ns);
		}
		if(!normalSamples.empty())
			projectNormalSamplesToMesh(normalSamples, targetMesh);
	}

	targetMesh->InvalidateGeomCache();
	targetMesh->InvalidateEdgeList();
	return true;
}

static TriObject*
rwioMakeProjectionTriObjectFromNode(INode *node, bool &temporary)
{
	temporary = false;
	if(node == NULL)
		return NULL;

	TimeValue t = GetCOREInterface()->GetTime();
	Object *base = getBaseObject(node);
	if(base == NULL)
		return NULL;

	if(base->ClassID() == Class_ID(TRIOBJ_CLASS_ID, 0))
		return (TriObject*)base;

	if(base->ClassID() == polyObjectClassID || base->ClassID() == EPOLYOBJ_CLASS_ID){
		PolyObject *poly = (PolyObject*)base;
		TriObject *tri = CreateNewTriObject();
		if(tri == NULL)
			return NULL;
		poly->GetMesh().OutToTri(tri->GetMesh());
		tri->GetMesh().buildBoundingBox();
		tri->GetMesh().InvalidateEdgeList();
		tri->GetMesh().InvalidateGeomCache();
		temporary = true;
		return tri;
	}

	Object *polyConverted = base->ConvertToType(t, polyObjectClassID);
	if(polyConverted &&
	   (polyConverted->ClassID() == polyObjectClassID || polyConverted->ClassID() == EPOLYOBJ_CLASS_ID)){
		PolyObject *poly = (PolyObject*)polyConverted;
		TriObject *tri = CreateNewTriObject();
		if(tri){
			poly->GetMesh().OutToTri(tri->GetMesh());
			tri->GetMesh().buildBoundingBox();
			tri->GetMesh().InvalidateEdgeList();
			tri->GetMesh().InvalidateGeomCache();
		}
		if(polyConverted != base)
			polyConverted->DeleteMe();
		if(tri){
			temporary = true;
			return tri;
		}
	}
	if(polyConverted && polyConverted != base)
		polyConverted->DeleteMe();

	Object *converted = base->ConvertToType(t, Class_ID(TRIOBJ_CLASS_ID, 0));
	if(converted && converted->ClassID() == Class_ID(TRIOBJ_CLASS_ID, 0)){
		temporary = (converted != base);
		return (TriObject*)converted;
	}

	if(converted && converted != base)
		converted->DeleteMe();
	return NULL;
}

static bool
rwioApplyProjectedAttributesToTarget(INode *targetNode, const RWIOImportedDffAttributeSource &source)
{
	if(targetNode == NULL || source.samples.empty())
		return false;

	TimeValue t = GetCOREInterface()->GetTime();
	bool temporaryTri = false;
	TriObject *tri = rwioMakeProjectionTriObjectFromNode(targetNode, temporaryTri);
	if(tri == NULL)
		return false;

	bool ok = projectAttributeSamplesToMesh(source, &tri->GetMesh());
	if(!ok){
		if(temporaryTri && tri)
			tri->DeleteMe();
		return false;
	}

	::Object *finalObj = rwioMakeProjectedNormalEditablePolyObject(tri);
	if(finalObj == NULL){
		if(temporaryTri && tri)
			tri->DeleteMe();
		return false;
	}

	targetNode->SetObjectRef(finalObj);
	if(temporaryTri && tri)
		tri->DeleteMe();

	if(rwioAttributeSourceHasDisplayVertexChannel(source)){
		targetNode->SetCVertMode(TRUE);
		targetNode->SetShadeCVerts(TRUE);
		targetNode->SetVertexColorType(DFFImport::vertexChannelType);
		targetNode->SetVertexColorMapChannel(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL);
	}

	targetNode->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
	GetCOREInterface()->RedrawViews(t, REDRAW_NORMAL);
	return true;
}

bool
rwioProjectImportedDffAttributes(INode *sourceNode, INode *targetNode)
{
	if(sourceNode == NULL || targetNode == NULL)
		return false;

	const RWIOImportedDffAttributeSource *source = NULL;

	std::unordered_map<INode*, RWIOImportedDffAttributeSource>::iterator sourceIt = rwioImportedDffAttributeSources.find(sourceNode);
	if(sourceIt != rwioImportedDffAttributeSources.end() && !sourceIt->second.samples.empty())
		source = &sourceIt->second;

	// QuadrifyAll++ swaps/rebuilds objects and, depending on the importer path,
	// the scene hierarchy can contain cloned/reparented INode instances after the
	// MAXScript pass.  The cache was valid (cachedAttributeSources > 0) but v35/v37
	// still reported attrProjected=0 because lookup only used the original INode*.
	// Node names are the stable imported DFF atomic identity, so fall back to name.
	if(source == NULL){
		std::basic_string<TCHAR> nameKey = rwioNodeNameKey(sourceNode);
		std::unordered_map<std::basic_string<TCHAR>, RWIOImportedDffAttributeSource>::iterator nameIt = rwioImportedDffAttributeSourcesByName.find(nameKey);
		if(nameIt != rwioImportedDffAttributeSourcesByName.end() && !nameIt->second.samples.empty())
			source = &nameIt->second;
	}

	if(source == NULL)
		return false;

	return rwioApplyProjectedAttributesToTarget(targetNode, *source);
}


bool
rwioProjectImportedDffNormals(INode *sourceNode, INode *targetNode);

static bool
rwioNodeVectorContains(const std::vector<INode*> &nodes, INode *node)
{
	for(size_t i = 0; i < nodes.size(); i++)
		if(nodes[i] == node)
			return true;
	return false;
}

int
rwioProjectCachedDffAttributesForRoots(const std::vector<INode*> &roots)
{
	int attrTried = 0;
	int normalTried = 0;
	int attrProjected = 0;
	int normalProjected = 0;
	std::vector<INode*> normalsAlreadyProjectedByAttributes;

	// v47: O(imported atomics), not O(scene hierarchy).  Also avoid a second full
	// normals projection for nodes whose attribute projection already carried normals.
	for(std::unordered_map<INode*, RWIOImportedDffAttributeSource>::iterator it = rwioImportedDffAttributeSources.begin(); it != rwioImportedDffAttributeSources.end(); ++it){
		attrTried++;
		if(it->first && rwioApplyProjectedAttributesToTarget(it->first, it->second)){
			attrProjected++;
			if(it->second.hasNormal){
				normalsAlreadyProjectedByAttributes.push_back(it->first);
				normalProjected++;
			}
		}
	}

	for(std::unordered_map<INode*, RWIOImportedDffNormalSource>::iterator it = rwioImportedDffNormalSources.begin(); it != rwioImportedDffNormalSources.end(); ++it){
		if(it->first && rwioNodeVectorContains(normalsAlreadyProjectedByAttributes, it->first))
			continue;
		normalTried++;
		if(it->first && rwioApplyProjectedNormalsToTarget(it->first, it->second.samples))
			normalProjected++;
	}

	if(the_listener && the_listener->edit_stream){
		the_listener->edit_stream->printf(
			_T("RWIO QuadrifyAll++ C++ direct-cache v49k normal-shading-defaults real-editable-poly: roots=%d attrTried=%d normalTried=%d cachedAttributeSources=%d cachedNormalSources=%d attrProjected=%d normalProjected=%d skippedDuplicateNormalPass=%d\n"),
			(int)roots.size(),
			attrTried,
			normalTried,
			(int)rwioImportedDffAttributeSources.size(),
			(int)rwioImportedDffNormalSources.size(),
			attrProjected,
			normalProjected,
			(int)normalsAlreadyProjectedByAttributes.size());
		the_listener->edit_stream->flush();
	}

	return attrProjected + normalProjected;
}


static Point3
rwioMeshFaceCenter(Mesh *mesh, int face)
{
	Face &f = mesh->faces[face];
	return (mesh->verts[f.v[0]] + mesh->verts[f.v[1]] + mesh->verts[f.v[2]]) / 3.0f;
}

static Point3
rwioMeshFaceNormalSafe(Mesh *mesh, int face)
{
	Face &f = mesh->faces[face];
	Point3 a = mesh->verts[f.v[0]];
	Point3 b = mesh->verts[f.v[1]];
	Point3 c = mesh->verts[f.v[2]];
	Point3 n = CrossProd(b - a, c - a);
	if(Length(n) > 0.000001f)
		return Normalize(n);
	return Point3(0.0f, 0.0f, 1.0f);
}

static bool
rwioMeshHasSpecifiedNormals(Mesh *mesh)
{
	if(mesh == NULL)
		return false;
	MeshNormalSpec *normalSpec = mesh->GetSpecifiedNormals();
	if(normalSpec == NULL || normalSpec->GetNumFaces() <= 0)
		return false;
	MeshNormalFace *normalFaces = normalSpec->GetFaceArray();
	if(normalFaces == NULL)
		return false;
	for(int face = 0; face < mesh->numFaces && face < normalSpec->GetNumFaces(); face++){
		for(int corner = 0; corner < 3; corner++){
			if(normalFaces[face].GetSpecified(corner))
				return true;
		}
	}
	return false;
}

static RWIOImportedDffNormalSource
buildDffNormalProjectionSource(Mesh *mesh)
{
	RWIOImportedDffNormalSource source;
	if(mesh == NULL || mesh->numFaces <= 0)
		return source;

	MeshNormalSpec *normalSpec = mesh->GetSpecifiedNormals();
	MeshNormalFace *normalFaces = normalSpec ? normalSpec->GetFaceArray() : NULL;
	int normalFaceCount = normalSpec ? normalSpec->GetNumFaces() : 0;
	source.samples.reserve(mesh->numFaces * 3);
	for(int face = 0; face < mesh->numFaces; face++){
		Point3 faceCenter = rwioMeshFaceCenter(mesh, face);
		Point3 faceNormal = rwioMeshFaceNormalSafe(mesh, face);
		for(int corner = 0; corner < 3; corner++){
			RWIOImportedDffNormalSample sample;
			sample.pos = mesh->verts[mesh->faces[face].v[corner]];
			sample.faceCenter = faceCenter;
			sample.faceNormal = faceNormal;
			sample.matId = mesh->faces[face].getMatID();
			sample.normal = faceNormal;

			if(normalSpec && normalFaces && face < normalFaceCount){
				int normalId = normalFaces[face].GetNormalID(corner);
				if(normalId >= 0){
					sample.normal = normalSpec->Normal(normalId);
					if(Length(sample.normal) > 0.000001f)
						sample.normal = Normalize(sample.normal);
					else
						sample.normal = faceNormal;
				}
			}
			source.samples.push_back(sample);
		}
	}
	return source;
}

static void
rememberDffNormalProjectionSource(INode *node, const RWIOImportedDffNormalSource &source)
{
	if(node == NULL || source.samples.empty())
		return;

	rwioImportedDffNormalSources[node] = source;

	std::basic_string<TCHAR> nameKey = rwioNodeNameKey(node);
	if(!nameKey.empty())
		rwioImportedDffNormalSourcesByName[nameKey] = source;
}

struct RWIONormalProjectionIndex
{
	std::unordered_map<RWIOProjectionGridKey, std::vector<int>, RWIOProjectionGridKeyHash> buckets;
};

static float
rwioNormalSampleScore(const RWIOImportedDffNormalSample &s, const Point3 &p, const Point3 &faceCenter, const Point3 &faceNormal, MtlID matId)
{
	float posScore = rwioLengthSquared(s.pos - p) * 1000000.0f;
	float centerScore = rwioLengthSquared(s.faceCenter - faceCenter) * 0.25f;
	float normalDot = DotProd(s.faceNormal, faceNormal);
	float normalScore = (1.0f - normalDot) * 0.05f;
	float matScore = (s.matId == matId) ? 0.0f : 10.0f;
	return posScore + centerScore + normalScore + matScore;
}

static void
rwioBuildNormalProjectionIndex(const std::vector<RWIOImportedDffNormalSample> &samples, RWIONormalProjectionIndex &index)
{
	index.buckets.clear();
	index.buckets.reserve(samples.size());
	for(size_t i = 0; i < samples.size(); i++)
		index.buckets[rwioProjectionKey(samples[i].pos)].push_back((int)i);
}

static const RWIOImportedDffNormalSample*
rwioFindBestNormalSampleLinear(const std::vector<RWIOImportedDffNormalSample> &samples, const Point3 &p, const Point3 &faceCenter, const Point3 &faceNormal, MtlID matId)
{
	const RWIOImportedDffNormalSample *best = NULL;
	float bestScore = FLT_MAX;
	for(size_t i = 0; i < samples.size(); i++){
		const RWIOImportedDffNormalSample &s = samples[i];
		float score = rwioNormalSampleScore(s, p, faceCenter, faceNormal, matId);
		if(score < bestScore){
			bestScore = score;
			best = &s;
		}
	}
	return best;
}

static const RWIOImportedDffNormalSample*
rwioFindBestNormalSampleIndexed(const std::vector<RWIOImportedDffNormalSample> &samples, const RWIONormalProjectionIndex &index, const Point3 &p, const Point3 &faceCenter, const Point3 &faceNormal, MtlID matId)
{
	const RWIOImportedDffNormalSample *best = NULL;
	float bestScore = FLT_MAX;
	RWIOProjectionGridKey key = rwioProjectionKey(p);

	for(int dx = -1; dx <= 1; dx++){
		for(int dy = -1; dy <= 1; dy++){
			for(int dz = -1; dz <= 1; dz++){
				RWIOProjectionGridKey nk;
				nk.x = key.x + dx;
				nk.y = key.y + dy;
				nk.z = key.z + dz;
				std::unordered_map<RWIOProjectionGridKey, std::vector<int>, RWIOProjectionGridKeyHash>::const_iterator it = index.buckets.find(nk);
				if(it == index.buckets.end())
					continue;
				const std::vector<int> &bucket = it->second;
				for(size_t bi = 0; bi < bucket.size(); bi++){
					const RWIOImportedDffNormalSample &s = samples[bucket[bi]];
					float score = rwioNormalSampleScore(s, p, faceCenter, faceNormal, matId);
					if(score < bestScore){
						bestScore = score;
						best = &s;
					}
				}
			}
		}
	}

	if(best == NULL)
		best = rwioFindBestNormalSampleLinear(samples, p, faceCenter, faceNormal, matId);
	return best;
}

static bool
projectNormalSamplesToMesh(const std::vector<RWIOImportedDffNormalSample> &samples, Mesh *targetMesh)
{
	if(samples.empty() || targetMesh == NULL || targetMesh->numFaces <= 0)
		return false;

	targetMesh->SpecifyNormals();
	MeshNormalSpec *normalSpec = targetMesh->GetSpecifiedNormals();
	if(normalSpec == NULL)
		return false;

	normalSpec->ClearNormals();
	normalSpec->SetNumFaces(targetMesh->numFaces);
	normalSpec->SetParent(targetMesh);

	RWIONormalProjectionIndex normalIndex;
	rwioBuildNormalProjectionIndex(samples, normalIndex);

	for(int face = 0; face < targetMesh->numFaces; face++){
		Point3 faceCenter = rwioMeshFaceCenter(targetMesh, face);
		Point3 faceNormal = rwioMeshFaceNormalSafe(targetMesh, face);
		MtlID matId = targetMesh->faces[face].getMatID();

		for(int corner = 0; corner < 3; corner++){
			Point3 p = targetMesh->verts[targetMesh->faces[face].v[corner]];
			const RWIOImportedDffNormalSample *best = rwioFindBestNormalSampleIndexed(samples, normalIndex, p, faceCenter, faceNormal, matId);

			Point3 n = best ? best->normal : faceNormal;
			if(Length(n) > 0.000001f)
				n = Normalize(n);
			else
				n = faceNormal;

			normalSpec->SetNormal(face, corner, n);
			int normalId = normalSpec->Face(face).GetNormalID(corner);
			if(normalId >= 0)
				normalSpec->SetNormalExplicit(normalId, true);
		}
	}

	normalSpec->SetAllExplicit(true);
	normalSpec->SetFlag(MESH_NORMAL_MODIFIER_SUPPORT, true);
	normalSpec->CheckNormals();
	targetMesh->InvalidateGeomCache();
	return true;
}

static bool
projectDffNormalsToMesh(INode *sourceNode, Mesh *targetMesh)
{
	if(sourceNode == NULL || targetMesh == NULL)
		return false;

	std::unordered_map<INode*, RWIOImportedDffNormalSource>::iterator it = rwioImportedDffNormalSources.find(sourceNode);
	if(it == rwioImportedDffNormalSources.end() || it->second.samples.empty())
		return false;

	return projectNormalSamplesToMesh(it->second.samples, targetMesh);
}

static void
rwioCopyTriMeshToPolyObject(PolyObject *poly, TriObject *tri)
{
	if(poly == NULL || tri == NULL)
		return;

	poly->GetMesh().SetFromTri(tri->GetMesh());
	poly->GetMesh().FillInMesh();
	poly->GetMesh().InvalidateGeomCache();
}

static ::Object*
rwioMakeProjectedNormalEditablePolyObject(TriObject *tri)
{
	if(tri == NULL)
		return NULL;

	TimeValue t = GetCOREInterface()->GetTime();

	// v47: Create a real Editable_Poly object if the Editable Poly class is
	// registered.  CreateEditablePolyObject() can legally fall back to a plain
	// PolyObject, which MaxScript/UI reports as PolyMeshObject and users then have
	// to convert manually.
	Object *epolyObj = (Object*)GetCOREInterface()->CreateInstance(GEOMOBJECT_CLASS_ID, EPOLYOBJ_CLASS_ID);
	if(epolyObj != NULL && epolyObj->ClassID() == EPOLYOBJ_CLASS_ID){
		PolyObject *epoly = (PolyObject*)epolyObj;
		rwioCopyTriMeshToPolyObject(epoly, tri);
		return epolyObj;
	}
	if(epolyObj != NULL)
		epolyObj->DeleteMe();

	PolyObject *poly = CreateEditablePolyObject();
	if(poly == NULL)
		return NULL;

	rwioCopyTriMeshToPolyObject(poly, tri);
	if(poly->ClassID() == EPOLYOBJ_CLASS_ID)
		return poly;

	Object *converted = poly->ConvertToType(t, EPOLYOBJ_CLASS_ID);
	if(converted != NULL && converted->ClassID() == EPOLYOBJ_CLASS_ID){
		if(converted != poly)
			poly->DeleteMe();
		return converted;
	}

	if(converted != NULL && converted != poly)
		converted->DeleteMe();

	// Last-resort fallback.  This should only happen if epoly.dlo is not registered.
	return poly;
}

static bool
rwioApplyProjectedNormalsToTarget(INode *targetNode, const std::vector<RWIOImportedDffNormalSample> &samples)
{
	if(targetNode == NULL || samples.empty())
		return false;

	bool temporaryTri = false;
	TriObject *tri = rwioMakeProjectionTriObjectFromNode(targetNode, temporaryTri);
	if(tri == NULL)
		return false;

	bool ok = projectNormalSamplesToMesh(samples, &tri->GetMesh());
	if(!ok){
		if(temporaryTri && tri)
			tri->DeleteMe();
		return false;
	}

	::Object *finalObj = rwioMakeProjectedNormalEditablePolyObject(tri);
	if(finalObj == NULL){
		if(temporaryTri && tri)
			tri->DeleteMe();
		return false;
	}

	targetNode->SetObjectRef(finalObj);
	if(temporaryTri && tri)
		tri->DeleteMe();

	targetNode->NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
	return true;
}

bool
rwioProjectImportedDffNormals(INode *sourceNode, INode *targetNode)
{
	if(sourceNode == NULL || targetNode == NULL)
		return false;

	const RWIOImportedDffNormalSource *source = NULL;

	std::unordered_map<INode*, RWIOImportedDffNormalSource>::iterator sourceIt = rwioImportedDffNormalSources.find(sourceNode);
	if(sourceIt != rwioImportedDffNormalSources.end() && !sourceIt->second.samples.empty())
		source = &sourceIt->second;

	if(source == NULL){
		std::basic_string<TCHAR> nameKey = rwioNodeNameKey(sourceNode);
		std::unordered_map<std::basic_string<TCHAR>, RWIOImportedDffNormalSource>::iterator nameIt = rwioImportedDffNormalSourcesByName.find(nameKey);
		if(nameIt != rwioImportedDffNormalSourcesByName.end() && !nameIt->second.samples.empty())
			source = &nameIt->second;
	}

	if(source == NULL)
		return false;

	return rwioApplyProjectedNormalsToTarget(targetNode, source->samples);
}

bool
rwioProjectExplicitNormalsFromNode(INode *sourceNode, INode *targetNode)
{
	if(sourceNode == NULL || targetNode == NULL)
		return false;

	bool temporaryTri = false;
	TriObject *sourceTri = rwioMakeProjectionTriObjectFromNode(sourceNode, temporaryTri);
	if(sourceTri == NULL)
		return false;

	Mesh *sourceMesh = &sourceTri->GetMesh();
	if(!rwioMeshHasSpecifiedNormals(sourceMesh)){
		if(temporaryTri && sourceTri)
			sourceTri->DeleteMe();
		return false;
	}

	RWIOImportedDffNormalSource source = buildDffNormalProjectionSource(sourceMesh);
	if(temporaryTri && sourceTri)
		sourceTri->DeleteMe();

	if(source.samples.empty())
		return false;

	return rwioApplyProjectedNormalsToTarget(targetNode, source.samples);
}



void
DFFImport::makeMesh(rw::Atomic *a, Mesh *maxmesh)
{
	using namespace rw;

	Geometry *g = a->geometry;

	// not all GTA tools generate face info correctly :(
	if(fixKam)
		FixMatAssignment(g);

	V3d *gverts = g->morphTargets[0].vertices;
	V3d *gnorms = g->morphTargets[0].normals;
	int *newind = new int[g->numVertices];
	buildFirstV3dIndices(gverts, g->numVertices, newind);
	maxmesh->setNumVerts(g->numVertices);
	Point3 maxvert;
	for(int32 i = 0; i < g->numVertices; i++){
		int j = newind[i];
		maxvert[0] = gverts[j].x;
		maxvert[1] = gverts[j].y;
		maxvert[2] = gverts[j].z;
		maxmesh->setVert(i, maxvert);
	}
	maxmesh->setNumFaces(g->numTriangles);
	for(int32 i = 0; i < g->numTriangles; i++){
		maxmesh->faces[i].setVerts(newind[g->triangles[i].v[0]],
		                           newind[g->triangles[i].v[1]],
		                           newind[g->triangles[i].v[2]]);
		maxmesh->faces[i].setSmGroup(0);
		maxmesh->faces[i].setMatID(g->triangles[i].matId);
		maxmesh->faces[i].setEdgeVisFlags(1, 1, 1);
	}

	for(int k = 0; k < g->numTexCoordSets; k++){
		maxmesh->setMapSupport(MAP_TEXCOORD0+k, TRUE);
		TexCoords *uvs = g->texCoords[k];
		buildFirstTexCoordIndices(uvs, g->numVertices, newind);
		maxmesh->setNumMapVerts(MAP_TEXCOORD0+k, g->numVertices);
		Point3 maxvert;
		for(int32 i = 0; i < g->numVertices; i++){
			int j = newind[i];
			maxvert[0] = uvs[j].u;
			maxvert[1] = 1-uvs[j].v;
			maxvert[2] = 0.0f;
			maxmesh->setMapVert(MAP_TEXCOORD0+k, i, maxvert);
		}
		maxmesh->setNumMapFaces(MAP_TEXCOORD0+k, g->numTriangles);
		TVFace *tvf = maxmesh->mapFaces(MAP_TEXCOORD0+k);
		for(int32 i = 0; i < g->numTriangles; i++){
			tvf[i].setTVerts(newind[g->triangles[i].v[0]],
			                 newind[g->triangles[i].v[1]],
			                 newind[g->triangles[i].v[2]]);
		}
	}

	if(g->flags & rw::Geometry::PRELIT){
		if(!maxmesh->mapSupport(MAP_ALPHA))
			maxmesh->setMapSupport(MAP_ALPHA, TRUE);
		rw::RGBA *prelit = g->colors;
		buildFirstColorIndices(prelit, g->numVertices, newind);
		maxmesh->setNumVertCol(g->numVertices);
		maxmesh->setNumMapVerts(MAP_ALPHA, g->numVertices);
		Point3 maxvert;
		for(int32 i = 0; i < g->numVertices; i++){
			int j = newind[i];
			maxvert[0] = prelit[j].red/255.0f;
			maxvert[1] = prelit[j].green/255.0f;
			maxvert[2] = prelit[j].blue/255.0f;
			maxmesh->vertCol[i] = maxvert;
			maxvert[0] = maxvert[1] = maxvert[2] = prelit[j].alpha/255.0f;
			maxmesh->setMapVert(MAP_ALPHA, i, maxvert);
		}
		maxmesh->setNumVCFaces(g->numTriangles);
		maxmesh->setNumMapFaces(MAP_ALPHA, g->numTriangles);
		TVFace *af = maxmesh->mapFaces(MAP_ALPHA);
		for(int32 i = 0; i < g->numTriangles; i++){
			maxmesh->vcFace[i].setTVerts(newind[g->triangles[i].v[0]],
			                             newind[g->triangles[i].v[1]],
			                             newind[g->triangles[i].v[2]]);
			af[i].setTVerts(maxmesh->vcFace[i].getAllTVerts());
		}
		maxmesh->setVCDisplayData(RWIO_VERTEX_COLOR_DISPLAY_MAP_CHANNEL);
	}

	gta::ExtraVertColors *colordata = PLUGINOFFSET(gta::ExtraVertColors, g, gta::extraVertColorOffset);
	if(colordata->nightColors){
		maxmesh->setMapSupport(MAP_EXTRACOLORS, TRUE);
		maxmesh->setMapSupport(MAP_EXTRAALPHA, TRUE);
		rw::RGBA *prelit = colordata->nightColors;
		buildFirstColorIndices(prelit, g->numVertices, newind);
		maxmesh->setNumMapVerts(MAP_EXTRACOLORS, g->numVertices);
		maxmesh->setNumMapVerts(MAP_EXTRAALPHA, g->numVertices);
		Point3 maxvert;
		for(int32 i = 0; i < g->numVertices; i++){
			int j = newind[i];
			maxvert[0] = prelit[j].red/255.0f;
			maxvert[1] = prelit[j].green/255.0f;
			maxvert[2] = prelit[j].blue/255.0f;
			maxmesh->setMapVert(MAP_EXTRACOLORS, i, maxvert);
			maxvert[0] = maxvert[1] = maxvert[2] = prelit[j].alpha/255.0f;
			maxmesh->setMapVert(MAP_EXTRAALPHA, i, maxvert);
		}
		maxmesh->setNumMapFaces(MAP_EXTRACOLORS, g->numTriangles);
		maxmesh->setNumMapFaces(MAP_EXTRAALPHA, g->numTriangles);
		TVFace *cf = maxmesh->mapFaces(MAP_EXTRACOLORS);
		TVFace *af = maxmesh->mapFaces(MAP_EXTRAALPHA);
		for(int32 i = 0; i < g->numTriangles; i++){
			cf[i].setTVerts(newind[g->triangles[i].v[0]],
			                newind[g->triangles[i].v[1]],
			                newind[g->triangles[i].v[2]]);
			af[i].setTVerts(cf[i].getAllTVerts());
		}
	}

	if((g->flags & rw::Geometry::NORMALS) && gnorms){
		for(int32 i = 0; i < g->numTriangles; i++){
			// See if we can figure out a better winding if we have normals
			V3d v01 = sub(gverts[g->triangles[i].v[1]], gverts[g->triangles[i].v[0]]);
			V3d v02 = sub(gverts[g->triangles[i].v[2]], gverts[g->triangles[i].v[0]]);
			V3d norm = cross(v01, v02);
			float dot0 = dot(norm, gnorms[g->triangles[i].v[0]]);
			float dot1 = dot(norm, gnorms[g->triangles[i].v[1]]);
			float dot2 = dot(norm, gnorms[g->triangles[i].v[2]]);
			if(dot0 < 0.0f && dot1 < 0.0f && dot2 < 0.0f)
				maxmesh->FlipNormal(i);
		}
	}

	if(this->dffHasQuadData && DFFImport::recoverQuads)
		applyStoredQuadEdgeData(g, maxmesh);

	maxmesh->DeleteIsoVerts();
	maxmesh->DeleteIsoMapVerts();

	maxmesh->buildNormals();

	if(DFFImport::autoSmooth)
		maxmesh->AutoSmooth(DegToRad(DFFImport::smoothingAngle), 0, 1);

	// Apply DFF explicit normals after winding, quad-edge recovery, iso-vertex cleanup,
	// and smoothing setup. This keeps imported normal shading valid on the final import mesh.
	if((DFFImport::explicitNormals || DFFImport::quadrifyAllPlus) && geometryHasNormalShadingData(g))
		applyDffExplicitNormals(g, maxmesh);

	maxmesh->buildBoundingBox();
	maxmesh->InvalidateEdgeList();
	maxmesh->InvalidateGeomCache();

	delete[] newind;
}

// TODO: delete
static void
getBonePositions(rw::Skin *skin, rw::HAnimHierarchy *hier, Point3 *positions)
{
	using namespace rw;

	::Matrix3 matrix;
	for(int32 i = 0; i < skin->numBones; i++){
		float *m = &skin->inverseMatrices[i*16];
		matrix.SetRow(0, Point3(m[0],  m[1],  m[2]));
		matrix.SetRow(1, Point3(m[4],  m[5],  m[6]));
		matrix.SetRow(2, Point3(m[8],  m[9],  m[10]));
		matrix.SetRow(3, Point3(m[12], m[13], m[14]));
		matrix.Invert();
		Point3 pos = matrix.GetRow(3);
		*positions++ = pos;
	}
}

// Skin weights are matched against imported RW vertices by position. Build a
// spatial index once per skinned atomic rather than scanning every RW vertex
// for every 3ds Max vertex (quadratic on large character models).
struct RwioSkinVertexIndex {
	std::unordered_map<RWIOProjectionGridKey, std::vector<int>, RWIOProjectionGridKeyHash> cells;
};
static void
buildSkinVertexIndex(rw::Geometry *geo, RwioSkinVertexIndex &index)
{
	index.cells.reserve(geo->numVertices);
	const rw::V3d *verts = geo->morphTargets[0].vertices;
	for(int i = 0; i < geo->numVertices; ++i){
		Point3 p(verts[i].x, verts[i].y, verts[i].z);
		index.cells[rwioProjectionKey(p)].push_back(i);
	}
}
static int
findVertex(rw::Geometry *geo, const RwioSkinVertexIndex &index, const Point3 &p)
{
	const rw::V3d *verts = geo->morphTargets[0].vertices;
	int closest = -1;
	float bestDistance = FLT_MAX;
	RWIOProjectionGridKey key = rwioProjectionKey(p);
	for(int dx = -1; dx <= 1; ++dx)
		for(int dy = -1; dy <= 1; ++dy)
			for(int dz = -1; dz <= 1; ++dz){
				RWIOProjectionGridKey neighbour = { key.x + dx, key.y + dy, key.z + dz };
				auto it = index.cells.find(neighbour);
				if(it == index.cells.end()) continue;
				for(int v : it->second){
					float x = verts[v].x - p.x, y = verts[v].y - p.y, z = verts[v].z - p.z;
					float d = x*x + y*y + z*z;
					if(d < bestDistance){ bestDistance = d; closest = v; }
				}
			}
	if(closest >= 0) return closest;
	// Preserve the legacy nearest-vertex semantics for unusual transformed meshes.
	for(int i = 0; i < geo->numVertices; ++i){
		float x = verts[i].x - p.x, y = verts[i].y - p.y, z = verts[i].z - p.z;
		float d = x*x + y*y + z*z;
		if(d < bestDistance){ bestDistance = d; closest = i; }
	}
	return closest;
}

// Caller must DeleteMe() a converted TriObject when it differs from
// EvalWorldState().obj. Returning Mesh* alone discarded that ownership.
static TriObject*
getTriObject(INode *node, TimeValue t, bool &temporary)
{
	temporary = false;
	::Object *obj = node->EvalWorldState(t).obj;
	if(obj == NULL || !obj->CanConvertToType(triObjectClassID))
		return NULL;
	TriObject *tri = (TriObject*)obj->ConvertToType(t, triObjectClassID);
	temporary = tri != NULL && tri != obj;
	return tri;
}

static ::Object*
makeEditablePolyObject(TriObject *tri, TimeValue t)
{
	if(tri == nil)
		return nil;

	::Object *poly = rwioMakeProjectedNormalEditablePolyObject(tri);
	if(poly != nil){
		tri->DeleteMe();
		return poly;
	}

	if(tri->CanConvertToType(EPOLYOBJ_CLASS_ID)){
		::Object *obj = tri->ConvertToType(t, EPOLYOBJ_CLASS_ID);
		if(obj){
			if(obj != tri)
				tri->DeleteMe();
			return obj;
		}
	}

	if(tri->CanConvertToType(polyObjectClassID)){
		::Object *obj = tri->ConvertToType(t, polyObjectClassID);
		if(obj){
			if(obj != tri)
				tri->DeleteMe();
			return obj;
		}
	}

	return tri;
}

static rw::Skin*
getSkin(rw::Atomic *a)
{
	return rw::Skin::get(a->geometry);
}

void
DFFImport::axesHeuristics(rw::Frame *f)
{
	static rw::Matrix bipmat = {
		{0.0f, 0.0f, 1.0f}, rw::Matrix::TYPEORTHONORMAL,
		{1.0f, 0.0f, 0.0f}, 0,
		{0.0f, 1.0f, 0.0f}, 0,
		{0.0f, 0.0f, 0.0f}, 0
	};

	// If the root transform is not identity we're in
	// RW world coordinates.
	// But we have to be careful with the flags, IDENTITY is not set by frame stream read.
	rw::Matrix root = f->matrix;
	root.optimize();
	this->isWorldSpace = !(root.flags & rw::Matrix::IDENTITY);

	// This heuristic is wrong if we aren't in world space
	// and the root's only child happens to have a biped matrix.
	// AND this with the existance of a hierarchy after the call
	// to improve the guess (who uses bipeds without a hierarchy?).
	rw::Frame *child = f->child;
	this->isBiped = 0;
	if(child && child->next == nil)
		this->isBiped = rw::equal(child->matrix.right, bipmat.right) &&
			rw::equal(child->matrix.at, bipmat.at) &&
			rw::equal(child->matrix.up, bipmat.up) &&
			rw::equal(child->matrix.pos, bipmat.pos);
}

void
printHier(INode *node, int indent)
{
	for(int i = 0; i < indent; i++)
		lprintf(_T("   "));
	lprintf(_T("%s\n"), node->NodeName());
	for(int i = 0; i < node->NumberOfChildren(); i++)
		printHier(node->GetChildNode(i), indent+1);
}

void
DFFImport::saveNodes(INode *node)
{
	lastImported.push_back(node);
	for(int i = 0; i < node->NumberOfChildren(); i++)
		saveNodes(node->GetChildNode(i));
}

static int
getNumChildren(rw::Frame *f)
{
	int n = 0;
	rw::Frame *c;
	for(c = f->child; c; c = c->next)
		n++;
	return n;
}

// return which child of the parent this frame is
static int
getChildIndex(rw::Frame *child)
{
	int n = 0;
	rw::Frame *c;
	if(child->getParent() == nil)
		return 0;
	for(c = child->getParent()->child; c; c = c->next){
		if(c == child)
			return n;
		n++;
	}
	// cannot happen
	return -1;
}

static int
cmpint(const void *a, const void *b)
{
	int *ia = (int*)a;
	int *ib = (int*)b;
	return *ia - *ib;
}

// return which child of the parent this frame is, based oh hanim hierarchy
// return -1 if not all children have an id
static int
getHierChildIndex(rw::Frame *child, rw::HAnimHierarchy *hier)
{
	int i, n;
	int thisIndex;
	int *indices;
	rw::Frame *c;
	if(child->getParent() == nil)
		return 0;

	indices = new int[getNumChildren(child->getParent())];

	n = 0;
	thisIndex = -1;
	for(c = child->getParent()->child; c; c = c->next){
		rw::HAnimData *hanim = rw::HAnimData::get(c);
		if(hanim->id < 0)
			goto err;
		indices[n] = hier->getIndex(hanim->id);
		if(c == child)
			thisIndex = indices[n];
		n++;
	}
	assert(thisIndex >= 0);
	qsort(indices, n, sizeof(int), cmpint);
	for(i = 0; i < n; i++)
		if(indices[i] == thisIndex){
			delete[] indices;
			return i;
		}
err:
	delete[] indices;
	return -1;
}

//#define USE_IMPNODES 0

/*
void
dumpUserData(rw::UserDataArray *ar)
{
	int i;
	lprintf(_T("name: %s\n"), ar->name);
	for(i = 0; i < ar->numElements; i++){
		switch(ar->datatype){
		case rw::USERDATAINT:
			lprintf(_T("	%d\n"), ar->getInt(i));
			break;
		case rw::USERDATAFLOAT:
			lprintf(_T("	%f\n"), ar->getFloat(i));
			break;
		case rw::USERDATASTRING:
			lprintf(_T("	%s\n"), ar->getString(i));
			break;
		}
	}
}

static rw::Frame*
dumpFrameUserDataCB(rw::Frame *f, void*)
{
	using namespace rw;
	int32 i;
	UserDataArray *ar;
	int32 n = UserDataArray::frameGetCount(f);
	for(i = 0; i < n; i++){
		ar = UserDataArray::frameGet(f, i);
		dumpUserData(ar);
	}
	f->forAllChildren(dumpFrameUserDataCB, nil);
	return f;
}

void
dumpUserData(rw::Clump *clump)
{
	lprintf(_T("Frames\n"));
	dumpFrameUserDataCB(clump->getFrame(), nil);
}
*/

void
attachBones(ISkinImportData *skinImp, INode *node)
{
	int i, n;
	skinImp->AddBoneEx(node, true);
	n = node->NumberOfChildren();
	for(i = 0; i < n; i++)
		attachBones(skinImp, node->GetChildNode(i));
}

char*
getFrameName(rw::Frame *f)
{
	char *name;
	name = gta::getNodeName(f);
	if(name[0] == '\0')
		return getUserName(rw::UserDataExtension::get(f));
	else
		return name;
}

void
setNameFromObjName(INode *node, const char *shapename)
{
	if(shapename == nil)
		return;
	const char *end = strstr(shapename, "Shape");
	if(end == nil)
		return;
	char nodename[MAX_PATH];
	size_t len = end-shapename;
	if(len >= MAX_PATH)
		len = MAX_PATH-1;
	memcpy(nodename, shapename, len);
	nodename[len] = '\0';
	node->SetName((TCHAR*)getMaxStr(nodename));
}

static rw::Clump*
readClump(const TCHAR *filename)
{
	using namespace rw;
	RWIOImportTimer timer(_T("readClump"));
	StreamFile in;
	Clump *c;

	if(in.open(getAsciiStr(filename), "rb") == nil)
		return nil;
	currentUVAnimDictionary = nil;
	TexDictionary::setCurrent(TexDictionary::create());
	debugFile = (char*)filename;
	ChunkHeaderInfo header;
	readChunkHeaderInfo(&in, &header);
	if(header.type == ID_UVANIMDICT){
		UVAnimDictionary *dict = UVAnimDictionary::streamRead(&in);
		currentUVAnimDictionary = dict;
		readChunkHeaderInfo(&in, &header);
	}
	if(header.type != ID_CLUMP){
		in.close();
		return nil;
	}
	c = Clump::streamRead(&in);
	in.close();
	if(c == nil)
		return nil;
	rwioImportDebug(_T("clump read: %d frame(s), %d atomic(s)"), c->getFrame()->count(), c->countAtomics());

	double pipeStart = rwioSeconds();
	FORLIST(lnk, c->atomics)
		gta::attachCustomPipelines(rw::Atomic::fromClump(lnk));	// attach xbox pipelines, which we want to uninstance
	int32 platform = findPlatform(c);
	if(platform){
		rw::platform = platform;
		switchPipes(c, platform);
	}
	rwioImportDebug(_T("pipeline switch/attach: %.3f s"), rwioSeconds() - pipeStart);
	double uninstStart = rwioSeconds();
	FORLIST(lnk, c->atomics){
		Atomic *a = Atomic::fromClump(lnk);
		a->uninstance();
		ps2::unconvertADC(a->geometry);
	}
	rwioImportDebug(_T("atomic uninstance/unconvert: %.3f s"), rwioSeconds() - uninstStart);
	return c;
}

struct DFFImportFeatureScan
{
	bool hasQuadData;
	bool hasNormalData;
	bool hasVertexChannelData;
};

static DFFImportFeatureScan
scanDffImportFeatures(const TCHAR *filename)
{
	using namespace rw;
	RWIOImportTimer timer(_T("pre-dialog feature scan"));
	DFFImportFeatureScan scan = { false, false, false };
	rw::Clump *c = readClump(filename);
	if(c == nil)
		return scan;
	scan.hasQuadData = clumpHasStoredQuadEdgeData(c);
	scan.hasNormalData = clumpHasNormalShadingData(c);
	scan.hasVertexChannelData = clumpHasVertexChannelData(c);
	c->destroy();
	if(currentUVAnimDictionary){
		currentUVAnimDictionary->destroy();
		currentUVAnimDictionary = nil;
	}
	TexDictionary::getCurrent()->destroy();
	rw::platform = rw::PLATFORM_D3D8;
	return scan;
}

int
scanDffImportFeatureMask(const TCHAR *filename)
{
	initRW();
	DFFImportFeatureScan scan = scanDffImportFeatures(filename);
	int mask = 0;
	if(scan.hasNormalData)
		mask |= 1;
	if(scan.hasVertexChannelData)
		mask |= 2;
	if(scan.hasQuadData)
		mask |= 4;
	return mask;
}

static int
validVertexChannelType(int type)
{
	switch(type){
	case nvct_color:
	case nvct_illumination:
	case nvct_alpha:
		return type;
	default:
		return nvct_color;
	}
}

static void
addVertexChannelType(HWND combo, const TCHAR *label, int type)
{
	int index = (int)SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)label);
	SendMessage(combo, CB_SETITEMDATA, index, type);
	if(type == validVertexChannelType(DFFImport::vertexChannelType))
		SendMessage(combo, CB_SETCURSEL, index, 0);
}

static HWND
createTooltip(HWND parent)
{
	INITCOMMONCONTROLSEX icc = {};
	icc.dwSize = sizeof(icc);
	icc.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&icc);

	HWND tooltip = CreateWindowEx(WS_EX_TOPMOST, TOOLTIPS_CLASS, NULL,
		WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		parent, NULL, hInstance, NULL);
	if(tooltip){
		SendMessage(tooltip, TTM_ACTIVATE, TRUE, 0);
		SendMessage(tooltip, TTM_SETMAXTIPWIDTH, 0, 360);
	}
	return tooltip;
}

static void
addTooltip(HWND parent, HWND tooltip, int controlId, const TCHAR *text)
{
	if(tooltip == NULL)
		return;
	HWND control = GetDlgItem(parent, controlId);
	if(control == NULL)
		return;

	TOOLINFO toolInfo = {};
	toolInfo.cbSize = sizeof(toolInfo);
	toolInfo.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
	toolInfo.hwnd = parent;
	toolInfo.uId = (UINT_PTR)control;
	toolInfo.lpszText = (LPTSTR)text;
	SendMessage(tooltip, TTM_ADDTOOL, 0, (LPARAM)&toolInfo);

	// Disabled controls do not receive mouse messages, so also cover their
	// dialog rectangle through the parent window.
	RECT rect;
	GetWindowRect(control, &rect);
	MapWindowPoints(NULL, parent, (LPPOINT)&rect, 2);
	TOOLINFO rectToolInfo = {};
	rectToolInfo.cbSize = sizeof(rectToolInfo);
	rectToolInfo.uFlags = TTF_SUBCLASS;
	rectToolInfo.hwnd = parent;
	rectToolInfo.uId = (UINT_PTR)controlId;
	rectToolInfo.rect = rect;
	rectToolInfo.lpszText = (LPTSTR)text;
	SendMessage(tooltip, TTM_ADDTOOL, 0, (LPARAM)&rectToolInfo);
}

BOOL
DFFImport::dffFileRead(const TCHAR *filename)
{
	using namespace rw;
	RWIOImportTimer importTimer(_T("dffFileRead"));

	struct SkinNodeInfo {
		INode *node;
		Atomic *atomic;
		int boneIdx;
	};

	Clump *c = readClump(filename);
	if(c == nil)
		return 0;

	//dumpUserData(c);


	//
	// Import
	//

	TimeValue t = ifc->GetTime();
	int32 numFrames = c->getFrame()->count();
	Frame **flist = new Frame*[numFrames];
	double frameListStart = rwioSeconds();
	makeFrameList(c->getFrame(), flist);
	rwioImportDebug(_T("makeFrameList: %.3f s"), rwioSeconds() - frameListStart);

	int numAtomics = c->countAtomics();
	int numSkinned = 0;

	HAnimHierarchy *hier = HAnimHierarchy::find(c->getFrame());
	if(hier)
		hier->attach();

	SkinNodeInfo *skinInfo = nil;
	if(numAtomics > 0){
		skinInfo = new SkinNodeInfo[numAtomics];
		memset(skinInfo, 0, sizeof(SkinNodeInfo)*numAtomics);
	}

	INode *bones[256];
	memset(bones, 0, sizeof(bones));

	axesHeuristics(c->getFrame());
	this->isBiped = this->isBiped && hier != nil;
	//lprintf("%s %s\n",
	//	this->isWorldSpace ? "world space" : "",
	//	this->isBiped ? "biped" : "");

	INode **nodelist = new INode*[numFrames];
	std::vector<PendingAtomicNode> pendingAtomicNodes;
#ifdef USE_IMPNODES
	ImpNode **impnodelist = new ImpNode*[numFrames];
#endif
	INode *rootnode = nil;
	double nodeLoopStart = rwioSeconds();
	for(int j = 0; j < numFrames; j++){
		double frameStart = rwioSeconds();
		Frame *f = flist[j];

		INode *node;
#ifdef USE_IMPNODES
		ImpNode *impnode = impifc->CreateNode();
		impifc->AddNodeToScene(impnode);
		node = impnode->GetINode();
		impnodelist[j] = impnode;
#else
		int hasObject = 0;
		// Dummy as placeholder
		::Object *obj = (::Object*)ifc->CreateInstance(HELPER_CLASS_ID, Class_ID(DUMMY_CLASS_ID, 0));
		float boxsize = 0.15f;
		((DummyObject*)obj)->SetBox(Box3(Point3(-boxsize/2.0f, -boxsize/2.0f, -boxsize/2.0f), Point3(boxsize/2.0f, boxsize/2.0f, boxsize/2.0f)));
		node = ifc->CreateObjectNode(obj);
#endif
		if(j == 0){
			assert(f->getParent() == nil);
			rootnode = node;
		}

		nodelist[j] = node;
		::Matrix3 tm;
		MRow *m = tm.GetAddr();
		rw::Matrix *rwm = f->getLTM();

		m[0][0] = rwm->right.x;
		m[0][1] = rwm->right.y;
		m[0][2] = rwm->right.z;
		m[1][0] = rwm->up.x;
		m[1][1] = rwm->up.y;
		m[1][2] = rwm->up.z;
		m[2][0] = rwm->at.x;
		m[2][1] = rwm->at.y;
		m[2][2] = rwm->at.z;
		m[3][0] = rwm->pos.x;
		m[3][1] = rwm->pos.y;
		m[3][2] = rwm->pos.z;
#ifdef USE_IMPNODES
		impnode->SetTransform(t, tm);
#else
		node->SetNodeTM(t, tm);
#endif

		// HAnim
		int boneIdx = 0;	// default to hierarchy root, skinned geometry may be above
		if(hier){
			HAnimData *hanim = HAnimData::get(f);
			node->SetUserPropInt(_T("tag"), hanim->id);
			if(hanim->id >= 0){
				// In theory getting index by id should work, but I've seen
				// files where an id was used more than once. hier->attach()
				// does some magic that causes the same frame not to be attached
				// to multiple nodes. Then we can use getIndex(Frame*), fun!

				// Doesn't always work: bones[hier->getIndex(hanim->id)] = node;
				boneIdx = hier->getIndex(f);
				bones[boneIdx] = node;
			}
		}

		if(hier){
			if(f->getParent() && getNumChildren(f->getParent()) > 1){
				int childNum = getHierChildIndex(f, hier);
				if(childNum < 0)
					lprintf(_T("warning: couldn't assign child number by hierarchy\n"));
				else
					node->SetUserPropInt(_T("childNum"), childNum);
			}
		}else{
			if(f->getParent() && getNumChildren(f->getParent()) > 1)
				node->SetUserPropInt(_T("childNum"), getChildIndex(f));
		}

		char *name = getFrameName(f);
		char maxname[32];
		if(name){
			snprintf(maxname, sizeof(maxname), DFFImport::prepend ? "!%s" : "%s", name);
#ifdef USE_IMPNODES
			impnode->SetName((TCHAR*)getMaxStr(maxname));
#else
			node->SetName((TCHAR*)getMaxStr(maxname));
#endif
		}

		// Objects
		int flipz = 0;
		FORLIST(lnk, f->objectList){
			ObjectWithFrame *obj = ObjectWithFrame::fromFrame(lnk);
			if(obj->object.type == Atomic::ID){
				Atomic *a = (Atomic*)obj;
				TriObject *tri = CreateNewTriObject();
				if(!tri) return 0;
				::Mesh *msh = &tri->GetMesh();
				// make a new node outside the hierarchy for skinned objects
				if(getSkin(a)){
					// Transform vertices to root space, but remove world transform
					rw::Matrix tmp, rootspace;
					rw::Matrix::invert(&tmp, f->root->getLTM());	// inverse world
					rw::Matrix::mult(&rootspace, f->getLTM(), &tmp);
					transformGeometry(a->geometry, &rootspace);

					double meshStart = rwioSeconds();
					makeMesh(a, msh);
					RWIOImportedDffNormalSource dffNormalSource = buildDffNormalProjectionSource(msh);
					RWIOImportedDffAttributeSource dffAttributeSource = buildDffAttributeProjectionSource(msh);
					rwioImportDebug(_T("makeMesh skinned: %.3f s (%d verts, %d tris)"), rwioSeconds() - meshStart, a->geometry->numVertices, a->geometry->numTriangles);
					double polyStart = rwioSeconds();
					::Object *poly = makeEditablePolyObject(tri, t);
					rwioImportDebug(_T("makeEditablePoly skinned: %.3f s"), rwioSeconds() - polyStart);
					if(!poly)
						return 0;

					INode *skinNode;
#ifdef USE_IMPNODES
					ImpNode *impnode = impifc->CreateNode();
					impifc->AddNodeToScene(impnode);
					impnode->Reference(poly);
					skinNode = impnode->GetINode();
//					impnode->SetTransform(t, node->GetNodeTM(t));
#else
					skinNode = ifc->CreateObjectNode(poly);
					skinNode->SetNodeTM(t, rootnode->GetNodeTM(t));
#endif
					setNameFromObjName(skinNode, getUserName(UserDataExtension::get(a->geometry)));
					makeMaterials(a, skinNode);
					setVertexChannelDisplay(skinNode, a->geometry, enableVertexChannelDisplayForThisImport);
					rememberDffAttributeProjectionSource(skinNode, dffAttributeSource);
					if(DFFImport::explicitNormals || DFFImport::quadrifyAllPlus)
						rememberDffNormalProjectionSource(skinNode, dffNormalSource);
					skinInfo[numSkinned].atomic = a;
					skinInfo[numSkinned].node = skinNode;
					skinInfo[numSkinned].boneIdx = boneIdx;
					numSkinned++;
				}else{
					double meshStart = rwioSeconds();
					makeMesh(a, msh);
					RWIOImportedDffNormalSource dffNormalSource = buildDffNormalProjectionSource(msh);
					RWIOImportedDffAttributeSource dffAttributeSource = buildDffAttributeProjectionSource(msh);
					rwioImportDebug(_T("makeMesh: %.3f s (%d verts, %d tris)"), rwioSeconds() - meshStart, a->geometry->numVertices, a->geometry->numTriangles);
					double polyStart = rwioSeconds();
					::Object *poly = makeEditablePolyObject(tri, t);
					rwioImportDebug(_T("makeEditablePoly: %.3f s"), rwioSeconds() - polyStart);
					if(!poly)
						return 0;
					INode *meshNode = node;
#ifdef USE_IMPNODES
					assert(!hasObject);
					impnode->Reference(poly);
#else
					if(hasObject){
						meshNode = ifc->CreateObjectNode(poly);
						meshNode->SetNodeTM(t, node->GetNodeTM(t));
						PendingAtomicNode pending;
						pending.parent = node;
						pending.node = meshNode;
						pending.tm = meshNode->GetNodeTM(t);
						pendingAtomicNodes.push_back(pending);
					}else{
						node->SetObjectRef(poly);
						hasObject = 1;
					}
#endif
					if(name == nil || meshNode != node)
						setNameFromObjName(meshNode, getUserName(UserDataExtension::get(a->geometry)));
					makeMaterials(a, meshNode);
					setVertexChannelDisplay(meshNode, a->geometry, enableVertexChannelDisplayForThisImport);
					rememberDffAttributeProjectionSource(meshNode, dffAttributeSource);
					if(DFFImport::explicitNormals || DFFImport::quadrifyAllPlus)
						rememberDffNormalProjectionSource(meshNode, dffNormalSource);
				}
			}else if(obj->object.type == rw::Light::ID){
				assert(!hasObject);
				rw::Light *l = (rw::Light*)obj;
				int maxtype;
				int type = l->getType();
				switch(type){
				case rw::Light::SOFTSPOT:
				case rw::Light::SPOT:
					maxtype = FSPOT_LIGHT;
					break;
				case rw::Light::DIRECTIONAL:
					maxtype = DIR_LIGHT;
					break;
				case rw::Light::POINT:
				case rw::Light::AMBIENT:
				default:
					maxtype = OMNI_LIGHT;
					break;
				}
				GenLight *gl = impifc->CreateLightObject(maxtype);
				gl->SetRGBColor(t, Point3(l->color.red, l->color.green, l->color.blue));
				if(type >= rw::Light::POINT)
					gl->SetAtten(t, ATTEN_END, l->radius);
				if(maxtype == FSPOT_LIGHT){
					float32 a = l->getAngle()*2*180.0f/pi;
					gl->SetFallsize(t, a);
					a -= type == rw::Light::SPOT ? 2.0f : 10.0f;
					gl->SetHotspot(t, a);
				}
				gl->SetAmbientOnly(type == rw::Light::AMBIENT);
#ifdef USE_IMPNODES
				impnode->Reference(gl);
#else
				node->SetObjectRef(gl);
				hasObject = 1;
#endif
				if(name == nil)
					setNameFromObjName(node, getUserName(UserDataExtension::get(l)));
				node->SetWireColor(0xFF00E5FF);
				ifc->AddLightToScene(node);
				gl->Enable(TRUE);
				flipz = 1;
			}else if(obj->object.type == rw::Camera::ID){
				assert(!hasObject);
				rw::Camera *c = (rw::Camera*)obj;
				GenCamera *gc = impifc->CreateCameraObject(c->projection == 1 ? FREE_CAMERA : PARALLEL_CAMERA);
#ifdef USE_IMPNODES
				impnode->Reference(gc);
#else
				node->SetObjectRef(gc);
				hasObject = 1;
#endif
				if(name == nil)
					setNameFromObjName(node, getUserName(UserDataExtension::get(c)));
				gc->SetFOV(t, atan(c->viewWindow.x)*2);
				gc->SetManualClip(1);
				gc->SetClipDist(t, CAM_HITHER_CLIP, c->nearPlane);
				gc->SetClipDist(t, CAM_YON_CLIP, c->farPlane);
				gc->Enable(TRUE);
				flipz = 1;
			}
		}
#ifdef USE_IMPNODES
		if(node->GetObjectRef() == nil)
			impnode->Reference((::Object*)ifc->CreateInstance(HELPER_CLASS_ID, Class_ID(DUMMY_CLASS_ID, 0)));
#else
	// is it deleted when the reference changes?
//		if(hasObject){
//			obj->DeleteMe();
//			obj = nil;
//		}
#endif

		if(flipz){
			AngAxis aa(Point3(0, 1, 0), static_cast<float>(pi));
			node->Rotate(t, tm, aa);	// ImpNode crash
		}
		rwioImportDebug(_T("frame %d/%d node/object import: %.3f s"), j+1, numFrames, rwioSeconds() - frameStart);
	}
	rwioImportDebug(_T("node/object loop: %.3f s"), rwioSeconds() - nodeLoopStart);

#if 0
	Point3 positions[256];
	if(skinAtomics && hier){
		rw::Skin *rwskin = rw::Skin::get(skinAtomics[0]->geometry);
		Matrix3 matrix;
		rw::Matrix *m = skinAtomics[0]->getFrame()->getLTM();
		matrix.SetRow(0, Point3(m->right.x,  m->right.y,  m->right.z));
		matrix.SetRow(1, Point3(m->up.x,  m->up.y,  m->up.z));
		matrix.SetRow(2, Point3(m->at.x,  m->at.y,  m->at.z));
		matrix.SetRow(3, Point3(m->pos.x,  m->pos.y,  m->pos.z));
		getBonePositions(rwskin, hier, positions);
		matrix.TransformPoints(positions, hier->numNodes);
		for(int32 i = 0; i < hier->numNodes; i++){
			INode *n = ifc->CreateObjectNode((::Object*)ifc->CreateInstance(HELPER_CLASS_ID, Class_ID(DUMMY_CLASS_ID, 0)));
			n->SetNodeTM(t, TransMatrix(positions[i]));
		}
	}
#endif

	/* build hierarchy */
	double hierarchyStart = rwioSeconds();
	for(int j = 0; j < numFrames; j++){
		if(flist[j]->getParent() == nil)
			continue;

		int frm = findPointer(flist[j]->getParent(), (void**)flist, numFrames);
		// keepTM is broken for ImpNodes so do it manually
		::Matrix3 tm = nodelist[j]->GetNodeTM(0);
		nodelist[frm]->AttachChild(nodelist[j], 0);
		nodelist[j]->SetNodeTM(0, tm);
	}
	for(size_t j = 0; j < pendingAtomicNodes.size(); j++){
		pendingAtomicNodes[j].parent->AttachChild(pendingAtomicNodes[j].node, 0);
		pendingAtomicNodes[j].node->SetNodeTM(0, pendingAtomicNodes[j].tm);
	}
	rwioImportDebug(_T("attach hierarchy: %.3f s"), rwioSeconds() - hierarchyStart);

	// Convert RW to Max world space.
	// Rotate Bipeds too because it looks nicer.
	double convertStart = rwioSeconds();
	if (this->convertHierarchy &&
		(this->isWorldSpace || this->isBiped)) {
		AngAxis aa(Point3(1, 0, 0), float(-pi / 2));
		::Matrix3 tm;
		tm.IdentityMatrix();
		rootnode->Rotate(t, tm, aa);		// ImpNode crash
		for (int i = 0; i < numSkinned; i++)
			skinInfo[i].node->Rotate(t, tm, aa);	// ImpNode crash
	}
	rwioImportDebug(_T("convert hierarchy/world rotation: %.3f s"), rwioSeconds() - convertStart);
	// Remember whether this was a biped for exporting
	if(this->isBiped && rootnode->NumberOfChildren() == 1)
		rootnode->GetChildNode(0)->SetUserPropBool(_T("fakeBiped"), TRUE);

	//
	// Skin
	//

	// Check that we have all nodes and display dummies as old-style bones
	double skinStart = rwioSeconds();
	if(numSkinned)
		for(int j = 0; j < hier->numNodes; j++){
			if(bones[j] == nil){
				lprintf(_T("Error: bone %d missing\n"), j);
				goto noskin;
			}
			::Object *obj = bones[j]->EvalWorldState(0).obj;
			if(obj && obj->SuperClassID() == HELPER_CLASS_ID && obj->ClassID() == Class_ID(DUMMY_CLASS_ID, 0)){
				bones[j]->ShowBone(2);
				bones[j]->SetWireColor(0xFF00FFFF);
			}
		}


	for(int i = 0; i < numSkinned && hier; i++){
		INode *skinNode = skinInfo[i].node;
		Skin *rwskin = getSkin(skinInfo[i].atomic);
		if(rwskin->numBones != hier->numNodes){
			lprintf(_T("number of bones doesn't match hierarchy\n"));
			goto noskin;
		}
		Modifier *skin = (Modifier*)ifc->CreateInstance(SClass_ID(OSM_CLASS_ID), SKIN_CLASSID);
		ISkinImportData *skinImp = (ISkinImportData*)skin->GetInterface(I_SKINIMPORTDATA);
		GetCOREInterface7()->AddModifier(*skinNode, *skin);

		attachBones(skinImp, bones[skinInfo[i].boneIdx]);

		// Assigning weights becomes a bit complicated because
		// we deleted isolated vertices.
		Tab<::INode*> bt;
		Tab<float> wt;
		bt.Resize(4);
		wt.Resize(4);
		float *w;
		uint8 *ix;
		bool temporaryTri = false;
		TriObject *skinTri = getTriObject(skinNode, t, temporaryTri);
		if(skinTri == NULL){
			lprintf(_T("RWIO: skin node cannot be converted to a triangle mesh\n"));
			continue;
		}
		::Mesh *maxmesh = &skinTri->GetMesh();
		int numVerts = maxmesh->getNumVerts();
		RwioSkinVertexIndex vertexIndex;
		buildSkinVertexIndex(skinInfo[i].atomic->geometry, vertexIndex);
		for(int j = 0; j < numVerts; j++){
			Point3 pos = maxmesh->getVert(j);
			int idx = findVertex(skinInfo[i].atomic->geometry, vertexIndex, pos);
			if(idx < 0) continue;
			w = &rwskin->weights[idx*4];
			ix = &rwskin->indices[idx*4];
			bt.ZeroCount();
			wt.ZeroCount();
			for(int k = 0; k < 4; k++){
				if(w[k] > 0.0f){
					bt.Append(1, &bones[ix[k]]);
					wt.Append(1, &w[k]);
				}
			}
			BOOL res = skinImp->AddWeights(skinNode, j, bt, wt);
		}
		if(temporaryTri)
			skinTri->DeleteMe();
	}
	rwioImportDebug(_T("skin setup: %.3f s (%d skinned atomic(s))"), rwioSeconds() - skinStart, numSkinned);

noskin:
	INode *realroot;
	// Remove dummy node if one was inserted on export
	if(this->convertHierarchy &&
	   (this->isWorldSpace || hier) &&
	   rootnode->GetChildNode(0)){
		realroot = rootnode->GetChildNode(0);
		rootnode->Delete(t, TRUE);	// ImpNode crash
	}else
		realroot = rootnode;

//	printHier(realroot, 0);
//	for(int i = 0; i < numSkinned; i++)
//		printHier(skinInfo[i].node, 0);

	// save nodes for selection
	double finishStart = rwioSeconds();
	saveNodes(realroot);
	for(int i = 0; i < numSkinned; i++)
		saveNodes(skinInfo[i].node);

	impifc->RedrawViews();
	rwioImportDebug(_T("save nodes/redraw: %.3f s"), rwioSeconds() - finishStart);
	delete[] skinInfo;
	delete[] nodelist;
#ifdef USE_IMPNODES
	delete[] impnodelist;
#endif
	delete[] flist;

	c->destroy();
	if(currentUVAnimDictionary)
		currentUVAnimDictionary->destroy();
	TexDictionary::getCurrent()->destroy();

	ifc->SelectNode(realroot);

	rw::platform = rw::PLATFORM_D3D8;
	return TRUE;
}

static INT_PTR CALLBACK
ImportDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	ISpinnerControl  *spin;
	HWND combo;
	DFFImport *imp = DLGetWindowLongPtr<DFFImport*>(hWnd); 
	static TCHAR recoverQuadsTip[] = _T("Restores saved RWIO quad edge visibility from the DFF so compatible triangles display as quads in Editable Poly.");
	static TCHAR recoverQuadsMissingTip[] = _T("This DFF has no RWIO quad display data. Export with Quad Display Data enabled first.");
	static TCHAR explicitNormalsTip[] = _T("Preserves DFF vertex normals as explicit Max normals. This only works on DFFs that already contain normal shading data.");
	static TCHAR explicitNormalsMissingTip[] = _T("This DFF has no normal shading data, so explicit normals cannot be imported.");
	static TCHAR vertexChannelTip[] = _T("Enables shaded vertex-channel display on imported nodes.");
	static TCHAR vertexChannelMissingTip[] = _T("This DFF has no vertex color, illumination, or alpha channel data to display.");
	switch(msg){
	case WM_INITDIALOG:
		imp = (DFFImport*)lParam;
		DLSetWindowLongPtr(hWnd, lParam); 
		CenterWindow(hWnd, GetParent(hWnd)); 
		CheckDlgButton(hWnd, IDC_CONVAXIS, DFFImport::convertHierarchy);
		CheckDlgButton(hWnd, IDC_AUTOSMOOTH, DFFImport::autoSmooth);
		if(imp && imp->dffHasNormalData){
			CheckDlgButton(hWnd, IDC_EXPLNORM, DFFImport::explicitNormals);
			EnableWindow(GetDlgItem(hWnd, IDC_EXPLNORM), TRUE);
		}else{
			CheckDlgButton(hWnd, IDC_EXPLNORM, FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_EXPLNORM), FALSE);
		}
		if(IsDlgButtonChecked(hWnd, IDC_EXPLNORM)){
			CheckDlgButton(hWnd, IDC_AUTOSMOOTH, FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_AUTOSMOOTH), FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_ANGLE), FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_ANGLESPIN), FALSE);
		}else if(IsDlgButtonChecked(hWnd, IDC_AUTOSMOOTH)){
			CheckDlgButton(hWnd, IDC_EXPLNORM, FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_EXPLNORM), FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_ANGLE), TRUE);
			EnableWindow(GetDlgItem(hWnd, IDC_ANGLESPIN), TRUE);
		}else{
			EnableWindow(GetDlgItem(hWnd, IDC_ANGLE), FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_ANGLESPIN), FALSE);
		}
		CheckDlgButton(hWnd, IDC_STDMAT, DFFImport::importStdMaterials);
		CheckDlgButton(hWnd, IDC_FIXKAM, DFFImport::fixKam);
		CheckDlgButton(hWnd, IDC_QUADRIFYALLPLUS, DFFImport::quadrifyAllPlus);
		EnableWindow(GetDlgItem(hWnd, IDC_QAP_DIAGRATIO), IsDlgButtonChecked(hWnd, IDC_QUADRIFYALLPLUS));
		EnableWindow(GetDlgItem(hWnd, IDC_QAP_DIAGRATIOSPIN), IsDlgButtonChecked(hWnd, IDC_QUADRIFYALLPLUS));
		DFFImport::vertexChannelType = validVertexChannelType(DFFImport::vertexChannelType);
		combo = GetDlgItem(hWnd, IDC_VERTCHANNELTYPE);
		addVertexChannelType(combo, _T("Color"), nvct_color);
		addVertexChannelType(combo, _T("Illum"), nvct_illumination);
		addVertexChannelType(combo, _T("Alpha"), nvct_alpha);
		if(imp && imp->dffHasVertexChannelData){
			CheckDlgButton(hWnd, IDC_VERTCHANNELDISPLAY, DFFImport::vertexChannelDisplay);
			EnableWindow(GetDlgItem(hWnd, IDC_VERTCHANNELDISPLAY), TRUE);
			EnableWindow(combo, DFFImport::vertexChannelDisplay);
		}else{
			CheckDlgButton(hWnd, IDC_VERTCHANNELDISPLAY, FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_VERTCHANNELDISPLAY), FALSE);
			EnableWindow(combo, FALSE);
		}
		if(imp && imp->dffHasQuadData){
			CheckDlgButton(hWnd, IDC_RECOVERQUADS, DFFImport::recoverQuads);
			EnableWindow(GetDlgItem(hWnd, IDC_RECOVERQUADS), TRUE);
		}else{
			CheckDlgButton(hWnd, IDC_RECOVERQUADS, FALSE);
			EnableWindow(GetDlgItem(hWnd, IDC_RECOVERQUADS), FALSE);
		}

		{
			HWND tooltip = createTooltip(hWnd);
			addTooltip(hWnd, tooltip, IDC_CONVAXIS, _T("Converts the imported RenderWare frame hierarchy from RW/GTA axes into 3ds Max-friendly orientation when needed."));
			addTooltip(hWnd, tooltip, IDC_AUTOSMOOTH, _T("Generates Max smoothing groups from the angle below when DFF explicit normals are not being preserved."));
			addTooltip(hWnd, tooltip, IDC_ANGLE, _T("Auto Smooth angle in degrees. Adjacent faces below this angle can share smoothing."));
			addTooltip(hWnd, tooltip, IDC_ANGLESPIN, _T("Adjusts the Auto Smooth angle in degrees."));
			addTooltip(hWnd, tooltip, IDC_EXPLNORM, (imp && imp->dffHasNormalData) ? explicitNormalsTip : explicitNormalsMissingTip);
			addTooltip(hWnd, tooltip, IDC_STDMAT, _T("Imports 3ds Max Standard materials; select RW Material to preserve RenderWare extensions."));
			addTooltip(hWnd, tooltip, IDC_FIXKAM, _T("Rebuilds face material IDs from RenderWare mesh sections for DFFs whose triangle material IDs are wrong or missing."));
			addTooltip(hWnd, tooltip, IDC_VERTCHANNELDISPLAY, (imp && imp->dffHasVertexChannelData) ? vertexChannelTip : vertexChannelMissingTip);
			addTooltip(hWnd, tooltip, IDC_VERTCHANNELTYPE, _T("Chooses which vertex channel Max displays: color, illumination, or alpha."));
			addTooltip(hWnd, tooltip, IDC_RECOVERQUADS, (imp && imp->dffHasQuadData) ? recoverQuadsTip : recoverQuadsMissingTip);
			addTooltip(hWnd, tooltip, IDC_QUADRIFYALLPLUS, _T("After import, auto-welds imported DFF meshes at 0.001 and runs QuadrifyAll++ preserving UVs, material, face IDs, and object ID."));
			addTooltip(hWnd, tooltip, IDC_QAP_DIAGRATIO, _T("QuadrifyAll++ edge cleanup strength. Lower values are safer and hide fewer edges. 0.85 is the default. 1.00 is permissive; 2.00 is aggressive/unsafe."));
			addTooltip(hWnd, tooltip, IDC_QAP_DIAGRATIOSPIN, _T("Adjusts how aggressively QuadrifyAll++ hides candidate triangle-pair edges from 0.00 to 2.00. Values above 1.00 relax extra safety gates."));
		}

		spin = GetISpinner(GetDlgItem(hWnd, IDC_ANGLESPIN));
		spin->LinkToEdit(GetDlgItem(hWnd, IDC_ANGLE), EDITTYPE_INT); 
		spin->SetLimits(0, 180, TRUE); 
		spin->SetScale(1.0f);
		spin->SetValue(DFFImport::smoothingAngle, FALSE);
		ReleaseISpinner(spin);

		spin = GetISpinner(GetDlgItem(hWnd, IDC_QAP_DIAGRATIOSPIN));
		spin->LinkToEdit(GetDlgItem(hWnd, IDC_QAP_DIAGRATIO), EDITTYPE_FLOAT);
		spin->SetLimits(0.0f, 2.0f, FALSE);
		spin->SetScale(0.01f);
		spin->SetValue(rwioClampQapDiagonalRatio(DFFImport::quadrifyAllPlusDiagonalRatio), FALSE);
		ReleaseISpinner(spin);
		break;
	case WM_COMMAND:
		switch(LOWORD(wParam)){
		case IDC_AUTOSMOOTH:
			if(HIWORD(wParam) == BN_CLICKED){
				BOOL checked = IsDlgButtonChecked(hWnd, IDC_AUTOSMOOTH);
				if(checked && imp && imp->dffHasNormalData)
					CheckDlgButton(hWnd, IDC_EXPLNORM, FALSE);
				if(imp && imp->dffHasNormalData)
					EnableWindow(GetDlgItem(hWnd, IDC_EXPLNORM), !checked);
				EnableWindow(GetDlgItem(hWnd, IDC_ANGLE), checked);
				EnableWindow(GetDlgItem(hWnd, IDC_ANGLESPIN), checked);
			}
			break;
		case IDC_EXPLNORM:
			if(HIWORD(wParam) == BN_CLICKED){
				BOOL checked = IsDlgButtonChecked(hWnd, IDC_EXPLNORM);
				if(checked)
					CheckDlgButton(hWnd, IDC_AUTOSMOOTH, FALSE);
				EnableWindow(GetDlgItem(hWnd, IDC_AUTOSMOOTH), !checked);
				EnableWindow(GetDlgItem(hWnd, IDC_ANGLE), !checked && IsDlgButtonChecked(hWnd, IDC_AUTOSMOOTH));
				EnableWindow(GetDlgItem(hWnd, IDC_ANGLESPIN), !checked && IsDlgButtonChecked(hWnd, IDC_AUTOSMOOTH));
			}
			break;
		case IDC_VERTCHANNELDISPLAY:
			if(HIWORD(wParam) == BN_CLICKED)
				EnableWindow(GetDlgItem(hWnd, IDC_VERTCHANNELTYPE),
					imp && imp->dffHasVertexChannelData && IsDlgButtonChecked(hWnd, IDC_VERTCHANNELDISPLAY));
			break;
		case IDC_QUADRIFYALLPLUS:
			if(HIWORD(wParam) == BN_CLICKED){
				BOOL qapChecked = IsDlgButtonChecked(hWnd, IDC_QUADRIFYALLPLUS);
				EnableWindow(GetDlgItem(hWnd, IDC_QAP_DIAGRATIO), qapChecked);
				EnableWindow(GetDlgItem(hWnd, IDC_QAP_DIAGRATIOSPIN), qapChecked);
			}
			break;
		case IDOK:
			DFFImport::convertHierarchy = IsDlgButtonChecked(hWnd, IDC_CONVAXIS);
			DFFImport::autoSmooth = IsDlgButtonChecked(hWnd, IDC_AUTOSMOOTH);
			if(imp && imp->dffHasNormalData)
				DFFImport::explicitNormals = IsDlgButtonChecked(hWnd, IDC_EXPLNORM);
			DFFImport::importStdMaterials = IsDlgButtonChecked(hWnd, IDC_STDMAT);
			DFFImport::fixKam = IsDlgButtonChecked(hWnd, IDC_FIXKAM);
			DFFImport::quadrifyAllPlus = IsDlgButtonChecked(hWnd, IDC_QUADRIFYALLPLUS);

			spin = GetISpinner(GetDlgItem(hWnd, IDC_QAP_DIAGRATIOSPIN));
			DFFImport::quadrifyAllPlusDiagonalRatio = rwioClampQapDiagonalRatio(spin->GetFVal());
			ReleaseISpinner(spin);

			if(imp){
				imp->enableVertexChannelDisplayForThisImport =
					imp->dffHasVertexChannelData && IsDlgButtonChecked(hWnd, IDC_VERTCHANNELDISPLAY);
				if(imp->dffHasVertexChannelData)
					DFFImport::vertexChannelDisplay = IsDlgButtonChecked(hWnd, IDC_VERTCHANNELDISPLAY);
			}
			combo = GetDlgItem(hWnd, IDC_VERTCHANNELTYPE);
			{
				int index = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
				if(index != CB_ERR)
					DFFImport::vertexChannelType = validVertexChannelType((int)SendMessage(combo, CB_GETITEMDATA, index, 0));
			}
			if(imp && imp->dffHasQuadData)
				DFFImport::recoverQuads = IsDlgButtonChecked(hWnd, IDC_RECOVERQUADS);

			spin = GetISpinner(GetDlgItem(hWnd, IDC_ANGLESPIN));
			DFFImport::smoothingAngle = spin->GetIVal();
			ReleaseISpinner(spin);

			EndDialog(hWnd, 1);
			break;
		case IDCANCEL:
			EndDialog(hWnd, 0);
			break;
		default:
			//lprintf("WM_COMMAND: %d\n", LOWORD(wParam));
			break;
		}
		break;
	default:
		//lprintf("MESG: %d\n", LOWORD(msg));
		return FALSE;
	}
	return TRUE;
}

DFFImport::DFFImport(void)
{
	dffHasQuadData = false;
	dffHasNormalData = false;
	dffHasVertexChannelData = false;
	enableVertexChannelDisplayForThisImport = false;
}

DFFImport::~DFFImport(void)
{
}

int
DFFImport::ExtCount(void)
{
	return 1;
}

const TCHAR*
DFFImport::Ext(int n)
{
	switch(n){
	case 0:
		return _T("DFF");
	}
	return _T("");
}

const TCHAR*
DFFImport::LongDesc(void)
{
	return _T(STR_DFFFILE); //GetStringT(IDS_DFFFILE);
}
	
const TCHAR*
DFFImport::ShortDesc(void)
{
	return _T(STR_DFFCLASSNAME); //GetStringT(IDS_CLASSNAME);
}

const TCHAR*
DFFImport::AuthorName(void)
{
	return _T(STR_AUTHOR); //GetStringT(IDS_AUTHOR);
}

const TCHAR*
DFFImport::CopyrightMessage(void)
{
	return _T(STR_COPYRIGHT); //GetStringT(IDS_COPYRIGHT);
}

const TCHAR *
DFFImport::OtherMessage1(void)
{
	return _T("");
}

const TCHAR *
DFFImport::OtherMessage2(void)
{
	return _T("");	
}

unsigned int
DFFImport::Version(void)
{				// Version number * 100 (i.e. v3.01 = 301)
	return VERSION;
}

void
DFFImport::ShowAbout(HWND hWnd)
{
}

int
DFFImport::DoImport(const TCHAR *filename, ImpInterface *ii, Interface *i, BOOL suppressPrompts)
{
	RWIOImportTimer timer(_T("DoImport"));
	initRW();
	this->ifc = i;
	this->impifc = ii;

	clearTextureCache();
	setCurrentImportDirectory(filename);
	lastImported.clear();
	rwioImportedDffNormalSources.clear();
	rwioImportedDffNormalSourcesByName.clear();
	rwioImportedDffAttributeSources.clear();
	rwioImportedDffAttributeSourcesByName.clear();
	ensureAdvancedViewportTransparency();
	DFFImportFeatureScan scan = scanDffImportFeatures(filename);
	this->dffHasQuadData = scan.hasQuadData;
	this->dffHasNormalData = scan.hasNormalData;
	this->dffHasVertexChannelData = scan.hasVertexChannelData;
	this->enableVertexChannelDisplayForThisImport =
		this->dffHasVertexChannelData && DFFImport::vertexChannelDisplay;
	if(!suppressPrompts){
		DFFImport::prepend = 0;
		if(!DialogBoxParam(hInstance, MAKEINTRESOURCE(IDD_DFFIMPORT),
			i->GetMAXHWnd(), ImportDlgProc, (LPARAM)this))
			return 1;
	}

	if(dffFileRead(filename)){
		if(DFFImport::quadrifyAllPlus){
			// Preserve the imported roots before running MAXScript.  Some legacy
			// selection helpers can clear/replace lastImported, but the C++ cache
			// is keyed by the stable INode pointers created during import.
			std::vector<INode*> rwioQapImportedRoots = lastImported;
			runRwioQuadrifyAllPlusImport();
			// Do the real attribute projection in C++ after the MAXScript topology
			// changes finish.  This avoids relying on def_visible_primitive exposure,
			// which can be unavailable in some 3ds Max/SDK builds.
			rwioProjectCachedDffAttributesForRoots(rwioQapImportedRoots);
		}
		clearTextureCache();
		setCurrentImportDirectory(NULL);
		return 1;
	}
	clearTextureCache();
	setCurrentImportDirectory(NULL);
	return 0;
}
