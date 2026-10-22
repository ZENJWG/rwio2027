@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
title RWIO 2027 - Visual Studio 2026 build
echo ==== RWIO 2027 automated build (Visual Studio 2026) ====
echo.
set "ROOT=%CD%"
set "PREMAKE=%ROOT%\premake5.exe"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSRESULT=%TEMP%\rwio-vswhere-%RANDOM%-%RANDOM%.txt"
set "SDK_BASE=%ProgramFiles%\Autodesk\3ds Max 2027 SDK"
set "SDK_DOWNLOAD_DIR=%ROOT%\deps\maxsdk2027"
set "SDK_PAGE=https://aps.autodesk.com/developer/overview/3ds-max-api"

if not exist "%PREMAKE%" (
  echo ERROR: premake5.exe was not found in "%ROOT%".
  goto :fail
)

call :ensure_git
if errorlevel 1 goto :fail
call :ensure_vs
if errorlevel 1 goto :fail
call :ensure_sdk
if errorlevel 1 goto :fail

if not exist "%ROOT%\deps" mkdir "%ROOT%\deps"
if not defined LIBRW set "LIBRW=%ROOT%\deps\librw"
if not defined LIBRWGTA set "LIBRWGTA=%ROOT%\deps\librwgta"
if not exist "%LIBRW%\premake5.lua" (
  echo.
  echo Downloading librw...
  git clone --depth 1 --recursive https://github.com/aap/librw.git "%LIBRW%"
  if errorlevel 1 goto :fail
)
if not exist "%LIBRWGTA%\premake5.lua" (
  echo.
  echo Downloading librwgta...
  git clone --depth 1 --recursive https://github.com/aap/librwgta.git "%LIBRWGTA%"
  if errorlevel 1 goto :fail
)

rem Restore the normal /MD Release configuration and update deprecated
rem legacy StaticRuntime / WinMain flags in upstream Premake scripts (idempotently).
call :restore_release "%LIBRW%\premake5.lua"
if errorlevel 1 goto :fail
call :restore_release "%LIBRWGTA%\premake5.lua"
if errorlevel 1 goto :fail

echo.
echo [1/3] Building librw (64-bit, null backend, Release)...
pushd "%LIBRW%" || goto :fail
"%PREMAKE%" vs2022
if errorlevel 1 (popd & goto :fail)
call :build_library "librw" "rw.lib"
if errorlevel 1 (popd & goto :fail)
popd

echo.
echo [2/3] Building librwgta (64-bit, null backend, Release)...
pushd "%LIBRWGTA%" || goto :fail
"%PREMAKE%" vs2022
if errorlevel 1 (popd & goto :fail)
call :build_library "librwgta" "rwgta.lib"
if errorlevel 1 (popd & goto :fail)
popd

echo.
echo [3/3] Generating and building RWIO...
"%PREMAKE%" vs2022
if errorlevel 1 goto :fail
if not exist "build\rwio2027.vcxproj" (
  echo ERROR: Premake did not generate build\rwio2027.vcxproj
  goto :fail
)
"%MSBUILD%" "build\rwio2027.vcxproj" /m /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /verbosity:minimal /nologo
if errorlevel 1 goto :fail
if not exist "%ROOT%\lib\amd64\Release\rwio2027.dli" (
  echo ERROR: Plugin output missing: lib\amd64\Release\rwio2027.dli
  goto :fail
)
echo.
echo BUILD SUCCEEDED
echo Plugin: "%ROOT%\lib\amd64\Release\rwio2027.dli"
if exist "%VSRESULT%" del /q "%VSRESULT%" >nul 2>&1
pause
exit /b 0

:ensure_git
where git >nul 2>&1
if not errorlevel 1 exit /b 0
where winget >nul 2>&1
if errorlevel 1 (
  echo ERROR: Git not found, and WinGet is not available to install it.
  exit /b 1
)
echo Git missing. Installing Git using WinGet...
winget install -e --id Git.Git --accept-package-agreements --accept-source-agreements
if errorlevel 1 exit /b 1
set "PATH=%ProgramFiles%\Git\cmd;%ProgramFiles(x86)%\Git\cmd;%PATH%"
where git >nul 2>&1
if errorlevel 1 (
  echo ERROR: Git was installed, but is not available in this session.
  echo Restart your terminal and run build.bat again.
  exit /b 1
)
exit /b 0

