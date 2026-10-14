<#
.SYNOPSIS
  Builds a clean dev game directory from a Steam Hexen II install (read-only on the source).

.DESCRIPTION
  Copies only the top-level files of <SteamDir>\data1 (pak0.pak, pak1.pak, PROGS.DAT,
  PROGS2.DAT, Strings.txt, Hexen.rc, ...) into <OutDir>\data1. Sub-folders (s0..s11,
  glhexen, ...) and the user-specific Config.cfg / Autoexec.cfg are NOT copied, so the
  dev engine always starts with default settings. Files whose size and timestamp already
  match are skipped, so re-running is idempotent and cheap.
#>
[CmdletBinding()]
param(
    [string]$SteamDir = 'D:\Program Files (x86)\Steam\steamapps\common\Hexen 2',
    [string]$OutDir   = 'build/game'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $repoRoot $OutDir }

$src = Join-Path $SteamDir 'data1'
$dst = Join-Path $OutDir 'data1'
$skip = @('Config.cfg', 'Autoexec.cfg')

if (-not (Test-Path -LiteralPath $src -PathType Container)) { throw "Source not found: $src" }
foreach ($need in 'pak0.pak', 'pak1.pak') {
    if (-not (Test-Path -LiteralPath (Join-Path $src $need))) { throw "Missing $need in $src" }
}

New-Item -ItemType Directory -Force -Path $dst | Out-Null

$copied = 0; $skipped = 0
Get-ChildItem -LiteralPath $src -File | Where-Object { $skip -notcontains $_.Name } | ForEach-Object {
    $target = Join-Path $dst $_.Name
    $same = $false
    if (Test-Path -LiteralPath $target) {
        $t = Get-Item -LiteralPath $target
        $same = ($t.Length -eq $_.Length) -and ($t.LastWriteTimeUtc -eq $_.LastWriteTimeUtc)
    }
    if ($same) { $skipped++ }
    else { Copy-Item -LiteralPath $_.FullName -Destination $target -Force; $copied++ }
}

Write-Host "setup-gamedir: $dst  (copied $copied, up to date $skipped)"
