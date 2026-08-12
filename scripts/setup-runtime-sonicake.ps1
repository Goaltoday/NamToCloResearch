[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SonicakeDir,

    [string]$Destination
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $PSScriptRoot "..\runtime\sonicake"
}

$SonicakeDir = (Resolve-Path $SonicakeDir).Path
$Destination = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

function Find-FirstExisting([string[]]$Candidates) {
    return $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}

$dll = Find-FirstExisting @(
    (Join-Path $SonicakeDir "assets\5868USB.dll"),
    (Join-Path $SonicakeDir "data\flutter_assets\assets\5868USB.dll"),
    (Join-Path $SonicakeDir "5868USB.dll")
)
$wav = Find-FirstExisting @(
    (Join-Path $SonicakeDir "data\flutter_assets\assets\wavs\nam_input_wav.wav"),
    (Join-Path $SonicakeDir "assets\wavs\nam_input_wav.wav"),
    (Join-Path $SonicakeDir "wavs\nam_input_wav.wav"),
    (Join-Path $SonicakeDir "nam_input_wav.wav")
)

if (-not $dll) { throw "5868USB.dll not found below '$SonicakeDir'." }
if (-not $wav) { throw "nam_input_wav.wav not found below '$SonicakeDir'." }

Copy-Item -Force $dll (Join-Path $Destination "5868USB.dll")
Copy-Item -Force $wav (Join-Path $Destination "nam_input_wav.wav")

# Copy local VC/MFC runtime DLLs when the Sonicake package ships them.
foreach ($name in @("mfc140.dll", "mfc140u.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")) {
    $candidate = Join-Path $SonicakeDir $name
    if (Test-Path $candidate) {
        Copy-Item -Force $candidate (Join-Path $Destination $name)
    }
}

$dllHash = (Get-FileHash -Algorithm SHA256 (Join-Path $Destination "5868USB.dll")).Hash.ToLowerInvariant()
$wavHash = (Get-FileHash -Algorithm SHA256 (Join-Path $Destination "nam_input_wav.wav")).Hash.ToLowerInvariant()
Write-Host "Sonicake runtime copied to: $Destination"
Write-Host "5868USB.dll SHA-256      : $dllHash"
Write-Host "nam_input_wav SHA-256    : $wavHash"

$knownWav = "9bb6c1b136dfbeb7538a6060499d98c89342b76ec568b76836e36ab98b29aa1a"
if ($wavHash -eq $knownWav) {
    Write-Host "Stimulus matches the Ampero/Valeton reference stimulus."
} else {
    Write-Warning "Stimulus hash differs from the analyzed reference. Document the hash before testing."
}

Write-Host ""
Write-Host "Next: run NamToClo.exe --check-runtime --provider sonicake --verbose."
