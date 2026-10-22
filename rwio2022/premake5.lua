-- RWIO for 3ds Max 2022
--
-- Environment overrides:
--   LIBRW       -> librw root
--   LIBRWGTA    -> librwgta root
--   MAXSDK2022  -> 3ds Max 2022 SDK maxsdk folder
--
-- Example:
--   set MAXSDK2022=C:\Program Files\Autodesk\3ds Max 2022 SDK\maxsdk
--   premake5 vs2022
--
-- This script intentionally generates Release only; Debug is disabled for this plugin build.

local Librw = os.getenv("LIBRW") or path.getabsolute("deps/librw")
local Librwgta = os.getenv("LIBRWGTA") or path.getabsolute("deps/librwgta")
local MaxSdk2022 = os.getenv("MAXSDK2022") or "C:/Program Files/Autodesk/3ds Max 2022 SDK/maxsdk"

local function firstExistingDir(paths)
	for _, p in ipairs(paths) do
		if os.isdir(p) then
			return p
		end
	end
	return paths[1]
end

local MaxSdkLibDir = firstExistingDir({
	path.join(MaxSdk2022, "lib/x64/Release"),
	path.join(MaxSdk2022, "x64/lib"),
	path.join(MaxSdk2022, "lib/x64")
})

workspace "rwio"
	configurations { "Release" }
	platforms { "amd64" }
	location "build"
	startproject "rwio2022"
	system "Windows"
	systemversion (os.getenv("RWIO_WINSDK_VERSION") or "10.0.17134.0")
	cppdialect "C++14"

	filter { "platforms:amd64" }
		architecture "x86_64"
	filter {}

project "rwio2022"
	kind "SharedLib"
	language "C++"
	targetname "rwio2022"
	targetextension ".dli"
	targetdir "lib/%{cfg.platform}/Release"
	objdir "build/obj/%{prj.name}/%{cfg.platform}/Release"
	characterset "Unicode"
	exceptionhandling "SEH"
	staticruntime "Off"
	flags { "MultiProcessorCompile" }

	files {
		"src/**.h",
		"src/**.hpp",
		"src/**.c",
		"src/**.cpp",
		"src/**.rc",
		"src/**.def"
	}

	includedirs {
		Librw,
		path.join(Librwgta, "src"),
		path.join(MaxSdk2022, "include")
	}

	libdirs {
		path.join(Librw, "lib/win-%{cfg.platform}-null/Release"),
		path.join(Librwgta, "lib/win-%{cfg.platform}-null/Release"),
		MaxSdkLibDir
	}

	links {
		"rw",
		"rwgta",
		"core",
		"geom",
		"gfx",
		"mesh",
		"MNMath",
		"poly",
		"maxutil",
		"maxscrpt",
		"Paramblk2",
		"bmm",
		"comctl32"
	}

	buildoptions {
		"/permissive-",
		"/Zc:__cplusplus",
		"/GR",
		"/we4706",
		"/we4390",
		"/we4557",
		"/we4546",
		"/we4545",
		"/we4295",
		"/we4310",
		"/we4130",
		"/we4611",
		"/we4213",
		"/we4121",
		"/we4715",
		"/w34701",
		"/w34265",
		"/wd4244",
		"/wd4018",
		"/wd4819"
	}

	defines {
		"WIN32",
		"_WINDOWS",
		"_USRDLL"
	}

	filter { "action:vs2022" }
		toolset "v141"
	filter { "action:vs2017" }
		toolset "v141"


	filter "configurations:Release"
		defines { "NDEBUG" }
		runtime "Release"
		optimize "Speed"
		symbols "Off"

	filter {}