:ensure_vs
if not exist "%VSWHERE%" (
  echo Visual Studio discovery tool is missing.
  call :install_vs_buildtools
  if errorlevel 1 exit /b 1
)
if not exist "%VSWHERE%" (
  echo ERROR: vswhere.exe still not found after installation.
  exit /b 1
)
call :find_vs_cpp
if defined VSROOT goto :validate_vs
rem Look for an existing VS 2026 installation without the required C++ tools.
"%VSWHERE%" -latest -products * -version "[18.0,19.0)" -property installationPath > "%VSRESULT%"
set "VS_ANY="
for /f "usebackq delims=" %%V in ("%VSRESULT%") do set "VS_ANY=%%V"
if not defined VS_ANY (
  echo VS 2026 not found. Installing VS 2026 Build Tools...
  call :install_vs_buildtools
  if errorlevel 1 exit /b 1
) else (
  echo VS 2026 was found, but the C++ workload is missing.
  echo Attempting to add Desktop development with C++...
  if not exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\setup.exe" goto :missing_cpp
  "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\setup.exe" modify --installPath "%VS_ANY%" --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --quiet --wait --norestart
  if errorlevel 1 goto :missing_cpp
)
call :find_vs_cpp
if not defined VSROOT goto :missing_cpp
:validate_vs
set "MSBUILD=%VSROOT%\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" set "MSBUILD=%VSROOT%\MSBuild\Current\Bin\amd64\MSBuild.exe"
if not exist "%MSBUILD%" (
  echo ERROR: MSBuild.exe is missing under "%VSROOT%".
  exit /b 1
)
echo Visual Studio 2026: "%VSROOT%"
exit /b 0

:find_vs_cpp
set "VSROOT="
rem Redirect to a temporary file: FOR /F command substitution breaks on
rem quoted paths such as "C:\Program Files (x86)\...\vswhere.exe".
"%VSWHERE%" -latest -products * -version "[18.0,19.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSRESULT%"
if errorlevel 1 exit /b 0
for /f "usebackq delims=" %%V in ("%VSRESULT%") do set "VSROOT=%%V"
exit /b 0

:install_vs_buildtools
where winget >nul 2>&1
if errorlevel 1 (
  echo ERROR: WinGet is not available.
  echo Install the VS 2026 "Desktop development with C++" workload manually.
  exit /b 1
)
winget install -e --id Microsoft.VisualStudio.2026.BuildTools --accept-package-agreements --accept-source-agreements --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
exit /b %errorlevel%

:missing_cpp
echo ERROR: Visual Studio 2026 C++ tools could not be detected.
echo Open Visual Studio Installer, Modify, and install "Desktop development with C++".
echo Make sure MSVC v145 and a Windows SDK are selected.
exit /b 1

:ensure_sdk
if defined MAXSDK2027 if exist "%MAXSDK2027%\include" goto :sdk_found
if exist "%SDK_BASE%\maxsdk\include" set "MAXSDK2027=%SDK_BASE%\maxsdk"
if defined MAXSDK2027 if exist "%MAXSDK2027%\include" goto :sdk_found
if exist "%SDK_BASE%\include" set "MAXSDK2027=%SDK_BASE%"
if defined MAXSDK2027 if exist "%MAXSDK2027%\include" goto :sdk_found
if exist "%ROOT%\deps\maxsdk2027\maxsdk\include" set "MAXSDK2027=%ROOT%\deps\maxsdk2027\maxsdk"
if defined MAXSDK2027 if exist "%MAXSDK2027%\include" goto :sdk_found
if exist "%ROOT%\deps\maxsdk2027\include" set "MAXSDK2027=%ROOT%\deps\maxsdk2027"
if defined MAXSDK2027 if exist "%MAXSDK2027%\include" goto :sdk_found

