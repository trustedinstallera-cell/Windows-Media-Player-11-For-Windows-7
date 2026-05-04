@echo off
set ArchDir=Win32
if %PROCESSOR_ARCHITECTURE%==AMD64 set ArchDir=x64
if %PROCESSOR_ARCHITECTURE%==ARM set ArchDir=ARM
if %PROCESSOR_ARCHITECTURE%==ARM64 set ArchDir=ARM64

start "" "%~dp0NSudo Launcher\%ArchDir%\NSudoLG.exe" -U:T -P:E "%~dp0Windows Media Player 11 for Windows 7.exe" --using-ti-mode
exit