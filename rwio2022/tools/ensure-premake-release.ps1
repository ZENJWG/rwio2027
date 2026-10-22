param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $PremakeFile
)
$ErrorActionPreference = 'Stop'

try {
    if (-not (Test-Path -LiteralPath $PremakeFile -PathType Leaf)) {
        throw "Missing Premake file: $PremakeFile"
    }

    $source = [System.IO.File]::ReadAllText($PremakeFile)
    $updated = $source

    # Upstream's Windows filter currently overrides the configuration list,
    # hiding the /MD Release configuration required by the plugin.
    $configPattern = '(?m)^([ \t]*)configurations\s*\{\s*"ReleaseStatic"\s*\}'
    $configMatches = [regex]::Matches($updated, $configPattern)
    if ($configMatches.Count -eq 1) {
        $updated = [regex]::Replace(
            $updated, $configPattern,
            '${1}configurations { "Release", "Debug", "ReleaseStatic" }')
        Write-Host "Enabled normal Release configuration: $PremakeFile"
    } elseif ($configMatches.Count -gt 1) {
        throw "Multiple ReleaseStatic-only configuration entries; upstream changed."
    } elseif ($updated -notmatch 'configurations\s*\{[^}]*"Release"') {
        throw "No normal Release configuration and no recognized patch location."
    }

    # librwgta still uses a Premake 4-era flag that Premake 5 rejects with:
    #   Error: invalid value 'StaticRuntime' for flags
    # Preserve ReleaseStatic's /MT behavior while leaving Release on /MD.
    # Don't remove the whole filter (that would silently alter other configs).
    $legacyPattern = '(?m)^([ \t]*)flags\s*\{\s*["'']StaticRuntime["'']\s*\}'
    $legacyMatches = [regex]::Matches($updated, $legacyPattern)
    if ($legacyMatches.Count -gt 1) {
        throw "Multiple legacy StaticRuntime flags; refusing ambiguous patch."
    }
    if ($legacyMatches.Count -eq 1) {
        $updated = [regex]::Replace(
            $updated, $legacyPattern,
            '${1}staticruntime "On"')
        Write-Host "Replaced unsupported StaticRuntime flag: $PremakeFile"
    }

    # Any remaining use inside flags would still break Premake project generation.
    if ($updated -match 'flags\s*\{[^}]*["'']StaticRuntime["'']') {
        throw "Unsupported StaticRuntime flag remains in Premake source."
    }

    # Upstream librwgta also uses flags { "WinMain" } for its WindowedApp
    # *tools*.  Current bundled Premake rejects it during project generation,
    # even though RWIO only builds the librwgta static-library target.
    # Use the supported entrypoint setting to preserve CRT initialization.
    # Unlike StaticRuntime, WinMain appears in several independent projects.
    $winMainPattern = '(?m)^([ \t]*)flags\s*\{\s*["'']WinMain["'']\s*\}'
    $winMainMatches = [regex]::Matches($updated, $winMainPattern)
    if ($winMainMatches.Count -gt 0) {
        $updated = [regex]::Replace(
            $updated, $winMainPattern,
            '${1}entrypoint "WinMainCRTStartup"')
        Write-Host ("Replaced {0} unsupported WinMain flags: {1}" -f
            $winMainMatches.Count, $PremakeFile)
    }

    # Fail early if a legacy flag survived (e.g., upstream reformatted it).
    # The two known unsupported flags are the only flags present in the
    # dependency Premake scripts at the time of this patch.
    if ($updated -match 'flags\s*\{[^}]*["'']WinMain["'']') {
        throw "Unsupported WinMain flag remains in Premake source."
    }

    if ($updated -ne $source) {
        [System.IO.File]::WriteAllText($PremakeFile, $updated)
    } else {
        Write-Host "Premake compatibility already applied: $PremakeFile"
    }
} catch {
    Write-Error $_
    exit 1
}
