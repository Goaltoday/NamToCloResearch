[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$AmperoDir,

    [string]$Destination = (Join-Path $PSScriptRoot "..\runtime")
)

$ErrorActionPreference = "Stop"
$AmperoDir = (Resolve-Path $AmperoDir).Path
$Destination = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

$dllCandidates = @(
    (Join-Path $AmperoDir "assets\HTUSBTools.dll"),
    (Join-Path $AmperoDir "data\flutter_assets\assets\HTUSBTools.dll"),
    (Join-Path $AmperoDir "HTUSBTools.dll")
)

$wavCandidates = @(
    (Join-Path $AmperoDir "data\flutter_assets\assets\wavs\nam_input_wav.wav"),
    (Join-Path $AmperoDir "assets\wavs\nam_input_wav.wav"),
    (Join-Path $AmperoDir "wavs\nam_input_wav.wav"),
    (Join-Path $AmperoDir "nam_input_wav.wav")
)

$dll = $dllCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$wav = $wavCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $dll) { throw "HTUSBTools.dll not found below '$AmperoDir'." }
if (-not $wav) { throw "nam_input_wav.wav not found below '$AmperoDir'." }

Copy-Item -Force $dll (Join-Path $Destination "HTUSBTools.dll")
Copy-Item -Force $wav (Join-Path $Destination "nam_input_wav.wav")

$dllHash = (Get-FileHash -Algorithm SHA256 (Join-Path $Destination "HTUSBTools.dll")).Hash.ToLowerInvariant()
$wavHash = (Get-FileHash -Algorithm SHA256 (Join-Path $Destination "nam_input_wav.wav")).Hash.ToLowerInvariant()

Write-Host "Runtime copied to: $Destination"
Write-Host "HTUSBTools.dll SHA-256 : $dllHash"
Write-Host "nam_input_wav SHA-256  : $wavHash"
Write-Host ""

$knownDll = "5ef8ac398ed0c00ca5d350ddfe8b94d8eefca58c5998c658bb50aecaca257626"
$knownWav = "9bb6c1b136dfbeb7538a6060499d98c89342b76ec568b76836e36ab98b29aa1a"

if ($dllHash -eq $knownDll) {
    Write-Host "DLL matches the Ampero II package analyzed for this research branch."
} else {
    Write-Warning "DLL hash differs from the analyzed package. It may be another legitimate Ampero version; document the hash before testing."
}

if ($wavHash -eq $knownWav) {
    Write-Host "Stimulus matches the reference stimulus also observed in the Valeton GP-200 research."
} else {
    Write-Warning "Stimulus hash differs from the analyzed reference. Keep this fact with your test results."
}

Write-Host ""
Write-Host "Next: run the compiled executable with --check-runtime."
