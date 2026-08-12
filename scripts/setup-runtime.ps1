param(
    [Parameter(Mandatory=$true)]
    [string]$AmperoDir,

    [string]$Destination
)

$ErrorActionPreference = 'Stop'

# Do not use $PSScriptRoot as a param() default: resolve it after param binding.
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $PSScriptRoot '..\runtime\ampero'
}

$dest = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $dest | Out-Null

$dllCandidates = @(
    (Join-Path $AmperoDir 'assets\HTUSBTools.dll'),
    (Join-Path $AmperoDir 'data\flutter_assets\assets\HTUSBTools.dll'),
    (Join-Path $AmperoDir 'HTUSBTools.dll')
)
$wavCandidates = @(
    (Join-Path $AmperoDir 'data\flutter_assets\assets\wavs\nam_input_wav.wav'),
    (Join-Path $AmperoDir 'assets\wavs\nam_input_wav.wav'),
    (Join-Path $AmperoDir 'wavs\nam_input_wav.wav'),
    (Join-Path $AmperoDir 'nam_input_wav.wav')
)

$dll = $dllCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
$wav = $wavCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $dll) { throw 'HTUSBTools.dll not found under the supplied Ampero directory.' }
if (-not $wav) { throw 'nam_input_wav.wav not found under the supplied Ampero directory.' }

Copy-Item -Force $dll (Join-Path $dest 'HTUSBTools.dll')
Copy-Item -Force $wav (Join-Path $dest 'nam_input_wav.wav')

Write-Host "Ampero runtime prepared at: $dest"
