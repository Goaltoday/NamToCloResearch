[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Nam,

    [string]$Output,

    [string]$Exe,

    [ValidateSet("data", "file")]
    [string]$Mode = "data",

    [int]$Timeout = 180,

    [switch]$KeepTemp,
    [switch]$VerboseLog
)

$ErrorActionPreference = "Stop"
$Nam = (Resolve-Path $Nam).Path

if (-not $Exe) {
    $candidates = @(
        (Join-Path $PSScriptRoot "..\NamToClo.exe"),
        (Join-Path $PSScriptRoot "..\build\Release\NamToClo.exe")
    )
    $Exe = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Exe) {
        throw "NamToClo.exe not found. Pass -Exe explicitly or build the project first."
    }
}
$Exe = [System.IO.Path]::GetFullPath($Exe)

if (-not $Output) {
    $Output = [System.IO.Path]::ChangeExtension($Nam, ".ampero.clo")
}
$Output = [System.IO.Path]::GetFullPath($Output)

$argsList = @($Nam, $Output, "--mode", $Mode, "--timeout", "$Timeout")
if ($KeepTemp) { $argsList += "--keep-temp" }
if ($VerboseLog) { $argsList += "--verbose" }

Write-Host "Running: $Exe"
Write-Host "Mode:    $Mode"
& $Exe @argsList
exit $LASTEXITCODE
