@echo off
rem Join a 1v1 netplay match as player 2.
cd /d "%~dp0"
set ADDR=
set /p ADDR=Host address (just Enter = this PC): 
if "%ADDR%"=="" set ADDR=127.0.0.1
start "" armored-corer.exe --join %ADDR% %*
