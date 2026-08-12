[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Nam,
    [string]$AmperoDir,
    [string]$SonicakeDir,
    [string]$OutputDir,
    [string]$ReferenceClo = ""
)

$ErrorActionPreference = "Stop"
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$exe = Join-Path $root "NamToClo.exe"

# Resolve defaults in the script body. Do not use $PSScriptRoot in param() defaults.
if ([string]::IsNullOrWhiteSpace($AmperoDir)) {
    $AmperoDir = Join-Path $root "runtime\ampero"
}
if ([string]::IsNullOrWhiteSpace($SonicakeDir)) {
    $SonicakeDir = Join-Path $root "runtime\sonicake"
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $root "cross-runtime-results"
}

if (-not (Test-Path $exe)) { throw "NamToClo.exe not found at '$exe'. Build/copy the executable first." }
if (-not (Test-Path $AmperoDir)) { throw "Ampero runtime not found at '$AmperoDir'." }
if (-not (Test-Path $SonicakeDir)) { throw "Sonicake runtime not found at '$SonicakeDir'." }
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$ampero = Join-Path $OutputDir "ampero_raw_2048.clo"
$sonicake = Join-Path $OutputDir "sonicake_raw.clo"

Write-Host "=== Ampero namConvertCloData ==="
& $exe $Nam $ampero --provider ampero --ampero-dir $AmperoDir --mode data --verbose
if ($LASTEXITCODE -ne 0) { throw "Ampero conversion failed: $LASTEXITCODE" }

Write-Host "=== Sonicake namConvertCloData ==="
& $exe $Nam $sonicake --provider sonicake --sonicake-dir $SonicakeDir --mode data --verbose
if ($LASTEXITCODE -ne 0) { throw "Sonicake conversion failed: $LASTEXITCODE" }

Write-Host "=== Compare raw Ampero vs Sonicake ==="
& $exe --compare-gp200 $ampero $sonicake | Tee-Object -FilePath (Join-Path $OutputDir "compare_ampero_vs_sonicake.txt")

Write-Host "=== Sonicake cloConvertSampleRate matrix applied to Ampero raw ==="
& $exe --clo-rate-matrix $ampero (Join-Path $OutputDir "ampero_rate_matrix") --sonicake-dir $SonicakeDir --verbose
if ($LASTEXITCODE -ne 0) { throw "Ampero rate matrix failed: $LASTEXITCODE" }

Write-Host "=== Sonicake cloConvertSampleRate matrix applied to Sonicake raw ==="
& $exe --clo-rate-matrix $sonicake (Join-Path $OutputDir "sonicake_rate_matrix") --sonicake-dir $SonicakeDir --verbose
if ($LASTEXITCODE -ne 0) { throw "Sonicake rate matrix failed: $LASTEXITCODE" }

if ($ReferenceClo -and (Test-Path $ReferenceClo)) {
    Write-Host "=== Sonicake cloConvertSampleRate matrix applied to reference CLO ==="
    & $exe --clo-rate-matrix $ReferenceClo (Join-Path $OutputDir "reference_rate_matrix") --sonicake-dir $SonicakeDir --verbose
    if ($LASTEXITCODE -ne 0) { throw "Reference rate matrix failed: $LASTEXITCODE" }

    & $exe --compare-gp200 $ampero $ReferenceClo | Tee-Object -FilePath (Join-Path $OutputDir "compare_ampero_vs_reference.txt")
    & $exe --compare-gp200 $sonicake $ReferenceClo | Tee-Object -FilePath (Join-Path $OutputDir "compare_sonicake_vs_reference.txt")
}

Write-Host "Done. Upload the whole output directory (ZIP) for analysis."
