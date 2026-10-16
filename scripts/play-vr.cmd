@echo off
rem hxn2vr: launch the Release build in VR (OpenXR runtime: Virtual Desktop / SteamVR).
rem Usage: scripts\play-vr.cmd [extra engine args], e.g. scripts\play-vr.cmd +vr_world_scale 1.2 +map demo1
rem Console log: build\game\debug_h2.log
setlocal
for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set GAME=%ROOT%\build\game
set EXE=%ROOT%\build\win64\Release\hxn2vr.exe
if not exist "%EXE%" (
  echo Release build not found: %EXE%
  echo Build it first: cmake --build --preset win64-release
  exit /b 1
)
if not exist "%GAME%\data1\pak0.pak" (
  echo Game data not found in %GAME%\data1 - run scripts\setup-gamedir.ps1 first.
  exit /b 1
)
pushd "%GAME%"
start "hxn2vr" "%EXE%" -basedir "%GAME%" -window -condebug +_enable_mouse 1 +vr_backend openxr +vr_debug_ticks 1 +vr_enabled 1 %*
popd
endlocal
