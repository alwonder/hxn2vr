# Offline regression checks only: extract the checker without executing the smoke runner.
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'vr-mock-smoke.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw "smoke syntax errors: $($errors -join '; ')" }
$definition = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Test-VrWallWindow'
}, $true)
if (-not $definition) { throw 'wall checker function missing' }
. ([scriptblock]::Create($definition.Extent.Text))

$prefix = @'
HXW_wall_fixture_ready distance 40.000
HXW_wall_stable_begin
VR: where origin 0.0 0.0 0.0 yaw 0.0
VR: where origin 0.0 0.0 0.0 yaw 0.0
HXW_wall_stable_end
HXW_wall_begin
VR: where origin 0.0 0.0 0.0 yaw 0.0
'@
$suffix = "`nVR: where origin 0.0 0.0 0.0 yaw 0.0`nHXW_wall_end`n"
$normal = @'
VR: fire muzzle 24.000 0.000 50.000 body 0.000 0.000 0.000 proj 32.000 0.000 50.000
VR: clamp raw 80.000 0.000 50.000 hit 40.000 0.000 50.000 start 0 eye 0.000 0.000 50.000 aim 1.000000 0.000000 0.000000
'@
$fallback = @'
VR: fire muzzle 0.000 0.000 50.000 body 0.000 0.000 0.000 proj 8.000 0.000 50.000
VR: clamp raw 80.000 0.000 50.000 hit 8.000 0.000 50.000 start 0 eye 0.000 0.000 50.000 aim 1.000000 0.000000 0.000000
'@
function Check-WallCase($name, $shot, $expected, $head = $prefix) {
    $actual = Test-VrWallWindow (($head + "`n" + $shot + $suffix).Replace("`r", ''))
    if ($actual -cne $expected) { throw "$name expected '$expected', got '$actual'" }
}
Check-WallCase 'ordinary near-side shot' $normal $null
Check-WallCase 'close-wall stock eye spawn' $fallback $null
Check-WallCase 'inclusive 16-unit stock spawn' ($fallback.Replace('proj 8.000', 'proj 16.000')) $null
Check-WallCase 'solid eye' ($fallback.Replace('start 0', 'start 1')) 'approach put the eye in solid'
Check-WallCase 'solid eye on later shot' ($normal + "`n" + $fallback.Replace('start 0', 'start 1')) 'approach put the eye in solid'
Check-WallCase 'beyond wall' ($normal.Replace('proj 32.000', 'proj 41.000')) 'wall projectile is beyond the hit along aim'
Check-WallCase 'excess eye clearance' ($fallback.Replace('proj 8.000', 'proj 17.000')) 'eye fallback projectile exceeds stock 16-unit clearance'
Check-WallCase 'body drift' ($normal.Replace('body 0.000', 'body 1.000')) 'wall body moved during the shot window'
Check-WallCase 'approach drift' $normal 'wall approach is still moving before the shot' ($prefix.Replace('origin 0.0 0.0 0.0 yaw 0.0' + "`nHXW_wall_stable_end", 'origin 1.0 0.0 0.0 yaw 0.0' + "`nHXW_wall_stable_end"))
Write-Host 'wall-window offline regression checks: PASS (9 cases)'
