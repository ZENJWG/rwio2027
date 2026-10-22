param(
    [Parameter(Mandatory=$true)][string]$VsWhere,
    [Parameter(Mandatory=$true)][string]$ResultFile
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $VsWhere)) { throw "vswhere.exe missing at $VsWhere" }
$installs = @(& $VsWhere -all -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath |
             Where-Object { $_ -and (Test-Path -LiteralPath $_) })
if (-not $installs.Count) { throw "No Visual Studio C++ workloads detected; install Desktop development with C++." }
foreach($root in $installs) {
    $platformTools = @(Get-ChildItem -Path (Join-Path $root 'MSBuild\Microsoft\VC') -Filter Toolset.props -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '[\\/]PlatformToolsets[\\/]v141[\\/]Toolset\.props$' })
    if (-not $platformTools.Count) { continue }
    $buildPaths = @(
        (Join-Path $root 'MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path $root 'MSBuild\Current\Bin\amd64\MSBuild.exe'),
        (Join-Path $root 'MSBuild\15.0\Bin\MSBuild.exe')
    )
    foreach($build in $buildPaths) {
        if (Test-Path -LiteralPath $build) {
            $action = if ($root -match '[\\/]2017[\\/]') { 'vs2017' } else { 'vs2022' }
            $lines = @($root, $build, $action)
            [IO.File]::WriteAllLines($ResultFile, $lines)
            Write-Host "Found v141 toolset: $root"
            Write-Host "Project generator: $action"
            exit 0
        }
    }
}
Write-Host "ERROR: v141 toolset not found in installed Visual Studio instances." -ForegroundColor Red
Write-Host "3ds Max 2022 requires MSVC v141 (VS 2017) and SDK 10.0.17134.0."
Write-Host "Install the v141 optional component (if offered) via Visual Studio Installer,"
Write-Host "or install an older Visual Studio edition side-by-side that supports v141."
exit 1
