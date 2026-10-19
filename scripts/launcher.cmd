@echo off
powershell -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0launcher.ps1" %*
