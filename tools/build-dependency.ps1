param(
    [Parameter(Mandatory = $true)][string] $Solution,
    [Parameter(Mandatory = $true)][string] $ProjectName,
    [Parameter(Mandatory = $true)][string] $MSBuildPath
)
$ErrorActionPreference = 'Stop'

try {
    if (-not (Test-Path -LiteralPath $Solution -PathType Leaf)) {
        throw "Solution not found: $Solution"
    }
    if (-not (Test-Path -LiteralPath $MSBuildPath -PathType Leaf)) {
        throw "MSBuild not found: $MSBuildPath"
    }

    $contents = [IO.File]::ReadAllText($Solution)
    $section = [regex]::Match(
        $contents,
        '(?s)GlobalSection\(SolutionConfigurationPlatforms\)\s*=\s*preSolution(?<data>.*?)EndGlobalSection'
    )
    if (-not $section.Success) {
        throw "The solution contains no SolutionConfigurationPlatforms section: $Solution"
    }

    $entries = @([regex]::Matches(
        $section.Groups['data'].Value,
        '(?m)^\s*(?<cfg>[^|\r\n=]+)\|(?<platform>[^\r\n=]+?)\s*=\s*[^\r\n]+\s*$'
    ) | ForEach-Object {
        [pscustomobject]@{
            Config = $_.Groups['cfg'].Value.Trim()
            Platform = $_.Groups['platform'].Value.Trim()
        }
    })

    Write-Host "Available solution configurations:"
    foreach ($item in $entries) {
        Write-Host ("  {0}|{1}" -f $item.Config, $item.Platform)
    }

    # The RWIO plugin needs the x64 null backend and a /MD Release build.
    # Do not choose ReleaseStatic (which may use /MT), d3d9 or gl3.
    $selected = @($entries | Where-Object {
        $_.Config -eq 'Release' -and $_.Platform -eq 'win-amd64-null'
    } | Select-Object -First 1)
    if ($selected.Count -eq 0) {
        throw ("Cannot find Release|win-amd64-null in {0}. " +
            "See listed solution configurations above. The upstream Premake patch " +
            "or project generation must be corrected; no unsafe fallback was chosen." -f $Solution)
    }

    # Confirm the solution actually contains the intended project.
    $escaped = [regex]::Escape($ProjectName)
    $project = [regex]::Match(
        $contents,
        '(?m)^Project\("[^"]+"\)\s*=\s*"' + $escaped +
        '",\s*"[^"]+",\s*"(?<guid>\{[0-9A-Fa-f-]+\})"'
    )
    if (-not $project.Success) {
        throw "Project $ProjectName is missing from solution: $Solution"
    }

    $configuration = $selected[0].Config
    $platform = $selected[0].Platform
    $guid = [regex]::Escape($project.Groups['guid'].Value)
    $mapping = [regex]::Match(
        $contents,
        '(?m)^\s*' + $guid + '\.' +
        [regex]::Escape("$configuration|$platform") +
        '\.ActiveCfg\s*=\s*(?<target>[^\r\n]+)'
    )
    if (-not $mapping.Success) {
        throw "Missing project configuration mapping for $ProjectName in $configuration|$platform"
    }
    Write-Host ("Project configuration mapping: {0} -> {1}" -f
        "$configuration|$platform", $mapping.Groups['target'].Value.Trim())

    Write-Host "Building $ProjectName through its generated solution ..."
    & $MSBuildPath $Solution '/m' "/t:$ProjectName" `
        "/p:Configuration=$configuration" "/p:Platform=$platform" `
        '/p:PlatformToolset=v145' '/verbosity:minimal' '/nologo'
    if ($LASTEXITCODE -ne 0) {
        throw "$ProjectName`: MSBuild exited with code $LASTEXITCODE"
    }
} catch {
    Write-Host "ERROR: $_" -ForegroundColor Red
    exit 1
}
