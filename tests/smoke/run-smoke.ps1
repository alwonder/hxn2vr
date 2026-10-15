<#
.SYNOPSIS
  Runs the engine windowed, executes a scripted command list, takes a screenshot and quits.

.DESCRIPTION
  Writes <GameDir>\data1\smoke_<Name>.cfg: the given commands separated by WaitLines
  `wait` lines (uHexen2 caps at 72 fps, so 150 waits is about 2 s), then `screenshot`
  and `quit`. Old shots\*.tga are removed first. The engine runs with a timeout.

  Screenshot location: on Windows uHexen2 has no separate user directory
  (DO_USERDIRS is 0, host_parms->userdir == -basedir), and SCR_ScreenShot_f writes
  through FS_WriteFile(FS_USERDIR) -> <basedir>\data1\shots\hexen00.tga. The cfg is
  read by `exec` through the normal search path, which includes <basedir>\data1.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][string]$Name,
    [Parameter(Mandatory)][string[]]$Commands,
    [string[]]$ExtraArgs = @(),
    [string]$GameDir = 'build/game',
    [int]$TimeoutSec = 120,
    [int]$WaitLines = 150
)

$ErrorActionPreference = 'Stop'
function Fail([string]$m) { [Console]::Error.WriteLine($m); exit 1 }
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Resolve-Abs([string]$p) {
    if ([System.IO.Path]::IsPathRooted($p)) { return $p }
    # GameDir defaults are repo-relative; Exe may be relative to cwd. Prefer cwd, fall back to repo root.
    $c = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $p))
    if (Test-Path -LiteralPath $c) { return $c }
    $r = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $p))
    if (Test-Path -LiteralPath $r) { return $r }
    return $r
}

$Exe     = Resolve-Abs $Exe
$GameDir = Resolve-Abs $GameDir
if (-not (Test-Path -LiteralPath $Exe)) { throw "Exe not found: $Exe" }
$data1 = Join-Path $GameDir 'data1'
if (-not (Test-Path -LiteralPath (Join-Path $data1 'pak0.pak'))) {
    throw "Game dir not set up ($data1\pak0.pak missing); run scripts/setup-gamedir.ps1"
}

$cfgName = "smoke_$Name.cfg"
$lines = New-Object System.Collections.Generic.List[string]
foreach ($c in $Commands) {
    $lines.Add($c)
    for ($i = 0; $i -lt $WaitLines; $i++) { $lines.Add('wait') }
}
$lines.Add('screenshot')
for ($i = 0; $i -lt 3; $i++) { $lines.Add('wait') }   # screenshot is written synchronously; small margin
# Host_Quit_f opens the "really quit?" menu while connected and the console is up, which
# would hang a headless run; disconnect first so quit exits immediately.
$lines.Add('disconnect')
$lines.Add('quit')
[System.IO.File]::WriteAllText((Join-Path $data1 $cfgName), (($lines -join "`n") + "`n"),
    (New-Object System.Text.UTF8Encoding($false)))

$shots = Join-Path $data1 'shots'
if (Test-Path -LiteralPath $shots) { Get-ChildItem -LiteralPath $shots -Filter *.tga | Remove-Item -Force }

# ExtraArgs go before +exec: stuffcmds queues the + commands in order and `exec` inserts the cfg
# text in front of whatever is still queued, so + commands placed after +exec would only run
# after the whole script (i.e. after quit).
$argList = @('-basedir', $GameDir, '-window', '-width', '1280', '-height', '720') + $ExtraArgs + @('+exec', $cfgName)
$argStr = ($argList | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' '
Write-Host "run-smoke: $Exe $argStr"

$proc = Start-Process -FilePath $Exe -ArgumentList $argStr -WorkingDirectory $GameDir -PassThru
if (-not $proc.WaitForExit($TimeoutSec * 1000)) {
    try { $proc.Kill() } catch {}
    Fail "run-smoke: timeout after $TimeoutSec s, process killed"
    exit 1
}
$proc.WaitForExit()
if ($proc.ExitCode -ne 0) {
    Fail "run-smoke: engine exit code $($proc.ExitCode)"
    exit 1
}
$shot = Join-Path $shots 'hexen00.tga'
if (-not (Test-Path -LiteralPath $shot)) {
    Fail "run-smoke: no screenshot at $shot"
    exit 1
}
Write-Host "run-smoke: OK, $shot ($((Get-Item $shot).Length) bytes)"
exit 0
