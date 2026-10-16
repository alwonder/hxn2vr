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
       yaw change B -> C of -45 (315 mod 360, +-1: one snap turn to the right, once per push). Between
       B and C: `vr_calibrate` and the main trigger pressed twice (eye height, then arm): the log must have
       `VR: calibration done: eye height 1.65 m, arm 0.53 m` (the mock head is at 1.65 m, its right hand 0.535 m
       from the shoulder), and C must show no held key (the calibration took the trigger presses).
       Then the room-scale lean: C must show a lean under 0.5 units (the calibration recentered), the mock
       head is moved 0.2 m forward over 1 s (`vr_mock_head 0 0 -0.2 1`) and `vr_where` (D) must show a
       horizontal lean of 7.27 +- 0.5 units (0.2 m x 36.36 units/m, inside the 9.09-unit radius) pointing
       along the body yaw of D (+-5 deg: the lean is in world axes).
    h  map demo1, mock controller only -> room-scale walking on the server (clc_hxvr): `vr_where` (A), the mock
       head walks 1 m forward over 2 s (`vr_mock_head 0 0 -1 2`), `vr_where` (B). The head leans up to the
       0.25 m radius (9.09 units), the remaining 0.75 m (27 units) goes to the local server in the VR move
       block and the server walks the player with collision: A -> B must be at least 20 units apart on the
       floor plane, and the lean at B must be at the radius (9.09 +- 0.5 units). 2 s, not 1: the Debug exe
       can run the mock at ~6 frames/s, and one frame of more than 0.5 s at 1 m/s would be a head step over
       0.5 m, which the room-scale code drops as a tracking jump. 300 waits: >= 2 s also at the mock's 90 Hz.
       Part 2, the crouch: the mock head goes down 0.8 m over 2 s (1.65 -> 0.85 m, below the 1.0725 m crouch
       height) and `vr_where` (C) must show `crouch 1` and the game's view height under 30 (the progs lower it
       to 24); back up over 2 s, `vr_where` (D): `crouch 0`, view >= 45 (50). Then B (+crouch) pressed in the
       game, `vr_where` (E): crouchkey 1; Menu opens the main menu, B is released there, Menu closes it again,
       `vr_where` (F): held 0, crouchkey 0 (the release went out with the key it was pressed with, so
       -crouch ran although the menu was up). 200 waits per head move: >= 2 s at 90 Hz; TimeoutSec 300
       because the Debug exe can run this scenario at ~6 frames/s.
       The button part runs from delayed `vr_mock_hand ... <delay>` changes after the script has ended: a
       key's binding is appended to the command buffer (Cbuf_AddText), so while script lines are still
       queued, +crouch / -crouch would only run after the whole script and crouchkey could never be 1.
       E and F come from the off hand's Y, bound to +hxw (`-hxw` = vr_where: on the release, so Y itself is
       not counted as held), and the end (screenshot, disconnect, quit) from its stick click (`hxend`):
       -ExpectQuit, with the screenshot still checked (Shot).

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
    [string[]]$Scenarios = @('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'm'),
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
            'vr_calibrate',                                            # step 1: stand, look ahead, trigger
            'vr_mock_hand right trigger 1', 'vr_mock_hand right trigger 0',
            'vr_mock_hand right trigger 1', 'vr_mock_hand right trigger 0',   # step 2: arm out, trigger
            'vr_where',                                                # C (also: no key left held by the triggers)
            'vr_mock_head 0 0 -0.2 1',                                 # the head leans 0.2 m forward over 1 s
            ((@('wait') * 150) -join '; '),
            'vr_where');                                               # D: lean ~7.27 units along the body yaw
        Stereo = $true; Where = $true; Calibrate = $true; WaitLines = 3; Pending = $false }
    # room-scale walk (see the header): the vr_where lines A, B are checked in Test-VrLog
    h = @{ Commands = @(
            'map demo1', ((@('wait') * 150) -join '; '),             # the level is in
            'menu_main; togglemenu',                                   # no menu, whatever the start did
            'vr_where',                                                # A
            'vr_mock_head 0 0 -1 2',                                   # the head walks 1 m forward over 2 s
            ((@('wait') * 150) -join '; '),                            # 300 frames: >= 2 s at the mock's 90 Hz
            ((@('wait') * 150) -join '; '),
            'vr_where',                                                # B: the body walked ~27 units
            'vr_mock_head 0 -0.8 -1 2',                                # crouch for real: down 0.8 m over 2 s
            ((@('wait') * 200) -join '; '),
            'vr_where',                                                # C: crouch 1, view < 30
            'vr_mock_head 0 0 -1 2',                                   # stand up over 2 s
            ((@('wait') * 200) -join '; '),
            'vr_where',                                                # D: crouch 0, view >= 45
            # Y (off hand) released = vr_where; left stick click = the end (VR is up: the defaults are set)
            'bind VR_Y +hxw; alias +hxw ""; alias -hxw vr_where; alias hxend "screenshot; wait; wait; wait; disconnect; quit"; bind VR_OSTICK hxend',
            # after the script (delays in s): B pressed in the game, Y (E: crouchkey 1), Menu (the main menu opens),
            # B released in the menu, Menu (the menu closes), Y (F: held 0, crouchkey 0), stick click (quit)
            ('vr_mock_hand right b 1 0.6; vr_mock_hand left b 1 1.2; vr_mock_hand left b 0 1.8; ' +
             'vr_mock_hand left menu 1 2.4; vr_mock_hand left menu 0 3.0; vr_mock_hand right b 0 3.6; ' +
             'vr_mock_hand left menu 1 4.2; vr_mock_hand left menu 0 4.8; vr_mock_hand left b 1 5.4; ' +
             'vr_mock_hand left b 0 6.0; vr_mock_hand left stick 1 6.6'));
        Stereo = $true; RoomWalk = $true; ExpectQuit = $true; Shot = $true; WaitLines = 3; TimeoutSec = 300; Pending = $false }
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
    if ($sc.Calibrate) {
        # mock head at 1.65 m; the right hand at (0.2, 1.0, -0.3) is 0.535 m from the shoulder (0.2283, 1.4420, 0)
        if (-not ($vrLines -ccontains 'VR: calibration done: eye height 1.65 m, arm 0.53 m')) {
            return "log has no 'VR: calibration done: eye height 1.65 m, arm 0.53 m' line: $($vrLines -join ' | ')"
        }
    }
    if ($sc.Where) {
        $re = '^VR: where origin (\S+) (\S+) (\S+) yaw (\S+) lean (\S+) (\S+) view (\S+) crouch (\d) held (\d+) crouchkey (\d)$'
        $w = @($vrLines | Where-Object { $_ -cmatch $re })
        if ($w.Count -ne 4) { return "expected 4 'VR: where' lines (A, B, C, D), found $($w.Count): $($w -join ' | ')" }
        $p = @()
        foreach ($l in $w) {
            [void]($l -cmatch $re)
            $p += ,@{ X = [double]::Parse($Matches[1], [cultureinfo]::InvariantCulture); Y = [double]::Parse($Matches[2], [cultureinfo]::InvariantCulture)
                      Yaw = [double]::Parse($Matches[4], [cultureinfo]::InvariantCulture); Held = [int]$Matches[9]; CrouchKey = [int]$Matches[10]
                      LeanX = [double]::Parse($Matches[5], [cultureinfo]::InvariantCulture); LeanY = [double]::Parse($Matches[6], [cultureinfo]::InvariantCulture) }
        }
        $dist = [math]::Sqrt([math]::Pow($p[1].X - $p[0].X, 2) + [math]::Pow($p[1].Y - $p[0].Y, 2))
        Write-Host ("vr-mock-smoke: stick move A->B {0:N1} units, turn B->C {1:N1} deg" -f $dist, ((($p[2].Yaw - $p[1].Yaw) % 360 + 360) % 360))
        if ($dist -le 32) { return "the left stick moved the player only $dist units (A -> B), expected more than 32: $($w -join ' | ')" }
        $turn = (($p[2].Yaw - $p[1].Yaw) % 360 + 360) % 360
        if ([math]::Abs($turn - 315) -gt 1) { return "snap turn B -> C changed the yaw by $turn (mod 360), expected 315 +- 1: $($w -join ' | ')" }
        if ($p[2].Held -ne 0 -or $p[2].CrouchKey -ne 0) { return "buttons stuck after the sticks: $($w[2])" }
        # room-scale lean: none after the calibration's recenter (C), 0.2 m of head motion forward (D)
        $leanC = [math]::Sqrt([math]::Pow($p[2].LeanX, 2) + [math]::Pow($p[2].LeanY, 2))
        $leanD = [math]::Sqrt([math]::Pow($p[3].LeanX, 2) + [math]::Pow($p[3].LeanY, 2))
        Write-Host ("vr-mock-smoke: lean C {0:N2} units, D {1:N2} units" -f $leanC, $leanD)
        if ($leanC -ge 0.5) { return "lean $leanC units at C, expected under 0.5 (the calibration recenters): $($w[2])" }
        if ([math]::Abs($leanD - 7.27) -gt 0.5) { return "lean $leanD units at D after 0.2 m of head motion, expected 7.27 +- 0.5: $($w[3])" }
        $leanYaw = [math]::Atan2($p[3].LeanY, $p[3].LeanX) * 180 / [math]::PI
        $dYaw = (($leanYaw - $p[3].Yaw) % 360 + 540) % 360 - 180
        if ([math]::Abs($dYaw) -gt 5) { return "lean at D points at $leanYaw deg, expected along the body yaw $($p[3].Yaw) +- 5: $($w[3])" }
    }
    if ($sc.RoomWalk) {
        $re = '^VR: where origin (\S+) (\S+) (\S+) yaw (\S+) lean (\S+) (\S+) view (\S+) crouch (\d) held (\d+) crouchkey (\d)$'
        $w = @($vrLines | Where-Object { $_ -cmatch $re })
        if ($w.Count -ne 6) { return "expected 6 'VR: where' lines (A..F), found $($w.Count): $($w -join ' | ')" }
        $p = @()
        foreach ($l in $w) {
            [void]($l -cmatch $re)
            $p += ,@{ X = [double]::Parse($Matches[1], [cultureinfo]::InvariantCulture); Y = [double]::Parse($Matches[2], [cultureinfo]::InvariantCulture)
                      LeanX = [double]::Parse($Matches[5], [cultureinfo]::InvariantCulture); LeanY = [double]::Parse($Matches[6], [cultureinfo]::InvariantCulture)
                      View = [double]::Parse($Matches[7], [cultureinfo]::InvariantCulture); Crouch = [int]$Matches[8]
                      Held = [int]$Matches[9]; CrouchKey = [int]$Matches[10] }
        }
        $dist = [math]::Sqrt([math]::Pow($p[1].X - $p[0].X, 2) + [math]::Pow($p[1].Y - $p[0].Y, 2))
        $leanB = [math]::Sqrt([math]::Pow($p[1].LeanX, 2) + [math]::Pow($p[1].LeanY, 2))
        Write-Host ("vr-mock-smoke: room-scale walk A->B {0:N1} units, lean B {1:N2} units" -f $dist, $leanB)
        if ($dist -lt 20) { return "the room-scale walk moved the player only $dist units (A -> B), expected at least 20: $($w -join ' | ')" }
        if ([math]::Abs($leanB - 9.09) -gt 0.5) { return "lean $leanB units at B, expected the 9.09-unit radius +- 0.5: $($w[1])" }
        # part 2: physical crouch (C), standing again (D), B held in the game (E) and released in the menu (F)
        Write-Host ("vr-mock-smoke: crouch C view {0} crouch {1}, D view {2} crouch {3}, E crouchkey {4}, F held {5} crouchkey {6}" -f $p[2].View, $p[2].Crouch, $p[3].View, $p[3].Crouch, $p[4].CrouchKey, $p[5].Held, $p[5].CrouchKey)
        if ($p[0].Crouch -ne 0 -or $p[1].Crouch -ne 0) { return "crouch reported while standing (A, B): $($w[0]) | $($w[1])" }
        if ($p[2].Crouch -ne 1 -or $p[2].View -ge 30) { return "the head 0.8 m lower did not crouch the player (expected crouch 1, view < 30): $($w[2])" }
        if ($p[3].Crouch -ne 0 -or $p[3].View -lt 45) { return "the head back up did not stand the player up (expected crouch 0, view >= 45): $($w[3])" }
        if ($p[4].CrouchKey -ne 1 -or $p[4].Held -lt 1) { return "B in the game did not hold +crouch (expected crouchkey 1): $($w[4])" }
        if ($p[5].Held -ne 0 -or $p[5].CrouchKey -ne 0) { return "B released in the menu left a key held (expected held 0, crouchkey 0): $($w[5])" }
    }
    # Expected VR: lines are the start/stop/recenter/default-bindings notices (and vr_where output); anything else is an error report.
    $bad = @($vrLines | Where-Object { $_ -cne 'VR: started (mock)' -and $_ -cne 'VR: stopped' -and $_ -cne 'VR: recentered' -and
        $_ -cne 'VR: default controller bindings set' -and $_ -cnotmatch '^VR: where ' -and
        $_ -cnotmatch '^VR: calibration done: ' })
    if ($bad.Count) { return "log has VR error line(s): $($bad -join ' | ')" }
    return $null
}

$failed = @()
$results = @()
foreach ($s in $Scenarios) {
    if (-not $all.Contains($s)) { throw "unknown scenario '$s' (a, b, c, d, e, f, g, h, m)" }
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
            if ($sc.TimeoutSec -and [int]$sc.TimeoutSec -gt $TimeoutSec) { $runArgs.TimeoutSec = [int]$sc.TimeoutSec }
            if ($sc.ExpectQuit) { $runArgs.ExpectQuit = $true }
            & $runSmoke @runArgs
            if ($LASTEXITCODE -ne 0) { $why = "run-smoke failed ($LASTEXITCODE)" }
        }
        catch { $why = "run-smoke error: $_" }
        if (-not $why) {
            $why = Test-VrLog $log $sc
            if ($why -and $why.StartsWith('SKIP: ')) { $skip = $true; $why = $why.Substring(6) }
        }
        if (-not $why -and (-not $sc.ExpectQuit -or $sc.Shot)) {   # ExpectQuit runs take no screenshot, unless Shot
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
