<#
.SYNOPSIS
  VR smoke test on the mock backend: runs the engine with VR enabled (mirror = both eyes side
  by side), takes a screenshot and checks it is a stereo pair; also checks the console log.

.DESCRIPTION
  Scenarios (run-smoke.ps1 does the actual run):
    a  map demo1                       -> check_shot.py --stereo
    b  map demo1, changelevel demo2    -> check_shot.py --stereo
    c  no map (console only)           -> check_shot.py (not flat): the console is drawn into the 2D
       panel; without the panel the eyes would be a uniform clear colour.

  check_shot --stereo alone cannot prove VR ran, so every run also uses -condebug and the
  console log must contain `VR: started (mock)` and no `VR:` error line (`VR: recentered` is allowed;
  scenario a requires it: the auto-recenter on the first rendered frame). The log is written to
  <basedir>\debug_h2.log (userdir == basedir on Windows; run-smoke also uses GameDir as the
  working directory), deleted before each run and removed afterwards.

  The VR cvars are archived (vr_enabled, vr_backend, vr_mirror), so <GameDir>\data1\config.cfg
  is backed up before each run and restored afterwards; if there was none, the one the run
  wrote is deleted. No VR state leaks into later runs.

.EXAMPLE
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Exe build/win64/Debug/hxn2vr.exe
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Exe build/win64/Debug/hxn2vr.exe -Scenarios a
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Scenarios a,b -ExtraArgs "+vr_mock_motion 1"
#>
[CmdletBinding()]
param(
    [string]$Exe = 'build/win64/Debug/hxn2vr.exe',
    [string[]]$Scenarios = @('a', 'b', 'c'),
    [string]$GameDir = 'build/game',
    [switch]$IncludePending,
    [int]$TimeoutSec = 180,
    [string[]]$ExtraArgs = @()   # appended to the VR engine arguments, e.g. +vr_mock_motion 1
)

$ErrorActionPreference = 'Stop'
# `-File ... -Scenarios a,b` arrives as the single string "a,b"
$Scenarios = @($Scenarios | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
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
$vrArgs = @('-condebug', '+vr_backend', 'mock', '+vr_mirror', '2', '+vr_enabled', '1') + @($ExtraArgs | ForEach-Object { $_ -split ' ' } | Where-Object { $_ })

$all = [ordered]@{
    a = @{ Commands = @('map demo1'); Stereo = $true; Pending = $false; Recenter = $true }   # auto-recenter must have run
    b = @{ Commands = @('map demo1', 'changelevel demo2'); Stereo = $true; Pending = $false }
    c = @{ Commands = @('echo vr-mock-smoke: console only'); Stereo = $false; Pending = $false }   # 2D panel with no world
}

function Test-VrLog([string]$path, [bool]$requireRecenter) {
    if (-not (Test-Path -LiteralPath $path)) { return "no console log at $path" }
    # case-sensitive: vr_debug_ticks prints lowercase "vr:" statistics lines
    $vrLines = @(Get-Content -LiteralPath $path | Where-Object { $_ -cmatch '^VR:' })
    if (-not ($vrLines -contains 'VR: started (mock)')) { return "log has no 'VR: started (mock)' line" }
    if ($requireRecenter -and -not ($vrLines -ccontains 'VR: recentered')) { return "log has no 'VR: recentered' line (auto-recenter did not run)" }
    # Expected VR: lines are the start/stop/recenter notices; anything else is an error report.
    $bad = @($vrLines | Where-Object { $_ -cne 'VR: started (mock)' -and $_ -cne 'VR: stopped' -and $_ -cne 'VR: recentered' })
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

    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $log) { throw "cannot remove stale $log (engine still running?)" }
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
        if (-not $why) { $why = Test-VrLog $log ([bool]$sc.Recenter) }
        if (-not $why) {
            $shotArgs = @('-I', $checkShot, $shot)
            if ($sc.Stereo) { $shotArgs += '--stereo' }
            & py @shotArgs
            if ($LASTEXITCODE -ne 0) { $why = "check_shot failed ($LASTEXITCODE)" }
        }
    }
    finally {
        # Clean-up must not abort the remaining scenarios (e.g. a log still locked by a killed engine).
        try {
            if ($null -ne $cfgBackup) { [System.IO.File]::WriteAllBytes($config, $cfgBackup) }
            elseif (Test-Path -LiteralPath $config) { Remove-Item -LiteralPath $config -Force }   # written by this run
        }
        catch { Write-Warning "vr-mock-smoke: could not restore ${config}: $_" }
        Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
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
