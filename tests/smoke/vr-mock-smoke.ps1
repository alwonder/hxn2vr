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
    d  map demo1, vid_restart          -> check_shot.py --stereo: VR stops before the GL context is deleted
       and starts again on the new one (log: started, stopped, started again, no error line).
    e  vr_backend openxr, map demo1    -> flat fallback: no headset session can start, so the log has a
       `VR:` failure line, vr_enabled is reset to 0 and the screenshot is mono (check_shot passes,
       check_shot --stereo rejects it). SKIPPED if an OpenXR session does start (headset connected).
    g  no map, mock controller only    -> quit from the controller: the startup main menu is closed first
       (menu_main; togglemenu), then Menu opens the main menu, stick up
       moves the cursor to QUIT, A opens the "quit?" box, A again quits. run-smoke -ExpectQuit: the
       engine must exit by itself with code 0 before the timeout (no screenshot); the log must also
       have `VR: default controller bindings set` (the VR keys are unbound first, so the defaults are
       set when VR starts). Short waits (WaitLines 3): a stick held for 0.45 s would auto-repeat
       (the glue counts a long frame as at most 0.1 s, so 3 frames stay below that at any frame rate).
    m  map demo1, mock controller only -> a modal yes/no dialog answered from the controller: Menu opens
       the main menu, A -> Single Player, A on New Mission opens "start a new game?" (SCR_ModalMessage,
       whose wait loop runs no commands), B pressed 1 s later by a delayed `vr_mock_hand ... 1.0`
       answers no (the VR frames and controller polling go on inside the loop), the game stays; then
       Menu, stick up, A, A quit as in g. -ExpectQuit, WaitLines 3: a dialog that the controller cannot
       answer hangs until the timeout; a wrong "yes" opens the class menu and the quit path fails.

    f  map demo1, mock controller only -> stick locomotion and snap turning: `vr_where` (A), the left (off-hand)
       stick pushed forward for ~2 s, `vr_where` (B), the right (main-hand) stick pushed to the right and
       released, `vr_where` (C). The three `VR: where ...` lines must show A -> B more than 32 units apart
       on the floor plane (the mock's left aim is the identity, so the move follows the body yaw) and a
       yaw change B -> C of -45 (315 mod 360, +-1: one snap turn to the right, once per push).

  check_shot --stereo alone cannot prove VR ran, so every run also uses -condebug and the
  console log must contain `VR: started (mock)` and no `VR:` error line (`VR: recentered` is allowed;
  scenario a requires it: the auto-recenter on the first rendered frame; `VR: default controller
  bindings set` is allowed too, scenario g requires it). The log is written to
  <basedir>\debug_h2.log (userdir == basedir on Windows; run-smoke also uses GameDir as the
  working directory), deleted before each run and removed afterwards.

  The VR cvars are archived (vr_enabled, vr_backend, vr_mirror), so <GameDir>\data1\config.cfg
  is backed up before each run and restored afterwards; if there was none, the one the run
  wrote is deleted. No VR state leaks into later runs.

.EXAMPLE
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Exe build/win64/Debug/hxn2vr.exe
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Exe build/win64/Debug/hxn2vr.exe -Scenarios a
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Scenarios a,b -ExtraArgs "+vr_mock_motion 1"
  powershell -File tests/smoke/vr-mock-smoke.ps1 -Exe build/win64/Release/hxn2vr.exe -ExtraArgs "+vr_debug_ticks 1" -KeepLogDir <dir>
    (Release exe; the kept logs have the per-second "vr: N frames, M server ticks" lines)
#>
[CmdletBinding()]
param(
    [string]$Exe = 'build/win64/Debug/hxn2vr.exe',
    [string[]]$Scenarios = @('a', 'b', 'c', 'd', 'e', 'f', 'g', 'm'),
    [string]$GameDir = 'build/game',
    [switch]$IncludePending,
    [int]$TimeoutSec = 180,
    [string[]]$ExtraArgs = @(),  # appended to the VR engine arguments, e.g. +vr_mock_motion 1
    [string]$KeepLogDir = ''     # if set, each scenario's console log is copied there as vr_<s>.log
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
function Get-VrArgs([string]$backend) {
    @('-condebug', '+vr_backend', $backend, '+vr_mirror', '2', '+vr_enabled', '1') + @($ExtraArgs | ForEach-Object { $_ -split ' ' } | Where-Object { $_ })
}

$all = [ordered]@{
    a = @{ Commands = @('map demo1'); Stereo = $true; Pending = $false; Recenter = $true }   # auto-recenter must have run
    b = @{ Commands = @('map demo1', 'changelevel demo2'); Stereo = $true; Pending = $false }
    c = @{ Commands = @('echo vr-mock-smoke: console only'); Stereo = $false; Pending = $false }   # 2D panel with no world
    # vid_restart deletes the GL context: VR must stop before, start again after (2 starts, 1 stop)
    d = @{ Commands = @('map demo1', 'vid_restart'); Stereo = $true; Pending = $false; Restarts = 1 }
    # no headset: the OpenXR session cannot start, the game falls back to the flat view
    e = @{ Commands = @('map demo1', 'vr_enabled', 'vr_status'); Backend = 'openxr'; Fallback = $true; Pending = $false }
    # stick locomotion and snap turn (see the header): the vr_where lines A, B, C are checked in Test-VrLog
    f = @{ Commands = @(
            'map demo1', ((@('wait') * 150) -join '; '),             # the level is in
            'menu_main; togglemenu',                                   # no menu, whatever the start did
            'vr_where',                                                # A
            ('vr_mock_hand left sy 1; ' + ((@('wait') * 150) -join '; ')),   # ~2 s forward ...
            'vr_mock_hand left sy 0',                                  # ... then stop
            ((@('wait') * 20) -join '; '),
            'vr_where',                                                # B
            ('vr_mock_hand right sx 1; ' + ((@('wait') * 20) -join '; ')),   # one snap turn to the right
            ('vr_mock_hand right sx 0; ' + ((@('wait') * 20) -join '; ')),
            'vr_where');                                               # C
        Stereo = $true; Where = $true; WaitLines = 3; Pending = $false }
    # quit from the controller in the main menu (no map). The first line runs before VR starts (same
    # command buffer pass as +vr_enabled 1); the mock commands run once it is up. Hexen.rc opens the
    # main menu at startup (menu_main): `menu_main; togglemenu` closes it whatever Hexen.rc did, so
    # the Menu button has to open it again (cursor still on SINGLE PLAYER).
    g = @{ Commands = @(
            'menu_main; togglemenu; unbind VR_TRIGGER; unbind VR_GRIP; unbind VR_A; unbind VR_B; unbind VR_STICK; unbind VR_OTRIGGER; unbind VR_OGRIP; unbind VR_X; unbind VR_Y; unbind VR_OSTICK',
            'vr_mock_hand left menu 1', 'vr_mock_hand left menu 0',   # Escape: the main menu opens
            'vr_mock_hand right sy 1', 'vr_mock_hand right sy 0',     # up arrow: SINGLE PLAYER -> QUIT
            'vr_mock_hand right a 1', 'vr_mock_hand right a 0',       # Enter: the "quit?" box
            'vr_mock_hand right a 1', 'vr_mock_hand right a 0');      # Enter: quit (VR only)
        ExpectQuit = $true; WaitLines = 3; Bindings = $true; Pending = $false }
    # a modal yes/no dialog in game, answered "no" from the controller, then quit (see the header)
    m = @{ Commands = @(
            'map demo1', ((@('wait') * 150) -join '; '),             # the level is in
            'menu_main; togglemenu',                                   # no menu, whatever the start did
            'vr_mock_hand left menu 1', 'vr_mock_hand left menu 0',   # Escape: the main menu
            'vr_mock_hand right a 1', 'vr_mock_hand right a 0',       # Enter: Single Player
            'vr_mock_hand right b 1 1.0; vr_mock_hand right b 0 1.3; vr_mock_hand right a 1',   # New Mission: dialog, B = no
            'vr_mock_hand right a 0',
            'vr_mock_hand left menu 1', 'vr_mock_hand left menu 0',   # Escape: back to the main menu
            'vr_mock_hand right sy 1', 'vr_mock_hand right sy 0',     # up: QUIT
            'vr_mock_hand right a 1', 'vr_mock_hand right a 0',       # the "quit?" box
            'vr_mock_hand right a 1', 'vr_mock_hand right a 0');      # quit
        ExpectQuit = $true; WaitLines = 3; Pending = $false }
}

# Returns $null if the log is right, 'SKIP: ...' if the scenario cannot run here, else the failure text.
function Test-VrLog([string]$path, $sc) {
    if (-not (Test-Path -LiteralPath $path)) { return "no console log at $path" }
    $lines = @(Get-Content -LiteralPath $path)
    # case-sensitive: vr_debug_ticks prints lowercase "vr:" statistics lines
    $vrLines = @($lines | Where-Object { $_ -cmatch '^VR:' })
    if ($sc.Fallback) {
        if ($vrLines -ccontains 'VR: started (openxr)') {
            return 'SKIP: an OpenXR session started (headset / runtime available), the no-headset fallback cannot be tested'
        }
        if ($vrLines.Count -eq 0) { return "log has no 'VR:' line describing the failure" }
        # a failure line: anything but the informational runtime line and the stop notices
        if (-not ($vrLines | Where-Object { $_ -cnotmatch '^VR: (OpenXR runtime |recentered|started)' -and $_ -cnotmatch '^VR: stopped' })) {
            return "log has VR: lines but none describes a failure: $($vrLines -join ' | ')"
        }
        if (-not ($lines -ccontains '"vr_enabled" is "0"')) { return 'vr_enabled was not reset to 0 after the failed start' }
        if (-not ($vrLines | Where-Object { $_ -cmatch '^VR: stopped \(vr_enabled 0' })) { return "vr_status did not report 'stopped (vr_enabled 0'" }
        return $null
    }
    $startedCount = @($vrLines | Where-Object { $_ -ceq 'VR: started (mock)' }).Count
    $stoppedCount = @($vrLines | Where-Object { $_ -ceq 'VR: stopped' }).Count
    if ($startedCount -lt 1) { return "log has no 'VR: started (mock)' line" }
    if ($sc.Restarts) {
        $wantStarts = 1 + [int]$sc.Restarts
        if ($startedCount -ne $wantStarts) { return "expected $wantStarts 'VR: started (mock)' lines (start + restart), found $startedCount" }
        if ($stoppedCount -ne [int]$sc.Restarts) { return "expected $($sc.Restarts) 'VR: stopped' line(s) (before the video restart), found $stoppedCount" }
    }
    if ($sc.Recenter -and -not ($vrLines -ccontains 'VR: recentered')) { return "log has no 'VR: recentered' line (auto-recenter did not run)" }
    if ($sc.Bindings -and -not ($vrLines -ccontains 'VR: default controller bindings set')) { return "log has no 'VR: default controller bindings set' line" }
    if ($sc.Where) {
        $re = '^VR: where origin (\S+) (\S+) (\S+) yaw (\S+) lean (\S+) (\S+) view (\S+) crouch (\d) held (\d+) crouchkey (\d)$'
        $w = @($vrLines | Where-Object { $_ -cmatch $re })
        if ($w.Count -ne 3) { return "expected 3 'VR: where' lines (A, B, C), found $($w.Count): $($w -join ' | ')" }
        $p = @()
        foreach ($l in $w) {
            [void]($l -cmatch $re)
            $p += ,@{ X = [double]::Parse($Matches[1], [cultureinfo]::InvariantCulture); Y = [double]::Parse($Matches[2], [cultureinfo]::InvariantCulture)
                      Yaw = [double]::Parse($Matches[4], [cultureinfo]::InvariantCulture); Held = [int]$Matches[9]; CrouchKey = [int]$Matches[10] }
        }
        $dist = [math]::Sqrt([math]::Pow($p[1].X - $p[0].X, 2) + [math]::Pow($p[1].Y - $p[0].Y, 2))
        Write-Host ("vr-mock-smoke: stick move A->B {0:N1} units, turn B->C {1:N1} deg" -f $dist, ((($p[2].Yaw - $p[1].Yaw) % 360 + 360) % 360))
        if ($dist -le 32) { return "the left stick moved the player only $dist units (A -> B), expected more than 32: $($w -join ' | ')" }
        $turn = (($p[2].Yaw - $p[1].Yaw) % 360 + 360) % 360
        if ([math]::Abs($turn - 315) -gt 1) { return "snap turn B -> C changed the yaw by $turn (mod 360), expected 315 +- 1: $($w -join ' | ')" }
        if ($p[2].Held -ne 0 -or $p[2].CrouchKey -ne 0) { return "buttons stuck after the sticks: $($w[2])" }
    }
    # Expected VR: lines are the start/stop/recenter/default-bindings notices (and vr_where output); anything else is an error report.
    $bad = @($vrLines | Where-Object { $_ -cne 'VR: started (mock)' -and $_ -cne 'VR: stopped' -and $_ -cne 'VR: recentered' -and
        $_ -cne 'VR: default controller bindings set' -and $_ -cnotmatch '^VR: where ' })
    if ($bad.Count) { return "log has VR error line(s): $($bad -join ' | ')" }
    return $null
}

$failed = @()
$results = @()
foreach ($s in $Scenarios) {
    if (-not $all.Contains($s)) { throw "unknown scenario '$s' (a, b, c, d, e, f, g, m)" }
    $sc = $all[$s]
    if ($sc.Pending -and -not $IncludePending) {
        Write-Host "vr-mock-smoke ($s): PENDING (skipped; -IncludePending to run)"
        $results += "${s}: pending"
        continue
    }
    Write-Host "vr-mock-smoke ($s): $($sc.Commands -join '; ')"
    $backend = if ($sc.Backend) { $sc.Backend } else { 'mock' }

    if (Test-Path -LiteralPath $log) { Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $log) { throw "cannot remove stale $log (engine still running?)" }
    $cfgBackup = $null
    if (Test-Path -LiteralPath $config) {
        $cfgBackup = [System.IO.File]::ReadAllBytes($config)
    }
    $why = $null
    $skip = $false
    try {
        try {
            $runArgs = @{ Exe = $Exe; Name = "vr_$s"; Commands = $sc.Commands; ExtraArgs = (Get-VrArgs $backend)
                GameDir = $GameDir; TimeoutSec = $TimeoutSec }
            if ($sc.WaitLines) { $runArgs.WaitLines = [int]$sc.WaitLines }
            if ($sc.ExpectQuit) { $runArgs.ExpectQuit = $true }
            & $runSmoke @runArgs
            if ($LASTEXITCODE -ne 0) { $why = "run-smoke failed ($LASTEXITCODE)" }
        }
        catch { $why = "run-smoke error: $_" }
        if (-not $why) {
            $why = Test-VrLog $log $sc
            if ($why -and $why.StartsWith('SKIP: ')) { $skip = $true; $why = $why.Substring(6) }
        }
        if (-not $why -and -not $sc.ExpectQuit) {   # ExpectQuit runs take no screenshot
            if ($sc.Fallback) {
                # the flat fallback shot: a real picture (not one colour) but not a stereo pair
                & py -I $checkShot $shot
                if ($LASTEXITCODE -ne 0) { $why = "check_shot rejected the flat fallback screenshot ($LASTEXITCODE)" }
                if (-not $why) {
                    & py -I $checkShot $shot --stereo
                    if ($LASTEXITCODE -eq 0) { $why = 'check_shot --stereo accepted the screenshot: the fallback is not mono' }
                    else { Write-Host 'vr-mock-smoke: (the --stereo rejection above is expected: the shot is mono)' }
                }
            }
            else {
                $shotArgs = @('-I', $checkShot, $shot)
                if ($sc.Stereo) { $shotArgs += '--stereo' }
                & py @shotArgs
                if ($LASTEXITCODE -ne 0) { $why = "check_shot failed ($LASTEXITCODE)" }
            }
        }
    }
    finally {
        # Clean-up must not abort the remaining scenarios (e.g. a log still locked by a killed engine).
        try {
            if ($null -ne $cfgBackup) { [System.IO.File]::WriteAllBytes($config, $cfgBackup) }
            elseif (Test-Path -LiteralPath $config) { Remove-Item -LiteralPath $config -Force }   # written by this run
        }
        catch { Write-Warning "vr-mock-smoke: could not restore ${config}: $_" }
        if ($KeepLogDir -and (Test-Path -LiteralPath $log)) {
            try {
                New-Item -ItemType Directory -Force -Path $KeepLogDir | Out-Null
                Copy-Item -LiteralPath $log -Destination (Join-Path $KeepLogDir "vr_$s.log") -Force
            }
            catch { Write-Warning "vr-mock-smoke: could not keep the log: $_" }
        }
        Remove-Item -LiteralPath $log -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $GameDir "data1\smoke_vr_$s.cfg") -Force -ErrorAction SilentlyContinue
    }

    if ($skip) {
        Write-Host "vr-mock-smoke ($s): SKIPPED - $why"
        $results += "${s}: SKIPPED"
    }
    elseif ($why) {
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
