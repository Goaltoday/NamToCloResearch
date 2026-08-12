param(
    [Parameter(Mandatory=$true)]
    [string]$AmperoDir,
    [string]$Destination
)

if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $PSScriptRoot "..\runtime\ampero"
}

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$dllCandidates = @(
    (Join-Path $AmperoDir "assets\HTUSBTools.dll"),
    (Join-Path $AmperoDir "HTUSBTools.dll")
)
$wavCandidates = @(
    (Join-Path $AmperoDir "data\flutter_assets\assets\wavs\nam_input_wav.wav"),
    (Join-Path $AmperoDir "assets\wavs\nam_input_wav.wav"),
    (Join-Path $AmperoDir "nam_input_wav.wav")
)

$dll = $dllCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$wav = $wavCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $dll) { throw "HTUSBTools.dll was not found under $AmperoDir" }
if (-not $wav) { throw "nam_input_wav.wav was not found under $AmperoDir" }

Copy-Item $dll (Join-Path $Destination "HTUSBTools.dll") -Force
Copy-Item $wav (Join-Path $Destination "nam_input_wav.wav") -Force
Write-Host "Runtime prepared at: $Destination"