rem Find a locally extracted SDK with extra enclosing directories.
if exist "%SDK_DOWNLOAD_DIR%" (
  for /d /r "%SDK_DOWNLOAD_DIR%" %%D in (maxsdk) do if exist "%%D\include" set "MAXSDK2027=%%D"
)
if defined MAXSDK2027 if exist "%MAXSDK2027%\include" goto :sdk_found

rem Autodesk distributes this SDK from its official 3ds Max API page.
rem For access-controlled SDK downloads provide an authorized direct ZIP URL.
if defined MAXSDK2027_URL (
  echo SDK not found locally. Downloading from MAXSDK2027_URL...
  if not exist "%ROOT%\deps" mkdir "%ROOT%\deps"
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $uri=$env:MAXSDK2027_URL; if (-not $uri.StartsWith('https://', [StringComparison]::OrdinalIgnoreCase)) { throw 'MAXSDK2027_URL must be an HTTPS download URL' }; $zip=Join-Path $env:SDK_DOWNLOAD_DIR 'sdk.zip'; New-Item -ItemType Directory -Force -Path $env:SDK_DOWNLOAD_DIR ^| Out-Null; Invoke-WebRequest -Uri $uri -OutFile $zip; Expand-Archive -LiteralPath $zip -DestinationPath $env:SDK_DOWNLOAD_DIR -Force"
  if errorlevel 1 (
    echo ERROR: SDK download/extraction failed. An Autodesk sign-in may be required.
    exit /b 1
  )
  if exist "%SDK_DOWNLOAD_DIR%\maxsdk\include" set "MAXSDK2027=%SDK_DOWNLOAD_DIR%\maxsdk"
  if exist "%SDK_DOWNLOAD_DIR%\include" set "MAXSDK2027=%SDK_DOWNLOAD_DIR%"
  for /d /r "%SDK_DOWNLOAD_DIR%" %%D in (maxsdk) do if exist "%%D\include" set "MAXSDK2027=%%D"
)
if defined MAXSDK2027 if exist "%MAXSDK2027%\include" goto :sdk_found
echo.
echo ERROR: Autodesk 3ds Max 2027 SDK not found.
echo Official SDK download: %SDK_PAGE%
echo Download the 2027 SDK and extract it to deps\maxsdk2027\maxsdk,
echo or set MAXSDK2027 to the extracted SDK folder containing 'include'.
echo For a direct downloadable ZIP, set MAXSDK2027_URL and rerun build.bat.
start "" "%SDK_PAGE%"
exit /b 1
:sdk_found
echo Autodesk SDK: "%MAXSDK2027%"
exit /b 0

:restore_release
rem Keep Windows Release and ReleaseStatic configurations; avoid /MT vs /MD.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\ensure-premake-release.ps1" "%~1"
if errorlevel 1 (
  echo ERROR: Could not enable the Release configuration in "%~1".
  exit /b 1
)
exit /b 0

:build_library
rem Premake solution configurations are often mapped to different vcxproj
rem configuration/platform names. Build the exact solution configuration.
set "LIB_PROJECT=build\%~1.vcxproj"
set "LIB_SOLUTION=build\%~1.sln"
if not exist "%LIB_PROJECT%" (
  echo ERROR: Expected project "%LIB_PROJECT%" was not generated.
  exit /b 1
)
if not exist "%LIB_SOLUTION%" (
  echo ERROR: Expected solution "%LIB_SOLUTION%" was not generated.
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\build-dependency.ps1" -Solution "%CD%\%LIB_SOLUTION%" -ProjectName "%~1" -MSBuildPath "%MSBUILD%"
if errorlevel 1 exit /b 1
if not exist "lib\win-amd64-null\Release\%~2" (
  echo ERROR: Expected library not found: lib\win-amd64-null\Release\%~2
  echo Inspect the dependency's .vcxproj OutDir/TargetPath settings.
  exit /b 1
)
exit /b 0

:fail
if exist "%VSRESULT%" del /q "%VSRESULT%" >nul 2>&1
echo.
echo BUILD FAILED. Review the error above.
pause
exit /b 1
