@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
title RWIO 2022 - MSVC v141 build
echo ==== RWIO 2022 automated build (MSVC v141) ====
echo.
set "ROOT=%CD%"
set "PREMAKE=%ROOT%\premake5.exe"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
set "SDK_BASE=%ProgramFiles%\Autodesk\3ds Max 2022 SDK"
set "SDK_DOWNLOAD_DIR=%ROOT%\deps\maxsdk2022"
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
call :check_winsdk
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
"%PREMAKE%" %PREMAKE_ACTION%
if errorlevel 1 (popd & goto :fail)
call :build_library "librw" "rw.lib"
if errorlevel 1 (popd & goto :fail)
popd

echo.
echo [2/3] Building librwgta (64-bit, null backend, Release)...
pushd "%LIBRWGTA%" || goto :fail
"%PREMAKE%" %PREMAKE_ACTION%
if errorlevel 1 (popd & goto :fail)
call :build_library "librwgta" "rwgta.lib"
if errorlevel 1 (popd & goto :fail)
popd

echo.
echo [3/3] Generating and building RWIO...
"%PREMAKE%" %PREMAKE_ACTION%
if errorlevel 1 goto :fail
if not exist "build\rwio2022.vcxproj" (
  echo ERROR: Premake did not generate build\rwio2022.vcxproj
  goto :fail
)
"%MSBUILD%" "build\rwio2022.vcxproj" /m /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v141 /verbosity:minimal /nologo
if errorlevel 1 goto :fail
if not exist "%ROOT%\lib\amd64\Release\rwio2022.dli" (
  echo ERROR: Plugin output missing: lib\amd64\Release\rwio2022.dli
  goto :fail
)
echo.
echo BUILD SUCCEEDED
echo Plugin: "%ROOT%\lib\amd64\Release\rwio2022.dli"
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
  echo ERROR: Visual Studio discovery tool is missing: "%VSWHERE%".
  echo Install Visual Studio with Desktop development with C++ first.
  exit /b 1
)
set "VSSELECT=%TEMP%\rwio-2022-v141-%RANDOM%-%RANDOM%.txt"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\find-v141-msbuild.ps1" -VsWhere "%VSWHERE%" -ResultFile "%VSSELECT%"
if errorlevel 1 (
  if exist "%VSSELECT%" del /q "%VSSELECT%"
  if "%RWIO_NO_INSTALL%"=="1" exit /b 1
  echo Trying to add MSVC v141 through Visual Studio Installer...
  call :install_v141
  if errorlevel 1 exit /b 1
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\find-v141-msbuild.ps1" -VsWhere "%VSWHERE%" -ResultFile "%VSSELECT%"
  if errorlevel 1 exit /b 1
)
set "VSROOT="
set "MSBUILD="
set "PREMAKE_ACTION="
(
  set /p "VSROOT="
  set /p "MSBUILD="
  set /p "PREMAKE_ACTION="
) < "%VSSELECT%"
del /q "%VSSELECT%" >nul 2>&1
if not defined PREMAKE_ACTION exit /b 1
if not exist "%MSBUILD%" (
  echo ERROR: MSBuild could not be found: "%MSBUILD%"
  exit /b 1
)
echo Visual Studio: "%VSROOT%"
echo MSBuild: "%MSBUILD%"
echo Toolset: v141 ^(3ds Max 2022 requirement^)
exit /b 0


:install_v141
set "VS_ANY="
set "VSRESULT=%TEMP%\rwio-vswhere-%RANDOM%-%RANDOM%.txt"
"%VSWHERE%" -latest -products * -property installationPath > "%VSRESULT%"
for /f "usebackq delims=" %%V in ("%VSRESULT%") do set "VS_ANY=%%V"
del /q "%VSRESULT%" >nul 2>&1
if not defined VS_ANY (
  echo ERROR: A Visual Studio C++ installation is required to install v141.
  exit /b 1
)
set "VSSETUP=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\setup.exe"
if not exist "%VSSETUP%" (
  echo ERROR: Visual Studio Installer not found. Install the v141 optional component manually.
  exit /b 1
)
"%VSSETUP%" modify --installPath "%VS_ANY%" --add Microsoft.VisualStudio.Component.VC.v141.x86.x64 --passive --wait --norestart
if errorlevel 1 (
  echo ERROR: Could not install v141 into this Visual Studio edition.
  echo Try Visual Studio 2019/2022 with the v141 optional component side-by-side.
  exit /b 1
)
exit /b 0


