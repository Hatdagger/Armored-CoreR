@echo off
rem Host a 1v1 netplay match as player 1 (port 7777).
rem Windows may ask whether to let Armored-CoreR use the network: allow it on private networks.
cd /d "%~dp0"
start "" armored-corer.exe --host %*
