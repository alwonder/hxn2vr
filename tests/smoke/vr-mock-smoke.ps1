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
       (VR_StickRepeat counts a long frame as at most 0.1 s, so 3 frames stay below that at any frame rate).
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
       B and C: `vr_calibrate` typed with the console open (toggleconsole first; the command closes it) and the
       main trigger pressed twice (eye height, then arm): the log must have
       `VR: calibration done: eye height 1.65 m, arm 0.53 m` (the mock head is at 1.65 m, its right hand 0.535 m
       from the shoulder), and C must show no held key (the calibration took the trigger presses).
       Then the room-scale lean: C must show a lean under 0.5 units (the calibration recentered), the mock
       head is moved 0.2 m forward over 1 s (`vr_mock_head 0 0 -0.2 1`) and `vr_where` (D) must show a
       horizontal lean of 7.27 +- 0.5 units (0.2 m x 36.36 units/m, inside the 9.09-unit radius) pointing
       along the body yaw of D (+-5 deg: the lean is in world axes).
       The checked picture (hexen00) is the `screenshot` taken just before the left stick is released, with the
       default `vr_vignette 1`: the comfort vignette is on screen and the shot must still pass check_shot --stereo.
       (The final screenshot of the run-smoke wrapper is hexen01 and is not checked.)
    w  all four classes, one process -> tracked projectile muzzle > 8 units right of the body,
       projectile origin within 40 units of the muzzle; vr_fireinfo uses the distinct "VR: last fire"
       prefix; pose invalid -> no wrapped fire, but server mana decreases.
       Short per-class cfgs finish before delayed controller presses; server edict dumps verify the class
       and mana. Each class gets three 150-frame settling waits and a 28-second event sequence.
       After tuning, move the off-hand grip into view; hexen00 is an indicators-off baseline,
       hexen01 is the final shot with marker and dot enabled. Check stereo and new connected
       marker/dot colour patches against the baseline (check_shot --indicators-baseline).
       Then the B+Y tuning chord (right b + left b held 1 s): a pair released early toggles nothing, chord 1
       enters tuning (2 pulses), chord 2 saves, prints the screenshot line (hexen02 exists) and gives 4 pulses;
       +crouch is released by the second press and Y never reaches its binding.
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
       E and F come from the off hand's X, bound to +hxw (`-hxw` = vr_where: on the release, so X itself is
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
    [string[]]$Scenarios = @('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'm', 'w'),
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

# w uses four short cfgs in one process. Each cfg finishes before the delayed buttons fire:
# controller bindings append commands to Cbuf and must not sit behind a waiting test script.
# Sources: weapons.hc W_ChangeWeapon/W_Attack/CheatCommand; impulse.hc (9 requires skill < 3).
# Class 1 = purifier slot 4; classes 2/3/4 = ice staff/magic missile/crossbow slot 2.
$wCfgs = [ordered]@{}
foreach ($class in 1..4) {
    $slot = if ($class -eq 1) { 4 } else { 2 }
    $next = if ($class -lt 4) { "exec smoke_vr_w$($class + 1).cfg" } else { 'exec smoke_vr_wwall.cfg' }
    $commands = @(
        'skill 1; deathmatch 0; coop 0; vr_debug_fire 1; vr_compat 1; vr_mainhand 1; vr_world_scale 1.2; vr_eye_height 1.65; vr_roomscale 0; vr_seated 0; vr_height_offset 0; vr_crouch_physical 0; vr_mock_motion 0',
        'vr_haptics 1; vr_haptics_strength 1',
        "playerclass $class; map demo1",
        ((@('wait') * 150) -join '; '),
        'menu_main; togglemenu; god; notarget; impulse 9',
        ((@('wait') * 150) -join '; '),
        "impulse $slot",
        ((@('wait') * 150) -join '; '),
        'vr_mock_hand right valid 1; vr_mock_hand right active 1; vr_mock_hand right px 0.4; vr_mock_hand right py 1.0; vr_mock_hand right pz -0.3',
        'bind VR_TRIGGER +attack',
        "alias wtracked `"echo HXW_${class}_tracked_end; vr_fireinfo; edict 1`"; bind VR_X wtracked",
        "alias wbefore `"echo HXW_${class}_untracked_begin; edict 1`"; bind VR_Y wbefore",
        "alias wafter `"edict 1; echo HXW_${class}_untracked_end`"; bind VR_OSTICK wafter",
        "alias wnext `"$next`"; bind VR_OGRIP wnext",
        "echo HXW_${class}_tracked_begin; edict 1; vr_where",
        # Two seconds held, then three seconds for the firing animation/cooldown to settle.
        'vr_mock_hand right trigger 1 2; vr_mock_hand right trigger 0 4; vr_mock_hand left a 1 7; vr_mock_hand left a 0 8',
        # valid controls pose tracking, not active/buttons: the untracked trigger still attacks.
        'vr_mock_hand right valid 0 9; vr_mock_hand left b 1 12; vr_mock_hand left b 0 13; vr_mock_hand right trigger 1 15; vr_mock_hand right trigger 0 17',
        'vr_mock_hand left stick 1 22; vr_mock_hand left stick 0 23; vr_mock_hand left grip 1 28; vr_mock_hand left grip 0 29'
    )
    $cfgLines = @($commands | ForEach-Object { $_; 'wait'; 'wait'; 'wait' })
    $wCfgs["smoke_vr_w$class.cfg"] = $cfgLines -join "`n"
}

# Class 4 slot 2 is models/crossbow.mdl (crossbow.hc). A zero entry makes
# the 0.1 m tracking-X drag independently measurable in hand coordinates.
$tuneCommands = @(
    ((@('wait') * 150) -join '; '),
    'vr_gunangle 0; vr_mock_hand right valid 1; vr_mock_hand left grip 0; vr_mock_hand left stick 0; vr_mock_hand left px -0.2',
    ((@('wait') * 30) -join '; '),
    'bind VR_OGRIP "echo HXW_TUNE_BINDING_LEAK"; bind VR_OSTICK "echo HXW_TUNE_BINDING_LEAK"',
    'vr_weapon_offset models/crossbow.mdl 0 0 0 0 0 0 1; echo HXW_tune_before; vr_weapon_list; echo HXW_tune_before_end',
    'vr_weapon_tune',
    'vr_mock_hand left grip 1; vr_mock_hand left stick 1',
    ((@('wait') * 20) -join '; '),
    'vr_mock_hand left px -0.1',
    ((@('wait') * 20) -join '; '),
    'vr_mock_hand left grip 0; vr_mock_hand left stick 0',
    ((@('wait') * 20) -join '; '),
    'echo HXW_tune_after; vr_weapon_list; echo HXW_tune_after_end; vr_weapon_save; vr_weapon_tune',
    # Mock head is (0, 1.65, 0): the grip is 0.35 m forward, 0.12 m left and 0.15 m below it.
    # Finish tuning first so moving this hand cannot alter the saved weapon entry.
    'vr_mock_hand left active 1; vr_mock_hand left valid 1; vr_mock_hand left px -0.12; vr_mock_hand left py 1.5; vr_mock_hand left pz -0.35; vr_offhand_marker 0; vr_aimdot 0',
    ((@('wait') * 30) -join '; '),
    'screenshot',  # hexen00: identical pose with both indicators disabled
    'vr_offhand_marker 1; vr_aimdot 1',
    ((@('wait') * 30) -join '; '),  # at least 2 s at 15 fps; 5 s at 6 fps
    'screenshot; wait; wait; wait',
    # B+Y chord (main B = right b, off-hand Y = left b; B goes first so +crouch is pressed and then released).
    # Controller bindings append to the Cbuf behind a waiting script, so the presses are delayed mock events
    # (seconds) that run after this cfg has ended; X prints a report (marker + vr_where) at each checkpoint, and
    # the off-hand trigger execs the second cfg (the mock queue holds 16 events). Y has no binding of its own:
    # a leak shows as HXW_CHORD_Y_LEAK. This is after the final shot (hexen01): the chord's screenshot is the next slot.
    'bind VR_B +crouch; bind VR_Y "echo HXW_CHORD_Y_LEAK"; bind VR_X "echo HXW_chord_rep; vr_where"; bind VR_OTRIGGER "exec smoke_vr_wchord2.cfg"; vr_mock_hand right b 0; vr_mock_hand left b 0',
    # rep1: B alone (crouchkey 1); Y pressed and let go at 0.3 s, rep2: B still held, +crouch released, nothing toggled
    ('vr_mock_hand right b 1 1; vr_mock_hand left a 1 2; vr_mock_hand left a 0 2.3; vr_mock_hand left b 1 3; vr_mock_hand left b 0 3.3; ' +
     'vr_mock_hand left a 1 5; vr_mock_hand left a 0 5.3; vr_mock_hand right b 0 5.8'),
    # chord 1 (7.5 s .. 11 s): tuning on; rep3 while the pair is held; then the second cfg
    ('vr_mock_hand right b 1 7; vr_mock_hand left b 1 7.5; vr_mock_hand left a 1 10; vr_mock_hand left a 0 10.3; ' +
     'vr_mock_hand right b 0 11; vr_mock_hand left b 0 11; vr_mock_hand left trigger 1 12')
)
$chord2Commands = @(
    'vr_mock_hand left trigger 0; alias wquit "disconnect; quit"; bind VR_OSTICK wquit',
    # chord 2: tuning off, save, screenshot, pulses; rep4 while the pair is held; then the stick click quits
    ('vr_mock_hand right b 1 1; vr_mock_hand left b 1 1.5; vr_mock_hand left a 1 5; vr_mock_hand left a 0 5.3; ' +
     'vr_mock_hand right b 0 5.8; vr_mock_hand left b 0 5.8; vr_mock_hand left stick 1 8')
)
# Trace a clear wall fixture instead of relying on timed movement into demo1 stairs.
# The mock-only fixture uses noclip to eliminate gravity; two samples verify rest.
$wallCommands = @(
    '-forward; -back; -moveleft; -moveright; -moveup; -movedown; -jump; -attack',
    'vr_mock_hand left sx 0; vr_mock_hand left sy 0; vr_mock_hand right sx 0; vr_mock_hand right sy 0; vr_mock_hand right trigger 0; vr_mock_head 0 0 0',
    'vr_mock_hand right valid 1; vr_mock_hand right px 0; vr_mock_hand right py 1.0; vr_mock_hand right pz -2; vr_mock_hand right yaw 0; vr_mock_hand right pitch 0; vr_weapon_offset models/crossbow.mdl 0 0 0 0 0 0 1',
    ((@('wait') * 150) -join '; '),
    'vr_mock_wall place',
    ((@('wait') * 150) -join '; '),
    'echo HXW_wall_stable_begin; vr_where',
    ((@('wait') * 150) -join '; '),
    'vr_where; echo HXW_wall_stable_end',
    'alias wwallend "vr_where; echo HXW_wall_end; edict 1; impulse 1; exec smoke_vr_wmelee.cfg"; bind VR_X wwallend',
    'echo HXW_wall_begin; edict 1; vr_where',
    'vr_mock_hand right trigger 1 2; vr_mock_hand right trigger 0 4; vr_mock_hand left a 1 8; vr_mock_hand left a 0 9'
)
$wCfgs['smoke_vr_wwall.cfg'] = (@($wallCommands | ForEach-Object { $_; 'wait'; 'wait'; 'wait' }) -join "`n")
$meleeCommands = @(
    ((@('wait') * 150) -join '; '),
    'vr_mock_hand right pz -0.3; vr_mock_hand right px 0.4',
    'alias wmeleeend "echo HXW_melee_end; vr_mock_wall restore; impulse 2; exec smoke_vr_wtune.cfg"; bind VR_X wmeleeend',
    'echo HXW_melee_begin',
    'vr_mock_hand right trigger 1 2; vr_mock_hand right trigger 0 4; vr_mock_hand left a 1 8; vr_mock_hand left a 0 9'
)
$wCfgs['smoke_vr_wmelee.cfg'] = (@($meleeCommands | ForEach-Object { $_; 'wait'; 'wait'; 'wait' }) -join "`n")
$wCfgs['smoke_vr_wtune.cfg'] = (@($tuneCommands | ForEach-Object { $_; 'wait'; 'wait'; 'wait' }) -join "`n")
$wCfgs['smoke_vr_wchord2.cfg'] = (@($chord2Commands | ForEach-Object { $_; 'wait'; 'wait'; 'wait' }) -join "`n")

$all = [ordered]@{
    w = @{ Commands = @('exec smoke_vr_w1.cfg'); Stereo = $true; Weapon = $true; ExpectQuit = $true; Shot = $true; WaitLines = 3; TimeoutSec = 900; Pending = $false }
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
            'screenshot; vr_mock_hand left sy 0',                      # the picture while the stick is pushed (comfort vignette), then stop
            ((@('wait') * 20) -join '; '),
            'vr_where',                                                # B
            ('vr_mock_hand right sx 1; ' + ((@('wait') * 20) -join '; ')),   # one snap turn to the right
            ('vr_mock_hand right sx 0; ' + ((@('wait') * 20) -join '; ')),
            'toggleconsole',                                           # the command is typed in the console ...
            'vr_calibrate',                                            # ... which it closes; step 1: stand, look ahead, trigger
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
            # X (off hand) released = vr_where; left stick click = the end (VR is up: the defaults are set)
            'bind VR_X +hxw; alias +hxw ""; alias -hxw vr_where; alias hxend "screenshot; wait; wait; wait; disconnect; quit"; bind VR_OSTICK hxend',
            # after the script (delays in s): B pressed in the game, X (E: crouchkey 1), Menu (the main menu opens),
            # B released in the menu, Menu (the menu closes), X (F: held 0, crouchkey 0), stick click (quit)
            ('vr_mock_hand right b 1 0.6; vr_mock_hand left a 1 1.2; vr_mock_hand left a 0 1.8; ' +
             'vr_mock_hand left menu 1 2.4; vr_mock_hand left menu 0 3.0; vr_mock_hand right b 0 3.6; ' +
             'vr_mock_hand left menu 1 4.2; vr_mock_hand left menu 0 4.8; vr_mock_hand left a 1 5.4; ' +
             'vr_mock_hand left a 0 6.0; vr_mock_hand left stick 1 6.6'));
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

# Check wall geometry in world units along the logged aim, including stock eye fallback.
function Test-VrWallWindow([string]$text) {
    $num = '(-?\d+(?:\.\d+)?)'
    $ready = [regex]::Match($text, "(?m)^HXW_wall_fixture_ready distance $num$")
    if (-not $ready.Success) { return 'wall fixture did not find a clear hull/eye' }
    $distance = [double]::Parse($ready.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
    if ($distance -lt 20 -or $distance -gt 60) { return 'wall fixture distance is outside 20..60 units' }
    $stable = [regex]::Match($text, '(?ms)^HXW_wall_stable_begin[^\S\n]*\n(.*?)^HXW_wall_stable_end')
    $whereRe = "(?m)^VR: where origin $num $num $num yaw "
    $samples = [regex]::Matches($stable.Groups[1].Value, $whereRe)
    if ($samples.Count -ne 2) { return 'missing two wall approach rest samples' }
    $rest = @()
    foreach ($axis in 1..3) {
        $a = [double]::Parse($samples[0].Groups[$axis].Value, [Globalization.CultureInfo]::InvariantCulture)
        $b = [double]::Parse($samples[1].Groups[$axis].Value, [Globalization.CultureInfo]::InvariantCulture)
        if ([Math]::Abs($a - $b) -gt 0.1) { return 'wall approach is still moving before the shot' }
        $rest += $b
    }
    $wall = [regex]::Match($text, '(?ms)^HXW_wall_begin[^\S\n]*\n(.*?)^HXW_wall_end')
    if (-not $wall.Success) { return 'missing wall-shot window' }
    # Pair each spawn with its immediate clamp diagnostic; check every shot, not just the first.
    $fireRe = "^VR: fire muzzle $num $num $num body $num $num $num proj $num $num $num$"
    $probeRe = "^VR: clamp raw $num $num $num hit $num $num $num start ([01]) eye $num $num $num aim $num $num $num$"
    $shots = [regex]::Matches($wall.Groups[1].Value, "(?m)$fireRe\n$probeRe")
    $fireCount = [regex]::Matches($wall.Groups[1].Value, "(?m)$fireRe").Count
    if ($shots.Count -eq 0 -or $shots.Count -ne $fireCount) { return 'wall shot did not engage the clamp or is missing probe evidence' }
    foreach ($shot in $shots) {
        if ($shot.Groups[16].Value -eq '1') { return 'approach put the eye in solid' }
        $raw2 = 0.0; $clamped2 = 0.0; $muzzleEye2 = 0.0; $projectileEye2 = 0.0
        $shotAlong = 0.0; $hitAlong = 0.0; $aim2 = 0.0
        foreach ($axis in 1..3) {
            $muzzle = [double]::Parse($shot.Groups[$axis].Value, [Globalization.CultureInfo]::InvariantCulture)
            $body = [double]::Parse($shot.Groups[$axis + 3].Value, [Globalization.CultureInfo]::InvariantCulture)
            $projectile = [double]::Parse($shot.Groups[$axis + 6].Value, [Globalization.CultureInfo]::InvariantCulture)
            $raw = [double]::Parse($shot.Groups[$axis + 9].Value, [Globalization.CultureInfo]::InvariantCulture)
            $hit = [double]::Parse($shot.Groups[$axis + 12].Value, [Globalization.CultureInfo]::InvariantCulture)
            $eye = [double]::Parse($shot.Groups[$axis + 16].Value, [Globalization.CultureInfo]::InvariantCulture)
            $aim = [double]::Parse($shot.Groups[$axis + 19].Value, [Globalization.CultureInfo]::InvariantCulture)
            if ([Math]::Abs($body - $rest[$axis - 1]) -gt 0.15) { return 'wall body moved during the shot window' }
            if ($axis -le 2) {
                $raw2 += ($raw - $body) * ($raw - $body)
                $clamped2 += ($muzzle - $body) * ($muzzle - $body)
            }
            $muzzleEye2 += ($muzzle - $eye) * ($muzzle - $eye)
            $projectileEye2 += ($projectile - $eye) * ($projectile - $eye)
            $shotAlong += ($projectile - $eye) * $aim
            $hitAlong += ($hit - $eye) * $aim
            $aim2 += $aim * $aim
        }
        if ([Math]::Abs($aim2 - 1) -gt 0.001) { return 'invalid wall probe aim direction' }
        if ($clamped2 -ge $raw2 - 0.01) { return 'wall muzzle was not pulled back toward the body' }
        # Within clearance, clamping to the eye leaves stock progs its ordinary
        # eye + k*aim spawn (k <= 16). A real startsolid was rejected above.
        if ($muzzleEye2 -le 0.01) {
            if ($projectileEye2 -gt 16.01 * 16.01 -or $shotAlong -lt -0.01) { return 'eye fallback projectile exceeds stock 16-unit clearance' }
        }
        elseif ($shotAlong -gt $hitAlong + 0.01) { return 'wall projectile is beyond the hit along aim' }
    }
    $after = [regex]::Matches($wall.Groups[1].Value, $whereRe)
    if ($after.Count -lt 2) { return 'missing wall-shot before/after origin samples' }
    foreach ($sample in $after) {
        foreach ($axis in 1..3) {
            $value = [double]::Parse($sample.Groups[$axis].Value, [Globalization.CultureInfo]::InvariantCulture)
            if ([Math]::Abs($value - $rest[$axis - 1]) -gt 0.1) { return 'wall body moved during the shot window' }
        }
    }
    return $null
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
    if ($sc.Weapon) {
        $text = $lines -join "`n"
        if ($lines -ccontains 'HXW_TUNE_BINDING_LEAK') { return 'tuning leaked the off-hand grip/stick to bindings' }
        $entries = @()
        foreach ($phase in @('before', 'after')) {
            $window = [regex]::Match($text, "(?ms)^HXW_tune_${phase}[^\S\n]*\n(.*?)^HXW_tune_${phase}_end[^\S\n]*$")
            if (-not $window.Success) { return "missing tune $phase window" }
            $entry = [regex]::Match($window.Groups[1].Value, '(?m)^vr_weapon_offset "models/crossbow\.mdl" ([^\r\n]+)')
            if (-not $entry.Success) { return "missing crossbow entry $phase tuning" }
            $values = @($entry.Groups[1].Value.Trim() -split '\s+' | ForEach-Object { [double]::Parse($_, [cultureinfo]::InvariantCulture) })
            if ($values.Count -ne 7) { return "invalid tune $phase entry" }
            $entries += ,$values
        }
        foreach ($axis in 0..2) {
            if ([math]::Abs($entries[0][$axis]) -gt 0.001) { return 'tune baseline offset is not zero' }
            $expected = if ($axis -eq 1) { -0.1 * 50 / 1.65 * 1.2 } else { 0 }
            if ([math]::Abs($entries[1][$axis] - $expected) -gt 0.05) { return "grab did not move entry axis $axis by expected $expected units" }
        }
        foreach ($axis in 3..5) {
            if ([math]::Abs($entries[1][$axis]) -gt 0.01) { return 'translation-only grab rotated the entry' }
        }
        if ([math]::Abs($entries[1][6] - 1) -gt 0.001) { return 'translation-only grab changed scale' }
        if (-not ($vrLines -ccontains 'VR: saved 1 weapon offsets to vr_weapons.cfg')) { return 'missing weapon save confirmation' }
        $weaponCfg = Join-Path $GameDir 'data1\vr_weapons.cfg'
        if (-not (Test-Path -LiteralPath $weaponCfg)) { return "no saved weapon cfg at $weaponCfg" }
        $saved = @([IO.File]::ReadAllLines($weaponCfg))
        $afterEntry = [regex]::Match($text, '(?ms)^HXW_tune_after[^\S\n]*\n(vr_weapon_offset "models/crossbow\.mdl" [^\r\n]+)')
        if ($saved.Count -ne 1 -or -not $afterEntry.Success -or $saved[0] -cne $afterEntry.Groups[1].Value) { return 'saved cfg does not match the moved entry from vr_weapon_list' }
        # B+Y chord (end of the tune cfgs). Reports rep1..rep4 (X: marker + vr_where): B alone; after the pair released
        # early; chord 1 held; chord 2 held.
        if ($text -cmatch 'HXW_CHORD_Y_LEAK') { return 'the chord leaked Y to its binding' }
        $reps = @([regex]::Matches($text, '(?m)^HXW_chord_rep[^\S\n]*\n[^\n]*VR: where [^\n]* crouchkey (\d+)[^\n]*$'))
        if ($reps.Count -ne 4) { return "expected 4 chord reports, got $($reps.Count)" }
        $ck = @($reps | ForEach-Object { [int]$_.Groups[1].Value })
        if ($ck[0] -ne 1) { return 'B alone did not hold +crouch before the chord' }
        if ($ck[1] -ne 0) { return 'the second press of the pair did not release +crouch ' }
        if ($ck[2] -ne 0 -or $ck[3] -ne 0) { return 'a chord left +crouch held while the pair was down' }
        $between = { param($a, $b) $text.Substring($a.Index + $a.Length, $b.Index - $a.Index - $a.Length) }
        $tunePulse = '(?m)^VR: haptic [01] 0\.5 0\.06 tune\r?$'
        $cancel = & $between $reps[0] $reps[1]
        if ([regex]::Matches($cancel, $tunePulse).Count -ne 0 -or $cancel -cmatch 'VR: saved') { return 'a pair released before 1 s toggled tuning' }
        $on = & $between $reps[1] $reps[2]
        if ([regex]::Matches($on, $tunePulse).Count -ne 2) { return 'chord 1 did not give one pulse on each hand' }
        if ($on -cmatch 'VR: saved') { return 'chord 1 saved instead of entering tuning' }
        $off = & $between $reps[2] $reps[3]
        if ([regex]::Matches($off, $tunePulse).Count -ne 4) { return 'chord 2 did not give two pulses on each hand' }
        if ($off -cnotmatch '(?m)^VR: saved 1 weapon offsets to vr_weapons\.cfg\r?$') { return 'chord 2 did not save the weapon offsets' }
        $shotLine = [regex]::Match($off, '(?m)^VR: weapon tuning screenshot (shots/hexen\d\d\.tga)\r?$')
        if (-not $shotLine.Success) { return 'chord 2 printed no screenshot line' }
        if (-not (Test-Path -LiteralPath (Join-Path $GameDir ('data1\' + ($shotLine.Groups[1].Value -replace '/', '\'))))) { return 'the chord screenshot file is missing' }
        if ([regex]::Matches($text, '(?m)^VR: saved 1 weapon offsets to vr_weapons\.cfg\r?$').Count -ne 2) { return 'expected exactly two weapon saves (vr_weapon_save and chord 2)' }
        $num = '(-?\d+(?:\.\d+)?)'
        $fireRe = "^VR: fire muzzle $num $num $num body $num $num $num proj $num $num $num$"
        $melee = [regex]::Match($text, '(?ms)^HXW_melee_begin[^\S\n]*\n(.*?)^HXW_melee_end')
        if (-not $melee.Success -or $melee.Groups[1].Value -cnotmatch '(?m)^VR: haptic 1 0\.6 0\.04 sound\r?$') { return 'melee did not exercise sound haptic path A' }
        $wallError = Test-VrWallWindow $text
        if ($wallError) { return $wallError }
        foreach ($class in 1..4) {
            $tracked = [regex]::Matches($text, "(?ms)^HXW_${class}_tracked_begin[^\S\n]*\n(.*?)^HXW_${class}_tracked_end[^\S\n]*$")
            if ($tracked.Count -ne 1) { return "class ${class}: missing/duplicate tracked window" }
            $section = $tracked[0].Groups[1].Value
            $where = [regex]::Match($section, '(?m)^VR: where origin \S+ \S+ \S+ yaw (-?\d+(?:\.\d+)?) ')
            if (-not $where.Success) { return "class ${class}: missing body yaw" }
            $yaw = [double]::Parse($where.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) * [Math]::PI / 180
            $fires = @($section -split "`n" | ForEach-Object { $_.TrimEnd("`r") } | Where-Object { $_ -cmatch $fireRe })
            if ($fires.Count -lt 1) { return "class ${class}: no wrapped projectile" }
            if ($section -cnotmatch '(?ms)^VR: fire muzzle [^\n]*\n.*?^VR: haptic 1 0\.6 0\.04 (?:sound|spawn)\r?$') { return "class $($class): no right-hand weapon haptic after firing" }
            foreach ($fire in $fires) {
                [void]($fire -cmatch $fireRe)
                $dx = [double]::Parse($Matches[1], [Globalization.CultureInfo]::InvariantCulture) - [double]::Parse($Matches[4], [Globalization.CultureInfo]::InvariantCulture)
                $dy = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture) - [double]::Parse($Matches[5], [Globalization.CultureInfo]::InvariantCulture)
                $right = $dx * [Math]::Sin($yaw) - $dy * [Math]::Cos($yaw)
                if ($right -le 8) { return "class ${class}: muzzle is only $right units to the right (expected > 8): $fire" }
                $distance2 = 0.0
                foreach ($axis in 1..3) {
                    $delta = [double]::Parse($Matches[$axis + 6], [Globalization.CultureInfo]::InvariantCulture) - [double]::Parse($Matches[$axis], [Globalization.CultureInfo]::InvariantCulture)
                    $distance2 += $delta * $delta
                }
                if ($distance2 -gt 1600) { return "class ${class}: projectile is more than 40 units from the muzzle: $fire" }
            }
            # The distinct prefix proves this is vr_fireinfo, not a late real fire line.
            $info = [regex]::Match($text, "(?m)^HXW_${class}_tracked_end[^\S\n]*\n([^\r\n]+)")
            $expectedInfo = $fires[-1] -creplace '^VR: fire ', 'VR: last fire '
            if (-not $info.Success -or $info.Groups[1].Value -cne $expectedInfo) { return "class ${class}: vr_fireinfo did not replay the last wrapped fire" }
            # Relaxed py=1.0 m firing must leave the real server origin z unchanged.
            $beforeBody = [regex]::Match($tracked[0].Groups[1].Value, "(?ms)^EDICT 1:\r?\n.*?^origin\s+[^\r\n]*?(-?\d+(?:\.\d+)?)['\s]*$")
            $afterBody = [regex]::Match($text, "(?ms)^HXW_$($class)_tracked_end[^\S\n]*\n[^\n]*\n\s*EDICT 1:\r?\n.*?^origin\s+[^\r\n]*?(-?\d+(?:\.\d+)?)['\s]*$")
            if (-not $beforeBody.Success -or -not $afterBody.Success -or $beforeBody.Groups[1].Value -ne $afterBody.Groups[1].Value) { return "class $($class): low-hand shot changed body z or missing origin dump" }
            $untracked = [regex]::Matches($text, "(?ms)^HXW_$($class)_untracked_begin[^\S\n]*\n(.*?)^HXW_$($class)_untracked_end[^\S\n]*$")
            if ($untracked.Count -ne 1) { return "class ${class}: missing/duplicate untracked window" }
            $section = $untracked[0].Groups[1].Value
            if ($section -cmatch '(?m)^VR: fire') { return "class ${class}: wrapper ran with an untracked hand" }
            $dumps = [regex]::Matches($section, '(?ms)^EDICT 1:\r?\n(.*?)(?=^EDICT 1:|\z)')
            if ($dumps.Count -ne 2) { return "class ${class}: expected two server player dumps" }
            $mana = @()
            foreach ($dump in $dumps) {
                $sum = 0.0
                foreach ($field in @('bluemana', 'greenmana')) {
                    $m = [regex]::Match($dump.Groups[1].Value, "(?m)^$field\s+(-?\d+(?:\.\d+)?)\s*$")
                    # ED_Print omits zero-valued fields.
                    if ($m.Success) { $sum += [double]::Parse($m.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) }
                }
                $pc = [regex]::Match($dump.Groups[1].Value, '(?m)^playerclass\s+(\d+(?:\.\d+)?)\s*$')
                if (-not $pc.Success -or [double]::Parse($pc.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) -ne $class) { return "class ${class}: server playerclass mismatch" }
                $mana += $sum
            }
            if ($mana[0] - $mana[1] -lt 0.5) { return "class ${class}: untracked shot consumed no mana ($($mana -join ' -> '))" }
        }
    }
    # Expected VR: lines are the start/stop/recenter/default-bindings notices (and vr_where output); anything else is an error report.
    $bad = @($vrLines | Where-Object { $_ -cne 'VR: started (mock)' -and $_ -cne 'VR: stopped' -and $_ -cne 'VR: recentered' -and
        $_ -cne 'VR: default controller bindings set' -and $_ -cnotmatch '^VR: where ' -and
        -not ($sc.Weapon -and $_ -ceq 'VR: saved 1 weapon offsets to vr_weapons.cfg') -and
        -not ($sc.Weapon -and $_ -cmatch '^VR: weapon tuning screenshot shots/hexen\d\d\.tga$') -and
        -not ($sc.Weapon -and $_ -cmatch '^VR: haptic [01] 0\.5 0\.06 tune$') -and
        -not ($sc.Weapon -and $_ -cmatch '^VR: haptic [01] (?:0(?:\.\d+)?|1) 0\.(?:04|12) (?:sound|spawn|damage)$') -and
        -not ($sc.Weapon -and $_ -cmatch '^VR: clamp raw (?:-?\d+\.\d{3} ){3}hit (?:-?\d+\.\d{3} ){3}start [01] eye (?:-?\d+\.\d{3} ){3}aim -?\d+\.\d{6} -?\d+\.\d{6} -?\d+\.\d{6}$') -and
        $_ -cnotmatch '^VR: calibration done: ' -and -not ($sc.Weapon -and $_ -cmatch '^VR: (?:last )?fire muzzle -?\d+\.\d{3} -?\d+\.\d{3} -?\d+\.\d{3} body -?\d+\.\d{3} -?\d+\.\d{3} -?\d+\.\d{3} proj -?\d+\.\d{3} -?\d+\.\d{3} -?\d+\.\d{3}$') })
    if ($bad.Count) { return "log has VR error line(s): $($bad -join ' | ')" }
    return $null
}

$failed = @()
$results = @()
foreach ($s in $Scenarios) {
    if (-not $all.Contains($s)) { throw "unknown scenario '$s' (a, b, c, d, e, f, g, h, m, w)" }
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
    $wBackups = @{}
    try {
        if ($sc.Weapon) {
            # Isolate the auto-loaded tuning file. FS_USERDIR is data1 under
            # the Windows user base (GameDir), as for config.cfg.
            $weaponCfg = Join-Path $GameDir 'data1\vr_weapons.cfg'
            $wBackups[$weaponCfg] = if (Test-Path -LiteralPath $weaponCfg) { [IO.File]::ReadAllBytes($weaponCfg) } else { $null }
            Remove-Item -LiteralPath $weaponCfg -Force -ErrorAction SilentlyContinue
            foreach ($name in $wCfgs.Keys) {
                $path = Join-Path $GameDir "data1\$name"
                $wBackups[$path] = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllBytes($path) } else { $null }
                [IO.File]::WriteAllText($path, $wCfgs[$name], [Text.Encoding]::ASCII)
            }
        }
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
                $checkedShot = $shot
                if ($sc.Weapon) { $checkedShot = Join-Path $GameDir 'data1\shots\hexen01.tga' }
                $shotArgs = @('-I', $checkShot, $checkedShot)
                if ($sc.Stereo) { $shotArgs += '--stereo' }
                if ($sc.Weapon) { $shotArgs += "--indicators-baseline=$shot" }
                & py @shotArgs
                if ($LASTEXITCODE -ne 0) { $why = "check_shot failed ($LASTEXITCODE)" }
            }
        }
    }
    finally {
        foreach ($path in $wBackups.Keys) {
            if ($null -ne $wBackups[$path]) { [IO.File]::WriteAllBytes($path, $wBackups[$path]) }
            else { Remove-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue }
        }
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
