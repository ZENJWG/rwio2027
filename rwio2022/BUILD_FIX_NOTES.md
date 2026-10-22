# RWIO 2022 build notes

Build with `build.bat` and 2022 SDK, MSVC v141, and Windows SDK 10.0.17134.0.

The included dependency patch handles legacy Premake `StaticRuntime` and `WinMain`, and enables the /MD Release configuration.
The dependency build helper maps the solution's Release|win-amd64-null configuration to the actual project platform.
Only RWIO 2022's C++ target is changed; the previous 2027 project is not included.

**UNTESTED:** 3ds Max 2022 is not installed on the author's machine. Build and runtime compatibility have not been verified.
