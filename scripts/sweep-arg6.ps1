[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Nam,

    [int]$Start = 0,
    [int]$End = 15,

    [string]$OutputDir,
    [string]$Exe,
    [int]$Timeout = 180,
    [switch]$KeepTemp,
    [switch]$VerboseLog
)

$ErrorActionPreference = "Stop"

if ($Start -lt 0 -or $End -lt $Start) {
    throw "Invalid range. Require 0 <= Start <= End."
}

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

if (-not $OutputDir) {
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($Nam)
    $OutputDir = Join-Path (Get-Location) ("arg6-sweep-" + $baseName)
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

function Read-U32LE([byte[]]$Bytes, [int]$Offset) {
    if ($Bytes.Length -lt ($Offset + 4)) { return $null }
    return [BitConverter]::ToUInt32($Bytes, $Offset)
}

function Get-LastNonZero([byte[]]$Bytes) {
    for ($i = $Bytes.Length - 1; $i -ge 0; $i--) {
        if ($Bytes[$i] -ne 0) { return $i }
    }
    return -1
}

$rows = @()

for ($value = $Start; $value -le $End; $value++) {
    $clo = Join-Path $OutputDir ("arg6_{0:D2}.clo" -f $value)
    $log = Join-Path $OutputDir ("arg6_{0:D2}.log.txt" -f $value)

    Write-Host ""
    Write-Host "===== arg6=$value ====="

    $argsList = @(
        $Nam,
        $clo,
        "--mode", "data",
        "--arg6", "$value",
        "--timeout", "$Timeout"
    )
    if ($KeepTemp) { $argsList += "--keep-temp" }
    if ($VerboseLog) { $argsList += "--verbose" }

    & $Exe @argsList 2>&1 | Tee-Object -FilePath $log
    $exitCode = $LASTEXITCODE

    $magic = ""
    $physicalSize = 0
    $declaredSize = $null
    $payloadSize = $null
    $modelField = $null
    $lastNonZero = $null

    if (Test-Path $clo) {
        [byte[]]$bytes = [System.IO.File]::ReadAllBytes($clo)
        $physicalSize = $bytes.Length
        if ($bytes.Length -ge 4) {
            $magic = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 4)
        }
        $declaredSize = Read-U32LE $bytes 0x04
        $payloadSize = Read-U32LE $bytes 0x14
        $modelField = Read-U32LE $bytes 0x84
        $lastNonZero = Get-LastNonZero $bytes
    }

    $isGp200Shape = ($magic -eq "VTSI" -and
                     $declaredSize -eq 0x1288 -and
                     $payloadSize -eq 0x1200 -and
                     $modelField -eq 0x0400)

    $row = [PSCustomObject]@{
        Arg6 = $value
        ExitCode = $exitCode
        File = [System.IO.Path]::GetFileName($clo)
        Magic = $magic
        PhysicalSize = $physicalSize
        DeclaredSizeHex = if ($null -ne $declaredSize) { "0x{0:X}" -f $declaredSize } else { "" }
        PayloadSizeHex = if ($null -ne $payloadSize) { "0x{0:X}" -f $payloadSize } else { "" }
        ModelFieldHex = if ($null -ne $modelField) { "0x{0:X}" -f $modelField } else { "" }
        LastNonZeroHex = if ($null -ne $lastNonZero -and $lastNonZero -ge 0) { "0x{0:X}" -f $lastNonZero } else { "" }
        GP200Shape = $isGp200Shape
    }
    $rows += $row

    Write-Host ("Summary arg6={0}: exit={1}, magic={2}, physical={3}, declared={4}, payload={5}, model={6}, last={7}, GP200={8}" -f `
        $row.Arg6, $row.ExitCode, $row.Magic, $row.PhysicalSize, $row.DeclaredSizeHex,
        $row.PayloadSizeHex, $row.ModelFieldHex, $row.LastNonZeroHex, $row.GP200Shape)

    if ($isGp200Shape) {
        Write-Host "*** GP-200-shaped VTSI found at arg6=$value ***"
    }
}

$csv = Join-Path $OutputDir "sweep-summary.csv"
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv

Write-Host ""
Write-Host "Sweep complete."
Write-Host "Results: $OutputDir"
Write-Host "Summary: $csv"
Write-Host ""
$rows | Format-Table -AutoSize