:ensure_sdk
if defined MAXSDK2022 if exist "%MAXSDK2022%\include" goto :sdk_found
if exist "%SDK_BASE%\maxsdk\include" set "MAXSDK2022=%SDK_BASE%\maxsdk"
if defined MAXSDK2022 if exist "%MAXSDK2022%\include" goto :sdk_found
if exist "%SDK_BASE%\include" set "MAXSDK2022=%SDK_BASE%"
if defined MAXSDK2022 if exist "%MAXSDK2022%\include" goto :sdk_found
if exist "%ROOT%\deps\maxsdk2022\maxsdk\include" set "MAXSDK2022=%ROOT%\deps\maxsdk2022\maxsdk"
if defined MAXSDK2022 if exist "%MAXSDK2022%\include" goto :sdk_found
if exist "%ROOT%\deps\maxsdk2022\include" set "MAXSDK2022=%ROOT%\deps\maxsdk2022"
if defined MAXSDK2022 if exist "%MAXSDK2022%\include" goto :sdk_found

rem Find a locally extracted SDK with extra enclosing directories.
if exist "%SDK_DOWNLOAD_DIR%" (
  for /d /r "%SDK_DOWNLOAD_DIR%" %%D in (maxsdk) do if exist "%%D\include" set "MAXSDK2022=%%D"
)
if defined MAXSDK2022 if exist "%MAXSDK2022%\include" goto :sdk_found

rem Autodesk distributes this SDK from its official 3ds Max API page.
rem For access-controlled SDK downloads provide an authorized direct ZIP URL.
if defined MAXSDK2022_URL (
  echo SDK not found locally. Downloading from MAXSDK2022_URL...
  if not exist "%ROOT%\deps" mkdir "%ROOT%\deps"
  powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "$ErrorActionPreference='Stop'; $uri=$env:MAXSDK2022_URL; if (-not $uri.StartsWith('https://', [StringComparison]::OrdinalIgnoreCase)) { throw 'MAXSDK2022_URL must be an HTTPS download URL' }; $zip=Join-Path $env:SDK_DOWNLOAD_DIR 'sdk.zip'; New-Item -ItemType Directory -Force -Path $env:SDK_DOWNLOAD_DIR ^| Out-Null; Invoke-WebRequest -Uri $uri -OutFile $zip; Expand-Archive -LiteralPath $zip -DestinationPath $env:SDK_DOWNLOAD_DIR -Force"
  if errorlevel 1 (
    echo ERROR: SDK download/extraction failed. An Autodesk sign-in may be required.
    exit /b 1
  )
  if exist "%SDK_DOWNLOAD_DIR%\maxsdk\include" set "MAXSDK2022=%SDK_DOWNLOAD_DIR%\maxsdk"
  if exist "%SDK_DOWNLOAD_DIR%\include" set "MAXSDK2022=%SDK_DOWNLOAD_DIR%"
  for /d /r "%SDK_DOWNLOAD_DIR%" %%D in (maxsdk) do if exist "%%D\include" set "MAXSDK2022=%%D"
)
if defined MAXSDK2022 if exist "%MAXSDK2022%\include" goto :sdk_found
echo.
echo ERROR: Autodesk 3ds Max 2022 SDK not found.
echo Official SDK download: %SDK_PAGE%
echo Download the 2022 SDK and extract it to deps\maxsdk2022\maxsdk,
echo or set MAXSDK2022 to the extracted SDK folder containing 'include'.
echo For a direct downloadable ZIP, set MAXSDK2022_URL and rerun build.bat.
start "" "%SDK_PAGE%"
exit /b 1
:sdk_found
echo Autodesk SDK: "%MAXSDK2022%"
exit /b 0

:check_winsdk
if not defined RWIO_WINSDK_VERSION set "RWIO_WINSDK_VERSION=10.0.17134.0"
set "WINSDKDIR=%ProgramFiles(x86)%\Windows Kits\10\Include\%RWIO_WINSDK_VERSION%"
if not exist "%WINSDKDIR%\um\Windows.h" (
  echo ERROR: Windows SDK %RWIO_WINSDK_VERSION% not found.
  echo Autodesk specifies Windows SDK 10.0.17134.0 for 3ds Max 2022.
  echo Use Visual Studio Installer - Individual components - Windows 10 SDK,
  echo or set RWIO_WINSDK_VERSION to a locally installed SDK version.
  echo A different SDK version is not verified for ABI compatibility.
  exit /b 1
)
echo Windows SDK: %RWIO_WINSDK_VERSION%
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
echo.
echo BUILD FAILED. Review the error above.
pause
exit /b 1
