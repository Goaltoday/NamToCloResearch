[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Nam,
    [string]$OutputDir = ".\gp200-matrix",
    [string]$Exe = ".\NamToClo.exe"
)
$ErrorActionPreference = 'Stop'
$Nam = (Resolve-Path $Nam).Path
$Exe = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force $OutputDir | Out-Null
$cases = @(
    @{Name='normal';   Args=@()},
    @{Name='size';     Args=@('--gp200-size')},
    @{Name='rate';     Args=@('--gp200-rate')},
    @{Name='combined'; Args=@('--gp200-combined')}
)
$rows = @()
foreach ($c in $cases) {
    $out = Join-Path $OutputDir ($c.Name + '.clo')
    $log = Join-Path $OutputDir ($c.Name + '.log.txt')
    $args = @($Nam, $out, '--mode','data','--keep-temp','--verbose') + $c.Args
    & $Exe @args 2>&1 | Tee-Object -FilePath $log
    $exit = $LASTEXITCODE
    if (Test-Path $out) {
        $bytes = [IO.File]::ReadAllBytes($out)
        function U32([int]$off) { [BitConverter]::ToUInt32($bytes,$off) }
        $last = -1
        for ($i=$bytes.Length-1; $i -ge 0; $i--) { if ($bytes[$i] -ne 0) { $last=$i; break } }
        $rows += [pscustomobject]@{
            Case=$c.Name; ExitCode=$exit; File=[IO.Path]::GetFileName($out); PhysicalSize=$bytes.Length;
            Magic=[Text.Encoding]::ASCII.GetString($bytes,0,4);
            DeclaredSizeHex=('0x{0:X}' -f (U32 4)); PayloadSizeHex=('0x{0:X}' -f (U32 0x14));
            ModelFieldHex=('0x{0:X}' -f (U32 0x84)); LastNonZeroHex=('0x{0:X}' -f $last)
        }
    } else {
        $rows += [pscustomobject]@{Case=$c.Name;ExitCode=$exit;File='';PhysicalSize=0;Magic='';DeclaredSizeHex='';PayloadSizeHex='';ModelFieldHex='';LastNonZeroHex=''}
    }
}
$csv = Join-Path $OutputDir 'matrix-summary.csv'
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 $csv
$rows | Format-Table -AutoSize
Write-Host "Summary: $csv"
