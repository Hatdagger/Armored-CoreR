@echo off
rem Drag a Tinkercad export (.zip from Export -> .OBJ) onto this file,
rem or several tiles at once (named A1.zip, B1.zip, "A2 rot90.zip"... see docs\MAPS.md).
rem They become stage 50 and the game opens on it (AC TEST).
rem Keep this file and tinkercad_stage.py next to armored-corer.exe.
rem Needs Python 3 (python.org; tick "Add python.exe to PATH").
rem Another stage number: set ACR_MAP_ID=51 before running, or edit the line below.
cd /d "%~dp0"
if "%~1"=="" ( echo Drag your Tinkercad .zip file^(s^) onto this file. & pause & exit /b 1 )
if "%ACR_MAP_ID%"=="" set ACR_MAP_ID=50
set NAME=%~n1
if not "%~2"=="" set NAME=Custom Map
set PY=python
where py >nul 2>nul && set PY=py -3
%PY% tinkercad_stage.py %* --import-dir import --id %ACR_MAP_ID% --name "%NAME%"
if errorlevel 1 ( pause & exit /b 1 )
set ACR_STAGE=%ACR_MAP_ID%
start "" armored-corer.exe
