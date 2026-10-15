<#
.SYNOPSIS
  VR smoke test on the mock backend: runs the engine with VR enabled (mirror = both eyes side
  by side), takes a screenshot and checks it is a stereo pair; also checks the console log.

.DESCRIPTION
  Scenarios (run-smoke.ps1 does the actual run):
    a  map demo1                       -> check_shot.py --stereo
    b  map demo1, changelevel demo2    -> check_shot.py --stereo
    c  no map (console only)           -> check_shot.py (not flat). PENDING until Task 1.8 (2D
       panel): without the panel the eyes are a uniform clear colour, so this scenario is
       expected to FAIL now; it only runs with -IncludePending.

  check_shot --stereo alone cannot prove VR ran, so every run also uses -condebug and the
  console log must contain `VR: started (mock)` and no `VR:` error line. The log is written to
  <basedir>\debug_h2.log (userdir == basedir on Windows; run-smoke also uses GameDir as the
  working directory), deleted before each run and removed afterwards.

  The VR cvars are archived (vr_enabled, vr_backend, vr_mirror), so <GameDir>\data1\config.cfg
  is backed up before each run and restored afterwards: no VR state leaks into later runs.

.EXAMPLE
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Exe build/win64/Debug/hxn2vr.exe
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Exe build/win64/Debug/hxn2vr.exe -Scenarios a
#>
[CmdletBinding()]
param(
    [string]$Exe = 'build/win64/Debug/hxn2vr.exe',
    [string[]]$Scenarios = @('a', 'b', 'c'),
    [string]$GameDir = 'build/game',
    [switch]$IncludePending,
    [int]$TimeoutSec = 180
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Resolve-Abs([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    $c = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $p))
    if (Test-Path -LiteralPath $c) { return $c }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $p))
}
$Exe = Resolve-Abs $Exe
$GameDir = Resolve-Abs $GameDir
$runSmoke = Join-Path $PSScriptRoot 'run-smoke.ps1'
$checkShot = Join-Path $PSScriptRoot 'check_shot.py'
$shot = Join-Path $GameDir 'data1\shots\hexen00.tga'
$log = Join-Path $GameDir 'debug_h2.log'
$config = Join-Path $GameDir 'data1\config.cfg'

# Placed before +exec by run-smoke, so they run before the scripted commands.
$vrArgs = @('-condebug', '+vr_backend', 'mock', '+vr_mirror', '2', '+vr_enabled', '1')

$all = [ordered]@{
    a = @{ Commands = @('map demo1'); Stereo = $true; Pending = $false }
    b = @{ Commands = @('map demo1', 'changelevel demo2'); Stereo = $true; Pending = $false }
    c = @{ Commands = @('echo vr-mock-smoke: console only'); Stereo = $false; Pending = $true }   # until Task 1.8
}

function Test-VrLog([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return "no console log at $path" }
    $vrLines = @(Get-Content -LiteralPath $path | Where-Object { $_ -match '^VR:' })
    if (-not ($vrLines -contains 'VR: started (mock)')) { return "log has no 'VR: started (mock)' line" }
    # Expected VR: lines are the start/stop notices; anything else is an error report.
    $bad = @($vrLines | Where-Object { $_ -ne 'VR: started (mock)' -and $_ -ne 'VR: stopped' })
    if ($bad.Count) { return "log has VR error line(s): $($bad -join ' | ')" }
    return $null
}

$failed = @()
$results = @()
foreach ($s in $Scenarios) {
    if (-not $all.Contains($s)) { throw "unknown scenario '$s' (a, b, c)" }
    $sc = $all[$s]
    if ($sc.Pending -and -not $IncludePending) {
        Write-Host "vr-mock-smoke ($s): PENDING (skipped; -IncludePending to run)"
        $results += "${s}: pending"
        continue
    }
    Write-Host "vr-mock-smoke ($s): $($sc.Commands -join '; ')"

    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
    $cfgBackup = $null
    if (Test-Path -LiteralPath $config) {
        $cfgBackup = [System.IO.File]::ReadAllBytes($config)
    }
    $why = $null
    try {
        try {
            & $runSmoke -Exe $Exe -Name "vr_$s" -Commands $sc.Commands -ExtraArgs $vrArgs `
                -GameDir $GameDir -TimeoutSec $TimeoutSec
            if ($LASTEXITCODE -ne 0) { $why = "run-smoke failed ($LASTEXITCODE)" }
        }
        catch { $why = "run-smoke error: $_" }
        if (-not $why) { $why = Test-VrLog $log }
        if (-not $why) {
            $shotArgs = @('-I', $checkShot, $shot)
            if ($sc.Stereo) { $shotArgs += '--stereo' }
            & py @shotArgs
            if ($LASTEXITCODE -ne 0) { $why = "check_shot failed ($LASTEXITCODE)" }
        }
    }
    finally {
        if ($null -ne $cfgBackup) { [System.IO.File]::WriteAllBytes($config, $cfgBackup) }
        if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force }
        Remove-Item -LiteralPath (Join-Path $GameDir "data1\smoke_vr_$s.cfg") -Force -ErrorAction SilentlyContinue
    }

    if ($why) {
        $tag = if ($sc.Pending) { 'FAIL (pending, expected until Task 1.8)' } else { 'FAIL' }
        Write-Host "vr-mock-smoke ($s): $tag - $why"
        $results += "${s}: $tag"
        if (-not $sc.Pending) { $failed += $s }
    }
    else {
        Write-Host "vr-mock-smoke ($s): PASS"
        $results += "${s}: PASS"
    }
}

Write-Host "vr-mock-smoke: $($results -join ', ')"
if ($failed.Count) { exit 1 }
exit 0
